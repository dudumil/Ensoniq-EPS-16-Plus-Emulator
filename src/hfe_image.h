#pragma once
// HxC Floppy Emulator images (.HFE, format revision 1, "HXCPICFE"): the flux of each track as MFM
// cells, as made by the HxC software, Gotek / FlashFloppy and Greaseweazle tools. An Ensoniq disk is
// a standard IBM MFM disk: 80 cylinders, 2 sides, 512-byte sectors (10 per track on a DD disk, 20 on an
// ASR-10 HD disk).
//   header (512 bytes): "HXCPICFE", revision, tracks, sides, encoding (0 = IBM MFM), bit rate (kbit/s),
//   RPM, interface mode, 1, track list offset (in 512-byte blocks), write allowed...
//   track list: per cylinder, the offset of its data (in 512-byte blocks) and its length in bytes
//   track data: 512-byte blocks, the first 256 bytes for side 0 and the next 256 for side 1; each byte
//   holds 8 MFM cells, the first one in bit 0.
// The floppy controllers work on the raw image: decode() on load (the sectors are found by their ID
// fields), encode() on save (the tracks are written again as standard MFM tracks, with the file's own
// track lengths). create() makes a new image from a raw one.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hfe {
constexpr int CYLS = 80, SECTOR = 512;

// a file named *.hfe (any case)
inline bool is_hfe_path(const std::string &p) {
    if (p.size() < 4) return false;
    std::string e = p.substr(p.size() - 4);
    for (auto &c : e) c = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return e == ".hfe";
}
// the floppy disk images the emulator takes as the OS disk (raw, Giebler, HxC)
inline bool is_floppy_image_ext(std::string e) {
    for (auto &c : e) c = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return e == ".img" || e == ".ede" || e == ".eda" || e == ".hfe";
}

inline bool looks_like(const std::vector<uint8_t> &d) {
    return d.size() >= 1024 && memcmp(d.data(), "HXCPICFE", 8) == 0;
}

struct Image {
    std::vector<uint8_t> file;      // the whole file (written back with new track data)
    int first_id = 1;               // the number of the first sector on a track (0 on some Ensoniq disks)
};

namespace detail {
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline uint16_t crc16(const uint8_t *p, size_t n, uint16_t c = 0xffff) {
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}
// the cells of one side of a track (bit i = cell i)
inline std::vector<uint8_t> side_cells(const std::vector<uint8_t> &f, size_t off, size_t len, int side) {
    std::vector<uint8_t> cells;
    cells.reserve(len / 2 * 8);
    for (size_t pos = 0; pos < len; pos += 512) {
        size_t base = off + pos + (size_t)side * 256, n = std::min<size_t>(256, (len - pos) / 2);
        for (size_t i = 0; i < n && base + i < f.size(); i++)
            for (int b = 0; b < 8; b++) cells.push_back((uint8_t)(f[base + i] >> b & 1));
    }
    return cells;
}
inline uint8_t mfm_byte(const std::vector<uint8_t> &c, size_t p) {      // p = first cell (a clock cell)
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v = (uint8_t)(v << 1 | (p + 2 * i + 1 < c.size() ? c[p + 2 * i + 1] : 0));
    return v;
}
// MFM cells of a byte stream; marks[i] = true: the byte is an A1 sync byte (clock missing: 4489)
struct Writer {
    std::vector<uint8_t> cells;
    uint8_t prev = 0;
    void byte(uint8_t v, bool sync = false) {
        for (int i = 7; i >= 0; i--) {
            uint8_t d = v >> i & 1;
            uint8_t clk = (uint8_t)(!(prev | d));
            if (sync && i == 2) clk = 0;          // A1 with the clock between bits 4 and 5 missing
            cells.push_back(clk); cells.push_back(d);
            prev = d;
        }
    }
    void fill(uint8_t v, int n) { for (int i = 0; i < n; i++) byte(v); }
};
// the MFM cells of one side of a track holding `spt` sectors from raw, `capacity` data bytes long
inline std::vector<uint8_t> make_track(const uint8_t *raw, int cyl, int head, int spt, int first_id, size_t capacity) {
    Writer w;
    const size_t fixed_per_sector = 12 + 4 + 4 + 2 + 22 + 12 + 4 + SECTOR + 2;
    const size_t lead = 80 + 12 + 4 + 50;
    long room = (long)capacity - (long)lead - (long)(fixed_per_sector * spt) - 16;
    int gap3 = (int)std::max<long>(4, std::min<long>(84, room / spt));
    w.fill(0x4e, 80); w.fill(0x00, 12);
    w.byte(0xc2); w.byte(0xc2); w.byte(0xc2); w.byte(0xfc);     // index mark (clock-intact C2s: harmless)
    w.fill(0x4e, 50);
    for (int s = 0; s < spt; s++) {
        uint8_t id[8] = { 0xa1, 0xa1, 0xa1, 0xfe, (uint8_t)cyl, (uint8_t)head, (uint8_t)(first_id + s), 2 };
        uint16_t c = crc16(id, 8);
        w.fill(0x00, 12);
        for (int i = 0; i < 3; i++) w.byte(0xa1, true);
        for (int i = 3; i < 8; i++) w.byte(id[i]);
        w.byte((uint8_t)(c >> 8)); w.byte((uint8_t)c);
        w.fill(0x4e, 22); w.fill(0x00, 12);
        const uint8_t *d = raw + ((size_t)(cyl * 2 + head) * spt + s) * SECTOR;
        uint8_t mark[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
        c = crc16(d, SECTOR, crc16(mark, 4));
        for (int i = 0; i < 3; i++) w.byte(0xa1, true);
        w.byte(0xfb);
        for (int i = 0; i < SECTOR; i++) w.byte(d[i]);
        w.byte((uint8_t)(c >> 8)); w.byte((uint8_t)c);
        w.fill(0x4e, gap3);
    }
    while (w.cells.size() < capacity * 16) w.byte(0x4e);
    w.cells.resize(capacity * 16);
    return w.cells;
}
inline void put_side(std::vector<uint8_t> &f, size_t off, size_t len, int side, const std::vector<uint8_t> &cells) {
    size_t k = 0;
    for (size_t pos = 0; pos < len; pos += 512) {
        size_t base = off + pos + (size_t)side * 256, n = std::min<size_t>(256, (len - pos) / 2);
        for (size_t i = 0; i < n && base + i < f.size(); i++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++, k++) if (k < cells.size() && cells[k]) v |= (uint8_t)(1 << b);
            f[base + i] = v;
        }
    }
}
} // namespace detail

