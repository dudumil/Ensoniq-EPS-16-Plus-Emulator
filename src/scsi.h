// SCSI for the ASR-10 (SP-3 option): WD33C93 bus interface controller plus image-backed
// targets (hard disk / CD-ROM), with ZuluSCSI-style image naming:
//   HD<id>[<lun>][_<blocksize>].<ext>   hard disk (writable)
//   RE<id>... / MO<id>...               removable / magneto-optical (writable, removable)
//   CD<id>[...].iso                      CD-ROM (read only, 2048-byte native blocks)
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <functional>

class ScsiDevice {
public:
    enum Type { HD, CD, REMOVABLE };
    static std::unique_ptr<ScsiDevice> open(const std::string &path, Type type, int block_size, bool readonly);
    ~ScsiDevice();

    // Execute a command. For data-out commands, call first with data_out == nullptr to learn
    // the expected length (returned in out_len), then again with the data.
    // Returns the SCSI status byte; data_in receives the bytes for the data-in phase.
    uint8_t execute(const uint8_t *cdb, int cdb_len, const std::vector<uint8_t> *data_out,
                    std::vector<uint8_t> &data_in, uint32_t &out_len);

    Type type = HD;
    std::string path;
    bool readonly = false;
    uint32_t block_size = 512;      // current logical block size (MODE SELECT can change it)
    uint64_t bytes = 0;
    std::string describe() const;

private:
    FILE *f = nullptr;
    uint8_t sense_key = 0, asc = 0, ascq = 0;
    uint8_t check(uint8_t key, uint8_t a, uint8_t q = 0) { sense_key = key; asc = a; ascq = q; return 0x02; }
    bool read_blocks(uint64_t lba, uint32_t n, std::vector<uint8_t> &out);
    bool write_blocks(uint64_t lba, uint32_t n, const uint8_t *data);
    uint64_t blocks() const { return bytes / block_size; }
};

class Wd33c93 {
public:
    void reset();
    // host interface ($FC5001 = address register / auxiliary status, $FC5003 = data)
    uint8_t read(int a);
    void    write(int a, uint8_t v);
    // DMA port ($FC5801, shared with the floppy controller)
    bool    dreq() const;
    bool    dma_active() const { return phase == DATA_IN || phase == DATA_OUT; }
    uint8_t dma_read();
    void    dma_write(uint8_t v);

    std::unique_ptr<ScsiDevice> dev[8];
    std::function<void(bool)> irq_cb;   // INTRQ (aux status INT)
    bool irq() const { return aux & AUX_INT; }
    bool log = false;

    // attach images found in a directory using ZuluSCSI names; returns a report
    std::string attach_dir(const std::string &dir, int host_id = 3);
    // attach one file named like ZuluSCSI (HD3.img, CD5.iso...); returns "" if the name doesn't match
    std::string attach_file(const std::string &path, int host_id = 3);
    std::string summary() const;        // one line per attached device
    // bus activity counter (commands and data bytes), read by the front end for the drive LED
    std::atomic<uint32_t> activity{0};
    // data bytes written to a drive: the LED blinks faster while writing than while reading
    std::atomic<uint32_t> writes{0};
    bool force_readonly = false;
    bool attach(int id, const std::string &path, ScsiDevice::Type type, int block_size = 0);
    void raise(uint8_t code);           // queue an interrupt (status code)

private:
    enum { AUX_INT = 0x80, AUX_LCI = 0x40, AUX_BSY = 0x20, AUX_CIP = 0x10, AUX_DBR = 0x01 };
    enum Phase { IDLE, DATA_IN, DATA_OUT } phase = IDLE;
    uint8_t regs[32] = {};
    uint8_t addr = 0, aux = 0;
    std::deque<uint8_t> irqs;           // pending interrupt status codes
    std::vector<uint8_t> buf;           // data phase buffer
    size_t pos = 0;
    uint32_t xfer_left = 0;             // transfer count register value
    int target = -1;
    uint8_t cdb[16] = {};
    int cdb_len = 6;

    void command(uint8_t c);
    void select_and_transfer();
    void finish(uint8_t status_byte);
    void next_irq();
    void update_dbr();
    void irq_update() { bool s = (aux & AUX_INT) != 0; if (s != irq_state) { irq_state = s; if (irq_cb) irq_cb(s); } }
    bool irq_state = false;
    bool dma_mode() const { return (regs[0x01] & 0xe0) != 0; }
    void set_count(uint32_t n) { xfer_left = n; regs[0x12] = n >> 16; regs[0x13] = n >> 8; regs[0x14] = n; }
    void data_out_done();
};
