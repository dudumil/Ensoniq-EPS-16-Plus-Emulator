// WD1772 floppy controller, sector-level emulation with one 3.5" DD drive holding a raw
// Ensoniq disk image (80 tracks, 2 sides, 10 x 512-byte sectors = 800 KB), or an Ensoniq Mirage
// disk image (80 tracks, 1 side, sectors 0-4 of 1024 bytes and sector 5 of 512 bytes = 440 KB),
// read by Command > System > LOAD MIRAGE-DSK SOUND.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

class Wd1772 {
public:
    void reset();
    uint8_t read(int reg);              // 0 status, 1 track, 2 sector, 3 data
    void    write(int reg, uint8_t v);
    void    run(int clocks);            // CPU clocks (10 MHz)
    bool    drq() const { return m_drq; }
    bool    intrq() const { return m_intrq; }

    bool load(const std::string &path);
    bool save();
    void eject();
    bool has_disk() const { return !disk.empty(); }
    bool write_protect = false;
    int  side = 0;                      // from the side select output
    int  first_sector = 0;              // sector number of the first sector on a track
    bool dirty = false;
    std::string path;
    std::function<void(bool)> irq_cb, drq_cb;
    bool log = false;
    double cpu_clock = 10000000.0;
    bool motor_on() const { return m_motor > 0; }
    bool mirage() const { return mirage_; }
    static constexpr long MIRAGE_SIZE = 450560;   // 80 x (5 x 1024 + 512)

private:
    std::vector<uint8_t> disk;
    int tracks = 80, heads = 2, spt = 10, ssize = 512;
    bool mirage_ = false;               // Mirage geometry (see above)
    int sec_size(int sec) const;        // bytes in sector `sec` (sector register numbering)
    uint8_t status = 0, track = 0, sector = 0, data = 0, cmd = 0;
    int head_pos = 0;                   // physical cylinder
    int dir = 1;
    bool m_drq = false, m_intrq = false;
    int  m_motor = 0;                   // clocks left before the motor stops
public:
    unsigned step_count = 0;
    bool fast = true;                   // fast transfers and seeks (off = real drive timing)
    int byte_clocks() const;            // type I commands issued (for the disk-change line)
private:
    enum { IDLE, DELAY, READ, WRITE, RADDR, WTRACK } state = IDLE;
    int  delay = 0, byte_timer = 0, pos = 0;
    bool multi = false, type1 = true;
    std::vector<uint8_t> buf;
    long offset(int cyl, int hd, int sec) const;
    void set_drq(bool s) { if (s != m_drq) { m_drq = s; if (drq_cb) drq_cb(s); } }
    void set_irq(bool s) { if (s != m_intrq) { m_intrq = s; if (irq_cb) irq_cb(s); } }
    void finish(uint8_t err);
    void start_sector();
    uint8_t type1_status() const;
};