// file -> raw image (80 cylinders x 2 sides x spt x 512 bytes); err says why when false
inline bool decode(const std::vector<uint8_t> &f, std::vector<uint8_t> &raw, Image &img, std::string &err) {
    using namespace detail;
    if (!looks_like(f)) { err = "not an HFE image"; return false; }
    if (f[8] != 0) { err = "HFE format revision " + std::to_string(f[8]) + " is not supported (only revision 1)"; return false; }
    const int tracks = f[9], sides = f[10];
    if (f[11] != 0 && f[11] != 0xff) { err = "the HFE image is not an MFM disk"; return false; }
    if (sides < 1 || sides > 2 || tracks < 1) { err = "bad HFE header"; return false; }
    const size_t list = (size_t)rd16(&f[18]) * 512;
    if (list + 4 * (size_t)tracks > f.size()) { err = "bad HFE track list"; return false; }
    std::vector<std::vector<uint8_t>> sectors((size_t)CYLS * 2 * 64);   // [(cyl*2+head)*64 + r]
    std::vector<uint8_t> have((size_t)CYLS * 2 * 64, 0);
    int min_r = 255, max_r = -1;
    for (int t = 0; t < tracks && t < CYLS; t++) {
        size_t off = (size_t)rd16(&f[list + 4 * t]) * 512, len = rd16(&f[list + 4 * t + 2]);
        if (off >= f.size()) continue;
        for (int s = 0; s < sides; s++) {
            std::vector<uint8_t> c = side_cells(f, off, len, s);
            // find the sync marks (three 4489 words in a row), then ID and data fields
            int idc = -1, idh = -1, idr = -1, idn = -1;
            uint16_t sh = 0;
            for (size_t p = 0; p < c.size(); p++) {
                sh = (uint16_t)(sh << 1 | c[p]);
                if (sh != 0x4489 || p + 1 + 32 + 16 > c.size()) continue;
                size_t q = p + 1;                     // the cell after the first sync word
                int n = 1;
                auto word = [&](size_t at) { uint16_t v = 0; for (int i = 0; i < 16; i++) v = (uint16_t)(v << 1 | (at + i < c.size() ? c[at + i] : 0)); return v; };
                while (n < 3 && word(q) == 0x4489) { q += 16; n++; }
                if (n < 3) continue;
                uint8_t mark = mfm_byte(c, q);
                if (mark == 0xfe) {
                    uint8_t h[8] = { 0xa1, 0xa1, 0xa1, 0xfe };
                    for (int i = 0; i < 4; i++) h[4 + i] = mfm_byte(c, q + 16 * (1 + i));
                    uint16_t crc = (uint16_t)(mfm_byte(c, q + 16 * 5) << 8 | mfm_byte(c, q + 16 * 6));
                    if (crc16(h, 8) == crc) { idc = h[4]; idh = h[5]; idr = h[6]; idn = h[7]; }
                    else idc = -1;
                    p = q + 16 * 7 - 1; sh = 0;
                } else if ((mark == 0xfb || mark == 0xf8) && idc >= 0) {
                    if (idn != 2) { err = "the HFE disk has sectors that are not 512 bytes (not an Ensoniq EPS / ASR disk?)"; return false; }
                    std::vector<uint8_t> d(SECTOR + 4);
                    d[0] = d[1] = d[2] = 0xa1; d[3] = mark;
                    std::vector<uint8_t> body(SECTOR);
                    for (int i = 0; i < SECTOR; i++) body[i] = mfm_byte(c, q + 16 * (1 + i));
                    uint16_t crc = (uint16_t)(mfm_byte(c, q + 16 * (1 + SECTOR)) << 8 | mfm_byte(c, q + 16 * (2 + SECTOR)));
                    uint8_t m4[4] = { 0xa1, 0xa1, 0xa1, mark };
                    if (crc16(body.data(), SECTOR, crc16(m4, 4)) == crc && idc < CYLS && idh < 2 && idr < 64) {
                        size_t k = ((size_t)idc * 2 + idh) * 64 + idr;
                        sectors[k] = body; have[k] = 1;
                        if (idr < min_r) min_r = idr;
                        if (idr > max_r) max_r = idr;
                    }
                    idc = -1;
                    p = q + 16 * (3 + SECTOR) - 1; sh = 0;
                }
            }
        }
    }
    if (max_r < 0) { err = "no readable sectors in the HFE image"; return false; }
    const int spt = max_r - min_r + 1;
    if (spt != 10 && spt != 20 && spt != 9 && spt != 18) { err = "unexpected disk layout in the HFE image (" + std::to_string(spt) + " sectors per track)"; return false; }
    raw.assign((size_t)CYLS * 2 * spt * SECTOR, 0);
    for (int cyl = 0; cyl < CYLS; cyl++)
        for (int h = 0; h < 2; h++)
            for (int r = 0; r < spt; r++) {
                size_t k = ((size_t)cyl * 2 + h) * 64 + (min_r + r);
                if (have[k]) memcpy(&raw[(((size_t)cyl * 2 + h) * spt + r) * SECTOR], sectors[k].data(), SECTOR);
            }
    img.file = f;
    img.first_id = min_r;
    return true;
}

