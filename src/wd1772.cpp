#include "wd1772.h"
#include "u8file.h"
#include <cstdio>
#include <cstring>

namespace {
enum { S_BUSY = 0x01, S_DRQ = 0x02, S_INDEX = 0x02, S_LOST = 0x04, S_TR00 = 0x04, S_CRC = 0x08,
       S_RNF = 0x10, S_SPIN = 0x20, S_WP = 0x40, S_MOTOR = 0x80 };
constexpr int BYTE_CLOCKS_REAL = 320; // 32 us per byte at 250 kbit/s (10 MHz CPU clocks)
constexpr int BYTE_CLOCKS_FAST = 4;   // fast mode: as quick as the DMA or the CPU takes the bytes
constexpr int MOTOR_CLOCKS = 20000000; // ~2 s (10 revolutions) after the last command
}

int Wd1772::byte_clocks() const { return fast ? BYTE_CLOCKS_FAST : BYTE_CLOCKS_REAL; }

void Wd1772::reset() {
    status = 0; cmd = 0; state = IDLE; m_motor = 0;
    set_drq(false); set_irq(false);
}

bool Wd1772::load(const std::string &p) {
    FILE *f = u8fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    std::vector<uint8_t> d(n);
    bool ok = fread(d.data(), 1, n, f) == (size_t)n;
    fclose(f);
    if (!ok || n <= 0) return false;
    disk.swap(d); path = p; dirty = false;
    mirage_ = disk.size() == (size_t)MIRAGE_SIZE;
    heads = mirage_ ? 1 : 2;
    spt = mirage_ ? 6 : disk.size() == 1638400 ? 20 : disk.size() == 737280 ? 9 : disk.size() == 1474560 ? 18 : 10;
    return true;
}

bool Wd1772::save() {
    if (disk.empty() || path.empty() || !dirty) return true;
    FILE *f = u8fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(disk.data(), 1, disk.size(), f) == disk.size();
    fclose(f);
    if (ok) dirty = false;
    return ok;
}

void Wd1772::eject() { save(); disk.clear(); path.clear(); mirage_ = false; heads = 2; spt = 10; }

int Wd1772::sec_size(int sec) const {
    if (mirage_) return sec - first_sector == 5 ? 512 : 1024;
    return ssize;
}

long Wd1772::offset(int cyl, int hd, int sec) const {
    sec -= first_sector;
    if (disk.empty() || cyl < 0 || cyl >= tracks || hd < 0 || hd >= heads || sec < 0 || sec >= spt) return -1;
    if (mirage_) return (long)cyl * (5 * 1024 + 512) + sec * 1024L;   // 5 x 1024, then the 512-byte sector
    return ((long)(cyl * heads + hd) * spt + sec) * ssize;
}

uint8_t Wd1772::type1_status() const {
    uint8_t s = status & (S_BUSY | S_RNF | S_CRC);
    if (m_motor > 0) s |= S_MOTOR | S_SPIN;
    if (head_pos == 0) s |= S_TR00;
    if (write_protect && has_disk()) s |= S_WP;
    return s;
}

uint8_t Wd1772::read(int reg) {
    switch (reg & 3) {
    case 0:
        set_irq(false);
        if (type1) return type1_status();
        return (status & ~(S_MOTOR | S_DRQ)) | (m_motor > 0 ? S_MOTOR : 0) | (m_drq ? S_DRQ : 0);
    case 1: return track;
    case 2: return sector;
    default:
        if (state == READ || state == RADDR) set_drq(false);
        return data;
    }
}

void Wd1772::finish(uint8_t err) {
    status = (status & ~S_BUSY) | err;
    state = IDLE;
    set_drq(false);
    set_irq(true);
}

void Wd1772::start_sector() {
    long off = offset(head_pos, side, sector);
    if (log) fprintf(stderr, "[FDC] %s c=%d h=%d s=%d (track reg %d)%s\n", (cmd & 0x20) ? "WRITE" : "READ", head_pos, side, sector, track, off < 0 ? " RNF" : "");
    if (off < 0 || track != head_pos) { finish(S_RNF); return; }
    pos = 0; byte_timer = byte_clocks() * 4;
    if (cmd & 0x20) {
        if (write_protect) { finish(S_WP); return; }
        buf.assign(sec_size(sector), 0);
        state = WRITE; set_drq(true);
    } else {
        buf.assign(disk.begin() + off, disk.begin() + off + sec_size(sector));
        state = READ;
    }
}

