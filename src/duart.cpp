#include <cstdlib>
#include <cstdio>
#include "duart.h"

static const double brg_set0[16] = { 50, 110, 134.5, 200, 300, 600, 1200, 1050, 2400, 4800, 7200, 9600, 38400, 0, 0, 0 };
static const double brg_set1[16] = { 75, 110, 134.5, 150, 300, 600, 1200, 2000, 2400, 4800, 1800, 9600, 19200, 0, 0, 0 };

void Duart::reset() {
    for (auto &c : chan) c = Chan{};
    acr = imr = isr = ipcr = opcr = 0; opr = 0;
    ctur = ctlr = counter = 0; ct_running = false; ct_acc = 0;
    irq_state = false;
    for (int c = 0; c < 2; c++) update_sr(c);
}

void Duart::update_irq() {
    bool s = (isr & imr) != 0;
    if (s != irq_state) { irq_state = s; if (irq) irq(s); }
}

void Duart::update_sr(int c) {
    Chan &ch = chan[c];
    uint8_t sr = ch.sr & 0xf0;            // keep error bits
    if (ch.fifo_n > 0) sr |= 0x01;        // RxRDY
    if (ch.fifo_n >= 3) sr |= 0x02;       // FFULL
    if (ch.tx_en && !ch.thr_full) sr |= 0x04;  // TxRDY
    if (ch.tx_en && !ch.thr_full && !ch.tx_busy) sr |= 0x08;  // TxEMT
    ch.sr = sr;
    // ISR bits: A: 0 TxRDYA, 1 RxRDYA/FFULLA ; B: 4 TxRDYB, 5 RxRDYB/FFULLB
    int tb = c ? 4 : 0, rb = c ? 5 : 1;
    if (sr & 0x04) isr |= 1 << tb; else isr &= ~(1 << tb);
    bool rxi = (ch.mr1 & 0x40) ? (sr & 0x02) : (sr & 0x01);
    if (rxi) isr |= 1 << rb; else isr &= ~(1 << rb);
    update_irq();
}

double Duart::byte_clocks(int c) const {
    const Chan &ch = chan[c];
    int sel = ch.csr & 0x0f;   // transmitter select (low nibble)
    double b;
    if (sel >= 0x0e) b = baud[c];
    else if (sel == 0x0d) b = 0;   // timer
    else b = (acr & 0x80) ? brg_set1[sel] : brg_set0[sel];
    if (b <= 0) b = baud[c];
    int bits = 1 + (5 + (ch.mr1 & 3)) + ((ch.mr1 & 0x18) == 0x10 ? 0 : 1) + 1;
    return xtal * bits / b;
}

uint8_t Duart::read(int reg) {
    reg &= 15;
    switch (reg) {
    case 0: case 8: {
        Chan &ch = chan[reg >> 3];
        uint8_t v = ch.mrptr ? ch.mr2 : ch.mr1; ch.mrptr = 1; return v;
    }
    case 1: case 9: return chan[reg >> 3].sr;
    case 3: case 11: {
        int c = reg >> 3; Chan &ch = chan[c];
        uint8_t v = 0;
        if (ch.fifo_n > 0) {
            v = ch.fifo[0];
            ch.fifo[0] = ch.fifo[1]; ch.fifo[1] = ch.fifo[2]; ch.fifo_n--;
        }
        update_sr(c);
        return v;
    }
    case 4: { uint8_t v = ipcr | (ip & 0x0f); ipcr = 0; isr &= ~0x80; update_irq(); return v; }
    case 5: return isr;
    case 6: return counter >> 8;
    case 7: return counter & 0xff;
    case 12: return ivr;
    case 13: return ip | 0x80;
    case 14:   // start counter
        counter = (ctur << 8) | ctlr; ct_running = true; ct_acc = 0;
        return 0xff;
    case 15:   // stop counter
        if (!(acr & 0x40)) ct_running = false;
        isr &= ~0x08; update_irq();
        return 0xff;
    }
    return 0xff;
}

