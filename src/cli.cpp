#include <cmath>
// Command-line runner for the EPS-16 Plus emulator (headless, for testing).
//   eps16-cli [-r romdir] [-d disk.img] [-t seconds] [-k t:button ...] [-w out.wav]
#include "eps16.h"
#include "efe_disk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
extern "C" {
#include "m68k.h"
}

static void wav_write(const char *path, const std::vector<float> &s, int rate) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    uint32_t n = s.size() * 2, v;
    fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); uint16_t h = 1; fwrite(&h, 2, 1, f); h = 2; fwrite(&h, 2, 1, f);
    v = rate; fwrite(&v, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f); h = 4; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    for (float x : s) { int i = (int)(x * 32767); i = i > 32767 ? 32767 : i < -32768 ? -32768 : i; int16_t q = i; fwrite(&q, 2, 1, f); }
    fclose(f);
}

int main(int argc, char **argv) {
    std::string romdir = "roms", disk, wav;
    double secs = 10;
    struct Ev { double t; int code; bool down; };
    std::vector<Ev> evs;
    struct Note { double t; int ch, n, v; };
    std::vector<Note> notes;
    double in_freq = 0, in_amp = 0, in_ph = 0;
    std::vector<std::pair<double, std::string>> swaps;
    std::vector<std::string> scsi_files;
    std::vector<Ev> pkeys;
    bool noscsi = false;
    struct AV { double t; int c, v; };
    std::vector<AV> avals;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-r" && i + 1 < argc) romdir = argv[++i];
        else if (a == "-d" && i + 1 < argc) disk = argv[++i];
        else if (a == "-t" && i + 1 < argc) secs = atof(argv[++i]);
        else if (a == "-w" && i + 1 < argc) wav = argv[++i];
        else if (a == "-n" && i + 1 < argc) {   // t:note:vel:dur[:chan]  MIDI note
            double t, d = 1; int n, v = 100, ch = 1; sscanf(argv[++i], "%lf:%d:%d:%lf:%d", &t, &n, &v, &d, &ch);
            evs.push_back({ t, 0x10000 | ((ch - 1) << 16 << 4) | (0x90 << 8) | n, (bool)v });
            notes.push_back({ t, ch, n, v }); notes.push_back({ t + d, ch, n, 0 });
        }
        else if (a == "-s" && i + 1 < argc) {   // t:image  swap the floppy
            std::string v = argv[++i]; size_t c = v.find(':');
            swaps.push_back({ atof(v.substr(0, c).c_str()), v.substr(c + 1) });
        }
        else if (a == "-p" && i + 1 < argc) {   // t:key  panel keyboard key (0..60), held 1 s
            double t; int k; sscanf(argv[++i], "%lf:%d", &t, &k); pkeys.push_back({ t, k, true }); pkeys.push_back({ t + 1, k, false });
        }
        else if (a == "-a" && i + 1 < argc) {   // t:channel:value  analog input (0..1023)
            double t; int c, v; sscanf(argv[++i], "%lf:%d:%d", &t, &c, &v); avals.push_back({ t, c, v });
        }
        else if (a == "--no-scsi") noscsi = true;
        else if (a == "-h" && i + 1 < argc) { scsi_files.push_back(argv[++i]); }
        else if (a == "-i" && i + 1 < argc) {   // freq:amp  sine wave on the sampling input
            sscanf(argv[++i], "%lf:%lf", &in_freq, &in_amp);
        }
        else if (a == "-k" && i + 1 < argc) {   // t:code (press 0.1 s)
            double t, d = 0.1; int c; sscanf(argv[++i], "%lf:%i:%lf", &t, &c, &d);   // t:code[:hold seconds]
            evs.push_back({ t, c, true }); evs.push_back({ t + d, c, false });
        }
    }
    Eps16 m;
    if (!m.load_rom_dir(romdir)) { fprintf(stderr, "no ROM in %s\n", romdir.c_str()); return 1; }
    if (getenv("EPS_TRACE_IO")) m.trace_io = true;
    m.scsi_fitted = !noscsi;
    m.reset();
    if (const char *e = getenv("EPS_RING")) {   // dump the last instructions when PC enters [lo,hi]
        static uint32_t lo, hi; sscanf(e, "%x:%x", &lo, &hi);
        static uint32_t ring[256]; static int rp = 0; static bool done = false;
        m.on_instr = [](uint32_t pc) {
            ring[rp++ & 255] = pc;
            if (!done && pc >= lo && pc <= hi) {
                done = true;
                for (int i = 0; i < 256; i++) { uint32_t p = ring[(rp + i) & 255]; char b[128]; m68k_disassemble(b, p, M68K_CPU_TYPE_68000); fprintf(stderr, "%06x %s\n", p, b); }
                fprintf(stderr, "D0=%08x D1=%08x A0=%08x A1=%08x A7=%08x SR=%04x\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A7), m68k_get_reg(NULL, M68K_REG_SR));
            }
        };
    }
    if (const char *e = getenv("EPS_RING_OP")) {   // dump the last instructions before opcode X (after skipping K)
        static uint32_t op; static int skip = 0; sscanf(e, "%x", &op);
        if (const char *k = getenv("EPS_RING_SKIP")) skip = atoi(k);
        static uint32_t ring[400]; static int rp = 0; static bool done = false; static Eps16 *mm; mm = &m;
        m.on_instr = [](uint32_t pc) {
            ring[rp++ % 400] = pc;
            if (!done && mm->read(pc, 2) == op && skip-- <= 0) {
                done = true;
                for (int i = 0; i < 400; i++) { uint32_t p = ring[(rp + i) % 400]; char b[128]; m68k_disassemble(b, p, M68K_CPU_TYPE_68000); fprintf(stderr, "%06x %s\n", p, b); }
                fprintf(stderr, "D0=%08x D1=%08x D2=%08x A0=%08x A1=%08x A7=%08x SR=%04x\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A7), m68k_get_reg(NULL, M68K_REG_SR));
            }
        };
    }
    if (const char *e = getenv("EPS_FLOW")) {   // log jump targets (non-sequential PCs) between t0 and t1
        static double t0, t1; sscanf(e, "%lf:%lf", &t0, &t1);
        static uint32_t prev = 0; static Eps16 *mm; mm = &m;
        m.on_instr = [](uint32_t pc) {
            double t = mm->cycles() / mm->cpu_clock;
            if (t >= t0 && t < t1 && (pc < prev || pc > prev + 10)) fprintf(stderr, "%.5f %06x <- %06x\n", t, pc, prev);
            prev = pc;
        };
    }
    if (const char *e = getenv("EPS_TRACE_N")) {
        static long n = atol(e); static uint32_t lo = 0, hi = 0xffffff;
        if (const char *r = getenv("EPS_TRACE_RANGE")) sscanf(r, "%x:%x", &lo, &hi);
        m.on_instr = [](uint32_t pc) { if (n > 0 && pc >= lo && pc <= hi) { n--; char b[128]; m68k_disassemble(b, pc, M68K_CPU_TYPE_68000); fprintf(stderr, "%06x %s  D0=%08x A0=%08x A7=%08x\n", pc, b, m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A7)); } };
    }
    if (const char *e = getenv("EPS_DISASM")) {
        uint32_t lo, hi; sscanf(e, "%x:%x", &lo, &hi);
        for (uint32_t p = lo; p < hi;) { char b[128]; int n = m68k_disassemble(b, p, M68K_CPU_TYPE_68000); printf("%06x %s\n", p, b); p += n; }
        return 0;
    }
    for (auto &f : scsi_files) fprintf(stderr, "%s\n", m.scsi.attach_file(f).c_str());
    if (!disk.empty() && efe::is_single_file(disk)) {   // an EFE: put it on a temporary floppy
        std::string path, label, err = efe::make_disk_file({ disk }, false, "eps16-cli-", path, label);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        disk = path;
    }
    for (auto &sw : swaps) if (efe::is_single_file(sw.second)) {
        std::string path, label, err = efe::make_disk_file({ sw.second }, false, "eps16-cli-", path, label);
        if (err.empty()) sw.second = path; else fprintf(stderr, "%s\n", err.c_str());
    }
    if (!disk.empty() && !m.insert_disk(disk)) fprintf(stderr, "cannot open %s\n", disk.c_str());
    std::string lastd;
    std::vector<float> out;
    double step = 0.01;
    std::map<uint32_t, int> pcs;
    bool pcprof = getenv("EPS_PCPROF") != nullptr;
    const double fdc_log_t = getenv("EPS_FDC_LOG") ? atof(getenv("EPS_FDC_LOG")) : -1;   // log floppy commands from time t
    for (double t = 0; t < secs; t += step) {
        if (fdc_log_t >= 0 && t >= fdc_log_t && !m.fdc.log) { m.fdc.log = true; fprintf(stderr, "[%.2f fdc log on]\n", t); }
        for (auto &e : evs) if (e.t >= t && e.t < t + step && e.code < 0x10000) m.panel.button(e.code, e.down);
        for (auto &sw : swaps) if (sw.first >= t && sw.first < t + step) { m.eject_disk(); m.insert_disk(sw.second); printf("[swap %s]\n", sw.second.c_str()); }
        for (auto &e : avals) if (e.t >= t && e.t < t + step) m.analog[e.c & 7] = (uint16_t)e.v;
        for (auto &e : pkeys) if (e.t >= t && e.t < t + step) m.panel.key(e.code, 100, e.down);
        for (auto &n : notes) if (n.t >= t && n.t < t + step) { m.midi_in(0x90 | (n.ch - 1)); m.midi_in(n.n); m.midi_in(n.v); }
        if (in_freq > 0) {
            std::vector<float> buf; int n = (int)(44100 * step);
            for (int k = 0; k < n; k++) { float v = (float)(in_amp * sin(in_ph)); in_ph += 2 * M_PI * in_freq / 44100; buf.push_back(v); buf.push_back(v); }
            m.feed_input(buf.data(), n, 44100);
        }
        m.run((int64_t)(m.cpu_clock * step));
        if (pcprof) pcs[m.last_pc() & ~0xf]++;
        std::string d = m.panel.text() + " | " + m.panel.annunciators();
        if (d != lastd) { printf("%7.3f |%s\n", t, d.c_str()); fflush(stdout); lastd = d; }
        out.insert(out.end(), m.audio.begin(), m.audio.end()); m.audio.clear();
    }
    printf("final pc=%06x meter=%d ind=", m.last_pc(), m.panel.meter);
    for (int i = 0; i < 16; i++) printf("%d", m.panel.ind[i]);
    printf("\n");
    if (pcprof) { std::vector<std::pair<int, uint32_t>> v; for (auto &p : pcs) v.push_back({ p.second, p.first }); std::sort(v.rbegin(), v.rend()); for (int i = 0; i < 15 && i < (int)v.size(); i++) printf("  pc %06x x%d\n", v[i].second, v[i].first); }
    if (const char *e = getenv("EPS_DISASM_END")) {
        uint32_t lo, hi; sscanf(e, "%x:%x", &lo, &hi);
        for (uint32_t p = lo; p < hi;) { char b[128]; int n = m68k_disassemble(b, p, M68K_CPU_TYPE_68000); printf("%06x %s\n", p, b); p += n; }
    }
    if (getenv("EPS_DUMP_OTIS")) m.otis.dump(stdout);
    if (getenv("EPS_DUMP_ESP")) { m.esp.list_program(stdout); printf("esp halted=%d\n", m.esp.halted()); }
    if (const char *f = getenv("EPS_DUMP_WAVE")) { FILE *o = fopen(f, "wb"); fwrite(m.wave.data(), 2, m.wave.size(), o); fclose(o); }
    if (const char *f = getenv("EPS_DUMP_RAM")) { FILE *o = fopen(f, "wb"); fwrite(m.ram.data(), 1, m.ram.size(), o); fclose(o); }
    if (getenv("EPS_SAVE_DISK") && m.fdc.dirty) m.fdc.save();   // write the floppy back
    if (!wav.empty()) wav_write(wav.c_str(), out, 44100);
    return 0;
}
