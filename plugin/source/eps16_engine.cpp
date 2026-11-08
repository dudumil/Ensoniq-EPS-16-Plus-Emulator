#include "eps16_engine.h"
#include "efe_disk.h"
#include "settings.h"
#include "m68k.h"
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// ------------------------------------------------------------------ shared 68000 core
static std::mutex g_mx;                 // guards every machine and the Musashi globals
static Eps16Engine *g_owner = nullptr;  // instance whose CPU context is loaded in Musashi

// Patch Select buttons used by the editor (same indices as RackUI): a resistor network on AN1
static const int PATCH_L = 40, PATCH_R = 41;

Eps16Engine::Eps16Engine() { cpu_ctx.resize(m68k_context_size()); }

Eps16Engine::~Eps16Engine() {
    std::lock_guard<std::mutex> g(g_mx);
    if (m && m->fdc.dirty) m->fdc.save();
    if (g_owner == this) g_owner = nullptr;
    m.reset();
    g_machine = g_owner ? g_owner->m.get() : nullptr;
}

void Eps16Engine::activate() {
    if (g_owner != this) {
        if (g_owner) m68k_get_context(g_owner->cpu_ctx.data());
        m68k_set_context(cpu_ctx.data());
        g_owner = this;
    }
    g_machine = m.get();
}

static bool copy_if_missing(const fs::path &src, const fs::path &dst) {
    std::error_code ec;
    if (fs::exists(dst, ec)) return true;
    if (!fs::exists(src, ec)) return false;
    fs::create_directories(dst.parent_path(), ec);
    return fs::copy_file(src, dst, ec);
}

bool Eps16Engine::boot(const std::string &resources, const std::string &user) {
    std::lock_guard<std::mutex> g(g_mx);
    res_dir = resources; user_dir = user;
    if (g_owner && g_owner != this) m68k_get_context(g_owner->cpu_ctx.data());   // keep the running one
    m.reset(new Eps16());                  // the constructor points g_machine at the new machine
    g_owner = this;
    m->fdc.fast = true;                    // fast floppy transfers, as in the stand-alone program
    m->out_rate = fs;
    m->midi_out = [this](uint8_t b) { midi_out.push_back(b); };
    // ROMs: inside the bundle, or in the user folder (<user>/roms or <user>)
    const std::string dirs[3] = { resources, (fs::u8path(user) / "roms").u8string(), user };
    std::string rom_dir;
    for (auto &d : dirs) if (m->load_rom_dir(d)) { rom_dir = d; break; }
    if (rom_dir.empty()) {
        err = "ROM do EPS-16 Plus nao encontrada: copia eps16plus-u27.bin e eps16plus-u28.bin para " + (fs::u8path(user) / "roms").u8string();
        return ok = false;
    }
    // the OS disk lives in the user folder (writable, shared with the stand-alone program)
    std::error_code ec;
    fs::path os_user = fs::u8path(user) / "os130.img";
    for (auto &d : dirs) if (copy_if_missing(fs::u8path(d) / "os130.img", os_user)) break;
    if (!fs::exists(os_user, ec))                  // no os130.img: the first *.img found
        for (auto &d : dirs) {
            std::vector<fs::path> imgs;
            for (auto &e : fs::directory_iterator(fs::u8path(d), ec)) {
                std::string ext = e.path().extension().string();
                for (auto &c : ext) c = (char)tolower((unsigned char)c);
                if (ext == ".img" && e.is_regular_file(ec)) imgs.push_back(e.path());
            }
            std::sort(imgs.begin(), imgs.end());
            if (!imgs.empty() && copy_if_missing(imgs[0], os_user)) break;
        }
    floppy = fs::exists(os_user, ec) ? os_user.u8string() : std::string();
    if (!floppy.empty() && !m->fdc.load(floppy)) floppy.clear();
    m->analog[Eps16::AN_VOLUME] = 1000;
    apply_settings(*m, true);              // the stand-alone program's settings (eps16.cfg)
    m->reset();
    ok = true;
    set_status(floppy.empty() ? "Sem disquete do OS: arrasta uma imagem para o painel" : "A arrancar o OS...");
    return true;
}

void Eps16Engine::set_status(const std::string &s) { status = s; status_serial++; }

void Eps16Engine::set_sample_rate(double rate) {
    std::lock_guard<std::mutex> g(g_mx);
    fs = rate > 1000 ? rate : 44100;
    if (m) { m->out_rate = fs; m->audio.clear(); m->audio_aux.clear(); }
}

