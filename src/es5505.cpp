#include "es5505.h"

namespace {
enum : uint16_t {
    CR_STOP0 = 0x0001, CR_STOP1 = 0x0002, CR_BS = 0x0004, CR_LPE = 0x0008, CR_BLE = 0x0010,
    CR_IRQE = 0x0020, CR_DIR = 0x0040, CR_IRQ = 0x0080, CR_CA0 = 0x0100, CR_CA1 = 0x0200,
    CR_LP3 = 0x0400, CR_LP4 = 0x0800,
    CR_STOPMASK = CR_STOP0 | CR_STOP1,
};
constexpr uint32_t ACC_MASK = (1u << 29) - 1;
constexpr int FRAC = 9;

// one-pole sections; K is a 12-bit coefficient left justified in 16 bits (as in MAME's code)
// Yn = K (Xn - Yn-1) + Yn-1
inline int32_t lowpass(int32_t x, uint16_t k, int32_t y1) {
    return ((int32_t)(k >> 4) * (x - y1) / 4096) + y1;
}
// Yn = Xn - Xn-1 + K Yn-1  (K offset by 0.5)
inline int32_t highpass(int32_t x, uint16_t k, int32_t y1, int32_t x1) {
    return x - x1 + ((int32_t)(k >> 4) * y1) / 8192 + y1 / 2;
}
inline int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
}

ES5505::ES5505() { reset(); }

void ES5505::reset() {
    for (auto &v : m_voice) v = Voice{};
    m_active = 31; m_irqv = 0x80; m_page = 0; m_sermode = 1;
    for (auto &c : m_ch) c = 0;
}

// 16-bit sample * 1MMMM, keep the 16 MSBs, then shift by the exponent (table in the spec)
int32_t ES5505::volume(int32_t s, uint8_t vol) {
    int e = vol >> 4, m = vol & 15;
    if (e == 0) return 0;
    int32_t r = (s * (16 + m)) >> 5;
    return r >> (15 - e);
}

void ES5505::generate_voice(Voice &v, int32_t *dest) {
    uint32_t acc = v.accum & ACC_MASK;
    // STOP0/STOP1 only stop the address ALU (spec 2.2): a stopped voice is still fetched,
    // filtered and mixed at its volume, so its filters keep settling and the output does not
    // drop to zero abruptly when a one-shot sample reaches its end
    const bool stopped = (v.control & CR_STOPMASK) != 0;
    int bank = (v.control & CR_BS) ? 1 : 0;
    uint32_t a = acc >> FRAC;
    int32_t s1 = (int16_t)read_sample(bank, a & 0xfffff);
    int32_t s2 = (int16_t)read_sample(bank, (a + 1) & 0xfffff);
    int32_t frac = acc & ((1 << FRAC) - 1);
    int32_t s = s1 + (((s2 - s1) * frac) >> FRAC);

    // filters: poles 1 and 2 are low pass with K1; poles 3 and 4 by LP3/LP4
    s = lowpass(s, v.k1, v.o1n1); v.o1n1 = s;
    s = lowpass(s, v.k1, v.o2n1); v.o2n2 = v.o2n1; v.o2n1 = s;
    switch ((v.control >> 10) & 3) {   // LP4:LP3
    case 0:
        s = highpass(s, v.k2, v.o3n1, v.o2n2); v.o3n2 = v.o3n1; v.o3n1 = s;
        s = highpass(s, v.k2, v.o4n1, v.o3n2); v.o4n1 = s;
        break;
    case 1:   // LP3
        s = lowpass(s, v.k1, v.o3n1); v.o3n2 = v.o3n1; v.o3n1 = s;
        s = highpass(s, v.k2, v.o4n1, v.o3n2); v.o4n1 = s;
        break;
    case 2:   // LP4
        s = lowpass(s, v.k2, v.o3n1); v.o3n2 = v.o3n1; v.o3n1 = s;
        s = lowpass(s, v.k2, v.o4n1); v.o4n1 = s;
        break;
    case 3:
        s = lowpass(s, v.k1, v.o3n1); v.o3n2 = v.o3n1; v.o3n1 = s;
        s = lowpass(s, v.k2, v.o4n1); v.o4n1 = s;
        break;
    }
    s = clamp16(s);
    dest[0] += volume(s, v.lvol);
    dest[1] += volume(s, v.rvol);
    if (stopped) return;

    // address generation and looping (spec 4.1: the remainder past the end keeps the phase)
    int64_t ac = acc, st = v.start, en = v.end;
    if (!(v.control & CR_DIR)) {
        ac += v.freq;
        if (ac >= en) {
            if (v.control & CR_IRQE) v.control |= CR_IRQ;
            if (!(v.control & CR_LPE)) { v.control |= CR_STOP0; ac = en; }
            else if (v.control & CR_BLE) { ac = en - (ac - en); v.control |= CR_DIR; }
            else ac = st + (ac - en);
        }
    } else {
        ac -= v.freq;
        if (ac <= st) {
            if (v.control & CR_IRQE) v.control |= CR_IRQ;
            if (!(v.control & CR_LPE)) { v.control |= CR_STOP0; ac = st; }
            else if (v.control & CR_BLE) { ac = st + (st - ac); v.control &= ~CR_DIR; }
            else ac = en - (st - ac);
        }
    }
    acc = (uint32_t)ac;
    v.accum = acc & ACC_MASK;
}

