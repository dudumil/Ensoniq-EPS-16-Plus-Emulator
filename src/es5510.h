// Ensoniq ES5510 "ESP" effects DSP.
// Ported from MAME's es5510.cpp (BSD-3-Clause, copyright Christian Brunschen)
// into a standalone class for the ASR-10 emulator (also used by the EPS-16 Plus and DP/4 emulators).
#pragma once
#include <cstdint>
#include <vector>
#include <cstdio>

class ES5510 {
public:
    static constexpr uint32_t DRAM_SIZE = 1 << 20;   // 16-bit words (ASR-10 has 256KB = 128K words, wraps)
    ES5510();
    void reset();

    uint8_t host_r(int offset);
    void    host_w(int offset, uint8_t data);

    // serial ports: index 0..7 = 0L,0R,1L,1R,2L,2R,3L,3R
    int16_t ser_r(int i) const { return ser[i]; }
    void    ser_w(int i, int16_t v) { ser[i] = v; gpr[234 + (i ^ 1)] = (int32_t)((uint32_t)(int32_t)v << 8); }

    void set_halt(bool h) { halt_asserted = h; }
    bool halted() const { return state == HALTED; }
    uint8_t serial_ctl() const { return host_serial; }   // bits 2-5: SER0-3 are outputs

    // Execute one complete sample period (run the program from PC 0 to END).
    void run_frame();
private:
    void mul_writeback();
    void alu_writeback();
    void drain_pipeline();
public:
    uint32_t dram_mask = DRAM_SIZE - 1;   // delay RAM addressing as in MAME
    bool log = false;
    void list_program(FILE *f);

private:
    enum { RUNNING, HALTED } state = HALTED;
    enum Src { REG = 1, DELAY = 2, BOTH = 3 };
    struct OpSel { uint8_t alu_src, alu_dst, mac_src, mac_dst; };
    enum RamCycle { READ = 0, WRITE = 1, DUMP = 2 };
    enum RamAccess { A_DELAY = 0, A_TABLE_A, A_TABLE_B, A_IO };
    struct RamCtl { RamCycle cycle; RamAccess access; };
    static const OpSel OPSEL[16];
    static const RamCtl RAMCTL[8];
    static const int ALU_OPERANDS[16];

    struct Alu { uint8_t aReg, bReg, src, dst, op; int32_t aValue, bValue, result; bool update_ccr, write_result; } alu{};
    struct Mac { uint8_t cReg, dReg, src, dst; bool accumulate; int32_t cValue, dValue; int64_t product, result; bool write_result; } mulacc{};
    struct Ram { int32_t address; bool io; RamCycle cycle; } ram{}, ram_p{}, ram_pp{};
    void ram_write_cycle(const Ram &r);

    bool halt_asserted = false;
public:
    // EPS-16 Plus: a host GPR read is served at the end of the next ESP frame (the sampling
    // loop polls a flag that the ESP program raises every other frame)
    bool defer_gpr_reads = false;
    // a MOV into CCR keeps the value written (the flags of that MOV are not stored over it): the
    // EPS-16 Plus sampling program needs this; the ASR-10 emulator keeps the older behaviour
    bool ccr_write_wins = true;
private:
    int read_pending = -1;
    uint8_t pc = 0;
    uint8_t refresh_pc = 0;
    // the register file as the program reads it: 0x00-0xBF the GPRs; 0xC0-0xFF the special
    // registers, kept up to date as copies of the state they show (so that reading any register
    // is one load); 0x100 = DIL as an operand source
    int32_t gpr[0x104];
    static constexpr int DIL_SRC = 0x100, TRASH = 0x101;   // TRASH: results that go to no register
    void sync_spr();
    int16_t ser[8];     // 0L 0R 1L 1R 2L 2R 3L 3R
    int64_t machl = 0;
    bool mac_overflow = false;
    int16_t dil = 0;
    int32_t memsiz = 0xffffff, memmask = 0, memincrement = 0x1000000;
    int8_t memshift = 24;
    int32_t dlength = 0, abase = 0, bbase = 0, dbase = 0, sigreg = 0;
    int mulshift = 2;
    uint8_t ccr = 0, cmr = 0;
    int16_t dol[2] = {0, 0};
    int dol_count = 0;
    uint64_t instr[160];
    // the instructions decoded once (when written), for step()
    struct Dec { uint8_t aReg, bReg, cReg, dReg, op, alu_src, alu_dst, mac_src, mac_dst, ram_cycle, ram_access, acc, skippable, one_operand; uint16_t c_src, a_src, b1_src, mac_w, alu_w;
                 // for run_frame(): the RAM cycle is an I/O one; the result goes to a special register;
                 // the ALU updates the CCR (upd[ccr_write_wins]: not when it is a MOV into the CCR that wins)
                 uint8_t ram_io, mac_spec, alu_spec, aupd, upd[2]; };
    Dec dec[160];
    void decode(int i);
    void finish_dec(Dec &d);
    bool itrace = false, hostlog = false;   // debug (ESP_ITRACE / ESP_HOSTLOG environment variables)
    std::vector<int16_t> dram;

    int32_t dol_latch = 0, dil_latch = 0, gpr_latch = 0;
    uint32_t dadr_latch = 0;
    uint64_t instr_latch = 0;
    uint8_t ram_ctl = 0, host_control = 0x04, host_serial = 0;

    int16_t dram_r(int a) const { return dram[a & dram_mask]; }
    void dram_w(int a, int16_t v) { dram[a & dram_mask] = v; }
    int32_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, int32_t value);
    // the fast path: general registers inline, special registers through read_reg / write_reg
    int32_t rd(uint8_t reg) { return reg < 0xc0 ? gpr[reg] : read_reg(reg); }
    void wr(uint8_t reg, int32_t v) { if (reg < 0xc0) gpr[reg] = v & 0x00ffffff; else write_reg(reg, v); }
    void write_to_dol(int32_t value);
    int32_t alu_operation(uint8_t op, int32_t a, int32_t b, uint8_t &flags);
    void alu_operation_end();
};
