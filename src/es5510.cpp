// Ensoniq ES5510 ESP - see es5510.h for attribution.
#include "es5510.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>

#if defined(__GNUC__) || defined(__clang__)
#define ESP_INLINE __attribute__((always_inline)) inline
#else
#define ESP_INLINE inline
#endif

namespace {
constexpr uint8_t FLAG_N = 1 << 7, FLAG_C = 1 << 6, FLAG_V = 1 << 5, FLAG_LT = 1 << 4, FLAG_Z = 1 << 3, FLAG_NOT = 1 << 2;
constexpr uint8_t FLAG_MASK = FLAG_N | FLAG_C | FLAG_V | FLAG_LT | FLAG_Z;
inline uint8_t setFlagTo(uint8_t ccr, uint8_t f, bool s) { return s ? (ccr | f) : (ccr & ~f); }
inline bool isFlagSet(uint8_t ccr, uint8_t f) { return (ccr & f) != 0; }
inline int32_t sext24(int32_t v) { return (int32_t)((uint32_t)v << 8) >> 8; }
inline int64_t sext48(int64_t v) { return (int64_t)((uint64_t)v << 16) >> 16; }

inline int32_t add(int32_t a, int32_t b, uint8_t &flags) {
    // (branch-free) C = carry out of bit 23, N = sign, Z = zero, V = signed overflow, LT = N xor V
    const uint32_t r = (uint32_t)a + (uint32_t)b;
    const uint32_t c = (r >> 24) & 1, n = (r >> 23) & 1, z = (r & 0x00ffffff) == 0;
    const uint32_t v = ((~((uint32_t)a ^ (uint32_t)b) & ((uint32_t)a ^ r)) >> 23) & 1;
    flags = (uint8_t)((flags & ~(FLAG_C | FLAG_N | FLAG_Z | FLAG_V | FLAG_LT)) | (c << 6) | (n << 7) | (z << 3) | (v << 5) | ((n ^ v) << 4));
    return (int32_t)(r & 0x00ffffff);
}
inline int16_t round_24_to_16(int32_t v) { return (int16_t)(v >> 8); }
inline int32_t saturate(int32_t value, uint8_t &flags, bool negative) {
    if (isFlagSet(flags, FLAG_V)) {
        flags = setFlagTo(flags, FLAG_N, negative);
        flags &= ~FLAG_Z;
        return negative ? 0x00800000 : 0x007fffff;
    }
    return value;
}
inline int32_t negate(int32_t v) { return ((v ^ 0x00ffffff) + 1) & 0x00ffffff; }
inline int32_t asl(int32_t value, int shift, uint8_t &flags) {
    int32_t src24 = value & 0x00ffffff;
    bool carry = (src24 >> (24 - shift)) & 1;
    int64_t shifted = (int64_t)sext24(src24) << shift;
    bool overflow = (shifted > 0x007fffffLL) || (shifted < -0x00800000LL);
    int32_t result = overflow ? ((shifted < 0) ? 0x00800000 : 0x007fffff) : ((int32_t)shifted & 0x00ffffff);
    (void)carry;   // ASL2/ASL8: C unchanged (datasheet 3.3.3)
    flags = setFlagTo(flags, FLAG_V, overflow);
    flags = setFlagTo(flags, FLAG_N, (result & 0x00800000) != 0);
    flags = setFlagTo(flags, FLAG_Z, (result & 0x00ffffff) == 0);
    return result;
}
int8_t countLowOnes(int32_t x) { int8_t n = 0; while ((x & 1) == 1) { ++n; x >>= 1; } return n; }
}

const int ES5510::ALU_OPERANDS[16] = { 2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,0 };
const ES5510::OpSel ES5510::OPSEL[16] = {
    { REG, REG, REG, REG }, { REG, REG, REG, DELAY }, { REG, REG, REG, BOTH }, { REG, REG, DELAY, REG },
    { REG, REG, DELAY, BOTH }, { REG, DELAY, REG, REG }, { REG, DELAY, DELAY, REG }, { REG, BOTH, REG, REG },
    { REG, BOTH, DELAY, REG }, { DELAY, REG, REG, REG }, { DELAY, REG, REG, DELAY }, { DELAY, REG, REG, BOTH },
    { DELAY, REG, DELAY, REG }, { DELAY, REG, DELAY, BOTH }, { DELAY, BOTH, REG, REG }, { DELAY, BOTH, DELAY, REG },
};
const ES5510::RamCtl ES5510::RAMCTL[8] = {
    { READ, A_DELAY }, { WRITE, A_DELAY }, { READ, A_TABLE_A }, { WRITE, A_TABLE_A },
    { READ, A_TABLE_B }, { DUMP, A_DELAY }, { READ, A_IO }, { WRITE, A_IO },
};

