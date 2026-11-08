// EPS-16 Plus emulator - SDL2 front end
//   rack front panel (display, LEDs, 38 buttons, sliders, drive), 61-note keyboard, audio output,
//   MIDI in/out (RtMidi, optional), floppy image insert/eject (drag & drop).
#include "eps16.h"
#include "vfd.h"
#include "rack_ui.h"
#include "efe_disk.h"
#include "ui_text.h"
#include "native_menu.h"
#include "win/eps16_icon64.h"
#include "audio_io.h"
#include "settings.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <filesystem>
#include "../third_party/font8x8_basic.h"
#ifdef EPS_WITH_RTMIDI
#include "../third_party/RtMidi.h"
#endif

// ------------------------------------------------------------------ window layout
// The rack front panel fills the top (see rack_ui.*); below it a strip with pitch/mod wheels
// and a 61-note keyboard (the rack has none: it is for playing with the mouse or the computer
// keyboard), then the status lines.
static const int W = 1400, H = 606;                    // full window (with the keyboard strip)                    // full window (with the keyboard strip)
static const int STRIP_Y = 398, STATUS_Y = 606;
static const int STATUS_H = 0;                         // (the status line is gone: menus + window title)
// current layout: the keyboard strip can be hidden (F9 / TECLADO), leaving just the rack
static int H_cur = H, status_y = STATUS_Y;

// ------------------------------------------------------------------ drawing
static SDL_Renderer *R;

static void fill(int x, int y, int w, int h, uint32_t c) {
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, 255);
    SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(R, &r);
}
static void frame(int x, int y, int w, int h, uint32_t c) {
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, 255);
    SDL_Rect r{x, y, w, h}; SDL_RenderDrawRect(R, &r);
}
static void glyph(int x, int y, unsigned char ch, int s, uint32_t c, bool dots = false) {
    if (ch > 127) ch = '?';
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, 255);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = (uint8_t)font8x8_basic[ch][row];
        for (int col = 0; col < 8; col++) if (bits & (1 << col)) {
            SDL_Rect r{x + col * s, y + row * s, dots ? s - 1 : s, dots ? s - 1 : s};
            SDL_RenderFillRect(R, &r);
        }
    }
}
static void text(int x, int y, const char *t, int s, uint32_t c) {
    for (; *t; t++, x += 8 * s) glyph(x, y, (unsigned char)*t, s, c);
}
static void text_c(int cx, int y, const char *t, int s, uint32_t c) {
    // centred; '\n' starts a new line (block is vertically centred on y)
    std::vector<std::string> lines(1);
    for (const char *p = t; *p; p++) { if (*p == '\n') lines.emplace_back(); else lines.back() += *p; }
    int y0 = y - (int)(lines.size() - 1) * 5 * s;
    for (size_t i = 0; i < lines.size(); i++)
        text(cx - (int)lines[i].size() * 4 * s, y0 + (int)i * 10 * s, lines[i].c_str(), s, c);
}

// ------------------------------------------------------------------ keyboard
static const int KEYS = 61, KB_X = 176, KB_Y = 412, KB_W = 1206, KB_H = 182;
static bool is_black(int k) { int n = k % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
static int white_index(int k) { static const int wi[12] = {0,0,1,1,2,3,3,4,4,5,5,6}; return (k / 12) * 7 + wi[k % 12]; }
static const int WHITE = 36;
static SDL_Rect key_rect(int k) {
    double ww = (double)KB_W / WHITE;
    if (!is_black(k)) return SDL_Rect{KB_X + (int)(white_index(k) * ww), KB_Y, (int)ww - 1, KB_H};
    int x = KB_X + (int)((white_index(k) + 1) * ww - ww * 0.32);
    return SDL_Rect{x, KB_Y, (int)(ww * 0.64), KB_H * 62 / 100};
}
static int key_at(int x, int y) {
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < KEYS; k++) {
            if (is_black(k) != (pass == 0)) continue;
            SDL_Rect r = key_rect(k);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return k;
        }
    return -1;
}

// ------------------------------------------------------------------ analog controls
// value 0..1 mapped onto the 10-bit A/D channel of the controller
struct Ctl { int ch; const char *label; float x, y, w, h; bool vertical; bool spring; double val; bool on_rack; };
static std::vector<Ctl> make_controls(const RackUI &rack) {
    RackUI::Slider v = rack.volume_slider(), d = rack.data_slider();
    return {
        { Eps16::AN_PITCH,  "PITCH", 26,  KB_Y, 52, KB_H - 64, true,  true,  0.5, false },
        { Eps16::AN_MOD,    "MOD",   96,  KB_Y, 52, KB_H - 64, true,  false, 0.0, false },
        { Eps16::AN_DATA,   "DATA ENTRY", d.x, d.y, d.w, d.h, true, false, 0.5, true },
        { Eps16::AN_VOLUME, "VOLUME",     v.x, v.y, v.w, v.h, true, false, 1.0, true },
    };
}
static uint16_t ctl_raw(const Ctl &c) {
    double v = c.val < 0 ? 0 : c.val > 1 ? 1 : c.val;
    // The OS reads the sliders against the reference input (AN7 = 600): Volume is at maximum from
    // ~1.6 x the reference, Data Entry spans about 0.48..1.6 x the reference (-99..+99 measured).
    if (c.ch == Eps16::AN_VOLUME) return (uint16_t)(v * 1023);
    // A real Data Entry slider never reads exactly 0 at the bottom. The OS treats a reading of 0
    // as a moving slider and keeps forcing the value to the minimum, which locked the arrows.
    if (c.ch == Eps16::AN_DATA) return (uint16_t)(280 + v * (985 - 280));
    return (uint16_t)(v * 1023);
}
static bool ctl_hit(const Ctl &c, int x, int y) {      // the knob overhangs the travel a little
    float m = c.on_rack ? 16 : 0;
    return x >= c.x && x < c.x + c.w && y >= c.y - m && y < c.y + c.h + m;
}
static void ctl_from_mouse(Ctl &c, int x, int y) {
    c.val = c.vertical ? 1.0 - (double)(y - c.y) / c.h : (double)(x - c.x) / c.w;
    if (c.val < 0) c.val = 0; if (c.val > 1) c.val = 1;
}

// ------------------------------------------------------------------ audio ring buffer
// Single producer (emulation thread) / single consumer (audio callback). One frame = 8 floats:
// main L/R, AUX1 L/R, AUX2 L/R, AUX3 L/R.
struct AudioRing {
    static constexpr int CH = 8;
    explicit AudioRing(size_t frames) : buf(frames * CH), cap(frames) {}
    size_t size() const { return w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire); }
    bool push(const float *f) {
        size_t wi = w.load(std::memory_order_relaxed);
        if (wi - r.load(std::memory_order_acquire) >= cap) return false;
        memcpy(&buf[(wi % cap) * CH], f, CH * sizeof(float));
        w.store(wi + 1, std::memory_order_release);
        return true;
    }
    bool pop(float *f) {
        size_t ri = r.load(std::memory_order_relaxed);
        if (ri == w.load(std::memory_order_acquire)) return false;
        memcpy(f, &buf[(ri % cap) * CH], CH * sizeof(float));
        r.store(ri + 1, std::memory_order_release);
        return true;
    }
    void reset() { r.store(0); w.store(0); }      // only while neither side is running
private:
    std::vector<float> buf;
    size_t cap;
    std::atomic<size_t> w{0}, r{0};
};

// ------------------------------------------------------------------ MIDI ports
// Input bytes are queued by the RtMidi thread and fed to the machine by the emulation thread.
struct MidiPorts {
    std::mutex qmx, pmx;                 // queue / ports (the input callback only takes qmx)
    std::deque<uint8_t> q;
    std::string in_name, out_name, error;
#ifdef EPS_WITH_RTMIDI
    std::unique_ptr<RtMidiIn> in;
    std::unique_ptr<RtMidiOut> out;
#endif
    std::vector<std::string> in_ports() {
        std::vector<std::string> r;
#ifdef EPS_WITH_RTMIDI
        try { RtMidiIn p; for (unsigned i = 0; i < p.getPortCount(); i++) r.push_back(p.getPortName(i)); } catch (...) {}
#endif
        return r;
    }
    std::vector<std::string> out_ports() {
        std::vector<std::string> r;
#ifdef EPS_WITH_RTMIDI
        try { RtMidiOut p; for (unsigned i = 0; i < p.getPortCount(); i++) r.push_back(p.getPortName(i)); } catch (...) {}
#endif
        return r;
    }
    // open the named ports ("" = none); returns false and sets `error` if something failed
    bool apply(const std::string &inn, const std::string &outn) {
        std::lock_guard<std::mutex> g(pmx);
        error.clear();
#ifdef EPS_WITH_RTMIDI
        in.reset(); out.reset(); in_name.clear(); out_name.clear();
        try {
            if (!inn.empty()) {
                std::unique_ptr<RtMidiIn> p(new RtMidiIn());
                for (unsigned i = 0; i < p->getPortCount(); i++) if (p->getPortName(i) == inn) {
                    p->openPort(i, "EPS-16 Plus in");
                    p->ignoreTypes(false, true, true);   // keep SysEx, drop timing clock and active sensing
                    p->setCallback([](double, std::vector<unsigned char> *msg, void *ud) {
                        auto *self = (MidiPorts *)ud;
                        std::lock_guard<std::mutex> g2(self->qmx);
                        for (auto b : *msg) self->q.push_back(b);
                    }, this);
                    in = std::move(p); in_name = inn; break;
                }
                if (!in) error = "MIDI In not found: " + inn;
            }
        } catch (std::exception &e) { error = std::string("MIDI In: ") + e.what(); in.reset(); }
        try {
            if (!outn.empty()) {
                std::unique_ptr<RtMidiOut> p(new RtMidiOut());
                for (unsigned i = 0; i < p->getPortCount(); i++) if (p->getPortName(i) == outn) {
                    p->openPort(i, "EPS-16 Plus out"); out = std::move(p); out_name = outn; break;
                }
                if (!out && error.empty()) error = "MIDI Out not found: " + outn;
            }
        } catch (std::exception &e) { if (error.empty()) error = std::string("MIDI Out: ") + e.what(); out.reset(); }
#else
        if (!inn.empty() || !outn.empty()) error = "this build has no MIDI support";
#endif
        return error.empty();
    }
    void send(std::vector<unsigned char> &msg) {
        std::lock_guard<std::mutex> g(pmx);
#ifdef EPS_WITH_RTMIDI
        if (out) { try { out->sendMessage(&msg); } catch (...) {} }
#else
        (void)msg;
#endif
    }
    void close() {
        std::lock_guard<std::mutex> g(pmx);
#ifdef EPS_WITH_RTMIDI
        in.reset(); out.reset();
#endif
    }
};