void Eps16Engine::run_commands() {
    std::vector<std::function<void(Eps16 &)>> todo;
    { std::lock_guard<std::mutex> g(cmd_mx); todo.swap(cmds); }
    for (auto &f : todo) f(*m);
}

void Eps16Engine::ensure_frames(size_t frames) {
    for (int guard = 0; m->audio.size() / 2 < frames && guard < 10000; guard++) {
        size_t have = m->audio.size() / 2;
        double clocks = (double)(frames - have) * m->cpu_clock / fs;
        m->run((int64_t)clocks + 64);
    }
}

void Eps16Engine::process(float *const *out, const float *in_l, const float *in_r, int frames,
                          const std::vector<EngineMidi> &midi, const bool aux_active[3]) {
    std::lock_guard<std::mutex> g(g_mx);
    if (!ok || frames <= 0) {
        for (int c = 0; c < 8; c++) if (out[c]) memset(out[c], 0, sizeof(float) * (frames > 0 ? frames : 0));
        return;
    }
    activate();
    run_commands();
    bool sep_before = m->aux_separate();
    for (int i = 0; i < 3; i++) m->aux_to_main[i] = !aux_active[i];
    if (m->aux_separate() != sep_before) m->audio_aux.assign((m->audio.size() / 2) * 6, 0.0f);
    if (in_l && in_r) {
        static thread_local std::vector<float> tmp;
        tmp.resize((size_t)frames * 2);
        for (int i = 0; i < frames; i++) { tmp[2 * i] = in_l[i]; tmp[2 * i + 1] = in_r[i]; }
        m->feed_input(tmp.data(), frames, fs);
    }
    const int ch = midi_channel.load();
    for (auto &e : midi) {
        ensure_frames((size_t)std::max(0, std::min(e.offset, frames)));
        for (int k = 0; k < e.len; k++) {
            uint8_t b = e.bytes[k];
            // the OS disk is set to MIDI MULTI (instruments on channels 9-16): channel messages
            // from the host go to the chosen channel, like the stand-alone "MIDI In > channel"
            if (ch >= 1 && ch <= 16 && b >= 0x80 && b < 0xf0) b = (uint8_t)((b & 0xf0) | (ch - 1));
            m->midi_in(b);
        }
    }
    ensure_frames((size_t)frames);
    const float *a = m->audio.data();
    if (out[0]) for (int i = 0; i < frames; i++) out[0][i] = a[2 * i];
    if (out[1]) for (int i = 0; i < frames; i++) out[1][i] = a[2 * i + 1];
    bool sep = m->aux_separate() && m->audio_aux.size() >= (size_t)frames * 6;
    for (int c = 2; c < 8; c++) {
        if (!out[c]) continue;
        if (sep) { const float *x = m->audio_aux.data(); for (int i = 0; i < frames; i++) out[c][i] = x[6 * i + (c - 2)]; }
        else memset(out[c], 0, sizeof(float) * frames);
    }
    m->audio.erase(m->audio.begin(), m->audio.begin() + (size_t)frames * 2);
    if (sep) m->audio_aux.erase(m->audio_aux.begin(), m->audio_aux.begin() + (size_t)frames * 6);
    else m->audio_aux.clear();
}

std::vector<uint8_t> Eps16Engine::take_midi_out() {
    std::vector<uint8_t> r;
    r.swap(midi_out);             // filled while the machine runs inside process(): same thread
    return r;
}

// ------------------------------------------------------------------ control
static void push(std::mutex &mx, std::vector<std::function<void(Eps16 &)>> &q, std::function<void(Eps16 &)> f) {
    std::lock_guard<std::mutex> g(mx); q.push_back(std::move(f));
}

void Eps16Engine::button(int idx, bool down) {
    push(cmd_mx, cmds, [this, idx, down](Eps16 &a) {
        if (idx == PATCH_L || idx == PATCH_R) {
            patch_held[idx - PATCH_L] = down;
            static const uint16_t v[4] = { 0, 770, 500, 1010 };   // none, left, right, both
            a.analog[1] = v[(patch_held[0] ? 1 : 0) | (patch_held[1] ? 2 : 0)];
        } else if (idx >= 0 && idx < 64) a.panel.button(idx, down);
    });
}

void Eps16Engine::analog(int ch, uint16_t v) {
    push(cmd_mx, cmds, [ch, v](Eps16 &a) { if (ch >= 0 && ch < 8) a.analog[ch] = v; });
}

