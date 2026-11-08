// Ensoniq EPS-16 Plus (rack): MC68000 @ 10 MHz + ES5700 GLU + ES5701 Super-GLU,
// ES5505 OTIS, ES5510 ESP, HD68440 DMAC, MC68681 DUART, WD1772 FDC, optional WD33C93 SCSI.
//
// Memory map (from the boot ROM and the main board schematic):
//   000000-07FFFF  OS DRAM (mirrored at F80000-FFFFFF); boot ROM overlaid at 0 for the reset vectors
//   200000-20001F  ES5505 OTIS (16-bit, A4-A1 = register)
//   240000-2400FF  HD68440 DMAC (ch0 = floppy, ch1 = SCSI)
//   260000-2601FF  ES5510 ESP (odd bytes, A8-A1 = host register)
//   280000-28001F  MC68681 DUART (odd bytes): A = MIDI, B = front panel
//   2C0000-2C0007  WD1772 FDC (odd bytes)
//   300000-300003  WD33C93 SCSI (optional), 340000 its DMA data port
//   600000-7FFFFF  sound memory (1M x 16 max, OTIS bank BS=1), 400000-5FFFFF bank 0 (not fitted)
//   C00000-C1FFFF  boot ROM (2 x 27C512)
// DUART outputs (pins = inverted OPR): OP0 /DSEL, OP1 SSEL, OP2 SAMPEN, OP3 ESP HALT,
//   OP4 AN1, OP5 AN2, OP6 AN0, OP7 LINE/MIC.  Inputs: IP0 DSTAT, IP1 /DSKCHG, IP2 E (1.25 MHz).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <atomic>
#include "duart.h"
#include "wd1772.h"
#include "hd63450.h"
#include "es5505.h"
#include "es5510.h"
#include "panel.h"
#include "scsi.h"

class Eps16 {
public:
    Eps16();
    bool load_rom(const std::string &even, const std::string &odd);
    bool load_rom_dir(const std::string &dir);
    void set_wave_mb(int mb);              // sound memory: 1, 2 or 4 MB
    void reset();
    void run(int64_t clocks);
    uint64_t cycles() const { return total_cycles; }

    bool insert_disk(const std::string &path);
    void eject_disk();

    void feed_input(const float *lr, size_t frames, double rate);
    float in_peak = 0;

    void midi_in(uint8_t b) { duart.rx_byte(0, b); }
    std::function<void(uint8_t)> midi_out;

    double out_rate = 44100.0;
    std::vector<float> audio;              // interleaved L/R at out_rate
    bool aux_to_main[3] = { true, true, true };
    std::vector<float> audio_aux;          // 6 channels when an output is taken out of the mix
    bool aux_separate() const { return !aux_to_main[0] || !aux_to_main[1] || !aux_to_main[2]; }
    bool fx_enabled = true;
    bool aux1_mix = true;                  // AUX 1 bus (OTIS channel 1) mixed dry into the main outputs
    bool bus_to_main[2] = { true, true };  // BUS 2 / BUS 3 reach the ESP (main mix); off = OEX-6 outputs only

    uint32_t read(uint32_t a, int sz);
    void     write(uint32_t a, uint32_t v, int sz);
    int      int_ack(int level);

    Duart   duart;
    Wd1772  fdc;
    Hd63450 dmac;
    ES5505  otis;
    ES5510  esp;
    Panel   panel;
    Wd33c93 scsi;
    bool scsi_fitted = true;

    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;              // OS RAM
    uint32_t ram_mask = 0x7ffff;
    std::vector<uint16_t> wave;            // sound memory
    uint32_t wave_words = 0;

    double cpu_clock = 10000000.0;
    double otis_clock = 10000000.0;
    int esp_out_port = 1;                  // ESP serial output feeding the main DAC
    uint8_t op_pins = 0xff;                // DUART output pins
    // analog mux inputs (AN2-AN0): values 0..1023
    enum { AN_PITCH = 0, AN_MOD = 2, AN_VOLUME = 3, AN_PEDAL = 4, AN_DATA = 5, AN_REF = 7 };
    uint16_t analog[8] = { 512, 0, 0, 1023, 1023, 627, 0, 600 };

    bool trace_io = false, trace_unknown = true;
    uint32_t last_pc() const;
    void seed_pot_filter(uint16_t v);
    std::function<void(uint32_t)> on_instr;

private:
    uint64_t total_cycles = 0;
    double otis_acc = 0, duart_acc = 0;
    bool boot_overlay = true;
    bool irq_duart = false, irq_dmac = false, irq_otis = false;
    void update_ipl();
    void otis_frame();
    float next_input();
    uint64_t ser_clock = 0, ser_next = 0;  // OTIS clocks, for the serial word timing
    double esp_rate = 44643;
    double resamp_pos = 0;
    float last[8] = {};
    std::mutex input_mx;
    std::deque<float> input;
    double input_rate = 44100, in_pos = 0;
    float in_last = 0;
    int unknown_budget = 300;
    bool dskchg = false;                   // /DSKCHG pin state (true = high = no change)
    unsigned last_steps = 0;
    uint32_t io_read(uint32_t a, int sz);
    uint32_t wave_index(uint32_t a) const;
    void io_write(uint32_t a, uint32_t v, int sz);
};

extern Eps16 *g_machine;