// ------------------------------------------------------------------ text helpers
// the 8x8 font is plain ASCII: fold UTF-8 (device names) to ASCII for display
static std::string ascii(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { r += (char)c; continue; }
        unsigned cp = 0; int extra = 0;
        if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; } else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; } else { r += '?'; continue; }
        for (int k = 0; k < extra && i + 1 < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[++i] & 0x3f);
        static const char *latin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
        r += (cp >= 0xc0 && cp <= 0xff) ? latin[cp - 0xc0] : '?';
    }
    return r;
}
// upper case for UTF-8 text with Latin-1 accented letters (a toupper() per byte breaks them)
static std::string upper_utf8(const std::string &s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++) {
        unsigned char c = (unsigned char)r[i];
        if (c < 0x80) r[i] = (char)toupper(c);
        else if (c == 0xc3 && i + 1 < r.size()) {
            unsigned char d = (unsigned char)r[i + 1];
            if (d >= 0xa0 && d <= 0xbe && d != 0xb7) r[i + 1] = (char)(d - 0x20);
            i++;
        }
    }
    return r;
}
static std::string clip(const std::string &s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 2) + ".."; }

// ------------------------------------------------------------------ main
static void usage() {
    printf("eps16 [--disk image.img] [--mem MB] [--u28 u28.bin --u27 u27.bin] [--midi-in N] [--midi-out N]\n"
           "      [--list-midi] [--readonly] [--no-fx] [--real-disk-speed] [--scale F]\n"
           "      [--audio-driver ID] [--audio-in] [--list-audio] [--input-wav file.wav]\n"
           "      [--scsi-dir DIR] [--scsi ID:image[:cd]] [--no-scsi]\n"
           "Audio and MIDI devices are chosen on the options page (F10) and saved in eps16.cfg.\n");
}

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetConsoleProcessList(unsigned long *, unsigned long);
extern "C" __declspec(dllimport) int __stdcall FreeConsole(void);
#endif

// first disk image in dir: os130.img if present, otherwise the first *.img (alphabetically)
static std::string find_disk(const std::string &dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const char *n : { "/os130.img", "/Ensoniq_EPS16_OS_v130_untouched_Omniflop_format.img" })
        if (fs::exists(dir + n, ec)) return dir + n;
    std::vector<std::string> imgs;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (auto &c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".img" && e.is_regular_file(ec)) imgs.push_back(e.path().string());
    }
    std::sort(imgs.begin(), imgs.end());
    return imgs.empty() ? "" : imgs[0];
}