void Eps16Engine::save_floppy() {
    std::lock_guard<std::mutex> g(g_mx);
    if (m && m->fdc.dirty && m->fdc.save()) set_status("Disquete gravada: " + m->fdc.path);
}

void Eps16Engine::insert_files(const std::vector<std::string> &files) {
    push(cmd_mx, cmds, [this, files](Eps16 &a) {
        std::string path, label, err = efe::make_disk_file(files, false, "eps16-", path, label);
        if (!err.empty()) { set_status(err); return; }
        if (a.fdc.dirty) a.fdc.save();
        if (a.insert_disk(path)) { floppy = path; set_status("\"" + label + "\" na disquete: carrega em LOAD para o carregar"); }
        else set_status("Nao consegui abrir " + path);
    });
}

void Eps16Engine::insert_disk(const std::string &path) {
    if (efe::is_single_file(path)) { insert_files({ path }); return; }
    push(cmd_mx, cmds, [this, path](Eps16 &a) {
        std::error_code ec;
        auto size = fs::file_size(fs::u8path(path), ec);
        if (!ec && size > 2 * 1024 * 1024) {      // a hard disk image: on the SCSI bus
            bool before[8];
            for (int i = 0; i < 8; i++) before[i] = a.scsi.dev[i] != nullptr;
            std::string r = a.scsi.attach_file(path);
            for (int i = 0; i < 8; i++) if (!before[i] && a.scsi.dev[i]) {
                if (scsi_list.size() <= (size_t)i) scsi_list.resize(i + 1);
                scsi_list[i] = path;
            }
            set_status(r.empty() ? "Nao consegui ligar " + path : r + "  (Power ~1 s para o OS o ver)");
            return;
        }
        if (a.fdc.dirty) a.fdc.save();
        if (a.insert_disk(path)) { floppy = path; set_status("Disquete: " + path); }
        else set_status("Nao consegui abrir " + path);
    });
}

void Eps16Engine::eject_disk() {
    push(cmd_mx, cmds, [this](Eps16 &a) {
        if (!a.fdc.has_disk()) { set_status("Drive vazio"); return; }
        if (a.fdc.dirty) a.fdc.save();
        a.eject_disk(); floppy.clear(); set_status("Disquete ejetada");
    });
}

void Eps16Engine::attach_scsi(const std::string &path) {
    push(cmd_mx, cmds, [this, path](Eps16 &a) {
        int id = -1;
        for (int i = 0; i < 8 && id < 0; i++) if (i != 3 && !a.scsi.dev[i]) id = i;
        if (id < 0) { set_status("SCSI: nao ha IDs livres"); return; }
        if (!a.scsi.attach(id, path, ScsiDevice::HD)) { set_status("Nao consegui abrir " + path); return; }
        if (scsi_list.size() <= (size_t)id) scsi_list.resize(id + 1);
        scsi_list[id] = path;
        set_status("SCSI ID " + std::to_string(id) + ": " + path + "  (Power ~1 s para o OS o ver)");
    });
}

std::string Eps16Engine::scsi_folder_path() const {
    std::error_code ec;
    if (!scsi_folder.empty() && fs::is_directory(fs::u8path(scsi_folder), ec)) return scsi_folder;
    fs::path d = fs::u8path(user_dir) / "scsi";
    return fs::is_directory(d, ec) ? d.u8string() : std::string();
}

std::string Eps16Engine::attach_scsi_folder(Eps16 &a) {
    for (auto &d : a.scsi.dev) d.reset();
    std::error_code ec;
    for (size_t i = 0; i < scsi_list.size(); i++)        // images attached one by one stay
        if (!scsi_list[i].empty() && fs::exists(fs::u8path(scsi_list[i]), ec)) a.scsi.attach((int)i, scsi_list[i], ScsiDevice::HD);
    std::string d = scsi_folder_path();
    if (!d.empty()) a.scsi.attach_dir(d);
    return d;
}

// The plug-in has no settings of its own: it follows the stand-alone program's eps16.cfg (SCSI
// folder, MIDI In > channel, BUS 2/3 into main, AUX 1 off), read at start and whenever it changes.
static fs::file_time_type cfg_time(const std::string &p) {
    std::error_code ec;
    auto t = fs::last_write_time(fs::u8path(p), ec);
    return ec ? fs::file_time_type::min() : t;
}

