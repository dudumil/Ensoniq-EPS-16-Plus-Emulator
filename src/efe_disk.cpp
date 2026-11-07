#include "efe_disk.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace efe {

namespace {
constexpr int BLOCK = 512;
constexpr int DIR_ENTRIES = 39;            // two directory blocks of 26-byte entries
constexpr int FAT_PER_BLOCK = 170;         // 3-byte entries

bool read_file(const std::string &p, std::vector<uint8_t> &d) {
    std::ifstream f(std::filesystem::u8path(p), std::ios::binary);
    if (!f) return false;
    d.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool header_ok(const std::vector<uint8_t> &d) {
    return d.size() > BLOCK && d[0] == 0x0d && d[1] == 0x0a && d[0x31] == 0x1a && d[0x32] != 0;
}
void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void put32(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
}

bool is_single_file(const std::string &path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> h(BLOCK + 1);
    f.read((char *)h.data(), h.size());
    if (f.gcount() < (std::streamsize)h.size()) return false;
    return header_ok(h);
}

std::string make_disk(const std::vector<std::string> &files, bool allow_hd, std::vector<uint8_t> &img, std::string &label) {
    struct F { std::vector<uint8_t> data; int blocks; std::string name; };
    std::vector<F> fs;
    int total = 0;
    for (auto &p : files) {
        F f;
        if (!read_file(p, f.data)) return "Cannot open " + p;
        if (!header_ok(f.data)) return "Not an Ensoniq file (EFE/EFA): " + std::filesystem::u8path(p).filename().u8string();
        f.blocks = (int)((f.data.size() - BLOCK + BLOCK - 1) / BLOCK);
        f.name.assign((const char *)&f.data[0x12], 12);
        total += f.blocks;
        fs.push_back(std::move(f));
    }
    if (fs.empty()) return "No files";
    if ((int)fs.size() > DIR_ENTRIES) return "Too many files for one disk (39 at most)";

    // 800 KB double density: 1600 blocks, FAT in blocks 5-14; 1.6 MB high density: 3200 blocks, FAT 5-23
    int nblocks = 1600, nfat = 10;
    if (total > nblocks - 15) {
        if (!allow_hd || total > 3200 - 24) return "Too big for a floppy disk (" + std::to_string(total) + " blocks)";
        nblocks = 3200; nfat = 19;
    }
    const int first_data = 5 + nfat;
    img.assign((size_t)nblocks * BLOCK, 0);
    uint8_t *d = img.data();

    for (int i = 0; i < BLOCK; i += 2) { d[i] = 0x6d; d[i + 1] = 0xb6; }      // block 0: format pattern

    label = fs[0].name;
    while (!label.empty() && label.back() == ' ') label.pop_back();

    uint8_t *id = d + BLOCK;                 // block 1: device ID
    id[0x01] = 0x80; id[0x02] = 0x01;
    put16(id + 0x04, nblocks == 1600 ? 10 : 20);   // sectors per track
    put16(id + 0x06, 2);                            // heads
    put16(id + 0x08, 80);                           // tracks
    put32(id + 0x0a, BLOCK);
    put32(id + 0x0e, nblocks);
    id[0x12] = 0x1e; id[0x13] = 0x02;
    id[0x1e] = 0xff;
    for (int i = 0; i < 7; i++) id[0x1f + i] = i < (int)label.size() ? (uint8_t)label[i] : ' ';
    id[0x26] = 'I'; id[0x27] = 'D';

    uint8_t *os = d + 2 * BLOCK;             // block 2: OS block (no OS on this disk)
    os[0x1c] = 'O'; os[0x1d] = 'S';

    d[4 * BLOCK + 0x1fe] = 'D'; d[4 * BLOCK + 0x1ff] = 'R';
    auto fat_set = [&](int b, unsigned v) {
        uint8_t *p = d + (5 + b / FAT_PER_BLOCK) * BLOCK + (b % FAT_PER_BLOCK) * 3;
        p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v;
    };
    for (int k = 0; k < nfat; k++) { d[(5 + k) * BLOCK + 0x1fe] = 'F'; d[(5 + k) * BLOCK + 0x1ff] = 'B'; }
    for (int b = 0; b < first_data; b++) fat_set(b, 1);     // system blocks

    int next = first_data;
    for (size_t i = 0; i < fs.size(); i++) {
        const F &f = fs[i];
        for (int k = 0; k < f.blocks; k++) {
            size_t src = BLOCK + (size_t)k * BLOCK;
            size_t n = std::min<size_t>(BLOCK, f.data.size() - src);
            memcpy(d + (size_t)(next + k) * BLOCK, &f.data[src], n);
            fat_set(next + k, k + 1 < f.blocks ? (unsigned)(next + k + 1) : 1u);   // chain; 1 = last block
        }
        uint8_t *e = d + 3 * BLOCK + i * 26;   // directory entry
        e[0] = 0; e[1] = f.data[0x32];
        memcpy(e + 2, &f.data[0x12], 12);
        put16(e + 14, f.blocks); put16(e + 16, f.blocks);
        put32(e + 18, next);
        next += f.blocks;
    }
    put32(os, nblocks - next);                 // free blocks
    return "";
}

std::string make_disk_file(const std::vector<std::string> &files, bool allow_hd, const std::string &prefix, std::string &path, std::string &label) {
    std::vector<uint8_t> img;
    std::string err = make_disk(files, allow_hd, img, label);
    if (!err.empty()) return err;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = ".";
    std::string base;
    for (char c : label) base += (isalnum((unsigned char)c) || c == '-') ? c : '_';
    if (base.empty()) base = "disk";
    std::filesystem::path p = dir / (prefix + base + ".img");
    std::ofstream o(p, std::ios::binary);
    if (!o) return "Cannot write " + p.u8string();
    o.write((const char *)img.data(), img.size());
    if (!o) return "Cannot write " + p.u8string();
    path = p.u8string();
    return "";
}

}
