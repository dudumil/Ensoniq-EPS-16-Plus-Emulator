// Ensoniq EPS-16 Plus machine glue (see eps16.h for the memory map).
#include "u8file.h"
#include "eps16.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#define DBG(name) ([]() { static const bool f = getenv(#name) != nullptr; return f; }())
extern "C" {
#include "m68k.h"
}

Eps16 *g_machine = nullptr;

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return g_machine->read(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return g_machine->read(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return g_machine->read(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { g_machine->write(a, v, 1); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_machine->write(a, v, 2); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { g_machine->write(a, v, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return g_machine->read(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_machine->read(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return g_machine->read(a, 4); }
int asr_int_ack(int level) { return g_machine->int_ack(level); }
void asr_reset_instr(void) {}
void asr_instr_hook(unsigned int pc) { if (g_machine->on_instr) g_machine->on_instr(pc); }
}

Eps16::Eps16() {
    g_machine = this;
    ram.assign(0x80000, 0);
    set_wave_mb(2);
    duart.xtal = 5000000.0;           // X1 = 10 MHz / 2 (sequencer timer: 2500 x 2 / 5 MHz = 1 ms)
    duart.ip2_hz = 1250000.0;         // IP2 = OTIS E clock
    duart.baud[0] = duart.baud[1] = 31250.0;   // IP3/IP4 = 500 kHz (/16)
    duart.ip = 0x7f;
    duart.irq = [this](bool s) { irq_duart = s; update_ipl(); };
    duart.tx = [this](int ch, uint8_t b) {
        if (ch == 0) { if (midi_out) midi_out(b); }
        else {
            if (panel.log) fprintf(stderr, "[PANEL %.4f pc=%06x] %02x %c\n", total_cycles / cpu_clock, last_pc(), b, (b >= 0x20 && b < 0x7f) ? b : '.');
            panel.rx_from_cpu(b);
        }
    };
    duart.op_changed = [this](uint8_t opr) {
        uint8_t pins = ~opr;
        uint8_t old = op_pins; op_pins = pins;
        fdc.side = (pins & 0x02) ? 1 : 0;          // SSEL pin high = side 1
        esp.set_halt((pins & 0x08) != 0);          // EPSHALT high = ESP halted
        if (DBG(EPS_DBG_OP) && old != pins) fprintf(stderr, "OP pins %02x pc=%06x t=%.4f\n", pins, last_pc(), total_cycles / cpu_clock);
    };
    panel.rack = !getenv("EPS_KBD");
    panel.eps = true;
    esp.defer_gpr_reads = !getenv("EPS_NODEFER");
    panel.send = [this](uint8_t b) { if (panel.log) fprintf(stderr, "[PANEL->CPU %.4f] %02x\n", total_cycles / cpu_clock, b); duart.rx_byte(1, b); };
    otis.irq_cb = [this](bool s) { irq_otis = s; update_ipl(); };
    otis.read_sample = [this](int bank, uint32_t addr) -> uint16_t {
        if (!bank || addr >= wave_words) return 0;
        return wave[addr];
    };
    otis.read_pot = [this]() -> uint16_t {
        int sel = ((op_pins >> 6) & 1) | (((op_pins >> 4) & 1) << 1) | (((op_pins >> 5) & 1) << 2);
        return analog[sel] & 0x3ff;
    };
    dmac.mem_r8 = [this](uint32_t a) { return (uint8_t)read(a, 1); };
    dmac.mem_w8 = [this](uint32_t a, uint8_t v) { write(a, v, 1); };
    dmac.irq_cb = [this](bool s) { irq_dmac = s; update_ipl(); };
    fdc.drq_cb = [this](bool s) { dmac.set_req(0, s); };
    fdc.irq_cb = [this](bool s) { dmac.set_pcl(0, !s); };    // INTRQ -> Q1 -> /PCL0
    scsi.irq_cb = [this](bool s) { dmac.set_pcl(1, !s); };   // (assumed) WD33C93 INTRQ -> /PCL1
    if (DBG(EPS_DBG_FDC)) fdc.log = true;
    if (DBG(EPS_DBG_DMA)) dmac.log = true;
    if (DBG(EPS_DBG_PANEL)) panel.log = true;
    if (getenv("EPS_NOFX")) fx_enabled = false;
    if (getenv("EPS_NOAUX1")) aux1_mix = false;
    if (getenv("EPS_SLOWDISK")) fdc.fast = false;
    if (const char *e = getenv("EPS_AN")) { int i = 0; for (const char *p = e; *p && i < 8; i++) { analog[i] = atoi(p); p = strchr(p, ','); if (!p) break; p++; } }
    if (const char *e = getenv("EPS_ESPOUT")) esp_out_port = atoi(e);
}

void Eps16::set_wave_mb(int mb) {
    wave_words = (uint32_t)mb << 19;
    wave.assign(wave_words, 0);
}

bool Eps16::load_rom(const std::string &even, const std::string &odd) {
    FILE *fe = u8fopen(even.c_str(), "rb"), *fo = u8fopen(odd.c_str(), "rb");
    if (!fe || !fo) { if (fe) fclose(fe); if (fo) fclose(fo); return false; }
    std::vector<uint8_t> e(0x10000), o(0x10000);
    size_t ne = fread(e.data(), 1, e.size(), fe), no = fread(o.data(), 1, o.size(), fo);
    fclose(fe); fclose(fo);
    if (ne != no || ne == 0) return false;
    rom.resize(ne * 2);
    for (size_t i = 0; i < ne; i++) { rom[i * 2] = e[i]; rom[i * 2 + 1] = o[i]; }
    return true;
}

// U28 holds the even (high) bytes and U27 the odd (low) bytes.
bool Eps16::load_rom_dir(const std::string &dir) {
    static const char *pairs[][2] = {
        { "eps16plus-u28.bin", "eps16plus-u27.bin" },
        { "u28.bin", "u27.bin" },
        { "eps_hi.bin", "eps_lo.bin" },
    };
    for (auto &p : pairs)
        if (load_rom(dir + "/" + p[0], dir + "/" + p[1])) return true;
    return false;
}

void Eps16::reset() {
    duart.reset();
    dmac.reset();
    fdc.reset();
    otis.reset();
    esp.reset();
    esp.set_halt(true);
    panel.reset();
    scsi.reset();
    // the WD33C93 interrupts once its own reset is complete: INTRQ -> /PCL1 sets PCT in
    // DMAC channel 1, which is how the boot ROM detects that the SCSI option is fitted
    if (scsi_fitted) scsi.raise(0x00);
    op_pins = 0xff;
    boot_overlay = true;
    irq_duart = irq_dmac = irq_otis = false;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    total_cycles = 0;
}

uint32_t Eps16::last_pc() const { return m68k_get_reg(NULL, M68K_REG_PPC); }

// GLU interrupt encoder (IPL1/IPL0): DUART 3, DMAC 2, OTIS 1
void Eps16::update_ipl() {
    int l = irq_duart ? 3 : irq_dmac ? 2 : irq_otis ? 1 : 0;
    m68k_set_irq(l);
}

int Eps16::int_ack(int level) {
    if (level == 3 && irq_duart) return duart.ivr_value();
    if (level == 2 && irq_dmac) { int v = dmac.irq_vector(); if (v >= 0) return v; }
    return M68K_INT_ACK_AUTOVECTOR;
}

uint32_t Eps16::io_read(uint32_t a, int sz) {
    if (sz == 4) return (io_read(a, 2) << 16) | io_read(a + 2, 2);
    uint32_t page = a & 0xfe0000;
    uint8_t v8 = 0xff;
    switch (page) {
    case 0x200000: {
        uint16_t v = otis.read((a >> 1) & 15);
        if (trace_io) fprintf(stderr, "OTIS R %02x = %04x pc=%06x\n", (a >> 1) & 15, v, last_pc());
        return sz == 2 ? v : (a & 1) ? (v & 0xff) : (v >> 8);
    }
    case 0x240000: {
        uint32_t v = sz == 2 ? (dmac.read(a & 0xfe) << 8) | dmac.read((a & 0xfe) | 1) : dmac.read(a & 0xff);
        if (trace_io) fprintf(stderr, "DMAC R %06x = %x pc=%06x\n", a, v, last_pc());
        return v;
    }
    case 0x260000: v8 = esp.host_r((a >> 1) & 0xff); break;
    case 0x280000: v8 = duart.read((a >> 1) & 15); break;
    case 0x2c0000: v8 = fdc.read((a >> 1) & 3); break;
    case 0x300000:
        if (!scsi_fitted) return sz == 1 ? 0xff : 0xffff;
        v8 = scsi.read((a >> 1) & 1); break;
    case 0x340000:
        if (!scsi_fitted) return sz == 1 ? 0xff : 0xffff;
        v8 = scsi.dma_read(); break;
    default:
        if (trace_unknown && unknown_budget > 0) { unknown_budget--; fprintf(stderr, "?? R%d %06x pc=%06x\n", sz * 8, a, last_pc()); }
        return sz == 1 ? 0xff : 0xffff;
    }
    if (trace_io) fprintf(stderr, "IO R %06x = %02x pc=%06x\n", a, v8, last_pc());
    return sz == 1 ? ((a & 1) ? v8 : 0xff) : (0xff00 | v8);
}

void Eps16::io_write(uint32_t a, uint32_t v, int sz) {
    if (sz == 4) { io_write(a, v >> 16, 2); io_write(a + 2, v & 0xffff, 2); return; }
    uint32_t page = a & 0xfe0000;
    if (trace_io && page != 0x200000) fprintf(stderr, "IO W%d %06x = %x pc=%06x\n", sz * 8, a, v, last_pc());
    switch (page) {
    case 0x200000:
        if (trace_io) fprintf(stderr, "OTIS W %02x = %04x pc=%06x\n", (a >> 1) & 15, v, last_pc());
        if (DBG(EPS_DBG_VOL)) { int r = (a >> 1) & 15; if (r <= 11) fprintf(stderr, "V %.5f p%02x r%d %04x sz%d pc=%06x sr=%04x\n", total_cycles / cpu_clock, otis.read(15), r, v, sz, last_pc(), m68k_get_reg(NULL, M68K_REG_SR)); }
        if (sz == 2) otis.write((a >> 1) & 15, v);
        else if (a & 1) otis.write((a >> 1) & 15, v & 0xff, 0x00ff);
        else otis.write((a >> 1) & 15, (v & 0xff) << 8, 0xff00);
        return;
    case 0x240000:
        if (sz == 2) { dmac.write(a & 0xfe, v >> 8); dmac.write((a & 0xfe) | 1, v & 0xff); }
        else dmac.write(a & 0xff, v);
        return;
    }
    if (sz == 1 && !(a & 1)) return;          // 8-bit devices sit on D7-D0
    uint8_t b = v & 0xff;
    switch (page) {
    case 0x260000: esp.host_w((a >> 1) & 0xff, b); return;
    case 0x280000: duart.write((a >> 1) & 15, b); return;
    case 0x2c0000: fdc.write((a >> 1) & 3, b); return;
    case 0x300000: if (scsi_fitted) scsi.write((a >> 1) & 1, b); return;
    case 0x340000: if (scsi_fitted) scsi.dma_write(b); return;
    }
    if (trace_unknown && unknown_budget > 0) { unknown_budget--; fprintf(stderr, "?? W%d %06x = %x pc=%06x\n", sz * 8, a, v, last_pc()); }
}

// CPU $600000-$7FFFFF = OTIS bank 1 (BS = 1), $400000-$5FFFFF = bank 0. The standard
// 1 MB and the 2X expander live in bank 1; returns an index >= wave_words when absent.
uint32_t Eps16::wave_index(uint32_t a) const {
    uint32_t w = (a & 0x1fffff) >> 1;
    if (a >= 0x600000) return w < wave_words ? w : 0xffffffff;
    return 0xffffffff;
}

uint32_t Eps16::read(uint32_t a, int sz) {
    a &= 0xffffff;
    if (a < 0x200000 || a >= 0xf80000) {
        if (boot_overlay && a < 8) {
            uint32_t v = 0; for (int i = 0; i < sz; i++) v = (v << 8) | rom[a + i];
            return v;
        }
        uint32_t o = a & ram_mask;
        if (sz == 1) return ram[o];
        if (sz == 2) return (ram[o] << 8) | ram[(o + 1) & ram_mask];
        return ((uint32_t)ram[o] << 24) | (ram[(o + 1) & ram_mask] << 16) | (ram[(o + 2) & ram_mask] << 8) | ram[(o + 3) & ram_mask];
    }
    if (a >= 0xc00000 && a < 0xc40000) {
        boot_overlay = false;
        uint32_t m = (uint32_t)rom.size() - 1, o = a & m;
        if (sz == 1) return rom[o];
        if (sz == 2) return (rom[o] << 8) | rom[(o + 1) & m];
        return ((uint32_t)rom[o] << 24) | (rom[(o + 1) & m] << 16) | (rom[(o + 2) & m] << 8) | rom[(o + 3) & m];
    }
    if (a >= 0x400000 && a < 0x800000) {
        if (sz == 4) return (read(a, 2) << 16) | read(a + 2, 2);
        uint32_t w = wave_index(a);
        uint16_t v = w < wave_words ? wave[w] : 0xffff;
        return sz == 2 ? v : (a & 1) ? (v & 0xff) : (v >> 8);
    }
    return io_read(a, sz);
}

void Eps16::write(uint32_t a, uint32_t v, int sz) {
    a &= 0xffffff;
    if (a < 0x200000 || a >= 0xf80000) {
        uint32_t o = a & ram_mask;
        if (sz == 1) ram[o] = v;
        else if (sz == 2) { ram[o] = v >> 8; ram[(o + 1) & ram_mask] = v; }
        else { ram[o] = v >> 24; ram[(o + 1) & ram_mask] = v >> 16; ram[(o + 2) & ram_mask] = v >> 8; ram[(o + 3) & ram_mask] = v; }
        return;
    }
    if (a >= 0xc00000 && a < 0xc40000) return;
    if (a >= 0x400000 && a < 0x800000) {
        if (sz == 4) { write(a, v >> 16, 2); write(a + 2, v & 0xffff, 2); return; }
        uint32_t w = wave_index(a);
        if (w >= wave_words) return;
        if (sz == 2) wave[w] = v;
        else if (a & 1) wave[w] = (wave[w] & 0xff00) | (v & 0xff);
        else wave[w] = (wave[w] & 0x00ff) | ((v & 0xff) << 8);
        return;
    }
    io_write(a, v, sz);
}

// ------------------------------------------------------------------ audio
void Eps16::feed_input(const float *lr, size_t frames, double rate) {
    std::lock_guard<std::mutex> g(input_mx);
    input_rate = rate;
    for (size_t i = 0; i < frames; i++) input.push_back(0.5f * (lr[i * 2] + lr[i * 2 + 1]));
    size_t maxs = (size_t)(rate * 0.25);
    while (input.size() > maxs) input.pop_front();
}

float Eps16::next_input() {
    std::lock_guard<std::mutex> g(input_mx);
    double fr = esp_rate;
    if (fr <= 0) return 0;
    in_pos += input_rate / fr;
    while (in_pos >= 1.0 && !input.empty()) { in_last = input.front(); input.pop_front(); in_pos -= 1.0; }
    if (in_pos >= 1.0) in_pos = 0;           // starved: hold the last value
    float a = std::fabs(in_last); if (a > in_peak) in_peak = a;
    return in_last;
}

void Eps16::otis_frame() {
    int32_t ch[8];
    otis.generate_frame(ch);
    // The EPS-16 Plus takes the OTIS serial words the other way round (the OTIS "left" word is
    // the right channel at the outputs and at the ESP): a voice panned left must come out left.
    for (int c = 0; c < 4; c++) std::swap(ch[c * 2], ch[c * 2 + 1]);
    double l = 0, r = 0;
    float aux[6] = {};
    const bool sampen = (op_pins & 0x04) != 0;
    // Serial port: a stereo word takes 32 bits at 2.5 MHz = 128 OTIS clocks. With fewer than 8
    // voices (ACT < 7, used while sampling) a frame is shorter than that, so only every other
    // frame is latched and sent to the ESP (OTIS spec 4.5): the ESP then runs at half the frame rate.
    const int frame_clocks = 16 * (otis.active_voices() + 1);
    ser_clock += frame_clocks;
    const bool ser_word = ser_clock >= ser_next;
    if (ser_word) ser_next = ser_clock + 128;
    if (ser_word) {   // the ESP always runs (the OS talks to it); with the effects off its output is bypassed
        for (int p = 0; p < 4; p++) {
            bool on = p < 2 || bus_to_main[p - 2];    // BUS 2/3 can be kept out of the main mix
            esp.ser_w(p * 2, on ? (int16_t)ch[p * 2] : 0); esp.ser_w(p * 2 + 1, on ? (int16_t)ch[p * 2 + 1] : 0);
        }
        if (sampen) {
            esp_rate = otis_clock / (double)(frame_clocks * ((128 + frame_clocks - 1) / frame_clocks));
            // SAMPEN: the 74LS157 puts the PCM78 A/D converter (mono) on the ESP's SER0 input
            float v = next_input();
            int iv = (int)(v * 32767.0f); iv = iv > 32767 ? 32767 : iv < -32768 ? -32768 : iv;
            esp.ser_w(0, (int16_t)iv); esp.ser_w(1, (int16_t)iv);
        }
        esp.run_frame();
        if (DBG(EPS_DBG_SER)) {
            static int16_t pk[16]; static long n = 0;
            for (int i = 0; i < 8; i++) { pk[i] = std::max<int16_t>(pk[i], std::abs(ch[i])); pk[8 + i] = std::max<int16_t>(pk[8 + i], std::abs(esp.ser_r(i))); }
            if (++n % 30000 == 0) { fprintf(stderr, "SER in"); for (int i = 0; i < 8; i++) fprintf(stderr, " %d", pk[i]); fprintf(stderr, " | out"); for (int i = 8; i < 16; i++) fprintf(stderr, " %d", pk[i]); fprintf(stderr, " halted=%d\n", esp.halted()); for (auto &x : pk) x = 0; }
        }
    }
    {
        l = esp.ser_r(esp_out_port * 2); r = esp.ser_r(esp_out_port * 2 + 1);
        if (esp.halted()) l = r = 0;
        if (!fx_enabled) { l = r = 0; for (int c = 0; c < 4; c++) if (c == 0 || (c >= 2 && bus_to_main[c - 2])) { l += ch[c * 2]; r += ch[c * 2 + 1]; } }
        if (aux1_mix) { l += ch[2]; r += ch[3]; }   // AUX 1 (no effect program reads it)
        // OEX-6 outputs: the OTIS channels 1-3 (latched serial data, DSER1-3) straight from the OTIS
        for (int a = 0; a < 3; a++) { aux[a * 2] = ch[(1 + a) * 2] / 32768.0f; aux[a * 2 + 1] = ch[(1 + a) * 2 + 1] / 32768.0f; }
    }
    const float k = 1.0f / 32768.0f;
    float cur[8] = { (float)l * k, (float)r * k };
    for (int a = 0; a < 3; a++)
        if (!aux_to_main[a]) { cur[2 + a * 2] = aux[a * 2]; cur[3 + a * 2] = aux[a * 2 + 1]; }
    const bool sep = aux_separate();
    double step = otis.sample_rate(otis_clock) / out_rate;
    while (resamp_pos < 1.0) {
        float t = (float)resamp_pos;
        audio.push_back(last[0] + (cur[0] - last[0]) * t);
        audio.push_back(last[1] + (cur[1] - last[1]) * t);
        if (sep) for (int i = 2; i < 8; i++) audio_aux.push_back(last[i] + (cur[i] - last[i]) * t);
        resamp_pos += step;
    }
    resamp_pos -= 1.0;
    for (int i = 0; i < 8; i++) last[i] = cur[i];
}

// ------------------------------------------------------------------ disk
bool Eps16::insert_disk(const std::string &path) {
    if (fdc.has_disk()) fdc.save();
    bool ok = fdc.load(path);
    dskchg = false;                         // /DSKCHG stays low until the next step pulse
    return ok;
}

void Eps16::eject_disk() { fdc.eject(); dskchg = false; }

void Eps16::run(int64_t clocks) {
    int64_t target = total_cycles + clocks;
    while ((int64_t)total_cycles < target) {
        int c = m68k_execute(16);
        if (dmac.active(0) || dmac.active(1)) c += dmac.run(64);
        total_cycles += c;
        fdc.run(c);
        if (scsi_fitted) dmac.set_req(1, scsi.dreq());
        duart_acc += c * (duart.xtal / cpu_clock);
        if (duart_acc >= 16) { duart.run(duart_acc); duart_acc = 0; }
        otis_acc += c * (otis_clock / cpu_clock);
        double frame_clocks = 16.0 * (otis.active_voices() + 1);
        while (otis_acc >= frame_clocks) { otis_acc -= frame_clocks; otis_frame(); }
        // drive status inputs
        duart.set_ip(0, !fdc.has_disk());   // DSTAT: low = disk in the drive
        if (fdc.step_count != last_steps) { last_steps = fdc.step_count; if (fdc.has_disk()) dskchg = true; }
        duart.set_ip(1, dskchg);            // /DSKCHG: low after a disk change until the head steps
    }
}