static void fatal(const std::string &msg) {
    fprintf(stderr, "%s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ensoniq EPS-16 Plus", msg.c_str(), nullptr);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    {   // started by double-click: the console window belongs to us alone, so hide it
        unsigned long pids[2];
        if (GetConsoleProcessList(pids, 2) == 1) FreeConsole();
    }
#endif
    std::string rom_dir = "roms"; bool lo_set = false, hi_set = false;
    std::string lo = "roms/eps16plus-u28.bin", hi = "roms/eps16plus-u27.bin", disk;   // lo = even (U28), hi = odd (U27)
    int ram_mb = 2, midi_in_port = -2, midi_out_port = -2;    // 2 MB sound memory = maximum expansion
    bool readonly = false, list_midi = false, nofx = false, real_disk = false;
    std::vector<std::string> scsi_dirs, scsi_specs, scsi_files; bool no_scsi = false;
    double scale = 1.0;
    bool audio_in = false, list_audio = false; std::string audio_driver;
    std::string input_wav_path; double input_wav_at = -1;
    double shot_at = -1, quit_at = -1; std::string shot_path;   // testing aids
    bool open_opts_at_start = false;
    std::vector<std::pair<double,int>> auto_buttons;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--disk") disk = next();
        else if (a == "--u28" || a == "--lo") { lo = next(); lo_set = true; }
        else if (a == "--u27" || a == "--hi") { hi = next(); hi_set = true; }
        else if (a == "--roms") rom_dir = next();
        else if (a == "--mem" || a == "--ram") ram_mb = atoi(next().c_str());
        else if (a == "--midi-in") midi_in_port = atoi(next().c_str());
        else if (a == "--midi-out") midi_out_port = atoi(next().c_str());
        else if (a == "--list-midi") list_midi = true;
        else if (a == "--readonly") readonly = true;
        else if (a == "--scsi-dir") scsi_dirs.push_back(next());
        else if (a == "--scsi") scsi_specs.push_back(next());
        else if (a == "--no-scsi") no_scsi = true;
        else if (a == "--no-fx") nofx = true;
        else if (a == "--real-disk-speed") real_disk = true;
        else if (a == "--audio-in") audio_in = true;
        else if (a == "--audio-driver") audio_driver = next();
        else if (a == "--list-audio") list_audio = true;
        else if (a == "--input-wav") input_wav_path = next();
        else if (a == "--input-wav-at") input_wav_at = atof(next().c_str());
        else if (a == "--scale") scale = atof(next().c_str());
        else if (a == "--screenshot") { shot_at = atof(next().c_str()); shot_path = next(); }
        else if (a == "--quit-after") quit_at = atof(next().c_str());
        else if (a == "--open-options") open_opts_at_start = true;
        else if (a == "--press") { double t; int i; if (sscanf(next().c_str(), "%lf:%d", &t, &i) == 2) auto_buttons.push_back({t, i}); }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a.size() > 4 && a[0] != '-') {      // bare image path: floppy, or SCSI if it is big
            std::error_code ec; auto sz = std::filesystem::file_size(a, ec);
            if (!ec && sz > 2 * 1024 * 1024) scsi_files.push_back(a); else disk = a;
        }
    }
    // an instrument file (.efe) given at start: boot from the OS disk, then put it in the drive
    std::string startup_efe;
    if (!disk.empty() && efe::is_single_file(disk)) { startup_efe = disk; disk.clear(); }

    Eps16 m;
    m.set_wave_mb(ram_mb);
    bool rom_ok = (lo_set || hi_set) ? m.load_rom(lo, hi) : (m.load_rom_dir(rom_dir) || m.load_rom(lo, hi));
    if (!rom_ok && !lo_set && !hi_set) {            // also look next to the executable
        if (char *bp = SDL_GetBasePath()) { rom_ok = m.load_rom_dir(std::string(bp) + rom_dir); SDL_free(bp); }
    }
    if (!rom_ok && !lo_set && !hi_set) {            // and in the per-user folder (…/Ensoniq/ASR10emu/roms)
        if (char *pp = SDL_GetPrefPath("Ensoniq", "EPS16emu")) { rom_ok = m.load_rom_dir(std::string(pp) + "roms"); SDL_free(pp); }
    }
    if (!rom_ok) {
        fatal("EPS-16 Plus ROM not found.\n\nPut eps16plus-u28.bin and eps16plus-u27.bin in a "
              "\"roms\" folder next to the program"
#ifdef __APPLE__
              " or in ~/Library/Application Support/Ensoniq/ASR10emu/roms"
#endif
              ".");
        usage(); return 1;
    }
    std::string pref_dir;                           // per-user folder (settings, writable disk images)
    if (char *pp = SDL_GetPrefPath("Ensoniq", "EPS16emu")) { pref_dir = pp; SDL_free(pp); }
    if (disk.empty()) {                             // no image given: use one from the roms folder
        disk = find_disk(rom_dir);
        if (disk.empty() && !pref_dir.empty()) disk = find_disk(pref_dir.substr(0, pref_dir.size() - 1));
        if (disk.empty() && !pref_dir.empty()) disk = find_disk(pref_dir + "roms");
        if (disk.empty()) if (char *bp = SDL_GetBasePath()) {
            disk = find_disk(std::string(bp) + rom_dir); SDL_free(bp);
#ifdef __APPLE__
            // inside the .app bundle the image must not be written to (it would break the
            // signature): work on a copy in ~/Library/Application Support/Ensoniq/ASR10emu
            if (!disk.empty() && !pref_dir.empty()) {
                namespace fs = std::filesystem;
                std::error_code ec;
                fs::path dst = fs::path(pref_dir) / fs::path(disk).filename();
                if (!fs::exists(dst, ec)) fs::copy_file(disk, dst, ec);
                if (fs::exists(dst, ec)) disk = dst.string();
            }
#endif
        }
        if (!disk.empty()) fprintf(stderr, "disk: %s\n", disk.c_str());
    }
    if (!disk.empty() && !m.fdc.load(disk)) fatal("Cannot open the disk image:\n" + disk);
    m.fx_enabled = !nofx;
    m.fdc.fast = !real_disk;                        // fast floppy transfers and seeks unless asked otherwise
    m.scsi_fitted = !no_scsi;
    m.reset();

    // SCSI (SP-3 option): ZuluSCSI-style folder (HD<id>.img, CD<id>.iso ...) and/or explicit images.
    // Without options a folder called "scsi" next to the program or in the working directory is used.
    m.scsi_fitted = !no_scsi;
    m.scsi.force_readonly = readonly;
    // the default folder: "scsi" in the working directory, in the settings folder or next to the program
    auto default_scsi_dir = [&]() -> std::string {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::is_directory("scsi", ec)) return "scsi";
        if (!pref_dir.empty() && fs::is_directory(pref_dir + "scsi", ec)) return pref_dir + "scsi";
        if (char *bp = SDL_GetBasePath()) {
            std::string d = std::string(bp) + "scsi"; SDL_free(bp);
            if (fs::is_directory(d, ec)) return d;
        }
        return "";
    };
    // without SCSI options on the command line the folder chosen in the options is used (below)
    const bool scsi_from_cfg = scsi_dirs.empty() && scsi_specs.empty() && scsi_files.empty();
    for (auto &d : scsi_dirs) printf("%s", m.scsi.attach_dir(d).c_str());
    for (auto &sp : scsi_specs) {
        int id = atoi(sp.c_str()); size_t c = sp.find(':');
        std::string path = c == std::string::npos ? "" : sp.substr(c + 1);
        bool cd = path.size() > 3 && path.substr(path.size() - 3) == ":cd";
        if (cd) path.resize(path.size() - 3);
        if (m.scsi.attach(id, path, cd ? ScsiDevice::CD : ScsiDevice::HD)) printf("SCSI ID %d: %s\n", id, m.scsi.dev[id]->describe().c_str());
        else fprintf(stderr, "cannot attach SCSI %s\n", sp.c_str());
    }
    for (auto &f : scsi_files) printf("%s\n", m.scsi.attach_file(f).c_str());

    // ---------------------------------------------------------------- SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }

    // ---------------------------------------------------------------- settings
    Settings cfg;
    {
        std::string dir;
        if (char *pp = SDL_GetPrefPath("Ensoniq", "EPS16emu")) { dir = pp; SDL_free(pp); }
        cfg.load(dir + "eps16.cfg");
    }
    if (!audio_driver.empty()) cfg.audio_api = audio_driver;
    if (audio_in) cfg.audio_in = "*";
    auto scsi_dir_of = [&](const Settings &st) {
        std::error_code ec;
        if (!st.scsi_dir.empty() && std::filesystem::is_directory(std::filesystem::u8path(st.scsi_dir), ec)) return st.scsi_dir;
        return default_scsi_dir();
    };
    if (scsi_from_cfg) { std::string d = scsi_dir_of(cfg); if (!d.empty()) printf("%s", m.scsi.attach_dir(d).c_str()); }

    // ---------------------------------------------------------------- MIDI
    MidiPorts midi;
    {
        auto ins = midi.in_ports(), outs = midi.out_ports();
        if (list_midi) {
            for (size_t i = 0; i < ins.size(); i++) printf("MIDI in  %zu: %s\n", i, ins[i].c_str());
            for (size_t i = 0; i < outs.size(); i++) printf("MIDI out %zu: %s\n", i, outs[i].c_str());
            return 0;
        }
        if (midi_in_port >= 0) { cfg.midi_in = (size_t)midi_in_port < ins.size() ? ins[midi_in_port] : ""; cfg.midi_in_set = true; }
        if (midi_out_port >= 0) cfg.midi_out = (size_t)midi_out_port < outs.size() ? outs[midi_out_port] : "";
        if (!cfg.midi_in_set && !ins.empty()) cfg.midi_in = ins[0];      // first run: first input port
        midi.apply(cfg.midi_in, cfg.midi_out);
        if (!midi.error.empty()) fprintf(stderr, "%s\n", midi.error.c_str());
        if (!midi.in_name.empty()) printf("MIDI in: %s\n", midi.in_name.c_str());
        if (!midi.out_name.empty()) printf("MIDI out: %s\n", midi.out_name.c_str());
    }
    // MIDI out: assemble complete messages from the DUART byte stream
    std::vector<unsigned char> out_msg; int out_need = 0;
    m.midi_out = [&](uint8_t b) {
        if (b >= 0xf8) { std::vector<unsigned char> v{b}; midi.send(v); return; }   // realtime
        if (out_need == -1) {                       // inside SysEx
            out_msg.push_back(b);
            if (b == 0xf7) { midi.send(out_msg); out_msg.clear(); out_need = 0; }
            return;
        }
        if (b & 0x80) {
            out_msg.assign(1, b);
            int hi4 = b & 0xf0;
            if (b == 0xf0) { out_need = -1; return; }
            out_need = (hi4 == 0xc0 || hi4 == 0xd0 || b == 0xf1 || b == 0xf3) ? 1 : (b == 0xf2 || hi4 < 0xf0) ? 2 : 0;
        } else {
            if (out_msg.empty()) return;
            out_msg.push_back(b);
        }
        if ((int)out_msg.size() == out_need + 1) {
            midi.send(out_msg);
            uint8_t st = out_msg[0];
            if (st < 0xf0) out_msg.assign(1, st); else out_msg.clear();   // running status
        }
    };

    // ---------------------------------------------------------------- audio devices
    std::map<std::string, std::vector<AudioDevice>> dev_cache;
    auto devices_of = [&](const std::string &api, bool refresh = false) -> const std::vector<AudioDevice> & {
        auto it = dev_cache.find(api);
        if (it == dev_cache.end() || refresh) it = dev_cache.insert_or_assign(api, AudioIO::devices(api)).first;
        return it->second;
    };
    const std::vector<AudioApi> apis = AudioIO::apis();
    if (cfg.audio_api.empty() || std::none_of(apis.begin(), apis.end(), [&](const AudioApi &a) { return a.id == cfg.audio_api; }))
        cfg.audio_api = apis.empty() ? "sdl" : apis[0].id;
    if (list_audio) {
        for (auto &a : apis) {
            printf("[%s] %s\n", a.id.c_str(), a.name.c_str());
            for (auto &dv : devices_of(a.id))
                printf("    %s  (saidas %d, entradas %d%s)\n", dv.name.c_str(), dv.outputs, dv.inputs, dv.default_out ? ", predefinido" : "");
        }
        return 0;
    }

    // ---------------------------------------------------------------- window
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");          // the .exe icon (all sizes) for the window
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    SDL_Window *win = SDL_CreateWindow("Ensoniq EPS-16 Plus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)(W * scale), (int)(H * scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#ifndef _WIN32
    if (SDL_Surface *ic = SDL_CreateRGBSurfaceWithFormatFrom((void *)APP_ICON64, 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_ABGR8888)) {
        SDL_SetWindowIcon(win, ic); SDL_FreeSurface(ic);
    }
#endif
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) R = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(R, W, H);
    VfdRenderer vfd;
    if (!vfd.init(R)) { fprintf(stderr, "VFD textures: %s\n", SDL_GetError()); return 1; }
    RackUI rack((float)W);
    if (!rack.init(R)) { fprintf(stderr, "panel textures: %s\n", SDL_GetError()); return 1; }
    UiText ui;
    if (!ui.init(R)) { fprintf(stderr, "font textures: %s\n", SDL_GetError()); return 1; }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
    SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);

    // WAV played into the sampler input (Ctrl+P)
    std::vector<float> wav_in; double wav_rate = 44100; size_t wav_pos = 0;
    std::atomic<bool> wav_playing{false};
    if (!input_wav_path.empty()) {
        SDL_AudioSpec ws; Uint8 *buf = nullptr; Uint32 len = 0;
        if (SDL_LoadWAV(input_wav_path.c_str(), &ws, &buf, &len)) {
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, ws.format, ws.channels, ws.freq, AUDIO_F32SYS, 2, ws.freq) >= 0) {
                cvt.len = (int)len; std::vector<Uint8> tmp((size_t)len * (cvt.len_mult > 0 ? cvt.len_mult : 1));
                memcpy(tmp.data(), buf, len); cvt.buf = tmp.data();
                if (cvt.needed) SDL_ConvertAudio(&cvt); else cvt.len_cvt = (int)len;
                wav_in.assign((float *)tmp.data(), (float *)(tmp.data() + cvt.len_cvt));
                wav_rate = ws.freq;
            }
            SDL_FreeWAV(buf);
        } else fprintf(stderr, "cannot load %s: %s\n", input_wav_path.c_str(), SDL_GetError());
    }

    // ---------------------------------------------------------------- emulation thread + audio
    // The machine runs in its own thread, kept just ahead of the audio device through a ring
    // buffer; the window thread takes `mmx` whenever it touches the machine.
    std::mutex mmx;
    AudioRing ring(1 << 15);
    std::atomic<bool> stop{false}, audio_active{false}, input_on{true};
    std::atomic<unsigned> target_frames{1024}, cur_rate{48000}, underruns{0};
    std::atomic<int> route_main{0}, route_aux[3], in_rel_l{0}, in_rel_r{1};
    for (auto &a : route_aux) a = -1;
    AudioIO aio;
    std::string audio_status;
    const int64_t slice = 16000;                    // 1 ms of CPU time

    auto render = [&](float *out, int ch, int frames) {
        std::fill(out, out + (size_t)ch * frames, 0.0f);
        const int mp = route_main * 2;
        int ap[3]; for (int a = 0; a < 3; a++) ap[a] = route_aux[a] >= 0 ? route_aux[a] * 2 : -1;
        float fr[AudioRing::CH];
        for (int f = 0; f < frames; f++) {
            if (!ring.pop(fr)) { underruns++; continue; }
            float *o = out + (size_t)f * ch;
            auto add = [&](int c, float v) { if (c >= 0 && c < ch) o[c] += v; };
            add(mp, fr[0]); add(mp + 1, fr[1]);
            for (int a = 0; a < 3; a++) if (ap[a] >= 0) { add(ap[a], fr[2 + a * 2]); add(ap[a] + 1, fr[3 + a * 2]); }
        }
    };
    auto capture = [&](const float *in, int ch, int frames) {
        if (!input_on || wav_playing) return;
        int l = std::min((int)in_rel_l, ch - 1), r = std::min((int)in_rel_r, ch - 1);
        float buf[2 * 256];
        for (int done = 0; done < frames; ) {
            int n = std::min(256, frames - done);
            for (int i = 0; i < n; i++) { buf[i * 2] = in[(size_t)(done + i) * ch + l]; buf[i * 2 + 1] = in[(size_t)(done + i) * ch + r]; }
            m.feed_input(buf, (size_t)n, (double)cur_rate);
            done += n;
        }
    };

    // one emulation step: MIDI in, WAV input, 1 ms of machine time, audio to the ring
    auto feed_wav = [&]() {
        if (!wav_playing) return;
        static double acc = 0;
        acc += wav_rate * slice / m.cpu_clock;
        size_t n = (size_t)acc; acc -= n;
        if (wav_pos >= wav_in.size()) { wav_playing = false; return; }
        n = std::min(n * 2, wav_in.size() - wav_pos) / 2;
        m.feed_input(&wav_in[wav_pos], n, wav_rate);
        wav_pos += n * 2;
    };
    static const char *dbg_wav = getenv("EPS_GUI_WAV");   // debug: everything the machine produced
    std::vector<float> dbg_out;
    auto step = [&]() {
        {
            std::lock_guard<std::mutex> g(midi.qmx);
            while (!midi.q.empty()) {
                uint8_t b = midi.q.front(); midi.q.pop_front();
                // channel messages go to the keyboard channel (the factory OS disk is in MULTI
                // mode with instruments 1-8 on channels 9-16, so a keyboard on channel 1 is silent)
                if (cfg.midi_remap && b >= 0x80 && b < 0xf0) b = (uint8_t)((b & 0xf0) | (std::clamp(cfg.kbd_channel, 1, 16) - 1));
                m.midi_in(b);
            }
        }
        feed_wav();
        m.run(slice);
        if (dbg_wav) dbg_out.insert(dbg_out.end(), m.audio.begin(), m.audio.end());
        if (audio_active) {
            size_t n = m.audio.size() / 2;
            bool sep = m.audio_aux.size() >= n * 6;
            float fr[AudioRing::CH] = {};
            for (size_t i = 0; i < n; i++) {
                fr[0] = m.audio[i * 2]; fr[1] = m.audio[i * 2 + 1];
                for (int c = 0; c < 6; c++) fr[2 + c] = sep ? m.audio_aux[i * 6 + c] : 0.0f;
                if (!ring.push(fr)) break;
            }
        }
        m.audio.clear(); m.audio_aux.clear();
    };
    std::thread emu([&]() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
        const double freq = (double)SDL_GetPerformanceFrequency();
        uint64_t last = SDL_GetPerformanceCounter();
        double pending = 0;
        while (!stop) {
            if (audio_active) {
                if (ring.size() >= target_frames) { std::this_thread::sleep_for(std::chrono::microseconds(500)); last = SDL_GetPerformanceCounter(); continue; }
                std::lock_guard<std::mutex> g(mmx);
                step();
            } else {                                // no audio device: pace by the wall clock
                uint64_t now = SDL_GetPerformanceCounter();
                double dt = (now - last) / freq; last = now;
                if (dt > 0.1) dt = 0.1;
                pending += dt * m.cpu_clock;
                {
                    std::lock_guard<std::mutex> g(mmx);
                    while (pending >= slice) { step(); pending -= slice; }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    });

    // (re)opens the audio streams from cfg; call with mmx held
    auto apply_audio = [&]() {
        audio_active = false;
        aio.close();
        ring.reset();
        m.audio.clear(); m.audio_aux.clear();
        const auto &devs = devices_of(cfg.audio_api);
        // outputs needed by the routing
        int outs = 2;
        for (auto &d : devs) if (d.name == cfg.audio_out || (cfg.audio_out.empty() && d.default_out)) { outs = d.outputs; break; }
        if (cfg.audio_api == "sdl") outs = 2;
        int need = cfg.main_pair * 2 + 2;
        for (int a = 0; a < 3; a++) if (cfg.aux_pair[a] >= 0) need = std::max(need, cfg.aux_pair[a] * 2 + 2);
        AudioSetup s;
        s.api = cfg.audio_api; s.out_dev = cfg.audio_out; s.in_dev = cfg.audio_in;
        s.rate = cfg.rate; s.buffer = cfg.buffer;
        s.out_channels = std::max(2, std::min(need, outs));
        s.in_first = cfg.in_left;
        s.in_count = 1;
        std::string err;
        bool ok = aio.open(s, render, capture, err);
        if (!ok && s.api != "sdl") {                 // fall back to SDL's default output so there is sound
            AudioSetup fb; fb.api = "sdl"; fb.buffer = s.buffer;
            std::string e2;
            if (aio.open(fb, render, capture, e2)) { ok = true; err = "failed: " + err + " - using SDL"; }
        }
        int och = ok ? aio.out_channels() : 2;
        int mp = cfg.main_pair * 2 + 1 < och ? cfg.main_pair : 0;
        route_main = mp;
        for (int a = 0; a < 3; a++) {
            int p = cfg.aux_pair[a];
            bool sep = p >= 0 && p != mp && p * 2 + 1 < och;
            m.aux_to_main[a] = !sep;
            route_aux[a] = sep ? p : -1;
            // the effect programs never read the AUX 1 bus: on an EPS without the OEX-6 it is
            // silent. Here it is mixed dry into the main outputs unless it has outputs of its
            // own or is switched off (also when the device lacks the outputs it was given).
            if (a == 0) m.aux1_mix = !sep && p != -2;
        }
        in_rel_l = cfg.in_left - s.in_first; in_rel_r = (int)in_rel_l;   // the EPS-16 Plus has one (mono) input
        if (aio.in_channels() == 2 && cfg.audio_api == "sdl") { in_rel_l = 0; in_rel_r = 0; }
        m.bus_to_main[0] = cfg.bus_main[0]; m.bus_to_main[1] = cfg.bus_main[1];
        cur_rate = ok ? aio.rate() : 48000;
        m.out_rate = cur_rate;
        unsigned buf = ok ? aio.buffer() : 256;
        target_frames = std::max(2 * buf, (unsigned)(cur_rate * 0.006)) + 32;   // ~6 ms ahead of the device
        underruns = 0;
        audio_status = ok ? aio.description() : "No audio: " + err;
        if (ok && !err.empty()) audio_status += "  (" + err + ")";
        audio_active = ok;
        fprintf(stderr, "%s\n", audio_status.c_str());
    };
    {
        std::lock_guard<std::mutex> g(mmx);
        apply_audio();
    }

    // ---------------------------------------------------------------- options page
    struct OptRow { std::string label; std::vector<std::string> items; int sel = 0; };
    bool opts_open = false;
    int drop_row = -1, drop_hover = -1, drop_top = 0;   // open drop-down list of the options page
    int opt_cur = 0;
    std::vector<OptRow> rows;
    Settings edit;                                  // settings being edited
    std::vector<std::string> midi_ins, midi_outs;
    NativeMenu nmenu;
    std::string opt_msg, scsi_note;
    enum { O_API, O_OUT, O_RATE, O_BUF, O_MAIN, O_AUX1, O_AUX2, O_AUX3, O_B2MAIN, O_B3MAIN, O_IN, O_INL, O_MIDIIN, O_MIDIOUT, O_KBCH, O_MIDIMAP, O_SCSI, O_N };
    static const unsigned BUFS[] = { 32, 64, 128, 256, 512, 1024, 2048 };
    auto out_dev_info = [&](const Settings &st) -> AudioDevice {
        for (auto &d : devices_of(st.audio_api)) if (d.outputs > 0 && (d.name == st.audio_out || (st.audio_out.empty() && d.default_out))) return d;
        for (auto &d : devices_of(st.audio_api)) if (d.outputs > 0) return d;
        return AudioDevice{};
    };
    auto in_dev_info = [&](const Settings &st) -> AudioDevice {
        for (auto &d : devices_of(st.audio_api)) if (d.inputs > 0 && (d.name == st.audio_in || (st.audio_in == "*" && d.default_in))) return d;
        return AudioDevice{};
    };
    // rebuild the option rows from `edit`
    auto build_rows = [&]() {
        rows.assign(O_N, OptRow{});
        auto &r = rows;
        r[O_API].label = "Audio driver";
        for (size_t i = 0; i < apis.size(); i++) { r[O_API].items.push_back(apis[i].name); if (apis[i].id == edit.audio_api) r[O_API].sel = (int)i; }
        const auto &devs = devices_of(edit.audio_api);
        r[O_OUT].label = "Output device";
        r[O_OUT].items.push_back("(default)");
        for (auto &d : devs) if (d.outputs > 0) { r[O_OUT].items.push_back(d.name); if (d.name == edit.audio_out) r[O_OUT].sel = (int)r[O_OUT].items.size() - 1; }
        AudioDevice od = out_dev_info(edit);
        r[O_RATE].label = "Sample rate";
        r[O_RATE].items.push_back("automatic");
        std::vector<unsigned> rates = od.rates.empty() ? std::vector<unsigned>{ 44100, 48000, 88200, 96000 } : od.rates;
        for (unsigned v : rates) { if (v < 22050 || v > 192000) continue; r[O_RATE].items.push_back(std::to_string(v) + " Hz"); if (v == edit.rate) r[O_RATE].sel = (int)r[O_RATE].items.size() - 1; }
        r[O_BUF].label = "Buffer";
        for (unsigned b : BUFS) { r[O_BUF].items.push_back(std::to_string(b) + " samples"); if (b == edit.buffer) r[O_BUF].sel = (int)r[O_BUF].items.size() - 1; }
        int pairs = edit.audio_api == "sdl" ? 1 : std::max(1, od.outputs / 2);
        r[O_MAIN].label = "Main outputs";
        for (int p = 0; p < pairs; p++) r[O_MAIN].items.push_back("outputs " + std::to_string(p * 2 + 1) + "-" + std::to_string(p * 2 + 2));
        r[O_MAIN].sel = std::min(edit.main_pair, pairs - 1);
        for (int a = 0; a < 3; a++) {
            OptRow &x = r[O_AUX1 + a];
            // OEX-6 outputs: AUX 1, BUS 2, BUS 3 (BUS 2/3 are the internal effect buses)
            x.label = a == 0 ? "OEX-6 AUX 1" : "OEX-6 BUS " + std::to_string(a + 1);
            int first = 0;
            if (a == 0) { x.items.push_back("mixed into Main (dry)"); x.items.push_back("off"); first = 2; }
            else { x.items.push_back("off"); first = 1; }
            for (int p = 0; p < pairs; p++) x.items.push_back("outputs " + std::to_string(p * 2 + 1) + "-" + std::to_string(p * 2 + 2));
            int ap = edit.aux_pair[a];
            if (ap >= 0 && ap < pairs) x.sel = first + ap;
            else x.sel = (a == 0 && ap == -2) ? 1 : 0;
        }
        for (int b = 0; b < 2; b++) {
            OptRow &x = r[O_B2MAIN + b];
            x.label = "BUS " + std::to_string(b + 2) + " into Main";
            x.items = { "yes (as on the EPS)", "no (OEX-6 output only)" };
            x.sel = edit.bus_main[b] ? 0 : 1;
        }
        r[O_IN].label = "Input device";
        r[O_IN].items.push_back("(none)");
        r[O_IN].items.push_back("(default)");
        if (edit.audio_in == "*") r[O_IN].sel = 1;
        for (auto &d : devs) if (d.inputs > 0) {
            if (edit.audio_api == "asio" && d.name != od.name) continue;   // ASIO: one device at a time
            r[O_IN].items.push_back(d.name);
            if (d.name == edit.audio_in) r[O_IN].sel = (int)r[O_IN].items.size() - 1;
        }
        AudioDevice idv = in_dev_info(edit);
        int nin = edit.audio_api == "sdl" ? 2 : std::max(1, idv.inputs);
        r[O_INL].label = "Channel (mono)";
        for (int c = 0; c < nin; c++) r[O_INL].items.push_back(std::to_string(c + 1));
        r[O_INL].sel = std::min(edit.in_left, nin - 1);
        r[O_MIDIIN].label = "MIDI In";
        r[O_MIDIIN].items.push_back("(none)");
        for (auto &p : midi_ins) { r[O_MIDIIN].items.push_back(p); if (p == edit.midi_in) r[O_MIDIIN].sel = (int)r[O_MIDIIN].items.size() - 1; }
        r[O_MIDIOUT].label = "MIDI Out";
        r[O_MIDIOUT].items.push_back("(none)");
        for (auto &p : midi_outs) { r[O_MIDIOUT].items.push_back(p); if (p == edit.midi_out) r[O_MIDIOUT].sel = (int)r[O_MIDIOUT].items.size() - 1; }
        r[O_KBCH].label = "Keyboard channel";
        for (int c = 1; c <= 16; c++) r[O_KBCH].items.push_back(std::to_string(c));
        r[O_KBCH].sel = std::clamp(edit.kbd_channel, 1, 16) - 1;
        r[O_MIDIMAP].label = "MIDI In → channel";
        r[O_MIDIMAP].items = { "the on-screen keyboard one", "as received (all)" };
        r[O_MIDIMAP].sel = edit.midi_remap ? 0 : 1;
        // SCSI folder: default, the chosen folder (shown by its path), or pick another one
        r[O_SCSI].label = "Drive images folder";
        r[O_SCSI].items.push_back("\"scsi\" next to the program");
        if (!edit.scsi_dir.empty()) { r[O_SCSI].items.push_back(edit.scsi_dir); r[O_SCSI].sel = 1; }
        r[O_SCSI].items.push_back("Choose folder...");
    };
    // a row changed: write it back into `edit` and rebuild the dependent rows
    auto row_changed = [&](int row) {
        const OptRow &x = rows[row];
        const std::string &v = x.items.empty() ? std::string() : x.items[x.sel];
        switch (row) {
        case O_API: edit.audio_api = apis[x.sel].id; edit.audio_out.clear(); edit.audio_in.clear(); edit.rate = 0;
                    edit.main_pair = 0; for (auto &p : edit.aux_pair) p = -1; edit.in_left = edit.in_right = 0; break;
        case O_OUT: edit.audio_out = x.sel == 0 ? "" : v; edit.rate = 0; break;
        case O_RATE: edit.rate = x.sel == 0 ? 0 : (unsigned)atoi(v.c_str()); break;
        case O_BUF: edit.buffer = BUFS[x.sel]; break;
        case O_MAIN: edit.main_pair = x.sel; break;
        case O_AUX1: edit.aux_pair[0] = x.sel == 0 ? -1 : x.sel == 1 ? -2 : x.sel - 2; break;
        case O_AUX2: case O_AUX3: edit.aux_pair[row - O_AUX1] = x.sel - 1; break;
        case O_IN: edit.audio_in = x.sel == 0 ? "" : x.sel == 1 ? "*" : v; edit.in_left = edit.in_right = 0; break;
        case O_INL: edit.in_left = edit.in_right = x.sel; break;
        case O_B2MAIN: case O_B3MAIN: edit.bus_main[row - O_B2MAIN] = x.sel == 0; break;
        case O_MIDIMAP: edit.midi_remap = x.sel == 0; break;
        case O_MIDIIN: edit.midi_in = x.sel == 0 ? "" : v; edit.midi_in_set = true; break;
        case O_MIDIOUT: edit.midi_out = x.sel == 0 ? "" : v; break;
        case O_SCSI:
            if (x.sel == 0) edit.scsi_dir.clear();
            else if (x.sel == (int)x.items.size() - 1) {      // "Choose folder..."
                std::string d = nmenu.choose_folder_dialog("SCSI drive images folder (HD0.img, CD5.iso ...)");
                if (!d.empty()) edit.scsi_dir = d;
#if !defined(_WIN32) && !defined(__APPLE__)
                else opt_msg = "No folder chosen (the dialog needs zenity or kdialog): scsi_dir can also be set in eps16.cfg";
#endif
            }
            break;
        case O_KBCH: edit.kbd_channel = x.sel + 1; break;
        }
        build_rows();
    };
    auto open_options = [&]() {
        for (auto it = dev_cache.begin(); it != dev_cache.end(); ) {
            if (it->first == "asio" && cfg.audio_api == "asio" && audio_active) ++it;   // loading ASIO drivers would stop the stream
            else it = dev_cache.erase(it);
        }
        edit = cfg;
        midi_ins = midi.in_ports(); midi_outs = midi.out_ports();
        build_rows();
        opts_open = true;
        drop_row = -1;
    };
    auto apply_options = [&]() {           // call with mmx NOT held
        bool scsi_changed = edit.scsi_dir != cfg.scsi_dir;
        cfg = edit;
        cfg.midi_in_set = true;
        {
            std::lock_guard<std::mutex> g(mmx);
            apply_audio();
            if (scsi_changed) {           // the drives of the old folder go, the new folder's come in
                for (auto &d : m.scsi.dev) d.reset();
                std::string d = scsi_dir_of(cfg);
                if (!d.empty()) m.scsi.attach_dir(d);
                scsi_note = "SCSI: " + (d.empty() ? std::string("no folder") : d) + "  (restart the EPS to scan the new drives)";
            }
        }
        midi.apply(cfg.midi_in, cfg.midi_out);
        opt_msg = midi.error.empty() ? "" : midi.error;
        cfg.save();
    };
    // layout of the options page: a faceplate-like panel with two columns of drop-down fields
    const int OX = 110, OY = 52, OW = 1180, OH = 540;
    const int ROW_H = 30, Y0 = OY + 74, LABEL_W = 214, FIELD_W = 330, COL_X[2] = { OX + 36, OX + 616 };
    const SDL_Rect btn_apply{OX + OW - 214, OY + OH - 46, 70, 26}, btn_close{OX + OW - 112, OY + OH - 46, 70, 26};
    struct Sect { const char *title; int col, y, first, last; };
    // sections: title, column, top, first and last row
    const Sect sects[4] = {
        { "AUDIO OUTPUTS", 0, Y0, O_API, O_B3MAIN },
        { "SAMPLER INPUT", 1, Y0, O_IN, O_INL },
        { "MIDI", 1, Y0 + ROW_H * 3 + 18, O_MIDIIN, O_MIDIMAP },
        { "SCSI DRIVES", 1, Y0 + ROW_H * 8 + 36, O_SCSI, O_SCSI } };
    auto field_rect = [&](int row) -> SDL_Rect {
        for (const Sect &sc : sects)
            if (row >= sc.first && row <= sc.last)
                return SDL_Rect{ COL_X[sc.col] + LABEL_W, sc.y + ROW_H * (1 + row - sc.first) + 3, FIELD_W, ROW_H - 6 };
        return SDL_Rect{ 0, 0, 0, 0 };
    };
    // drop-down list of the open field
    const int DROP_H = 24, DROP_MAX = 12;
    auto drop_rect = [&]() -> SDL_Rect {
        SDL_Rect f = field_rect(drop_row);
        int n = std::min((int)rows[drop_row].items.size(), DROP_MAX);
        SDL_Rect r{ f.x, f.y + f.h + 2, f.w, n * DROP_H + 6 };
        if (r.y + r.h > H - 8) r.y = f.y - 2 - r.h;          // no room below: open upwards
        return r;
    };
    auto open_drop = [&](int row) {
        if (row < 0 || rows[row].items.empty()) return;
        drop_row = row; drop_hover = rows[row].sel;
        int n = (int)rows[row].items.size();
        drop_top = std::clamp(rows[row].sel - DROP_MAX / 2, 0, std::max(0, n - DROP_MAX));
    };
    auto drop_scroll_to = [&](int i) {
        int n = (int)rows[drop_row].items.size();
        if (i < drop_top) drop_top = i;
        if (i >= drop_top + DROP_MAX) drop_top = i - DROP_MAX + 1;
        drop_top = std::clamp(drop_top, 0, std::max(0, n - DROP_MAX));
    };
    // keyboard order of the fields
    const int ORDER[O_N] = { O_API, O_OUT, O_RATE, O_BUF, O_MAIN, O_AUX1, O_AUX2, O_AUX3, O_B2MAIN, O_B3MAIN,
                             O_IN, O_INL, O_MIDIIN, O_MIDIOUT, O_KBCH, O_MIDIMAP, O_SCSI };
    auto order_pos = [&](int row) { for (int i = 0; i < O_N; i++) if (ORDER[i] == row) return i; return 0; };
    bool show_kb = cfg.show_keyboard;
    // window layout: keyboard strip shown or hidden (it is always there while the options are
    // open, they need the room)
    auto apply_layout = [&]() {
        bool full = show_kb || opts_open;
        int nh = full ? H : STRIP_Y + STATUS_H;
        if (nh == H_cur) return;
        int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
        bool resizable = !(SDL_GetWindowFlags(win) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
        float sc = (float)wh / H_cur;
        H_cur = nh; status_y = full ? STATUS_Y : STRIP_Y;
        SDL_RenderSetLogicalSize(R, W, H_cur);
        if (resizable) SDL_SetWindowSize(win, ww, (int)(H_cur * sc + 0.5f));
    };
    nmenu.init(win, show_kb);
    auto toggle_keyboard = [&]() {
        show_kb = !show_kb; cfg.show_keyboard = show_kb; cfg.save();
        nmenu.set_keyboard(show_kb);
        apply_layout();
    };
    auto inside = [](const SDL_Rect &r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    auto change = [&](int row, int dir) {
        if (row < 0 || row >= O_N || rows[row].items.empty()) return;
        int n = (int)rows[row].items.size();
        rows[row].sel = (rows[row].sel + dir + n) % n;
        row_changed(row);
    };

    // ---------------------------------------------------------------- UI state
    auto controls = make_controls(rack);
    uint32_t power_down_t = 0; bool power_held = false;
    bool rec_latched = false;       // RECORD works as a toggle in the window
    // Patch Select buttons (on the keyboard model they sit left of the wheels; panel indices 0/1)
    int held_ctl = -1;
    { std::lock_guard<std::mutex> g(mmx); for (auto &c : controls) m.analog[c.ch] = ctl_raw(c); }
    int held_button = -1, held_key = -1;
    bool arrows_both = false;                       // right click on Up/Down: both arrows held
    bool kb_down[KEYS] = {};
    int velocity = 100;
    std::string status = disk.empty() ? "No disk: drop a disk image (.img) or an instrument (.efe) on the window" : "Disk: " + disk;
    uint32_t status_t = SDL_GetTicks();

    auto press_key = [&](int k, bool down, int vel) {
        if (k < 0 || k >= KEYS) return;
        if (kb_down[k] == down) return;
        kb_down[k] = down;
        // the rack has no keyboard: the strip is a MIDI keyboard on the chosen channel
        int ch = (std::clamp(cfg.kbd_channel, 1, 16) - 1);
        m.midi_in((uint8_t)(0x90 | ch)); m.midi_in((uint8_t)(k + 36)); m.midi_in((uint8_t)(down ? vel : 0));
    };
    // pitch / mod wheels of the strip: MIDI pitch bend and modulation (CC 1) on the same channel
    auto wheel_midi = [&](const Ctl &c) {
        if (c.on_rack) return;
        int ch = (std::clamp(cfg.kbd_channel, 1, 16) - 1);
        double v = std::clamp(c.val, 0.0, 1.0);
        if (c.ch == Eps16::AN_PITCH) { int b = std::clamp((int)(v * 16383 + 0.5), 0, 16383); m.midi_in((uint8_t)(0xe0 | ch)); m.midi_in((uint8_t)(b & 127)); m.midi_in((uint8_t)(b >> 7)); }
        else if (c.ch == Eps16::AN_MOD) { m.midi_in((uint8_t)(0xb0 | ch)); m.midi_in(1); m.midi_in((uint8_t)std::clamp((int)(v * 127 + 0.5), 0, 127)); }
    };
    // Patch Select is not a panel key: the two buttons switch resistors on the analog input AN1
    // (measured thresholds with the reference at 600: none < 368 < right < 628 < left < 916 < both)
    bool patch_held[2] = { false, false };
    auto btn = [&](int idx, bool down) {
        if (idx == RackUI::PATCH_L || idx == RackUI::PATCH_R) {
            patch_held[idx - RackUI::PATCH_L] = down;
            static const uint16_t v[4] = { 0, 770, 500, 1010 };   // none, left, right, both
            m.analog[1] = v[(patch_held[0] ? 1 : 0) | (patch_held[1] ? 2 : 0)];
            return;
        }
        m.panel.button(idx, down);
    };
    // a disk image: SCSI if it is named/sized like one (HD0.img, CD5.iso, > 2 MB), else the floppy drive
    // Ensoniq single files (.efe/.efa, Giebler format) dropped together go on one temporary
    // floppy, which is then put in the drive: load them with LOAD as from any disk
    std::vector<std::string> drop_files;
    auto insert_files = [&](const std::vector<std::string> &files) {
        std::string path, label, err = efe::make_disk_file(files, false, "eps16-", path, label);
        if (!err.empty()) { status = err; status_t = SDL_GetTicks(); return; }
        if (m.fdc.dirty && !readonly) m.fdc.save();
        if (m.insert_disk(path))
            status = files.size() == 1 ? "\"" + label + "\" is on the disk in the drive: press LOAD to load it"
                                       : std::to_string(files.size()) + " files are on the disk in the drive: press LOAD to load them";
        else status = "Cannot open " + path;
        status_t = SDL_GetTicks();
    };
    auto open_image = [&](const std::string &p) {
        if (efe::is_single_file(p)) { insert_files({ p }); return; }
        std::string r = m.scsi.attach_file(p);
        if (!r.empty()) { status = r; status_t = SDL_GetTicks(); return; }
        if (m.fdc.dirty && !readonly) m.fdc.save();
        if (m.insert_disk(p)) status = "Disk inserted: " + p; else status = "Cannot open " + p;
        status_t = SDL_GetTicks();
    };
    auto save_disk = [&]() {
        if (readonly || !m.fdc.dirty) return;
        if (m.fdc.save()) { status = "Disk saved: " + m.fdc.path; status_t = SDL_GetTicks(); }
    };

    auto shortcut = [&](SDL_Keycode kc) -> int {
        switch (kc) {
        case SDLK_UP: return 10; case SDLK_DOWN: return 11; case SDLK_LEFT: return 16; case SDLK_RIGHT: return 17;
        case SDLK_RETURN: case SDLK_KP_ENTER: return 35;
        case SDLK_BACKSPACE: case SDLK_ESCAPE: return 33;
        // number row 1-8 (and F1-F8) = Instrument/Track buttons 1-8
        case SDLK_1: case SDLK_F1: return 2; case SDLK_2: case SDLK_F2: return 8;
        case SDLK_3: case SDLK_F3: return 14; case SDLK_4: case SDLK_F4: return 20;
        case SDLK_5: case SDLK_F5: return 4; case SDLK_6: case SDLK_F6: return 34;
        case SDLK_7: case SDLK_F7: return 28; case SDLK_8: case SDLK_F8: return 22;
        case SDLK_r: return 3;        // RECORD (hold it and press Space for record + play)
        // numeric keypad = the page buttons, as printed on the keypad (1 = Env 1, 4 = Pitch... 0 = Track)
        case SDLK_KP_1: return 13; case SDLK_KP_2: return 18; case SDLK_KP_3: return 19; case SDLK_KP_4: return 24;
        case SDLK_KP_5: return 25; case SDLK_KP_6: return 30; case SDLK_KP_7: return 31; case SDLK_KP_8: return 36;
        case SDLK_KP_9: return 37; case SDLK_KP_0: return 12;
        case SDLK_SPACE: return 29;   // PLAY
        case SDLK_F11: return RackUI::PATCH_L; case SDLK_F12: return RackUI::PATCH_R;   // Patch Select
        }
        return -1;
    };
    int held_kbd_button[512]; for (auto &x : held_kbd_button) x = -1;
    bool quit = false;
    std::string shown_title;

    while (!quit) {
        Panel pv;                     // snapshot of the machine state for drawing
        bool fdc_dirty, has_disk, scsi_fitted, drive_busy;
        std::string scsi_sum, disk_path;
        bool want_options = false, want_apply = false, want_kb_toggle = false, want_open = false;
        {
            // collect the events first: on Windows SDL_PollEvent can block while the window is
            // dragged, and the machine must not stay locked meanwhile
            std::vector<SDL_Event> evs;
            { SDL_Event ev; while (SDL_PollEvent(&ev)) evs.push_back(ev); }
            std::unique_lock<std::mutex> lk(mmx, std::defer_lock);
            for (SDL_Event &e : evs) {
                if (e.type == SDL_QUIT) { quit = true; break; }
                if (NativeMenu::Cmd mc = nmenu.command(e)) {
                    if (!lk.owns_lock()) lk.lock();
                    switch (mc) {
                    case NativeMenu::OPEN_DISK: want_open = true; break;
                    case NativeMenu::EJECT_DISK:
                        if (m.fdc.has_disk()) { save_disk(); m.eject_disk(); status = "Disk ejected"; } else status = "The drive is empty";
                        status_t = SDL_GetTicks(); break;
                    case NativeMenu::SAVE_DISK: save_disk(); if (!m.fdc.dirty) { status = "Disk saved"; status_t = SDL_GetTicks(); } break;
                    case NativeMenu::RESET: save_disk(); m.reset(); status = "Reset"; status_t = SDL_GetTicks(); break;
                    case NativeMenu::EXIT: quit = true; break;
                    case NativeMenu::TOGGLE_KEYBOARD: want_kb_toggle = true; break;
                    case NativeMenu::OPTIONS: if (opts_open) opts_open = false; else want_options = true; break;
                    default: break;
                    }
                    continue;
                }
                // ---- options page
                if (opts_open) {
                    auto do_apply = [&]() { if (lk.owns_lock()) lk.unlock(); apply_options(); };
                    if (e.type == SDL_KEYDOWN) {
                        SDL_Keycode kc = e.key.keysym.sym;
                        if (drop_row >= 0) {                     // list open
                            int n = (int)rows[drop_row].items.size();
                            if (kc == SDLK_ESCAPE) drop_row = -1;
                            else if (kc == SDLK_UP) { drop_hover = std::max(0, drop_hover - 1); drop_scroll_to(drop_hover); }
                            else if (kc == SDLK_DOWN) { drop_hover = std::min(n - 1, drop_hover + 1); drop_scroll_to(drop_hover); }
                            else if (kc == SDLK_PAGEUP) { drop_hover = std::max(0, drop_hover - DROP_MAX); drop_scroll_to(drop_hover); }
                            else if (kc == SDLK_PAGEDOWN) { drop_hover = std::min(n - 1, drop_hover + DROP_MAX); drop_scroll_to(drop_hover); }
                            else if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER || kc == SDLK_SPACE) {
                                if (drop_hover >= 0) { rows[drop_row].sel = drop_hover; row_changed(drop_row); }
                                drop_row = -1;
                            }
                        } else {
                            if (kc == SDLK_ESCAPE || kc == SDLK_F10) opts_open = false;
                            else if (kc == SDLK_UP) opt_cur = ORDER[(order_pos(opt_cur) + O_N - 1) % O_N];
                            else if (kc == SDLK_DOWN || kc == SDLK_TAB) opt_cur = ORDER[(order_pos(opt_cur) + 1) % O_N];
                            else if (kc == SDLK_LEFT) change(opt_cur, -1);
                            else if (kc == SDLK_RIGHT) change(opt_cur, +1);
                            else if (kc == SDLK_RETURN || kc == SDLK_KP_ENTER || kc == SDLK_SPACE) open_drop(opt_cur);
                            else if (kc == SDLK_a) do_apply();
                        }
                    } else if (e.type == SDL_MOUSEMOTION && drop_row >= 0) {
                        SDL_Rect d = drop_rect();
                        int x = e.motion.x, y = e.motion.y;
                        if (inside(d, x, y)) drop_hover = std::min((int)rows[drop_row].items.size() - 1, drop_top + (y - d.y - 3) / DROP_H);
                    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        int x = e.button.x, y = e.button.y;
                        if (drop_row >= 0) {
                            SDL_Rect d = drop_rect();
                            if (inside(d, x, y)) {
                                int i = drop_top + (y - d.y - 3) / DROP_H;
                                if (i >= 0 && i < (int)rows[drop_row].items.size()) { rows[drop_row].sel = i; row_changed(drop_row); }
                                drop_row = -1;
                                continue;
                            }
                            int was = drop_row; drop_row = -1;
                            if (inside(field_rect(was), x, y)) continue;       // a second click closes it
                        }
                        if (inside(btn_apply, x, y)) do_apply();
                        else if (inside(btn_close, x, y)) opts_open = false;
                        else for (int row = 0; row < O_N; row++) {
                            SDL_Rect f = field_rect(row);
                            SDL_Rect hit{ f.x - LABEL_W, f.y - 3, f.w + LABEL_W, f.h + 6 };
                            if (inside(hit, x, y)) { opt_cur = row; if (inside(f, x, y)) open_drop(row); break; }
                        }
                    } else if (e.type == SDL_MOUSEWHEEL && drop_row >= 0) {
                        int n = (int)rows[drop_row].items.size();
                        drop_top = std::clamp(drop_top - e.wheel.y, 0, std::max(0, n - DROP_MAX));
                    }
                    continue;
                }
                if (!lk.owns_lock()) lk.lock();
                switch (e.type) {
                case SDL_DROPBEGIN: drop_files.clear(); break;
                case SDL_DROPFILE: {
                    std::string p = e.drop.file; SDL_free(e.drop.file);
                    if (efe::is_single_file(p)) drop_files.push_back(p);   // collected until the drop is complete
                    else open_image(p);
                    break;
                }
                case SDL_DROPCOMPLETE:
                    if (!drop_files.empty()) { insert_files(drop_files); drop_files.clear(); }
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    int x = e.button.x, y = e.button.y;
                    {
                        int bi = rack.button_at(x, y);
                        if (bi == 3) {               // RECORD latches: one click holds it down, the next releases it
                            rec_latched = !rec_latched; btn(3, rec_latched); break;
                        }
                        // right click on an arrow = both arrows at once: the OS jumps to the middle of
                        // the range (0 for -99..+99, 50 for 0..99), as when both are pressed on the unit
                        if ((bi == 10 || bi == 11) && e.button.button != SDL_BUTTON_LEFT) {
                            arrows_both = true; btn(10, true); btn(11, true); break;
                        }
                        if (bi >= 0) { held_button = bi; btn(bi, true); break; }
                        RackUI::Area a = rack.area_at(x, y);
                        if (a == RackUI::POWER) { power_held = true; power_down_t = SDL_GetTicks(); break; }
                        if (a == RackUI::EJECT) {
                            if (m.fdc.has_disk()) { save_disk(); m.eject_disk(); status = "Disk ejected"; }
                            else status = "The drive is empty: drop a disk image (.img) or an instrument (.efe) on the window";
                            status_t = SDL_GetTicks(); break;
                        }
                        if (a == RackUI::DRIVE) {
                            status = m.fdc.has_disk() ? "Disk in the drive: " + m.fdc.path + "  (eject button or Ctrl+E)"
                                                      : "The drive is empty: drop a disk image on the window";
                            status_t = SDL_GetTicks(); break;
                        }
                    }
                    for (size_t ci = 0; ci < controls.size() && held_button < 0; ci++) {
                        Ctl &c = controls[ci];
                        if (!c.on_rack && H_cur != H) continue;          // keyboard strip hidden
                        if (ctl_hit(c, x, y)) {
                            held_ctl = (int)ci; ctl_from_mouse(c, x, y); if (c.on_rack) m.analog[c.ch] = ctl_raw(c); wheel_midi(c);
                        }
                    }
                    if (held_button < 0 && held_ctl < 0 && H_cur == H) {
                        int k = key_at(x, y);
                        if (k >= 0) {
                            SDL_Rect r = key_rect(k);
                            int v = 30 + 97 * (y - r.y) / (r.h > 0 ? r.h : 1);   // lower on the key = louder
                            held_key = k; press_key(k, true, v > 127 ? 127 : v);
                        }
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                    if (power_held) {
                        power_held = false;
                        if (SDL_GetTicks() - power_down_t >= 700) { save_disk(); m.reset(); status = "Reset (Power)"; }
                        else status = "Hold Power down for about 1 s to restart";
                        status_t = SDL_GetTicks();
                    }
                    if (held_ctl >= 0) {
                        Ctl &c = controls[held_ctl];
                        if (c.spring) { c.val = 0.5; if (c.on_rack) m.analog[c.ch] = ctl_raw(c); wheel_midi(c); }
                        held_ctl = -1;
                    }
                    if (arrows_both) { btn(11, false); btn(10, false); arrows_both = false; }
                    if (held_button >= 0) { btn(held_button, false); held_button = -1; }
                    if (held_key >= 0) { press_key(held_key, false, 64); held_key = -1; }
                    break;
                case SDL_MOUSEMOTION:
                    if (held_ctl >= 0) { Ctl &c = controls[held_ctl]; ctl_from_mouse(c, e.motion.x, e.motion.y); if (c.on_rack) m.analog[c.ch] = ctl_raw(c); wheel_midi(c); }
                    if (held_key >= 0 && (e.motion.state & SDL_BUTTON_LMASK)) {
                        int k = key_at(e.motion.x, e.motion.y);
                        if (k >= 0 && k != held_key) { press_key(held_key, false, 64); held_key = k; press_key(k, true, velocity); }
                    }
                    break;
                case SDL_MOUSEWHEEL: {   // wheel = data entry (up/down arrows)
                    int idx = e.wheel.y > 0 ? 10 : 11;
                    btn(idx, true); btn(idx, false);
                    break;
                }
                case SDL_KEYDOWN: case SDL_KEYUP: {
                    bool down = e.type == SDL_KEYDOWN;
                    if (e.key.repeat) break;
                    SDL_Keycode kc = e.key.keysym.sym;
                    int sc = e.key.keysym.scancode & 511;
                    if (down && kc == SDLK_F10) { want_options = true; break; }
                    if (down && kc == SDLK_F9) { toggle_keyboard(); break; }
                    if (down && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (kc == SDLK_o) want_options = true;
                        if (kc == SDLK_e) { save_disk(); m.eject_disk(); status = "Disk ejected"; status_t = SDL_GetTicks(); }
                        if (kc == SDLK_s) save_disk();
                        if (kc == SDLK_f) { m.fx_enabled = !m.fx_enabled; status = m.fx_enabled ? "Effects on" : "Effects off (dry sound)"; status_t = SDL_GetTicks(); }
                        if (kc == SDLK_r) { m.reset(); status = "Reset"; status_t = SDL_GetTicks(); }
                        if (kc == SDLK_i) {
                            if (!aio.has_input()) status = "No audio input: choose one in View > Options";
                            else { input_on = !input_on; status = input_on ? "Audio input on" : "Audio input off"; }
                            status_t = SDL_GetTicks();
                        }
                        if (kc == SDLK_p) {
                            if (wav_in.empty()) status = "No --input-wav given";
                            else { wav_pos = 0; wav_playing = true; status = "Playing " + input_wav_path + " into the input"; }
                            status_t = SDL_GetTicks();
                        }
                        break;
                    }
                    int b = shortcut(kc);
                    if (b >= 0) {
                        if (down) { held_kbd_button[sc] = b; btn(b, true); }
                        else if (held_kbd_button[sc] >= 0 && held_kbd_button[sc] < 64) { btn(held_kbd_button[sc], false); held_kbd_button[sc] = -1; }
                        break;
                    }
                    break;
                }
                }
            }
            if (!lk.owns_lock()) lk.lock();
            // test hooks (emulated time)
            double emu_t = m.cycles() / m.cpu_clock;
            if (input_wav_at >= 0 && emu_t >= input_wav_at && !wav_in.empty()) { wav_pos = 0; wav_playing = true; input_wav_at = -1; }
            for (auto &ab : auto_buttons) {
                if (ab.first >= 0 && emu_t >= ab.first) {
                    if (ab.second == 1000) want_options = true;
                    else if (ab.second == 1003) { opt_cur = O_RATE; open_drop(opt_cur); }      // test: open a list
                    else if (ab.second == 1002) want_kb_toggle = true;                       // test: keyboard strip
                    else if (ab.second == 1001) want_apply = true;                          // test: apply options
                    else if (ab.second >= 2000) press_key(ab.second - 2000, true, 100);   // test: key on
                    else btn(ab.second, true);
                    ab.first = -(emu_t + 0.1);
                }
                else if (ab.first < -0.5 && emu_t >= -ab.first + (ab.second >= 2000 ? 0.9 : 0)) {
                    if (ab.second >= 2000) press_key(ab.second - 2000, false, 64);
                    else if (ab.second != 1000 && ab.second != 1002) btn(ab.second, false);
                    ab.first = -0.25;
                }
            }
            if (!startup_efe.empty() && emu_t >= 5.0) { insert_files({ startup_efe }); startup_efe.clear(); }
            if (quit_at >= 0 && emu_t >= quit_at) quit = true;
            pv = m.panel;
            fdc_dirty = m.fdc.dirty; has_disk = m.fdc.has_disk(); scsi_fitted = m.scsi_fitted; disk_path = m.fdc.path;
            drive_busy = m.fdc.motor_on();
            m.in_peak = 0;
            scsi_sum = scsi_fitted ? m.scsi.summary() : std::string("off");
            if (shot_at >= 0 && emu_t >= shot_at) shot_at = -2;      // take it after drawing
            if (open_opts_at_start) { open_opts_at_start = false; open_options(); apply_layout(); }
        }
        if (want_options) open_options();      // enumerates devices: done with the machine unlocked
        if (want_open) {                       // the file dialog runs with the machine unlocked
            std::string p = nmenu.open_disk_dialog();
            if (!p.empty()) { std::lock_guard<std::mutex> g(mmx); open_image(p); }
        }
        if (want_kb_toggle) toggle_keyboard();
        apply_layout();                        // the keyboard strip comes back while the options are open
        if (want_apply && opts_open) apply_options();

        // ------------------------------------------------------------ draw
        uint32_t t = SDL_GetTicks();
        bool blink_on = (t / 250) & 1;
        fill(0, 0, W, H_cur, 0x0c0d0e);
        {
            bool held_any[64] = {};
            if (held_button >= 0) held_any[held_button & 63] = true;
            if (arrows_both) held_any[10] = held_any[11] = true;
            if (rec_latched) held_any[3] = true;
            for (int s2 = 0; s2 < 512; s2++) if (held_kbd_button[s2] >= 0 && held_kbd_button[s2] < 64) held_any[held_kbd_button[s2]] = true;
            RackState rs;
            rs.panel = &pv; rs.ticks = t; rs.held = held_any;
            for (auto &c : controls) {
                if (c.ch == Eps16::AN_VOLUME) rs.volume = (float)c.val;
                if (c.ch == Eps16::AN_DATA) rs.data = (float)c.val;
            }
            rs.disk = has_disk; rs.drive_busy = drive_busy;
            rs.power_held = power_held;
            rack.draw(rs, vfd);
        }
        (void)blink_on;

        if (H_cur == H) {
            // controller strip: wheels and keyboard
            fill(0, STRIP_Y, W, STATUS_Y - STRIP_Y - 4, 0x161719);
            fill(0, STRIP_Y, W, 1, 0x2a2b2e);
            for (auto &c : controls) {
                if (c.on_rack) continue;
                int cx = (int)c.x, cy = (int)c.y, cw = (int)c.w, ch = (int)c.h;
                fill(cx - 3, cy - 3, cw + 6, ch + 6, 0x050505);
                fill(cx, cy, cw, ch, 0x1d1e21);
                // wheel: ribs that move with the value
                int off = (int)((1.0 - c.val) * 24);
                for (int ry = cy + (off % 8); ry < cy + ch; ry += 8) fill(cx + 4, ry, cw - 8, 2, 0x2c2d31);
                int py = cy + (int)((1.0 - c.val) * (ch - 14));
                fill(cx + 2, py, cw - 4, 14, 0x55585e);
                fill(cx + 2, py + 6, cw - 4, 2, 0xd0d0d0);
                text_c(cx + cw / 2, cy + ch + 6, c.label, 1, 0x8a8f96);
            }


            // keyboard
            for (int pass = 0; pass < 2; pass++)
                for (int k = 0; k < KEYS; k++) {
                    if (is_black(k) != (pass == 1)) continue;
                    SDL_Rect r = key_rect(k);
                    uint32_t c = is_black(k) ? (kb_down[k] ? 0x505a66 : 0x0d0d0e) : (kb_down[k] ? 0xb0c8e0 : 0xeeece6);
                    fill(r.x, r.y, r.w, r.h, c);
                    if (!is_black(k)) { frame(r.x, r.y, r.w, r.h, 0x3a3a3a); fill(r.x + 1, r.y + r.h - 5, r.w - 2, 4, kb_down[k] ? 0x98aec4 : 0xd4d2cc); }
                    else fill(r.x + 2, r.y + r.h - 6, r.w - 4, 4, 0x2a2b2d);
                    if (k % 12 == 0) { char n[8]; snprintf(n, sizeof n, "C%d", k / 12 + 2); text_c(r.x + r.w / 2, r.y + r.h - 16, n, 1, 0x707070); }
                }
            fill(KB_X, KB_Y, KB_W, 3, 0x7a1c14);           // felt strip

        }

        // window title: the disk in the drive, "*" when it has unsaved changes
        {
            std::string tt = std::string("Ensoniq EPS-16 Plus");
            if (has_disk) { std::string fn = disk_path; size_t sl = fn.find_last_of("/\\"); if (sl != std::string::npos) fn = fn.substr(sl + 1); tt += "  -  " + fn + (fdc_dirty ? " *" : ""); }
            if (tt != shown_title) { SDL_SetWindowTitle(win, tt.c_str()); shown_title = tt; }
        }
        // transient messages (disk inserted, ejected...): a small box over the bottom of the window
        if (!status.empty() && t - status_t < 3500) {
            std::string msg = ui.fit(UiText::BODY, status, W - 200.0f);
            float mw = ui.width(UiText::BODY, msg) + 36;
            int bx = (int)((W - mw) / 2), by = H_cur - 46, bh = 30;
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(R, 8, 9, 10, 225); SDL_Rect bxr{bx, by, (int)mw, bh}; SDL_RenderFillRect(R, &bxr);
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            frame(bx, by, (int)mw, bh, 0x3a3b3f);
            ui.draw(UiText::BODY, msg, bx + 18.0f, by + 7.0f, 0xe6e7e9);
        }

        // options page: same look as the faceplate (brushed dark metal, light grey legends, purple
        // rules, fields like the display window, light grey buttons with their legend above)
        if (opts_open) {
            const uint32_t LEG = 0xd2d3d5, LEG_DIM = 0x9a9ca0, PURPLE = 0x6c3a72, VFD = 0x6ff5ea, VFD_DIM = 0x3fa39b;
            auto vgrad = [&](int x, int y, int w, int h, uint32_t a, uint32_t b) {
                for (int i = 0; i < h; i++) {
                    float t = h > 1 ? (float)i / (h - 1) : 0;
                    auto ch = [&](int sh) { return (uint32_t)(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t + 0.5f); };
                    fill(x, y + i, w, 1, (ch(16) << 16) | (ch(8) << 8) | ch(0));
                }
            };
            auto tri_down = [&](float cx, float cy, float sz, uint32_t c) {
                SDL_Color col{ (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, 255 };
                SDL_Vertex v[3] = { { { cx - sz, cy - sz * 0.5f }, col, {0, 0} }, { { cx + sz, cy - sz * 0.5f }, col, {0, 0} }, { { cx, cy + sz * 0.6f }, col, {0, 0} } };
                SDL_RenderGeometry(R, nullptr, v, 3, nullptr, 0);
            };
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(R, 0, 0, 0, 165); SDL_Rect all{0, 0, W, H_cur}; SDL_RenderFillRect(R, &all);
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            // the plate
            fill(OX - 3, OY - 3, OW + 6, OH + 7, 0x050506);
            vgrad(OX, OY, OW, OH, 0x232427, 0x17181a);
            for (int yy = OY + 2; yy < OY + OH; yy += 2) {   // fine brushing
                uint32_t hsh = (uint32_t)yy * 2654435761u;
                if ((hsh >> 28) < 2) fill(OX, yy, OW, 1, 0x202124);
                else if ((hsh >> 28) < 4) fill(OX, yy, OW, 1, 0x1c1d1f);
            }
            fill(OX, OY, OW, 1, 0x4a4c51); fill(OX, OY + OH - 1, OW, 1, 0x0b0b0c);
            // title, letter-spaced like "DIGITAL SAMPLING WORKSTATION", with purple rules
            float tw = ui.width(UiText::TITLE, "OPTIONS  ·  AUDIO, MIDI AND SCSI", 3);
            ui.draw(UiText::TITLE, "OPTIONS  ·  AUDIO, MIDI AND SCSI", OX + OW / 2.0f, OY + 18, LEG, 3, 1);
            fill(OX + 36, OY + 30, (int)(OW / 2.0f - tw / 2 - 56), 2, PURPLE);
            fill((int)(OX + OW / 2.0f + tw / 2 + 20), OY + 30, (int)(OW / 2.0f - tw / 2 - 56), 2, PURPLE);
            // sections and rows
            for (const Sect &sc : sects) {
                int x0 = COL_X[sc.col], w0 = LABEL_W + FIELD_W;
                float sw = ui.width(UiText::BOLD, sc.title, 2.5f);
                ui.draw(UiText::BOLD, sc.title, (float)x0, sc.y + 6, LEG, 2.5f);
                fill((int)(x0 + sw + 12), sc.y + 14, (int)(w0 - sw - 12), 2, PURPLE);
                for (int row = sc.first; row <= sc.last; row++) {
                    SDL_Rect f = field_rect(row);
                    bool focus = row == opt_cur;
                    std::string lab = upper_utf8(rows[row].label);
                    ui.draw(UiText::SMALL, lab, (float)x0, f.y + 5, focus ? 0xffffff : LEG_DIM, 1.2f);
                    if (focus) fill(x0 - 12, f.y + 5, 4, 12, 0xff2a1a);         // a little red LED marks the focus
                    // field: smoked glass like the display window
                    fill(f.x - 2, f.y - 2, f.w + 4, f.h + 4, focus ? PURPLE : 0x2b2c2f);
                    fill(f.x, f.y, f.w, f.h, 0x030405);
                    std::string v = rows[row].items.empty() ? "-" : rows[row].items[rows[row].sel];
                    ui.draw(UiText::BODY, ui.fit(UiText::BODY, v, f.w - 40.0f), f.x + 10.0f, f.y + 3.0f, rows[row].items.empty() ? VFD_DIM : VFD);
                    fill(f.x + f.w - 26, f.y + 4, 1, f.h - 8, 0x1b1c1e);
                    tri_down(f.x + f.w - 13.0f, f.y + f.h / 2.0f, 5, drop_row == row ? VFD : LEG);
                }
            }
            // status
            int sy = OY + OH - 92;
            fill(OX + 36, sy - 12, OW - 72, 1, 0x2b2c2f);
            ui.draw(UiText::SMALL, "AUDIO", OX + 36.0f, (float)sy, LEG_DIM, 1.2f);
            ui.draw(UiText::BODY, ui.fit(UiText::BODY, audio_status, OW - 220.0f), OX + 128.0f, sy - 2.0f, audio_active ? 0x80e080 : 0xff8070);
            char u[64]; snprintf(u, sizeof u, "%u", (unsigned)underruns);
            ui.draw(UiText::SMALL, "DROPOUTS", OX + 36.0f, sy + 22.0f, LEG_DIM, 1.2f);
            ui.draw(UiText::BODY, u, OX + 128.0f, sy + 20.0f, underruns ? 0xffc060 : LEG);
            ui.draw(UiText::SMALL, "MIDI", OX + 230.0f, sy + 22.0f, LEG_DIM, 1.2f);
            std::string mi = "In: " + (midi.in_name.empty() ? std::string("-") : midi.in_name) + "     Out: " + (midi.out_name.empty() ? std::string("-") : midi.out_name);
            ui.draw(UiText::BODY, ui.fit(UiText::BODY, mi, 640), OX + 280.0f, sy + 20.0f, LEG);
            if (!opt_msg.empty()) ui.draw(UiText::BODY, ui.fit(UiText::BODY, opt_msg, OW - 400.0f), OX + 36.0f, sy + 44.0f, 0xff8070);
            else if (!scsi_note.empty()) ui.draw(UiText::BODY, ui.fit(UiText::BODY, scsi_note, OW - 400.0f), OX + 36.0f, sy + 44.0f, 0x80e080);
            ui.draw(UiText::SMALL, "Click a field to see its choices  ·  Arrows/Tab: move between fields  ·  Enter: open/choose  ·  A: apply  ·  Esc: close",
                    OX + 36.0f, OY + OH - 22.0f, 0x7a7c80);
            // buttons with their legends above, like Load/Cmd/Edit
            for (int k = 0; k < 2; k++) {
                const SDL_Rect &r = k ? btn_close : btn_apply;
                ui.draw(UiText::SMALL, k ? "CLOSE" : "APPLY", r.x + r.w / 2.0f, r.y - 16.0f, LEG, 1.2f, 1);
                fill(r.x - 1, r.y - 1, r.w + 2, r.h + 4, 0x060607);
                vgrad(r.x, r.y, r.w, r.h, 0x6e7074, 0x56585c);
                fill(r.x, r.y, r.w, 1, 0x8a8c90);
                fill(r.x, r.y + r.h - 1, r.w, 1, 0x3a3b3e);
            }
            // the open list, last so it covers everything
            if (drop_row >= 0) {
                SDL_Rect d = drop_rect();
                const auto &items = rows[drop_row].items;
                int n = (int)items.size();
                fill(d.x - 2, d.y - 2, d.w + 4, d.h + 4, PURPLE);
                fill(d.x, d.y, d.w, d.h, 0x040506);
                for (int i = drop_top; i < std::min(n, drop_top + DROP_MAX); i++) {
                    int iy = d.y + 3 + (i - drop_top) * DROP_H;
                    bool hov = i == drop_hover, cur = i == rows[drop_row].sel;
                    if (hov) fill(d.x + 2, iy, d.w - 4, DROP_H, 0x10302e);
                    if (cur) fill(d.x + 8, iy + DROP_H / 2 - 2, 4, 4, VFD);
                    ui.draw(UiText::BODY, ui.fit(UiText::BODY, items[i], d.w - 44.0f), d.x + 20.0f, iy + 4.0f, hov || cur ? VFD : VFD_DIM);
                }
                if (n > DROP_MAX) {                               // scroll bar
                    int th = std::max(16, (d.h - 6) * DROP_MAX / n), ty = d.y + 3 + (d.h - 6 - th) * drop_top / std::max(1, n - DROP_MAX);
                    fill(d.x + d.w - 7, d.y + 3, 3, d.h - 6, 0x1b1c1e);
                    fill(d.x + d.w - 7, ty, 3, th, LEG_DIM);
                }
            }
        }

        if (shot_at == -2) {
            SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, W, H_cur, 32, SDL_PIXELFORMAT_ARGB8888);
            int ow, oh; SDL_GetRendererOutputSize(R, &ow, &oh);
            if (ow == W && oh == H_cur && SDL_RenderReadPixels(R, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0)
                SDL_SaveBMP(sf, shot_path.c_str());
            SDL_FreeSurface(sf);
            shot_at = -1;
        }
        SDL_RenderPresent(R);
        SDL_Delay(audio_active ? 1 : 5);
    }
    stop = true;
    emu.join();
    aio.close();
    midi.close();
    {
        std::lock_guard<std::mutex> g(mmx);
        save_disk();
    }
    if (dbg_wav) if (FILE *f = fopen(dbg_wav, "wb")) {
        uint32_t n = (uint32_t)dbg_out.size(), sr = (uint32_t)cur_rate, br = sr * 4, ds = n * 2, rs = 36 + ds;
        fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        uint32_t fl = 16; uint16_t fmt = 1, chn = 2, ba = 4, bps = 16;
        fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&chn, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
        for (float v : dbg_out) { int x = (int)(std::clamp(v, -1.0f, 1.0f) * 32767); int16_t y = (int16_t)x; fwrite(&y, 2, 1, f); }
        fclose(f);
    }
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
