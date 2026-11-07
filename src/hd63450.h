// Hitachi HD68440 / HD68450 DMA controller (the 2-channel 68440 on the EPS-16 Plus).
// Only what the EPS-16 Plus firmware uses: single-block transfers between memory and an
// 8-bit device port, external request (REQ) or auto request, normal/error interrupts.
#pragma once
#include <cstdint>
#include <functional>

class Hd63450 {
public:
    void reset();
    uint8_t read(int off);              // off = byte offset within the 256-byte register block
    void    write(int off, uint8_t v);

    // request line of a channel (REQ input from the device)
    void set_req(int ch, bool s) { chan[ch].req = s; }
    // PCL input of a channel (level): a falling edge sets PCT (interrupt when DCR selects it)
    void set_pcl(int ch, bool level);
    // run: performs pending transfers; returns the number of bus cycles used
    int  run(int budget);

    std::function<uint8_t(uint32_t)>       mem_r8;
    std::function<void(uint32_t, uint8_t)> mem_w8;
    std::function<void(bool)> irq_cb;
    int  irq_vector() const;            // vector for the pending interrupt (or -1)
    bool irq() const { return irq_state; }
    bool log = false;
    bool active(int ch) const { return chan[ch].csr & 0x08; }

private:
    struct Chan {
        uint8_t csr = 0, cer = 0, dcr = 0, ocr = 0, scr = 0, ccr = 0, cpr = 0;
        uint8_t mfc = 0, dfc = 0, bfc = 0, niv = 0x0f, eiv = 0x0f;
        uint16_t mtc = 0, btc = 0;
        uint32_t mar = 0, dar = 0, bar = 0;
        bool req = false;
        bool pcl = true;
    } chan[4];
    uint8_t gcr = 0;
    bool irq_state = false;
    void update_irq();
    void start(int c);
    void complete(int c);
};
