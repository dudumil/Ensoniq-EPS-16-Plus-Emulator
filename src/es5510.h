// Ensoniq ES5510 "ESP" effects DSP.
// Ported from MAME's es5510.cpp (BSD-3-Clause, copyright Christian Brunschen)
// into a standalone class for the ASR-10 emulator.
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
    void    ser_w(int i, int16_t v) { ser[i] = v; }

    void set_halt(bool h) { halt_asserted = h; }
    bool halted() const { return state == HALTED; }

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
private:
    int read_pending = -1;
    uint8_t pc = 0;
    uint8_t refresh_pc = 0;
    int32_t gpr[0xc0];
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
    void write_to_dol(int32_t value);
    int32_t alu_operation(uint8_t op, int32_t a, int32_t b, uint8_t &flags);
    void alu_operation_end();
    void step();
};