ES5510::ES5510() : dram(DRAM_SIZE) { hostlog = getenv("ESP_HOSTLOG") != nullptr; reset(); }

void ES5510::reset() {
    pc = 0;
    memset(gpr, 0, sizeof(gpr));
    memset(instr, 0, sizeof(instr));
    for (int i = 0; i < 160; i++) decode(i);
    memset(ser, 0, sizeof(ser));
    std::fill(dram.begin(), dram.end(), 0);
    state = RUNNING;
    dil_latch = dol_latch = gpr_latch = 0; dadr_latch = 0;
    instr_latch = 0; ram_ctl = 0; host_control = 0x04; host_serial = 0;
    sigreg = 0; mulshift = 2;
    ram = ram_p = ram_pp = Ram{};
    alu = Alu{}; mulacc = Mac{};
    machl = 0; mac_overflow = false; dil = 0;
    memsiz = 0xffffff; memmask = 0; memincrement = 0x1000000; memshift = 24;
    dlength = abase = bbase = dbase = 0; ccr = cmr = 0; dol[0] = dol[1] = 0; dol_count = 0;
    sync_spr();
}

void ES5510::sync_spr() {
    for (int r = 234; r < 256; r++) gpr[r] = read_reg((uint8_t)r);
    gpr[DIL_SRC] = (int32_t)((uint32_t)(uint16_t)dil << 8);
}

uint8_t ES5510::host_r(int offset) {
    switch (offset) {
    case 0x00: return (gpr_latch >> 16) & 0xff;
    case 0x01: return (gpr_latch >> 8) & 0xff;
    case 0x02: return gpr_latch & 0xff;
    case 0x03: return (instr_latch >> 40) & 0xff;
    case 0x04: return (instr_latch >> 32) & 0xff;
    case 0x05: return (instr_latch >> 24) & 0xff;
    case 0x06: return (instr_latch >> 16) & 0xff;
    case 0x07: return (instr_latch >> 8) & 0xff;
    case 0x08: return instr_latch & 0xff;
    case 0x09: return (dil_latch >> 16) & 0xff;
    case 0x0a: return (dil_latch >> 8) & 0xff;
    case 0x0b: return 0;
    case 0x0c: return (dol_latch >> 16) & 0xff;
    case 0x0d: return (dol_latch >> 8) & 0xff;
    case 0x0e: return 0xff;
    case 0x0f: return (dadr_latch >> 16) & 0xff;
    case 0x10: return (dadr_latch >> 8) & 0xff;
    case 0x11: return dadr_latch & 0xff;
    case 0x12: return (host_control & ~0x04) | (read_pending >= 0 ? 0x04 : 0);   // bit 2 = transfer pending
    case 0x16:   // while halted the PC register counts through refresh cycles (datasheet 5.1.5)
        if (state == HALTED) { refresh_pc = (refresh_pc + 23) % 160; return refresh_pc; }
        return pc;
    case 0x17: return refresh_pc ^ 0x80;
    }
    return 0;
}