// raw image -> the file, with every track written again (the file's header and track lengths are kept)
inline std::vector<uint8_t> encode(const std::vector<uint8_t> &raw, const Image &img) {
    using namespace detail;
    std::vector<uint8_t> f = img.file;
    const int spt = (int)(raw.size() / ((size_t)CYLS * 2 * SECTOR));
    if (f.size() < 1024 || spt <= 0) return f;
    const int tracks = f[9], sides = f[10];
    const size_t list = (size_t)rd16(&f[18]) * 512;
    for (int t = 0; t < tracks && t < CYLS; t++) {
        size_t off = (size_t)rd16(&f[list + 4 * t]) * 512, len = rd16(&f[list + 4 * t + 2]);
        if (off + len > f.size()) continue;
        for (int s = 0; s < sides && s < 2; s++)
            put_side(f, off, len, s, make_track(raw.data(), t, s, spt, img.first_id, len / 2 / 2));
    }
    return f;
}

// a new HFE image of a raw one (DD: 250 kbit/s, HD: 500 kbit/s, 300 RPM)
inline std::vector<uint8_t> create(const std::vector<uint8_t> &raw, int first_id) {
    using namespace detail;
    const int spt = (int)(raw.size() / ((size_t)CYLS * 2 * SECTOR));
    const bool hd = spt > 10;
    const size_t side_bytes = hd ? 25000 : 12500;          // MFM cells / 8 for one revolution
    const size_t track_len = side_bytes * 2, blocks = (track_len + 511) / 512;
    std::vector<uint8_t> f(1024 + (size_t)CYLS * blocks * 512, 0xff);
    memset(f.data(), 0xff, 512);
    memcpy(f.data(), "HXCPICFE", 8);
    f[8] = 0; f[9] = CYLS; f[10] = 2; f[11] = 0;
    wr16(&f[12], hd ? 500 : 250); wr16(&f[14], 300);
    f[16] = 7; f[17] = 1; wr16(&f[18], 1); f[20] = 0xff; f[21] = 0xff;
    memset(&f[512], 0, 512);
    for (int t = 0; t < CYLS; t++) {
        wr16(&f[512 + 4 * t], (uint16_t)(2 + t * blocks));
        wr16(&f[512 + 4 * t + 2], (uint16_t)track_len);
    }
    Image img; img.file = f; img.first_id = first_id;
    return encode(raw, img);
}
} // namespace hfe
