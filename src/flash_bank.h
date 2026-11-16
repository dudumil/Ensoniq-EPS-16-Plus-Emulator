// Ensoniq FB-2 Flash Bank for the EPS-16 Plus (PCB 4010013401 "Flash Expander EPS-16+").
//
// Four Intel 28F020 (256K x 8) flash chips on the sound memory expansion connector, i.e. OTIS
// bank 0 = CPU $400000-$4FFFFF (mirrored at $500000): 512K 16-bit words = 1 MB.
//   - the address goes to the chips through the board's row/column latches (MA0-8 on RAS and
//     CAS0 -> A0-A17), MA9 (jumper JMP2) picks the half: CPU $400000-$47FFFF = the "FLO" pair,
//     $480000-$4FFFFF = the "FHI" pair;
//   - in each pair one chip holds the even (upper, D15-D8) bytes and one the odd bytes, written
//     separately (UCWE / LCWE);
//   - 12 V Vpp comes from a 7812 on the board: the chips can always be erased and programmed.
// The OS (boot ROM) finds the board by its Intelligent Identifier, formats it (all bytes
// programmed to 00, then the Quick-Erase algorithm), and writes the OS and instruments one byte
// at a time with the Quick-Pulse algorithm. Instruments in the flash are played from it
// directly: loading one only sets up pointers, which is why it is instantaneous. Files cannot be
// deleted: the only way to free the flash is to erase it all (format).
//
// The chips' command register (datasheet, table 3): 00h read, 90h (and 80h, which the boot ROM
// uses) Intelligent Identifier (89h, BDh), 20h 20h erase, A0h erase verify, 40h + data program,
// C0h program verify, FFh FFh reset. Erase and programming are done at once: the ROM's own delay
// loops give them the time they take on the real chips.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class FlashBank {
public:
    static const uint32_t SIZE = 0x100000;          // bytes
    static const uint32_t CHIP = 0x40000;           // bytes per 28F020

    void install(bool on);                          // board fitted (contents are kept)
    bool installed() const { return fitted; }
    void reset();                                   // power-up: every chip in read mode

    // CPU access, o = byte offset in the 1 MB window ($400000 = 0; even = upper byte)
    uint8_t read8(uint32_t o) const;
    void write8(uint32_t o, uint8_t v);
    uint16_t read16(uint32_t o) const {
        if (all_read) { o &= SIZE - 2; return (uint16_t)(mem[o] << 8 | mem[o + 1]); }
        return (uint16_t)(read8(o & ~1u) << 8 | read8(o | 1));
    }
    // the OTIS (bank 0), w = word address
    uint16_t sample(uint32_t w) const { return read16(w << 1); }

    // contents (a file of 1 MB, in CPU byte order)
    std::vector<uint8_t> mem = std::vector<uint8_t>(SIZE, 0xff);
    bool dirty = false;                             // changed since it was loaded / saved
    uint32_t changes = 0;                           // counts the writes that changed it
    bool load(const std::string &path);             // a missing file = a new (erased) board
    bool save(const std::string &path);
    static bool write_file(const std::string &path, const std::vector<uint8_t> &data);

private:
    enum Mode : uint8_t { READ, IDENT, ERASE_SETUP, ERASING, ERASE_VERIFY, PROG_SETUP, PROGRAMMING, PROG_VERIFY, RESET1 };
    struct Chip { Mode mode = READ; uint32_t latch = 0; };
    Chip chip[4];                                   // [pair * 2 + (odd byte)]
    bool fitted = false;
    bool all_read = true;                           // every chip returns array data
    static int chip_of(uint32_t o) { return (int)(((o >> 19) & 1) * 2 + (o & 1)); }
    static uint32_t addr_of(uint32_t o) { return (o >> 1) & (CHIP - 1); }
    uint8_t &cell(int c, uint32_t ca) { return mem[((uint32_t)(c >> 1) << 19) | (ca << 1) | (uint32_t)(c & 1)]; }
    uint8_t cell(int c, uint32_t ca) const { return mem[((uint32_t)(c >> 1) << 19) | (ca << 1) | (uint32_t)(c & 1)]; }
    void command(Chip &k, uint8_t v, uint32_t ca);
    void update_all_read();
};