void ES5510::host_w(int offset, uint8_t data) {
    switch (offset) {
    case 0x00: gpr_latch = (gpr_latch & 0x00ffff) | (data << 16); break;
    case 0x01: gpr_latch = (gpr_latch & 0xff00ff) | (data << 8); break;
    case 0x02: gpr_latch = (gpr_latch & 0xffff00) | data; break;
    case 0x03: instr_latch = (instr_latch & 0x00ffffffffffULL) | ((uint64_t)data << 40); break;
    case 0x04: instr_latch = (instr_latch & 0xff00ffffffffULL) | ((uint64_t)data << 32); break;
    case 0x05: instr_latch = (instr_latch & 0xffff00ffffffULL) | ((uint64_t)data << 24); break;
    case 0x06: instr_latch = (instr_latch & 0xffffff00ffffULL) | ((uint64_t)data << 16); break;
    case 0x07: instr_latch = (instr_latch & 0xffffffff00ffULL) | ((uint64_t)data << 8); break;
    case 0x08: instr_latch = (instr_latch & 0xffffffffff00ULL) | (uint64_t)data; break;
    case 0x0c: dol_latch = (dol_latch & 0x00ffff) | (data << 16); break;
    case 0x0d: dol_latch = (dol_latch & 0xff00ff) | (data << 8); break;
    case 0x0e: dol_latch = (dol_latch & 0xffff00) | data; break;
    case 0x0f: {   // writing DADR HI starts the transfer; DADR is left-justified like the GPR offsets
        dadr_latch = (dadr_latch & 0x00ffff) | (data << 16);
        int a = (dadr_latch & memmask) >> memshift;
        if (hostlog) fprintf(stderr, "ESPHOST ram %s ctl=%02x dadr=%06x -> word %05x dol=%04x\n", (ram_ctl & 0x80) ? "rd" : "wr", ram_ctl, dadr_latch, a, (dol_latch >> 8) & 0xffff);
        if (!(ram_ctl & 0x40)) { if (ram_ctl & 0x80) dil_latch = 0; }       // I/O space: nothing attached
        else if (ram_ctl & 0x80) dil_latch = dram_r(a) << 8;
        else dram_w(a, dol_latch >> 8);
        break;
    }
    case 0x10: dadr_latch = (dadr_latch & 0xff00ff) | (data << 8); break;
    case 0x11: dadr_latch = (dadr_latch & 0xffff00) | data; break;
    case 0x12:
        host_control = (host_control & 0x4) | (data & 0x3);
        if (host_control & 2) {
            if (state == HALTED) std::fill(dram.begin(), dram.end(), 0);
            host_control &= ~2;
        }
        break;
    case 0x14: ram_ctl = data; break;
    case 0x18: host_serial = data; break;
    case 0x1f:   // Halt Enable: immediate halt when HALT is asserted (pending pipeline work is lost)
        if (halt_asserted) { state = HALTED; host_control &= ~0x04; mulacc.write_result = alu.write_result = false; ram = ram_p = ram_pp = Ram{}; dol_count = 0; }
        break;
    case 0x80:
        if (data < 0xa0) instr_latch = instr[data];
        if (data < 0xc0 && defer_gpr_reads && state == RUNNING) read_pending = data;   // served by the next frame
        else if (data < 0xc0) gpr_latch = gpr[data] & 0xffffff;
        else if (data >= 0xea) {
            // host read-back of the special registers: unimplemented bits read as 1
            // (the OS effect-download verify masks exactly these bits)
            int32_t v = read_reg(data);
            if (data >= 0xf5 && data <= 0xf7) v |= 0x0f;          // DLENGTH/ABASE/BBASE: 20 bits, left justified
            else if (data == 0xf9) v |= 0x3f0000;                  // SIGREG
            else if (data == 0xfa) v |= 0x070000;                  // CCR
            else if (data == 0xfb) v |= 0x030000;                  // CMR
            else if (data == 0xf0 || data == 0xf1) v |= 0xff;      // SER3
            gpr_latch = v & 0xffffff;
        }
        break;
    case 0xa0:
        if (hostlog) fprintf(stderr, "ESPHOST gpr %02x <- %06x\n", data, gpr_latch & 0xffffff);
        write_reg(data, gpr_latch); break;
    case 0xc0:
        if (hostlog) fprintf(stderr, "ESPHOST instr %02x <- %012llx\n", data, (unsigned long long)instr_latch);
        if (data < 0xa0) { instr[data] = instr_latch & 0xffffffffffffULL; decode(data); }
        break;
    case 0xe0:
        if (hostlog) fprintf(stderr, "ESPHOST both %02x <- %012llx / %06x\n", data, (unsigned long long)instr_latch, gpr_latch & 0xffffff);
        if (data < 0xa0) { instr[data] = instr_latch; decode(data); }
        write_reg(data, gpr_latch);
        break;
    }
}