void Wd1772::write(int reg, uint8_t v) {
    switch (reg & 3) {
    case 0: {
        if ((v & 0xf0) == 0xd0) {          // force interrupt
            if (log) fprintf(stderr, "[FDC] force interrupt %02x\n", v);
            state = IDLE; status &= ~S_BUSY; set_drq(false);
            if (v & 0x0f) set_irq(true); else set_irq(false);
            type1 = true;
            return;
        }
        if (status & S_BUSY) return;
        cmd = v; set_irq(false);
        m_motor = MOTOR_CLOCKS;
        status = S_BUSY;
        if (!(v & 0x80)) {                  // type I
            type1 = true; step_count++;
            int target = head_pos;
            switch (v >> 4) {
            case 0: target = 0; track = 0; break;
            case 1: target = head_pos + (data - track); track = data; break;
            case 2: case 3: target = head_pos + dir; if (v & 0x10) track += dir; break;
            case 4: case 5: dir = 1; target = head_pos + 1; if (v & 0x10) track++; break;
            case 6: case 7: dir = -1; target = head_pos - 1; if (v & 0x10) track--; break;
            }
            if (target < 0) target = 0;
            if (target > 83) target = 83;
            int steps = target > head_pos ? target - head_pos : head_pos - target;
            head_pos = target;
            if ((v >> 4) == 0) track = 0;
            if (log) fprintf(stderr, "[FDC] cmd %02x -> cyl %d track reg %d\n", v, head_pos, track);
            state = DELAY; delay = fast ? 1000 + steps * 300 : 2000 + steps * 6 * 10000 / 2;   // 6 ms/step (sped up 2x)
            return;
        }
        type1 = false;
        switch (v >> 5) {
        case 4: case 5:                     // read / write sector
            multi = (v & 0x10) != 0;
            if (!has_disk()) { state = DELAY; delay = 50000; status |= S_RNF; return; }
            state = DELAY; delay = fast ? 1000 : (v & 0x04) ? 150000 : 2000;
            break;
        case 6:                             // read address (C0) / force interrupt handled above
            if (!has_disk()) { finish(S_RNF); return; }
            buf = { (uint8_t)head_pos, (uint8_t)side, sector, (uint8_t)(sec_size(sector) == 1024 ? 3 : 2), 0, 0 };
            pos = 0; state = RADDR; byte_timer = byte_clocks() * 8;
            break;
        case 7:                             // read track (E0) / write track (F0): format
            if (v & 0x10) {
                if (write_protect) { finish(S_WP); return; }
                state = WTRACK; pos = 0; byte_timer = byte_clocks(); set_drq(true);
            } else { finish(S_RNF); }
            break;
        }
        return;
    }
    case 1: track = v; return;
    case 2: sector = v; return;
    case 3:
        data = v;
        if (state == WRITE || state == WTRACK) set_drq(false);
        return;
    }
}

void Wd1772::run(int clocks) {
    if (m_motor > 0 && state == IDLE) m_motor -= clocks;
    switch (state) {
    case IDLE: return;
    case DELAY:
        delay -= clocks;
        if (delay > 0) return;
        if (type1) { finish(0); return; }
        if (status & S_RNF) { finish(S_RNF); return; }
        start_sector();
        return;
    case READ: case RADDR:
        byte_timer -= clocks;
        while (byte_timer <= 0 && (state == READ || state == RADDR)) {
            if (m_drq && fast) { byte_timer = 1; break; }   // fast mode: wait for the DMA, never lose data
            byte_timer += byte_clocks();
            if (m_drq) status |= S_LOST;    // previous byte not taken
            if (pos >= (int)buf.size()) {
                if (state == READ && multi) { sector++; start_sector(); if (state != READ) return; continue; }
                if (state == RADDR) track = buf[0];
                finish(status & S_LOST);
                return;
            }
            data = buf[pos++];
            set_drq(true);
        }
        return;
    case WRITE:
        byte_timer -= clocks;
        while (byte_timer <= 0 && state == WRITE) {
            if (m_drq && fast) { byte_timer = 1; break; }
            byte_timer += byte_clocks();
            if (m_drq) { status |= S_LOST; data = 0; }
            buf[pos++] = data;
            if (pos >= (int)buf.size()) {
                long off = offset(head_pos, side, sector);
                if (off >= 0) { memcpy(&disk[off], buf.data(), buf.size()); dirty = true; }
                if (multi) { sector++; start_sector(); if (state != WRITE) return; continue; }
                finish(status & S_LOST);
                return;
            }
            set_drq(true);
        }
        return;
    case WTRACK:
        // formatting: accept the raw track bytes for one revolution (6250 bytes) and clear the
        // sectors of this track in the image
        byte_timer -= clocks;
        while (byte_timer <= 0 && state == WTRACK) {
            if (m_drq && fast) { byte_timer = 1; break; }
            byte_timer += byte_clocks();
            pos++;
            if (pos >= (fast ? 6800 : 6250)) {
                for (int s = 0; s < spt; s++) {
                    long off = offset(head_pos, side, s + first_sector);
                    if (off >= 0) { memset(&disk[off], 0, sec_size(s + first_sector)); dirty = true; }
                }
                finish(0);
                return;
            }
            set_drq(true);
        }
        return;
    }
}
