// Ensoniq ES5505 "OTIS" wavetable voice generator (EPS-16 Plus, VFX, SD-1, SQ-1 ...).
// Written from the Ensoniq OTIS specification; the filter arithmetic follows MAME's
// es5506.cpp (BSD-3-Clause, Aaron Giles) as used for the ES5506 in the ASR-10 emulator.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>

class ES5505 {
public:
    ES5505();
    void reset();

    // 16-bit host interface: reg = A4-A1 (0..15)
    uint16_t read(int reg);
    void     write(int reg, uint16_t data, uint16_t mask = 0xffff);

    // one pass over all active voices; out[ch*2+0] = left, out[ch*2+1] = right (4 channels, 16-bit)
    void generate_frame(int32_t out[8]);

    int  active_voices() const { return m_active; }
    double sample_rate(double clock) const { return clock / (16.0 * (m_active + 1)); }

    std::function<uint16_t(int bank, uint32_t addr)> read_sample;   // 16-bit word, 20-bit address
    std::function<uint16_t()> read_pot;                            // 10-bit POT A/D
    std::function<void(bool)> irq_cb;                              // IRQ line asserted (true)
    bool irq_pending() const { return !(m_irqv & 0x80); }
    int16_t ser3_in[2] = { 0, 0 };                                 // A/D input on serial port 3 (SERMODE A/D)
    uint16_t sermode() const { return m_sermode; }
    void dump(FILE *f) const;

private:
    struct Voice {
        uint16_t control = 3;          // STOP0 | STOP1
        uint32_t freq = 0;             // 6.9 fixed point (FC >> 1)
        uint32_t start = 0, end = 0;   // 20.9
        uint32_t accum = 0;            // 20.9 (29 bits)
        uint16_t k1 = 0, k2 = 0;       // 12-bit coefficients, left justified in 16 bits
        uint8_t  lvol = 0, rvol = 0;
        int32_t  o4n1 = 0, o3n1 = 0, o3n2 = 0, o2n1 = 0, o2n2 = 0, o1n1 = 0;
    } m_voice[32];

    int m_active = 31;
    uint8_t m_irqv = 0x80;
    uint8_t m_page = 0;
    uint16_t m_sermode = 1;            // A/D bit set at reset
    int32_t m_ch[8] = {};              // channel registers as read back

    void generate_voice(Voice &v, int32_t *dest);
    static int32_t volume(int32_t s, uint8_t vol);
};