int32_t ES5510::read_reg(uint8_t reg) {
    if (reg < 0xc0) return gpr[reg];
    switch (reg) {
    case 234: return ser[1] << 8;   // SER0R
    case 235: return ser[0] << 8;   // SER0L
    case 236: return ser[3] << 8;
    case 237: return ser[2] << 8;
    case 238: return ser[5] << 8;
    case 239: return ser[4] << 8;
    case 240: return ser[7] << 8;
    case 241: return ser[6] << 8;
    case 242: return mac_overflow ? (machl < 0 ? 0x00000000 : 0x00ffffff) : (int32_t)(machl & 0x00ffffff);
    case 243: return mac_overflow ? (machl < 0 ? 0x00800000 : 0x007fffff) : (int32_t)((machl >> 24) & 0x00ffffff);
    case 244: return dil << 8;
    case 245: return dlength;
    case 246: return abase;
    case 247: return bbase;
    case 248: return dbase;
    case 249: return sigreg;
    case 250: return ccr << 16;
    case 251: return cmr << 16;
    case 252: return 0x00ffffff;
    case 253: return 0x00800000;
    case 254: return 0x007fffff;
    case 255: return 0;
    }
    return 0;
}

void ES5510::write_reg(uint8_t reg, int32_t value) {
    value &= 0x00ffffff;
    if (reg < 0xc0) { gpr[reg] = value; return; }
    switch (reg) {
    case 234: ser[1] = (value >> 8) & 0xffff; break;
    case 235: ser[0] = (value >> 8) & 0xffff; break;
    case 236: ser[3] = (value >> 8) & 0xffff; break;
    case 237: ser[2] = (value >> 8) & 0xffff; break;
    case 238: ser[5] = (value >> 8) & 0xffff; break;
    case 239: ser[4] = (value >> 8) & 0xffff; break;
    case 240: ser[7] = (value >> 8) & 0xffff; break;
    case 241: ser[6] = (value >> 8) & 0xffff; break;
    case 242: {
        int64_t masked = machl & ((int64_t)0x00ffffff << 24);
        machl = sext48(masked | (int64_t)(value & 0x00ffffff));
        break;
    }
    case 243: {
        int64_t masked = machl & (int64_t)0x00ffffff;
        machl = sext48(masked | ((int64_t)(value & 0x00ffffff) << 24));
        mac_overflow = false;
        break;
    }
    case 244:
        memshift = countLowOnes(value);
        memsiz = 0x00ffffff >> (24 - memshift);
        memmask = 0x00ffffff & ~memsiz;
        memincrement = 1 << memshift;
        dbase &= memmask;
        break;
    case 245: dlength = value; break;
    case 246: abase = value; break;
    case 247: bbase = value; break;
    case 248: dbase = value; break;
    case 249: sigreg = value; mulshift = ((sigreg >> 22) & 1) ? 1 : 2; break;
    case 250: ccr = (value >> 16) & FLAG_MASK; break;
    case 251: cmr = (value >> 16) & (FLAG_MASK | FLAG_NOT); break;
    }
    // the copies in gpr[] of what changed
    if (reg >= 234 && reg <= 241) gpr[reg] = read_reg(reg);
    else if (reg == 242 || reg == 243) { gpr[242] = read_reg(242); gpr[243] = read_reg(243); }
    else if (reg >= 244 && reg <= 251) { gpr[248] = dbase; gpr[reg] = read_reg(reg); }
}

ESP_INLINE void ES5510::write_to_dol(int32_t value) {
    int16_t d = round_24_to_16(value);
    if (dol_count >= 2) { dol[0] = dol[1]; dol[1] = d; }
    else dol[dol_count++] = d;
}

void ES5510::alu_operation_end() {
    if (halt_asserted) { state = HALTED; host_control &= ~0x04; }
    dbase -= memincrement;
    if (dbase < 0) dbase = dlength;
    gpr[248] = dbase;
}