void Eps16Engine::apply_settings(Eps16 &a, bool first) {
    std::string cfg = (fs::u8path(user_dir) / "eps16.cfg").u8string();
    cfg_stamp = cfg_time(cfg);
    Settings st; st.load(cfg);
    midi_channel = st.midi_remap ? st.kbd_channel : 0;
    a.bus_to_main[0] = st.bus_main[0]; a.bus_to_main[1] = st.bus_main[1];
    a.aux1_mix = st.aux_pair[0] != -2;
    if (first || st.scsi_dir != scsi_folder) {
        scsi_folder = st.scsi_dir;
        std::string d = attach_scsi_folder(a);
        if (!first) set_status(d.empty() ? "Sem pasta SCSI" : "Pasta SCSI da app: " + d + "  (Reset para o EPS ver as drives)");
    }
}

void Eps16Engine::check_settings() {
    if (!ok || cfg_time((fs::u8path(user_dir) / "eps16.cfg").u8string()) == cfg_stamp) return;
    push(cmd_mx, cmds, [this](Eps16 &a) { apply_settings(a, false); });
}

void Eps16Engine::detach_scsi() {
    push(cmd_mx, cmds, [this](Eps16 &a) {
        for (int i = 0; i < 8; i++) a.scsi.dev[i].reset();
        scsi_list.clear(); set_status("Discos SCSI desligados");
    });
}

void Eps16Engine::power_reset() {
    push(cmd_mx, cmds, [this](Eps16 &a) {
        if (a.fdc.dirty) a.fdc.save();
        a.reset(); set_status("Reset (Power)");
    });
}

std::string Eps16Engine::floppy_path() const { return floppy; }
std::vector<std::string> Eps16Engine::scsi_paths() const { return scsi_list; }

void Eps16Engine::restore(const std::string &fl, const std::vector<std::string> &scsi) {
    push(cmd_mx, cmds, [this, fl, scsi](Eps16 &a) {
        std::error_code ec;
        bool any = false;
        scsi_list.clear();
        for (size_t i = 0; i < scsi.size() && i < 8; i++) {
            if (scsi[i].empty() || i == 3 || !fs::exists(fs::u8path(scsi[i]), ec)) continue;
            if (scsi_list.size() <= i) scsi_list.resize(i + 1);
            scsi_list[i] = scsi[i]; any = true;
        }
        attach_scsi_folder(a);
        if (!fl.empty() && fl != floppy && fs::exists(fs::u8path(fl), ec)) {
            if (a.fdc.dirty) a.fdc.save();
            if (a.fdc.load(fl)) floppy = fl;
        }
        if (any || !fl.empty()) a.reset();           // the OS looks for SCSI devices when it boots
    });
}

EngineSnapshot Eps16Engine::snapshot() {
    EngineSnapshot s;
    std::lock_guard<std::mutex> g(g_mx);
    if (!m) return s;
    if (ok) { activate(); run_commands(); }
    s.booted = ok;
    s.panel = m->panel;
    s.disk = m->fdc.has_disk(); s.drive_busy = m->fdc.motor_on();
    s.floppy = s.disk ? m->fdc.path : std::string();
    s.scsi = m->scsi.summary();
    s.status = ok ? status : err; s.status_serial = status_serial;
    return s;
}

double Eps16Engine::emulated_seconds() {
    std::lock_guard<std::mutex> g(g_mx);
    return m ? m->cycles() / m->cpu_clock : 0;
}

// ------------------------------------------------------------------ folders
std::string eps16_module_dir() {
#if defined(_WIN32)
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&eps16_module_dir, &h);
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(h, buf, MAX_PATH * 2);
    return fs::path(std::wstring(buf, n)).parent_path().u8string();
#else
    Dl_info info;
    if (dladdr((void *)&eps16_module_dir, &info) && info.dli_fname)
        return fs::path(info.dli_fname).parent_path().u8string();
    return ".";
#endif
}

std::string eps16_user_dir() {
    fs::path p;
#if defined(_WIN32)
    if (const wchar_t *a = _wgetenv(L"APPDATA")) p = fs::path(a) / "Ensoniq" / "EPS16emu";
#elif defined(__APPLE__)
    if (const char *h = getenv("HOME")) p = fs::path(h) / "Library" / "Application Support" / "Ensoniq" / "EPS16emu";
#else
    if (const char *x = getenv("XDG_DATA_HOME")) p = fs::path(x) / "Ensoniq" / "EPS16emu";
    else if (const char *h = getenv("HOME")) p = fs::path(h) / ".local" / "share" / "Ensoniq" / "EPS16emu";
#endif
    if (p.empty()) p = fs::temp_directory_path() / "EPS16emu";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.u8string();
}