void Duart::write(int reg, uint8_t v) {
    reg &= 15;
    switch (reg) {
    case 0: case 8: {
        Chan &ch = chan[reg >> 3];
        if (ch.mrptr) ch.mr2 = v; else ch.mr1 = v;
        ch.mrptr = 1;
        break;
    }
    case 1: case 9: chan[reg >> 3].csr = v; break;
    case 2: case 10: {
        int c = reg >> 3; Chan &ch = chan[c];
        if (v & 1) ch.rx_en = true;
        if (v & 2) ch.rx_en = false;
        if (v & 4) ch.tx_en = true;
        if (v & 8) ch.tx_en = false;
        switch ((v >> 4) & 7) {
        case 1: ch.mrptr = 0; break;
        case 2:
            ch.rx_en = false; ch.fifo_n = 0; if (c != 1) ch.rxq.clear(); break;
        case 3: ch.tx_en = false; ch.thr_full = false; ch.tx_busy = false; break;
        case 4: ch.sr &= 0x0f; break;
        case 5: isr &= c ? ~0x40 : ~0x04; break;
        }
        update_sr(c);
        break;
    }
    case 3: case 11: {
        int c = reg >> 3; Chan &ch = chan[c];
        if (!ch.tx_en) break;
        ch.thr = v; ch.thr_full = true;
        update_sr(c);
        break;
    }
    case 4: acr = v; break;
    case 5: imr = v; update_irq(); break;
    case 6: ctur = v; break;
    case 7: ctlr = v; break;
    case 12: ivr = v; break;
    case 13: opcr = v; break;
    case 14: opr |= v; if (op_changed) op_changed(opr); break;
    case 15: opr &= ~v; if (op_changed) op_changed(opr); break;
    }
}

void Duart::run(double clocks) {
    // counter / timer
    int mode = (acr >> 4) & 7;
    double div = (mode == 3 || mode == 7) ? 16.0 : (mode == 6 ? 1.0 : 16.0);
    double rate = 1.0;
    if (ip2_hz > 0 && (mode & 3) <= 1) { rate = ip2_hz / xtal; div = (mode & 1) ? 16.0 : 1.0; }
    if (mode >= 4 || ct_running) {
        ct_acc += clocks * rate / div;
        while (ct_acc >= 1.0) {
            ct_acc -= 1.0;
            if (mode >= 4) {   // timer: square wave of period 2 x preload, ISR once per period
                if (counter == 0) {
                    counter = (ctur << 8) | ctlr;
                    ct_half = !ct_half;
                    if (!ct_half || !half_period_irq) isr |= 0x08;
                }
                else counter--;
            } else {
                counter--;
                if (counter == 0xffff) { isr |= 0x08; }
            }
        }
        update_irq();
    }
    for (int c = 0; c < 2; c++) {
        Chan &ch = chan[c];
        double bc = byte_clocks(c);
        // transmitter
        if (ch.tx_busy) {
            ch.tx_t -= clocks;
            if (ch.tx_t <= 0) { ch.tx_busy = false; if (tx) tx(c, ch.tx_sh); update_sr(c); }
        }
        if (!ch.tx_busy && ch.thr_full && ch.tx_en) {
            ch.tx_sh = ch.thr; ch.thr_full = false; ch.tx_busy = true; ch.tx_t = bc;
            update_sr(c);
        }
        // receiver
        if (ch.rx_t > 0) ch.rx_t -= clocks;
        // Channel B is the link to the panel micro. The OS resets and re-initialises that
        // receiver when it starts recording (RECORD + PLAY); bytes the panel sends meanwhile
        // (the button releases) are held back until the receiver is on again instead of being
        // lost, otherwise the OS believes RECORD is still held and ignores the next buttons.
        // (and they are not pushed into a full FIFO either: the panel micro waits)
        if (ch.rx_t <= 0 && !ch.rxq.empty() && (c != 1 || (ch.rx_en && ch.fifo_n < 3))) {
            uint8_t b = ch.rxq.front(); ch.rxq.pop_front();
            static const bool dbg = getenv("ASR_DBG_DUART") != nullptr;
            if (ch.rx_en) {
                if (ch.fifo_n < 3) ch.fifo[ch.fifo_n++] = b;
                else { ch.sr |= 0x10; if (dbg) fprintf(stderr, "[DUART] ch%d overrun, lost %02x\n", c, b); }   // overrun
                update_sr(c);
            } else if (dbg) fprintf(stderr, "[DUART] ch%d receiver off, lost %02x\n", c, b);
            ch.rx_t = bc;
        }
    }
}
