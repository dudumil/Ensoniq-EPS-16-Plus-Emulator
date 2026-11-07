#include "hd63450.h"
#include <cstdio>

namespace {
enum { CSR_COC = 0x80, CSR_BTC = 0x40, CSR_NDT = 0x20, CSR_ERR = 0x10, CSR_ACT = 0x08, CSR_PCT = 0x02 };
enum { CCR_STR = 0x80, CCR_CNT = 0x40, CCR_HLT = 0x20, CCR_SAB = 0x10, CCR_INT = 0x08 };
}

void Hd63450::reset() {
    for (auto &c : chan) c = Chan{};
    gcr = 0;
    irq_state = false;
}

void Hd63450::set_pcl(int ch, bool level) {
    Chan &c = chan[ch];
    if (c.pcl && !level) c.csr |= CSR_PCT;
    c.pcl = level;
    update_irq();
}

uint8_t Hd63450::read(int off) {
    Chan &c = chan[(off >> 6) & 3];
    switch (off & 0x3f) {
    case 0x00: return c.csr | (c.pcl ? 1 : 0);
    case 0x01: return c.cer;
    case 0x04: return c.dcr;
    case 0x05: return c.ocr;
    case 0x06: return c.scr;
    case 0x07: return c.ccr;
    case 0x0a: return c.mtc >> 8;
    case 0x0b: return c.mtc & 0xff;
    case 0x0c: return c.mar >> 24;
    case 0x0d: return c.mar >> 16;
    case 0x0e: return c.mar >> 8;
    case 0x0f: return c.mar;
    case 0x14: return c.dar >> 24;
    case 0x15: return c.dar >> 16;
    case 0x16: return c.dar >> 8;
    case 0x17: return c.dar;
    case 0x1a: return c.btc >> 8;
    case 0x1b: return c.btc & 0xff;
    case 0x1c: return c.bar >> 24;
    case 0x1d: return c.bar >> 16;
    case 0x1e: return c.bar >> 8;
    case 0x1f: return c.bar;
    case 0x25: return c.niv;
    case 0x27: return c.eiv;
    case 0x29: return c.mfc;
    case 0x2d: return c.cpr;
    case 0x31: return c.dfc;
    case 0x39: return c.bfc;
    case 0x3f: return gcr;
    }
    return 0xff;
}

void Hd63450::write(int off, uint8_t v) {
    int ch = (off >> 6) & 3;
    Chan &c = chan[ch];
    auto set_byte = [](uint32_t &r, int n, uint8_t v) { int sh = 8 * (3 - n); r = (r & ~(0xffu << sh)) | ((uint32_t)v << sh); };
    switch (off & 0x3f) {
    case 0x00: c.csr &= ~(v & 0xf6); if (!(c.csr & (CSR_COC | CSR_BTC | CSR_ERR | CSR_NDT))) c.cer = 0; update_irq(); break;
    case 0x04: c.dcr = v; break;
    case 0x05: c.ocr = v; break;
    case 0x06: c.scr = v; break;
    case 0x07:
        if (v & CCR_SAB) {                  // software abort
            c.ccr = 0; c.csr = (c.csr & ~CSR_ACT) | CSR_COC | CSR_ERR; c.cer = 0x11;
            update_irq(); break;
        }
        c.ccr = (c.ccr & CCR_STR) | (v & 0x6f) | (v & CCR_STR);
        if (v & CCR_STR) start(ch);
        update_irq();
        break;
    case 0x0a: c.mtc = (c.mtc & 0x00ff) | (v << 8); break;
    case 0x0b: c.mtc = (c.mtc & 0xff00) | v; break;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f: set_byte(c.mar, (off & 3), v); break;
    case 0x14: case 0x15: case 0x16: case 0x17: set_byte(c.dar, (off & 3), v); break;
    case 0x1a: c.btc = (c.btc & 0x00ff) | (v << 8); break;
    case 0x1b: c.btc = (c.btc & 0xff00) | v; break;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: set_byte(c.bar, (off & 3), v); break;
    case 0x25: c.niv = v; break;
    case 0x27: c.eiv = v; break;
    case 0x29: c.mfc = v; break;
    case 0x2d: c.cpr = v; break;
    case 0x31: c.dfc = v; break;
    case 0x39: c.bfc = v; break;
    case 0x3f: gcr = v; break;
    }
}

void Hd63450::start(int ch) {
    Chan &c = chan[ch];
    if (c.csr & CSR_ACT) { c.csr |= CSR_ERR; c.cer = 0x02; return; }   // operation timing error
    c.csr = (c.csr & ~(CSR_COC | CSR_BTC | CSR_NDT | CSR_ERR)) | CSR_ACT;
    c.ccr &= ~CCR_STR;
    if (log) fprintf(stderr, "[DMAC] ch%d start dcr=%02x ocr=%02x scr=%02x mar=%06x dar=%06x mtc=%d\n",
                     ch, c.dcr, c.ocr, c.scr, c.mar, c.dar, c.mtc);
    if (c.mtc == 0) complete(ch);
}

void Hd63450::complete(int ch) {
    Chan &c = chan[ch];
    c.csr = (c.csr & ~CSR_ACT) | CSR_COC;
    if (log) fprintf(stderr, "[DMAC] ch%d complete mar=%06x\n", ch, c.mar);
    update_irq();
}

int Hd63450::run(int budget) {
    int used = 0;
    for (int ch = 0; ch < 4; ch++) {
        Chan &c = chan[ch];
        if (!(c.csr & CSR_ACT) || (c.ccr & CCR_HLT)) continue;
        bool ext = (c.ocr & 3) >= 2;        // REQ line controlled
        while (used < budget && (c.csr & CSR_ACT) && (!ext || c.req)) {
            int size = (c.ocr >> 4) & 3;    // 0 byte, 1 word, 2 long
            int n = size == 0 ? 1 : size == 1 ? 2 : 4;
            bool dev_to_mem = (c.ocr & 0x80) != 0;
            for (int i = 0; i < n; i++) {
                if (dev_to_mem) mem_w8(c.mar + i, mem_r8(c.dar + (size ? i * 2 : 0)));
                else mem_w8(c.dar + (size ? i * 2 : 0), mem_r8(c.mar + i));
            }
            int mac = (c.scr >> 2) & 3, dac = c.scr & 3;
            if (mac == 1) c.mar += n; else if (mac == 2) c.mar -= n;
            if (dac == 1) c.dar += n; else if (dac == 2) c.dar -= n;
            used += 4 * n;
            if (--c.mtc == 0) { complete(ch); break; }
            if (ext) break;                 // one transfer per request (cycle steal)
        }
    }
    return used;
}

int Hd63450::irq_vector() const {
    for (int ch = 0; ch < 4; ch++) {
        const Chan &c = chan[ch];
        if (!(c.ccr & CCR_INT)) continue;
        if (c.csr & CSR_ERR) return c.eiv;
        if (c.csr & (CSR_COC | CSR_BTC | CSR_NDT)) return c.niv;
        if ((c.csr & CSR_PCT) && (c.dcr & 3) == 1) return c.niv;
    }
    return -1;
}

void Hd63450::update_irq() {
    bool s = irq_vector() >= 0;
    if (s != irq_state) { irq_state = s; if (irq_cb) irq_cb(s); }
}
