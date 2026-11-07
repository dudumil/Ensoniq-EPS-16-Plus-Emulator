// SCN2681 / MC68681 compatible DUART (subset used by the ASR-10).
#pragma once
#include <cstdint>
#include <deque>
#include <functional>

class Duart {
public:
    void reset();
    uint8_t read(int reg);
    void    write(int reg, uint8_t v);
    void    run(double clocks);              // clocks of the DUART X1 clock

    void rx_byte(int ch, uint8_t b) { chan[ch].rxq.push_back(b); }
    bool tx_idle(int ch) const { return !chan[ch].tx_busy && !chan[ch].thr_full; }
    void set_ip(int bit, bool v) {
        uint8_t old = ip; if (v) ip |= 1 << bit; else ip &= ~(1 << bit);
        if (old != ip && bit < 4) { ipcr |= 0x10 << bit; if (acr & (1 << bit)) { isr |= 0x80; update_irq(); } }
    }

    std::function<void(int, uint8_t)> tx;   // byte transmitted on channel
    std::function<void(bool)>         irq;
    std::function<void(uint8_t)>      op_changed;  // output pins (active-low register inverted)

    double xtal = 4000000;
    double ip2_hz = 0;                        // external counter clock on IP2 (0 = not connected)
    double baud[2] = { 31250, 31250 };       // used when channel clock is external
    uint8_t ip = 0x7f;
    uint8_t opr = 0;     // output port register (pin = ~opr)
    uint8_t ivr = 0x0f;
    int ivr_value() const { return ivr; }

private:
    struct Chan {
        uint8_t mr1 = 0, mr2 = 0, csr = 0, sr = 0, mrptr = 0;
        bool rx_en = false, tx_en = false;
        std::deque<uint8_t> rxq;          // bytes arriving on the line
        uint8_t fifo[3]; int fifo_n = 0;
        uint8_t thr = 0; bool thr_full = false;
        bool tx_busy = false; double tx_t = 0; uint8_t tx_sh = 0;
        double rx_t = 0;
    } chan[2];
    uint8_t acr = 0, imr = 0, isr = 0, ipcr = 0, opcr = 0;
    uint16_t ctur = 0, ctlr = 0, counter = 0;
    bool ct_running = false;
    bool ct_half = false;
 public: bool half_period_irq = true; private:
    double ct_acc = 0;
    bool irq_state = false;
    void update_irq();
    void update_sr(int c);
    double byte_clocks(int c) const;
};
