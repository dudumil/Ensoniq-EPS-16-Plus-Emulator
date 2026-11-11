// The emulated EPS-16 Plus inside the plug-in: owns one Eps16 machine and runs it in step with the host.
//
// The 68000 core (Musashi) keeps its state in globals, so several plug-in instances share it:
// every access to a machine goes through one process-wide lock and the CPU context of the
// instance that wants to run is swapped in first (activate()).
#pragma once
#include <atomic>
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include "eps16.h"

struct EngineMidi { int offset; uint8_t bytes[3]; int len; };

// what the editor needs to draw the front panel
struct EngineSnapshot {
    Panel panel;
    bool disk = false, drive_busy = false;
    uint32_t scsi_activity = 0;
    std::string floppy, scsi;            // floppy image path, SCSI summary
    std::string status;                  // last message for the status line
    uint32_t status_serial = 0;
    bool booted = false;
};

class Eps16Engine {
public:
    Eps16Engine();
    ~Eps16Engine();

    // resources = folder with the ROMs (and os130.img), user = writable folder for disk images
    bool boot(const std::string &resources, const std::string &user);
    bool booted() const { return ok; }
    const std::string &error() const { return err; }
    void message(const std::string &s) { set_status(s); }   // a line for the editor

    void set_sample_rate(double fs);
    // out[0..7] = main L/R, aux1 L/R, aux2 L/R, aux3 L/R (any may be null); in = stereo input or null.
    // aux_active[i] = AUX i+1 output bus is active in the host (it is then left out of the main mix)
    void process(float *const *out, const float *in_l, const float *in_r, int frames,
                 const std::vector<EngineMidi> &midi, const bool aux_active[3]);
    std::vector<uint8_t> take_midi_out();          // bytes sent by the EPS (audio thread)
    std::atomic<int> midi_channel{ 9 };            // host MIDI goes to this channel; 0 = unchanged (from eps16.cfg)

    // ----- control (any thread; queued and applied by process(), or directly when not processing)
    void button(int idx, bool down);               // panel button (RackUI index / PATCH_L / PATCH_R)
    void analog(int ch, uint16_t v);
    void insert_disk(const std::string &path);     // floppy (or SCSI hard disk when > 2 MB)
    void insert_files(const std::vector<std::string> &files);   // .efe/.efa files: put on a temporary floppy
    void eject_disk();
    void attach_scsi(const std::string &path);
    void detach_scsi();
    // settings come from the stand-alone program (eps16.cfg); call now and then (editor timer)
    // to pick up changes made there
    void check_settings();
    void power_reset();

    // state saved with the host project
    std::string floppy_path() const;
    std::vector<std::string> scsi_paths() const;
    void restore(const std::string &floppy, const std::vector<std::string> &scsi);

    EngineSnapshot snapshot();
    double emulated_seconds();                     // CPU time of the machine (diagnostics)
    void save_floppy();                            // write back a modified floppy image

private:
    void activate();                               // global lock must be held
    void run_commands();                           // global lock held, machine active
    void ensure_frames(size_t frames);
    void set_status(const std::string &s);
    std::string attach_scsi_folder(Eps16 &a);     // (re)attach the folder's drives + the user's images
    std::string scsi_folder_path() const;          // the folder actually used ("" = none)
    void apply_settings(Eps16 &a, bool first);     // read eps16.cfg and apply it

    std::unique_ptr<Eps16> m;
    std::vector<unsigned char> cpu_ctx;
    bool ok = false;
    std::string err, user_dir, res_dir;
    double fs = 44100;

    std::mutex cmd_mx;
    std::vector<std::function<void(Eps16 &)>> cmds;
    std::vector<uint8_t> midi_out;
    std::string status; uint32_t status_serial = 0;
    std::vector<std::string> scsi_list;            // images attached by the user, by SCSI ID
    std::string floppy;
    bool boot_scsi_ = false;
    std::string os_disk;                           // the OS disk image found at start (even when booting from SCSI)
    std::string scsi_folder;                       // chosen folder, "" = default
    std::string app_dir;                           // the stand-alone program's folder (from the .cfg)
    bool patch_held[2] = {};
    std::filesystem::file_time_type cfg_stamp{};
};

std::string eps16_module_dir();                    // folder of the plug-in binary
std::string eps16_user_dir();                      // per-user data folder (created)