ESP_INLINE int32_t ES5510::alu_operation(uint8_t op, int32_t a, int32_t b, uint8_t &flags) {
    int32_t tmp;
    switch (op) {
    case 0x0: tmp = add(a, b, flags); return saturate(tmp, flags, (a & 0x00800000) != 0);
    case 0x1: tmp = add(a, negate(b), flags); return saturate(tmp, flags, (a & 0x00800000) != 0);
    case 0x2: return add(a, b, flags);
    case 0x3: return add(a, negate(b), flags);
    case 0x4: add(a, negate(b), flags); return a;
    case 0x5: a &= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;
    case 0x6: a |= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;
    case 0x7: a ^= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;
    case 0x8: {
        flags &= ~FLAG_N;
        bool neg = (b & 0x00800000) != 0;
        flags = setFlagTo(flags, FLAG_C, neg);
        return neg ? (0x00ffffff ^ b) : b;   // ABS: N=0, C=sign, others unchanged
    }
    case 0x9: return b;
    case 0xa: return asl(b, 2, flags);
    case 0xb: return asl(b, 8, flags);
    case 0xc: flags &= ~FLAG_N; flags = setFlagTo(flags, FLAG_C, (b & 0x100) != 0); return (b << 15) & 0x007fffff;   // C = bit shifted into the sign position
    case 0xd: return add(0x007fffff, negate(b), flags);
    case 0xe: flags = setFlagTo(flags, FLAG_N, (b & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_C, (b & 1) != 0); return (b >> 1) | (b & 0x00800000);
    }
    return 0;
}

ESP_INLINE void ES5510::mul_writeback() {
    if (!mulacc.write_result) return;
    mulacc.write_result = false;
    mulacc.product = ((int64_t)sext24(mulacc.cValue) * (int64_t)sext24(mulacc.dValue)) << mulshift;
    mulacc.result = mulacc.accumulate ? mulacc.product + machl : mulacc.product;
    mac_overflow = (mulacc.result < -((int64_t)1 << 47) || mulacc.result >= ((int64_t)1 << 47));
    machl = mulacc.result;
    int32_t tmp = mac_overflow ? (machl < 0 ? 0x00800000 : 0x007fffff) : (int32_t)((mulacc.result & 0x0000ffffff000000ULL) >> 24);
    if (itrace) fprintf(stderr, "  pc%02x MUL c=%06x d=%06x acc=%d -> %06x machl=%012llx\n", pc, mulacc.cValue & 0xffffff, mulacc.dValue & 0xffffff, mulacc.accumulate, tmp & 0xffffff, (unsigned long long)(machl & 0xffffffffffffULL));
    if (mulacc.dst & REG) wr(mulacc.cReg, tmp);
    if (mulacc.dst & DELAY) write_to_dol(tmp);
}

ESP_INLINE void ES5510::alu_writeback() {
    if (!alu.write_result) return;
    alu.write_result = false;
    if (alu.op == 0xf) return;      // END: no ALU result
    uint8_t flags = ccr;
    alu.result = alu_operation(alu.op, alu.aValue, alu.bValue, flags);
    if (itrace) fprintf(stderr, "  pc%02x ALU op=%x a=%06x b=%06x -> %06x ccr=%02x dil=%04x\n", pc, alu.op, alu.aValue & 0xffffff, alu.bValue & 0xffffff, alu.result & 0xffffff, flags, dil & 0xffff);
    bool wrote_ccr = false;
    if (alu.op != 4) {
        if (alu.dst & REG) { wr(alu.aReg, alu.result); wrote_ccr = alu.aReg == 0xfa; }
        if (alu.dst & DELAY) write_to_dol(alu.result);
    }
    // a MOV into CCR sets the flags explicitly (the EPS-16 Plus sampling program alternates
    // them this way): the result flags must not overwrite the value just written
    if (alu.update_ccr && !(wrote_ccr && ccr_write_wins)) ccr = flags;
}

ESP_INLINE void ES5510::ram_write_cycle(const Ram &r) {
    if (r.cycle != READ) {
        if (r.cycle == WRITE && !r.io) dram_w(r.address, dol[0]);
        dol[0] = dol[1];
        if (dol_count > 0) --dol_count;
    }
}

// Halt at END: the ESP finishes the END instruction before stopping, so the machine is left
// in a known state (datasheet 5.3.1); halting also resets the DOL FIFO pointers (3.5.3).
void ES5510::drain_pipeline() {
    mul_writeback();
    alu_writeback();
    ram_write_cycle(ram);
    ram = ram_p = ram_pp = Ram{};
    dol_count = 0;
    sync_spr();
}

void ES5510::decode(int i) {
    const uint64_t in = instr[i];
    Dec &d = dec[i];
    const OpSel &os = OPSEL[(in >> 8) & 0x0f];
    const RamCtl &rc = RAMCTL[(in >> 3) & 7];
    d.aReg = (in >> 16) & 0xff; d.bReg = (in >> 24) & 0xff; d.cReg = (in >> 32) & 0xff; d.dReg = (in >> 40) & 0xff;
    d.op = (in >> 12) & 0x0f;
    d.alu_src = os.alu_src; d.alu_dst = os.alu_dst; d.mac_src = os.mac_src; d.mac_dst = os.mac_dst;
    d.ram_cycle = (uint8_t)rc.cycle; d.ram_access = (uint8_t)rc.access;
    d.acc = ((in >> 6) & 1) != 0;
    d.skippable = (in & 0x80) != 0;
    d.one_operand = ALU_OPERANDS[d.op] == 1;
    d.c_src = d.mac_src == REG ? d.cReg : DIL_SRC;
    d.a_src = d.alu_src == REG ? d.aReg : DIL_SRC;
    d.b1_src = d.alu_src == REG ? d.bReg : DIL_SRC;
    // where the results go: a register, or nowhere (also for the registers that ignore writes:
    // the unused ones and the constants ZERO, MAX, MIN, M1)
    auto target = [](uint8_t r) -> uint16_t { return (r < 0xc0 || (r >= 234 && r <= 251)) ? r : TRASH; };
    d.mac_w = (d.mac_dst & REG) ? target(d.cReg) : TRASH;
    d.alu_w = ((d.alu_dst & REG) && d.op != 4) ? target(d.aReg) : TRASH;     // (CMP writes no result)
}

void ES5510::run_frame() {
    struct ServeRead { ES5510 *e; ~ServeRead() { if (e->read_pending >= 0) { e->gpr_latch = e->gpr[e->read_pending] & 0xffffff; e->read_pending = -1; } } } serve{ this };
    static const char *trace = getenv("ESP_TRACE");     // debug: "every,reg,reg,..." (hex GPR numbers)
    static long frames = 0;
    if (trace && state == RUNNING) {
        ++frames;
        int every = atoi(trace);
        if (every > 0 && (frames % every) == 0) {
            fprintf(stderr, "ESP f=%ld", frames);
            for (const char *p = strchr(trace, ','); p; p = strchr(p + 1, ','))
                fprintf(stderr, " %02lx=%06x", strtol(p + 1, nullptr, 16), read_reg((uint8_t)strtol(p + 1, nullptr, 16)) & 0xffffff);
            fprintf(stderr, " machl=%012llx\n", (unsigned long long)(machl & 0xffffffffffffULL));
        }
        static long itf = getenv("ESP_ITRACE") ? atol(getenv("ESP_ITRACE")) : -1;
        itrace = (itf >= 0 && frames >= itf && frames < itf + 2);
    }
    if (state == HALTED) {
        if (halt_asserted) { host_control &= ~0x04; return; }
        state = RUNNING; host_control |= 0x04; pc = 0;
    }
    // Execute until END (bounded to the 160-instruction program store). One instruction cycle:
    // the RAM read issued two cycles ago lands in DIL; the previous instruction's multiplier
    // result is written; this instruction's multiplier operands are read; the previous
    // instruction's ALU result is written (and its flags); the skip test; this instruction's
    // ALU operands are read; the RAM cycle issued last cycle is done. The pipeline (pending
    // results, RAM cycles) is kept in local variables for the whole frame, and every register
    // read is one load from gpr[] (the special registers are kept there as copies).
    Ram r0 = ram, r1 = ram_p, r2 = ram_pp;
    bool mw = mulacc.write_result, macc = mulacc.accumulate;
    int32_t mc = mulacc.cValue, md = mulacc.dValue;
    uint8_t mdst = mulacc.dst, mcreg = mulacc.cReg;
    bool aw = alu.write_result, aupd = alu.update_ccr;
    uint8_t aop = alu.op, adst = alu.dst, areg = alu.aReg;
    int32_t aa = alu.aValue, ab = alu.bValue;
    auto target = [](uint8_t r) -> uint16_t { return (r < 0xc0 || (r >= 234 && r <= 251)) ? r : TRASH; };
    uint16_t mwr = (mdst & REG) ? target(mcreg) : TRASH, awr = ((adst & REG) && aop != 4) ? target(areg) : TRASH;
    auto store = [&]() {
        ram = r0; ram_p = r1; ram_pp = r2;
        mulacc.write_result = mw; mulacc.accumulate = macc; mulacc.cValue = mc; mulacc.dValue = md; mulacc.dst = mdst; mulacc.cReg = mcreg;
        alu.write_result = aw; alu.update_ccr = aupd; alu.op = aop; alu.dst = adst; alu.aReg = areg; alu.aValue = aa; alu.bValue = ab;
    };
    auto load = [&]() {
        r0 = ram; r1 = ram_p; r2 = ram_pp;
        mw = mulacc.write_result; macc = mulacc.accumulate; mc = mulacc.cValue; md = mulacc.dValue; mdst = mulacc.dst; mcreg = mulacc.cReg;
        aw = alu.write_result; aupd = alu.update_ccr; aop = alu.op; adst = alu.dst; areg = alu.aReg; aa = alu.aValue; ab = alu.bValue;
        mwr = (mdst & REG) ? target(mcreg) : TRASH; awr = ((adst & REG) && aop != 4) ? target(areg) : TRASH;
    };
    for (int i = 0; i < 160 + 2; i++) {
        const Dec &d = dec[pc];
        const bool at_end = d.op == 0x0f;
        // the RAM pipeline: the read issued two cycles ago arrives in DIL
        r2 = r1; r1 = r0;
        if (r2.cycle != WRITE) {
            dil = r2.io ? 0 : dram_r(r2.address);
            gpr[244] = (int32_t)((uint32_t)(int32_t)dil << 8);
            gpr[DIL_SRC] = (int32_t)((uint32_t)(uint16_t)dil << 8);
        }
        r0.cycle = (RamCycle)d.ram_cycle;
        r0.io = d.ram_access == A_IO;
        const int32_t offset = gpr[pc];
        switch (d.ram_access) {           // (see step())
        case A_DELAY: {
            uint32_t len = ((uint32_t)dlength >> memshift) + 1;
            uint32_t w = ((uint32_t)dbase >> memshift) + ((uint32_t)(offset & 0x00ffffff) >> memshift);
            if (w >= len) { w -= len; if (w >= len) w %= len; }
            r0.address = (int32_t)w & (memmask >> memshift);
            break;
        }
        case A_TABLE_A: r0.address = (((abase & memmask) + offset) & memmask) >> memshift; break;
        case A_TABLE_B: r0.address = (((bbase & memmask) + offset) & memmask) >> memshift; break;
        default: r0.address = offset & 0x00fffff0; break;
        }
        // the previous instruction's multiplier result
        if (mw) {
            mw = false;
            const int64_t prod = ((int64_t)sext24(mc) * (int64_t)sext24(md)) << mulshift;
            const int64_t res = macc ? prod + machl : prod;
            mac_overflow = (res < -((int64_t)1 << 47) || res >= ((int64_t)1 << 47));
            machl = res;
            const int32_t tmp = mac_overflow ? (machl < 0 ? 0x00800000 : 0x007fffff) : (int32_t)((res & 0x0000ffffff000000ULL) >> 24);
            gpr[242] = mac_overflow ? (machl < 0 ? 0x00000000 : 0x00ffffff) : (int32_t)(machl & 0x00ffffff);
            gpr[243] = tmp;
            if (mwr < 0xc0 || mwr >= 0x100) gpr[mwr] = tmp & 0x00ffffff;   // (a GPR, or nowhere)
            else write_reg((uint8_t)mwr, tmp);
            if (mdst & DELAY) write_to_dol(tmp);
        }
        // this instruction's multiplier operands
        mcreg = d.cReg; mdst = d.mac_dst; macc = d.acc; mwr = d.mac_w;
        mc = gpr[d.c_src];
        md = gpr[d.dReg];
        // the previous instruction's ALU result
        if (aw) {
            aw = false;
            if (aop != 0xf) {
                uint8_t flags = ccr;
                const int32_t res = alu_operation(aop, aa, ab, flags);
                if (awr < 0xc0 || awr >= 0x100) gpr[awr] = res & 0x00ffffff;
                else write_reg((uint8_t)awr, res);
                if (aop != 4 && (adst & DELAY)) write_to_dol(res);
                if (aupd && !(awr == 0xfa && ccr_write_wins)) { ccr = flags; gpr[250] = ccr << 16; }
            }
        }
        // the skip test, with the CCR the previous instruction left
        bool skip = false;
        if (d.skippable) {
            bool cond = (ccr & cmr & FLAG_MASK) != 0;
            if (isFlagSet(cmr, FLAG_NOT)) cond = !cond;
            skip = cond;
        }
        mw = !skip;
        areg = d.aReg; aop = d.op; adst = d.alu_dst; awr = d.alu_w;
        aw = !skip || aop == 4;
        aupd = !d.skippable || aop == 4;
        bool ended = false;
        if (aop == 0xf) {
            alu_operation_end();
            ended = true;
        } else if (d.one_operand) {
            ab = gpr[d.b1_src];
        } else {
            aa = gpr[d.a_src];
            ab = gpr[d.bReg];
        }
        ram_write_cycle(r1);
        if (ended) {
            pc = 0;
            if (state == HALTED) { store(); drain_pipeline(); load(); }
        } else if (++pc >= 160) pc = 0;
        if (at_end || state == HALTED) break;
    }
    store();
}

// ---------------------------------------------------------------- debug listing
static const char *regname(uint8_t r, char *buf) {
    static const char *spr[] = { "SER0R","SER0L","SER1R","SER1L","SER2R","SER2L","SER3R","SER3L","MACL","MACH","DIL","DLEN","ABASE","BBASE","DBASE","SIGREG","CCR","CMR","M1","MIN","MAX","ZERO" };
    if (r < 0xc0) { snprintf(buf, 16, "g%02x", r); return buf; }
    if (r >= 234) return spr[r - 234];
    snprintf(buf, 16, "?%02x", r); return buf;
}
void ES5510::list_program(FILE *f) {
    static const char *alu[16] = { "ADD","SUB","ADDU","SUBU","CMP","AND","OR","XOR","ABS","MOV","ASL2","ASL8","LS15","DIFF","ASR","END" };
    static const char *ramc[8] = { "rdD","wrD","rdA","wrA","rdB","dumpD","rdIO","wrIO" };
    for (int pc = 0; pc < 160; pc++) {
        uint64_t in = instr[pc];
        char a[16], b[16], c[16], d[16], ad[16], cd[16];
        uint8_t op = (in >> 12) & 15, sel = (in >> 8) & 15;
        const OpSel &os = OPSEL[sel];
        fprintf(f, "%02x: %012llx gpr=%06x  %s %s%s,%s -> %s%s | MAC%s %s%s*%s -> %s%s | %s%s\n", pc, (unsigned long long)in, gpr[pc] & 0xffffff,
            alu[op], os.alu_src == DELAY ? "DIL" : "", os.alu_src == DELAY ? "" : regname((in >> 16) & 0xff, a), regname((in >> 24) & 0xff, b),
            (os.alu_dst & REG) ? regname((in >> 16) & 0xff, ad) : "", (os.alu_dst & DELAY) ? "+DOL" : "",
            ((in >> 6) & 1) ? "+" : "", os.mac_src == DELAY ? "DIL" : "", os.mac_src == DELAY ? "" : regname((in >> 32) & 0xff, c), regname((in >> 40) & 0xff, d),
            (os.mac_dst & REG) ? regname((in >> 32) & 0xff, cd) : "", (os.mac_dst & DELAY) ? "+DOL" : "",
            ramc[(in >> 3) & 7], (in & 0x80) ? " skip" : "");
        if (op == 15) break;
    }
    fprintf(f, "GPRs:");
    for (int i = 0; i < 0xc0; i++) { if (i % 16 == 0) fprintf(f, "\n  %02x:", i); fprintf(f, " %06x", gpr[i] & 0xffffff); }
    fprintf(f, "\nSER: 0L=%04x 0R=%04x 1L=%04x 1R=%04x 2L=%04x 2R=%04x 3L=%04x 3R=%04x ccr=%02x cmr=%02x machl=%012llx\n",
            ser[0] & 0xffff, ser[1] & 0xffff, ser[2] & 0xffff, ser[3] & 0xffff, ser[4] & 0xffff, ser[5] & 0xffff, ser[6] & 0xffff, ser[7] & 0xffff,
            ccr, cmr, (unsigned long long)(machl & 0xffffffffffffULL));
    {
        std::map<int16_t, int> h; for (auto v : dram) h[v]++;
        std::vector<std::pair<int, int16_t>> top; for (auto &kv : h) top.push_back({kv.second, kv.first});
        std::sort(top.rbegin(), top.rend());
        fprintf(f, "DRAM top values:"); for (size_t i = 0; i < top.size() && i < 8; i++) fprintf(f, " %04x x%d", (uint16_t)top[i].second, top[i].first);
        fprintf(f, "\n");
    }
    fprintf(f, "dlength=%06x memsiz shift=%d abase=%06x bbase=%06x dbase=%06x sigreg=%06x halted=%d host_serial=%02x\n", dlength, memshift, abase, bbase, dbase, sigreg, state == HALTED, host_serial);
}
