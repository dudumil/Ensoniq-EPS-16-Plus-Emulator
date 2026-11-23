#include "flash_bank.h"
#include "u8file.h"
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

void FlashBank::install(bool on) { fitted = on; reset(); }

void FlashBank::reset() {
    for (auto &k : chip) k = Chip();
    all_read = true;
}

void FlashBank::update_all_read() {
    all_read = true;
    for (auto &k : chip) if (k.mode == IDENT || k.mode == ERASE_VERIFY || k.mode == PROG_VERIFY) all_read = false;
}

uint8_t FlashBank::read8(uint32_t o) const {
    o &= SIZE - 1;
    const int c = chip_of(o);
    const Chip &k = chip[c];
    switch (k.mode) {
    case IDENT: return (addr_of(o) & 1) ? 0xbd : 0x89;           // device / manufacturer
    case ERASE_VERIFY:                                           // the byte whose address was latched
    case PROG_VERIFY: return cell(c, k.latch);
    default: return mem[o];
    }
}

// a write to the command register (or, after 40h, the byte to program)
void FlashBank::command(Chip &k, uint8_t v, uint32_t ca) {
    switch (v) {
    case 0x00: k.mode = READ; break;
    case 0x80: case 0x90: k.mode = IDENT; break;
    case 0x20: k.mode = ERASE_SETUP; break;
    case 0xa0: k.mode = ERASE_VERIFY; k.latch = ca; break;
    case 0x40: k.mode = PROG_SETUP; break;
    case 0xc0: k.mode = PROG_VERIFY; break;                      // (the address latched by the program write)
    case 0xff: k.mode = k.mode == RESET1 ? READ : RESET1; break;
    default: k.mode = READ; break;
    }
}

void FlashBank::write8(uint32_t o, uint8_t v) {
    activity++;
    o &= SIZE - 1;
    const int c = chip_of(o);
    Chip &k = chip[c];
    const uint32_t ca = addr_of(o);
    if (k.mode == PROG_SETUP) {                                  // program: bits can only go from 1 to 0
        uint8_t &b = cell(c, ca);
        if ((b & v) != b) { b &= v; dirty = true; changes++; }
        k.latch = ca;
        k.mode = PROGRAMMING;
    } else if (k.mode == ERASE_SETUP) {
        if (v == 0x20) {                                         // erase: the whole chip to FFh
            for (uint32_t i = 0; i < CHIP; i++) {
                uint8_t &b = cell(c, i);
                if (b != 0xff) { b = 0xff; dirty = true; }
            }
            changes++;
            k.mode = ERASING;
        } else command(k, v, ca);                                // (an aborted erase)
    } else command(k, v, ca);
    update_all_read();
}

std::string FlashBank::file_for(const std::string &app_dir, const std::string &user_dir, bool create) {
    std::error_code ec;
    const fs::path old = user_dir.empty() ? fs::path() : fs::u8path(user_dir) / "fb2.bin";
    if (!app_dir.empty()) {
        const fs::path dir = fs::u8path(app_dir) / "flashbank", f = dir / "fb2.bin";
        if (fs::exists(f, ec)) return f.u8string();
        if (create) {
            fs::create_directories(dir, ec);
            // (a folder that cannot be written, e.g. under Program Files, is not used)
            const fs::path probe = dir / ".write-test";
            FILE *t = fs::is_directory(dir, ec) ? u8fopen(probe.u8string().c_str(), "wb") : nullptr;
            if (t) {
                fclose(t); fs::remove(probe, ec);
                if (!old.empty() && fs::exists(old, ec)) {          // the file from the old place moves here
                    fs::rename(old, f, ec);
                    if (ec) { ec.clear(); fs::copy_file(old, f, ec); }
                }
                return f.u8string();
            }
        }
    }
    if (!old.empty() && (create || fs::exists(old, ec))) return old.u8string();
    return std::string();
}

bool FlashBank::load(const std::string &p) {
    FILE *f = u8fopen(p.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d(SIZE, 0xff);
    size_t n = fread(d.data(), 1, SIZE, f);
    fclose(f);
    if (n == 0) return false;
    mem.swap(d);
    dirty = false;
    return true;
}

bool FlashBank::save(const std::string &p) {
    if (!write_file(p, mem)) return false;
    dirty = false;
    return true;
}

// written to a temporary file first: a crash while saving leaves the old contents
bool FlashBank::write_file(const std::string &p, const std::vector<uint8_t> &data) {
    std::string tmp = p + ".tmp";
    FILE *f = u8fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fclose(f) == 0) && ok;
    std::error_code ec;
    if (!ok) { fs::remove(fs::u8path(tmp), ec); return false; }
    fs::rename(fs::u8path(tmp), fs::u8path(p), ec);             // (replaces the old file)
    return !ec;
}
