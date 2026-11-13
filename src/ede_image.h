#pragma once
// Giebler disk images of Ensoniq floppies: .EDE (800 KB DD disk: EPS, EPS-16 Plus, ASR-10) and .EDA
// (1600 KB HD disk: ASR-10). A 512-byte header - a text line, a label, and a bit map of the blocks that
// were left out because they are unused - is followed by the stored 512-byte blocks:
//   0x000  0D 0A, description ("EPS-16 Disk", "ASR-10 Disk"...) padded with spaces, 0D 0A at 0x4E
//   0x050  label, 0D 0A 1A
//   0x0A0  .EDE: skip map of 1600 blocks (200 bytes)      0x060  .EDA: skip map of 3200 blocks (400 bytes)
//          bit 7 of the first byte is block 0; a set bit = block not stored (unformatted: 6D B6 6D B6...)
//   0x1FF  type (03 = EDE, CB = EDA; not always filled in)
// The floppy controllers work on the raw image: decode() on load, encode() on save.
#include <cstdint>
#include <cstring>
#include <vector>

namespace ede {
constexpr size_t BLOCK = 512;

inline bool looks_like(const std::vector<uint8_t> &d) {
    return d.size() > BLOCK && d[0] == 0x0d && d[1] == 0x0a && d[0x4e] == 0x0d && d[0x4f] == 0x0a && d[0x31] != 0x1a;
}

// file -> raw image (819200 or 1638400 bytes) and the header to write back; false if not such an image
inline bool decode(const std::vector<uint8_t> &file, std::vector<uint8_t> &raw, std::vector<uint8_t> &header) {
    if (!looks_like(file)) return false;
    struct Layout { size_t blocks, map; uint8_t type; };
    const Layout layouts[2] = { { 1600, 0xa0, 0x03 }, { 3200, 0x60, 0xcb } };
    const size_t stored_bytes = file.size() - BLOCK;
    int pick = -1;
    for (int i = 0; i < 2 && pick < 0; i++) {
        const Layout &l = layouts[i];
        size_t skipped = 0;
        for (size_t b = 0; b < l.blocks; b++) if (file[l.map + b / 8] & (0x80 >> (b % 8))) skipped++;
        size_t need = (l.blocks - skipped) * BLOCK;
        // exact, or with a few trailing bytes (a final 1A is common)
        if (stored_bytes >= need && stored_bytes - need < BLOCK) {
            if (file[0x1ff] == layouts[1 - i].type) continue;      // the type byte says the other format
            pick = i;
        }
    }
    if (pick < 0) return false;
    const Layout &l = layouts[pick];
    raw.assign(l.blocks * BLOCK, 0);
    size_t src = BLOCK;
    for (size_t b = 0; b < l.blocks; b++) {
        uint8_t *dst = &raw[b * BLOCK];
        if (file[l.map + b / 8] & (0x80 >> (b % 8))) { for (size_t i = 0; i < BLOCK; i += 2) { dst[i] = 0x6d; dst[i + 1] = 0xb6; } }
        else { memcpy(dst, &file[src], BLOCK); src += BLOCK; }
    }
    header.assign(file.begin(), file.begin() + BLOCK);
    return true;
}

// raw image -> file, keeping the header's text (unformatted blocks are left out again); empty if the
// image is not 1600 or 3200 blocks
inline std::vector<uint8_t> encode(const std::vector<uint8_t> &raw, const std::vector<uint8_t> &header) {
    std::vector<uint8_t> out;
    const size_t blocks = raw.size() / BLOCK;
    if (raw.size() % BLOCK || (blocks != 1600 && blocks != 3200) || header.size() != BLOCK) return out;
    const bool hd = blocks == 3200;
    const size_t map = hd ? 0x60 : 0xa0;
    out.assign(header.begin(), header.end());
    memset(&out[0x60], 0, 0x1ff - 0x60);                         // both map areas
    if (!hd) { memset(&out[0x60], ' ', 0x3d); out[0x9d] = 0x0d; out[0x9e] = 0x0a; out[0x9f] = 0x1a; }   // rest of the DD label line
    if (out[0x1ff] == 0x03 || out[0x1ff] == 0xcb) out[0x1ff] = hd ? 0xcb : 0x03;   // type, if it was filled in
    out.reserve(BLOCK + raw.size() + 1);
    for (size_t b = 0; b < blocks; b++) {
        const uint8_t *p = &raw[b * BLOCK];
        bool blank = true;
        for (size_t i = 0; i < BLOCK && blank; i += 2) blank = p[i] == 0x6d && p[i + 1] == 0xb6;
        if (blank) out[map + b / 8] |= (uint8_t)(0x80 >> (b % 8));
        else out.insert(out.end(), p, p + BLOCK);
    }
    out.push_back(0x1a);
    return out;
}
}