void ES5505::generate_frame(int32_t out[8]) {
    int32_t acc[8] = {};
    for (int n = 0; n <= m_active; n++) {
        Voice &v = m_voice[n];
        int ch = (v.control >> 8) & 3;
        generate_voice(v, &acc[ch * 2]);
        if ((v.control & CR_IRQ) && (m_irqv & 0x80)) {
            m_irqv = n & 0x1f;                 // bit 7 low = interrupt pending
            v.control &= ~CR_IRQ;
            if (irq_cb) irq_cb(true);
        }
    }
    for (int i = 0; i < 8; i++) { m_ch[i] = clamp16(acc[i]); out[i] = m_ch[i]; }
    if (m_sermode & 1) { m_ch[6] = ser3_in[0]; m_ch[7] = ser3_in[1]; }
}

void ES5505::write(int reg, uint16_t data, uint16_t mask) {
    reg &= 15;
    if (reg == 15) { m_page = (uint8_t)((m_page & ~mask) | (data & mask)) & 0x7f; return; }
    if (reg == 13) { m_active = data & 0x1f; return; }
    if (reg == 14) return;
    Voice &v = m_voice[m_page & 0x1f];
    auto merge16 = [&](uint32_t old) { return (uint16_t)((old & ~mask) | (data & mask)); };
    if (m_page < 0x20) {
        switch (reg) {
        case 0: v.control = merge16(v.control) & 0x0fff; break;
        case 1: v.freq = (merge16(v.freq << 1) >> 1) & 0x7fff; break;
        case 2: v.start = ((uint32_t)(merge16(v.start >> 16) & 0x1fff) << 16) | (v.start & 0xffff); break;
        case 3: v.start = (v.start & 0x1fff0000) | (merge16(v.start & 0xffff) & 0xffe0); break;
        case 4: v.end = ((uint32_t)(merge16(v.end >> 16) & 0x1fff) << 16) | (v.end & 0xffff); break;
        case 5: v.end = (v.end & 0x1fff0000) | (merge16(v.end & 0xffff) & 0xffe0); break;
        case 6: v.k2 = merge16(v.k2) & 0xfff0; break;
        case 7: v.k1 = merge16(v.k1) & 0xfff0; break;
        case 8: v.lvol = merge16(v.lvol << 8) >> 8; break;
        case 9: v.rvol = merge16(v.rvol << 8) >> 8; break;
        case 10: v.accum = ((uint32_t)(merge16(v.accum >> 16) & 0x1fff) << 16) | (v.accum & 0xffff); break;
        case 11: v.accum = (v.accum & 0x1fff0000) | merge16(v.accum & 0xffff); break;
        }
    } else if (m_page < 0x40) {
        auto s16 = [&](int32_t old) { return (int32_t)(int16_t)merge16((uint16_t)old); };
        switch (reg) {
        case 0: v.control = merge16(v.control) & 0x0fff; break;
        case 1: v.o4n1 = s16(v.o4n1); break;
        case 2: v.o3n1 = s16(v.o3n1); break;
        case 3: v.o3n2 = s16(v.o3n2); break;
        case 4: v.o2n1 = s16(v.o2n1); break;
        case 5: v.o2n2 = s16(v.o2n2); break;
        case 6: v.o1n1 = s16(v.o1n1); break;
        }
    } else {
        if (reg < 8 && (m_sermode & 2)) m_ch[reg] = (int16_t)merge16((uint16_t)m_ch[reg]);
        else if (reg == 8) m_sermode = merge16(m_sermode) & 0xf807;
    }
}

uint16_t ES5505::read(int reg) {
    reg &= 15;
    if (reg == 15) return m_page;
    if (reg == 13) return m_active;
    if (reg == 14) {
        uint16_t r = m_irqv;
        if (!(m_irqv & 0x80)) { m_irqv = 0x80; if (irq_cb) irq_cb(false); }
        return r;
    }
    const Voice &v = m_voice[m_page & 0x1f];
    if (m_page < 0x20) {
        switch (reg) {
        case 0: return v.control | 0xf000;
        case 1: return (uint16_t)(v.freq << 1);
        case 2: return (v.start >> 16) & 0x1fff;
        case 3: return v.start & 0xffe0;
        case 4: return (v.end >> 16) & 0x1fff;
        case 5: return v.end & 0xffe0;
        case 6: return v.k2;
        case 7: return v.k1;
        case 8: return v.lvol << 8;
        case 9: return v.rvol << 8;
        case 10: return (v.accum >> 16) & 0x1fff;
        case 11: return v.accum & 0xffff;
        }
        return 0;
    }
    if (m_page < 0x40) {
        switch (reg) {
        case 0: return v.control | 0xf000;
        case 1: return (uint16_t)v.o4n1;
        case 2: return (uint16_t)v.o3n1;
        case 3: return (uint16_t)v.o3n2;
        case 4: return (uint16_t)v.o2n1;
        case 5: return (uint16_t)v.o2n2;
        case 6: return (uint16_t)v.o1n1;
        }
        return 0;
    }
    if (reg < 8) return (uint16_t)m_ch[reg];
    if (reg == 8) return m_sermode | 0x07f8;
    if (reg == 9) return (uint16_t)((read_pot ? read_pot() & 0x3ff : 0) << 6);
    return 0;
}

void ES5505::dump(FILE *f) const {
    fprintf(f, "OTIS active=%d page=%02x irqv=%02x sermode=%04x\n", m_active, m_page, m_irqv, m_sermode);
    for (int n = 0; n <= m_active; n++) {
        const Voice &v = m_voice[n];
        if (v.control & 3) continue;
        fprintf(f, " v%02d cr=%03x fc=%04x start=%06x end=%06x acc=%06x.%03x lv=%02x rv=%02x k1=%04x k2=%04x\n", n, v.control,
                v.freq, v.start >> 9, v.end >> 9, v.accum >> 9, v.accum & 511, v.lvol, v.rvol, v.k1, v.k2);
    }
}
