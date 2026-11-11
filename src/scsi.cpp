#include "u8file.h"
#include "scsi.h"
#include <cstring>
#include <cctype>
#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

// ======================================================================= devices

std::unique_ptr<ScsiDevice> ScsiDevice::open(const std::string &path, Type type, int bs, bool ro) {
    FILE *f = nullptr;
    if (!ro) f = u8fopen(path.c_str(), "r+b");
    if (!f) { f = u8fopen(path.c_str(), "rb"); ro = true; }
    if (!f) return nullptr;
    std::unique_ptr<ScsiDevice> d(new ScsiDevice);
    d->f = f;
    d->path = path;
    d->type = type;
    d->readonly = ro || type == CD;
    d->block_size = bs > 0 ? bs : (type == CD ? 2048 : 512);
    fseek64(f, 0, SEEK_END);
    d->bytes = (uint64_t)ftell64(f);
    return d;
}

ScsiDevice::~ScsiDevice() { if (f) fclose(f); }

std::string ScsiDevice::describe() const {
    const char *t = type == CD ? "CD-ROM" : type == REMOVABLE ? "removable" : "disk";
    char b[512];
    snprintf(b, sizeof b, "%s %s (%.1f MB, %u-byte blocks%s)", t, path.c_str(), bytes / 1048576.0, block_size,
             readonly ? ", read-only" : "");
    return b;
}

bool ScsiDevice::read_blocks(uint64_t lba, uint32_t n, std::vector<uint8_t> &out) {
    uint64_t off = lba * block_size, len = (uint64_t)n * block_size;
    if (off + len > bytes) return false;
    out.resize((size_t)len);
    fseek64(f, (int64_t)off, SEEK_SET);
    return fread(out.data(), 1, (size_t)len, f) == len;
}

bool ScsiDevice::write_blocks(uint64_t lba, uint32_t n, const uint8_t *data) {
    uint64_t off = lba * block_size, len = (uint64_t)n * block_size;
    if (off + len > bytes) return false;
    fseek64(f, (int64_t)off, SEEK_SET);
    bool ok = fwrite(data, 1, (size_t)len, f) == len;
    fflush(f);
    return ok;
}

static void put24(uint8_t *p, uint32_t v) { p[0] = v >> 16; p[1] = v >> 8; p[2] = v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

uint8_t ScsiDevice::execute(const uint8_t *c, int, const std::vector<uint8_t> *out,
                            std::vector<uint8_t> &in, uint32_t &out_len) {
    in.clear();
    out_len = 0;
    const uint8_t op = c[0];
    if (op != 0x03) { sense_key = 0; asc = 0; ascq = 0; }
    auto alloc = [&](size_t n) { if (in.size() > n) in.resize(n); };

    switch (op) {
    case 0x00:              // TEST UNIT READY
    case 0x01:              // REZERO UNIT
    case 0x0b: case 0x2b:   // SEEK
    case 0x16: case 0x17:   // RESERVE / RELEASE
    case 0x1b:              // START STOP UNIT
    case 0x1d:              // SEND DIAGNOSTIC
    case 0x1e:              // PREVENT ALLOW MEDIUM REMOVAL
    case 0x2f:              // VERIFY
    case 0x35:              // SYNCHRONIZE CACHE
        return 0;

    case 0x03: {            // REQUEST SENSE
        in.assign(18, 0);
        in[0] = 0x70; in[2] = sense_key; in[7] = 10; in[12] = asc; in[13] = ascq;
        alloc(c[4] ? c[4] : 4);
        sense_key = asc = ascq = 0;
        return 0;
    }
    case 0x04:              // FORMAT UNIT (the image is the medium: nothing to do)
        if (readonly) return check(0x07, 0x27);
        return 0;

    case 0x12: {            // INQUIRY
        in.assign(36, ' ');
        in[0] = type == CD ? 0x05 : 0x00;
        in[1] = type == HD ? 0x00 : 0x80;            // removable medium
        in[2] = 0x02; in[3] = 0x02; in[4] = 31; in[5] = in[6] = in[7] = 0;
        const char *vendor = "ASR10EMU", *prod = type == CD ? "CD-ROM          " : "HARD DISK       ";
        memcpy(&in[8], vendor, 8); memcpy(&in[16], prod, 16); memcpy(&in[32], "1.0 ", 4);
        alloc(c[4]);
        return 0;
    }
    case 0x25: {            // READ CAPACITY
        in.assign(8, 0);
        uint64_t n = blocks();
        put32(&in[0], n ? (uint32_t)std::min<uint64_t>(n - 1, 0xffffffffu) : 0);
        put32(&in[4], block_size);
        return 0;
    }
    case 0x1a: {            // MODE SENSE(6)
        int page = c[2] & 0x3f;
        std::vector<uint8_t> p;
        auto add_page = [&](int code) {
            std::vector<uint8_t> pg;
            if (code == 0x01) { pg.assign(12, 0); pg[0] = 0x01; pg[1] = 10; }
            else if (code == 0x03) {               // format device
                pg.assign(24, 0); pg[0] = 0x03; pg[1] = 22;
                pg[10] = 0; pg[11] = 63;            // sectors per track
                pg[12] = block_size >> 8; pg[13] = block_size;
            } else if (code == 0x04) {             // rigid disk geometry
                pg.assign(24, 0); pg[0] = 0x04; pg[1] = 22;
                uint32_t cyl = (uint32_t)(blocks() / (16 * 63) + 1);
                put24(&pg[2], cyl); pg[5] = 16;
            } else return;
            p.insert(p.end(), pg.begin(), pg.end());
        };
        if (page == 0x3f) { add_page(0x01); add_page(0x03); add_page(0x04); }
        else if (page == 0x01 || page == 0x03 || page == 0x04) add_page(page);
        else if (page != 0) return check(0x05, 0x24);
        in.assign(4 + 8, 0);
        in[1] = 0;
        in[2] = readonly ? 0x80 : 0x00;          // device-specific: write protect
        in[3] = 8;
        put24(&in[5], (uint32_t)std::min<uint64_t>(blocks(), 0xffffff));
        put24(&in[9], block_size);
        in.insert(in.end(), p.begin(), p.end());
        in[0] = (uint8_t)(in.size() - 1);
        alloc(c[4]);
        return 0;
    }
    case 0x15: {            // MODE SELECT(6): only the block length is honoured
        uint32_t n = c[4];
        if (!out) { out_len = n; return 0; }
        if (out->size() >= 12 && (*out)[3] >= 8) {
            uint32_t bl = ((*out)[9] << 16) | ((*out)[10] << 8) | (*out)[11];
            if (bl == 256 || bl == 512 || bl == 1024 || bl == 2048 || bl == 4096) block_size = bl;
        }
        return 0;
    }
    case 0x08: case 0x28: {  // READ(6) / READ(10)
        uint64_t lba; uint32_t n;
        if (op == 0x08) { lba = ((c[1] & 0x1f) << 16) | (c[2] << 8) | c[3]; n = c[4] ? c[4] : 256; }
        else { lba = ((uint32_t)c[2] << 24) | (c[3] << 16) | (c[4] << 8) | c[5]; n = (c[7] << 8) | c[8]; }
        if (lba + n > blocks()) return check(0x05, 0x21);
        if (n && !read_blocks(lba, n, in)) return check(0x03, 0x11);
        return 0;
    }
    case 0x0a: case 0x2a: case 0x2e: {   // WRITE(6) / WRITE(10) / WRITE AND VERIFY(10)
        uint64_t lba; uint32_t n;
        if (op == 0x0a) { lba = ((c[1] & 0x1f) << 16) | (c[2] << 8) | c[3]; n = c[4] ? c[4] : 256; }
        else { lba = ((uint32_t)c[2] << 24) | (c[3] << 16) | (c[4] << 8) | c[5]; n = (c[7] << 8) | c[8]; }
        if (readonly) return check(0x07, 0x27);
        if (lba + n > blocks()) return check(0x05, 0x21);
        if (!out) { out_len = n * block_size; return 0; }
        if (n && !write_blocks(lba, n, out->data())) return check(0x03, 0x0c);
        return 0;
    }
    case 0x43: {            // READ TOC (single data track)
        if (type != CD) return check(0x05, 0x20);
        in.assign(20, 0);
        in[1] = 18; in[2] = 1; in[3] = 1;
        in[5] = 0x14; in[6] = 1;                     // track 1, data
        in[13] = 0x14; in[14] = 0xaa;                // lead-out
        put32(&in[16], (uint32_t)(bytes / 2048));
        alloc((c[7] << 8) | c[8]);
        return 0;
    }
    }
    return check(0x05, 0x20);   // ILLEGAL REQUEST, invalid command operation code
}

// ======================================================================= WD33C93

void Wd33c93::reset() {
    memset(regs, 0, sizeof regs);
    addr = 0; aux = 0; irqs.clear(); buf.clear(); pos = 0; phase = IDLE; target = -1;
    irq_update();
}

void Wd33c93::raise(uint8_t code) {
    irqs.push_back(code);
    if (!(aux & AUX_INT)) next_irq();
}

void Wd33c93::next_irq() {
    if (irqs.empty()) return;
    regs[0x17] = irqs.front();
    irqs.pop_front();
    aux |= AUX_INT;
    irq_update();
}

void Wd33c93::update_dbr() {
    aux &= ~AUX_DBR;
    if (dma_mode()) return;
    if (phase == DATA_IN && pos < buf.size()) aux |= AUX_DBR;
    if (phase == DATA_OUT && pos < buf.size()) aux |= AUX_DBR;
}

bool Wd33c93::dreq() const {
    if (!dma_mode()) return false;
    return (phase == DATA_IN || phase == DATA_OUT) && pos < buf.size();
}

uint8_t Wd33c93::read(int a) {
    if (a == 0) return aux;
    switch (addr) {
    case 0x19: return dma_read();
    case 0x17: {
        uint8_t v = regs[0x17];
        aux &= ~AUX_INT;
        next_irq();
        irq_update();
        addr = (addr + 1) & 0x1f;
        return v;
    }
    case 0x18: return regs[0x18];
    default: {
        uint8_t v = regs[addr];
        addr = (addr + 1) & 0x1f;
        return v;
    }
    }
}

void Wd33c93::write(int a, uint8_t v) {
    if (a == 0) { addr = v & 0x1f; return; }
    switch (addr) {
    case 0x18: regs[0x18] = v; command(v); return;
    case 0x19: dma_write(v); return;
    case 0x17: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: break;
    default: regs[addr] = v; break;
    }
    addr = (addr + 1) & 0x1f;
}

uint8_t Wd33c93::dma_read() {
    activity.fetch_add(1, std::memory_order_relaxed);
    if (phase != DATA_IN || pos >= buf.size()) return 0;
    uint8_t v = buf[pos++];
    if (xfer_left) set_count(xfer_left - 1);
    if (pos >= buf.size()) {
        uint8_t st = regs[0x0f];   // status byte was stored when the command executed
        finish(st);
    } else update_dbr();
    return v;
}

void Wd33c93::dma_write(uint8_t v) {
    activity.fetch_add(1, std::memory_order_relaxed);
    if (phase != DATA_OUT || pos >= buf.size()) return;
    buf[pos++] = v;
    if (xfer_left) set_count(xfer_left - 1);
    if (pos >= buf.size()) data_out_done();
    else update_dbr();
}

void Wd33c93::data_out_done() {
    std::vector<uint8_t> in; uint32_t ol = 0;
    uint8_t st = dev[target]->execute(cdb, cdb_len, &buf, in, ol);
    if (log) fprintf(stderr, "[SCSI] id%d data-out %zu bytes -> status %02x\n", target, buf.size(), st);
    finish(st);
}

void Wd33c93::finish(uint8_t status_byte) {
    phase = IDLE;
    buf.clear(); pos = 0;
    aux &= ~(AUX_BSY | AUX_CIP | AUX_DBR);
    regs[0x0f] = status_byte;    // after Select-and-Transfer the Target LUN register holds the status
    regs[0x10] = 0x60;           // command phase: command complete
    raise(0x16);                 // Select-and-Transfer completed
    raise(0x85);                 // followed by the target's disconnect
}

void Wd33c93::command(uint8_t c) {
    uint8_t cc = c & 0x7f;
    if (log) fprintf(stderr, "[SCSI] command %02x (dest id %d)\n", c, regs[0x15] & 7);
    switch (cc) {
    case 0x00:   // RESET
        irqs.clear(); phase = IDLE; buf.clear(); pos = 0; aux = 0; irq_update();
        raise((regs[0x00] & 0x08) ? 0x01 : 0x00);
        break;
    case 0x01:   // ABORT
        phase = IDLE; buf.clear(); pos = 0; aux &= ~(AUX_BSY | AUX_CIP | AUX_DBR);
        raise(0x22);
        break;
    case 0x02: case 0x03:   // assert ATN / negate ACK
        break;
    case 0x04:   // DISCONNECT
        phase = IDLE; buf.clear(); pos = 0; aux &= ~(AUX_BSY | AUX_CIP | AUX_DBR);
        raise(0x85);
        break;
    case 0x08: case 0x09:
        select_and_transfer();
        break;
    case 0x06: case 0x07: {   // plain select: report success or timeout
        int id = regs[0x15] & 7;
        raise(dev[id] && id != (regs[0x00] & 7) ? 0x11 : 0x42);
        break;
    }
    default:
        raise(0x40);          // invalid / unsupported (target-mode) command
        break;
    }
}

void Wd33c93::select_and_transfer() {
    activity.fetch_add(1, std::memory_order_relaxed);
    target = regs[0x15] & 7;
    set_count(((uint32_t)regs[0x12] << 16) | (regs[0x13] << 8) | regs[0x14]);
    if (!dev[target] || target == (regs[0x00] & 7)) {
        regs[0x10] = 0x00;
        raise(0x42);          // selection timeout
        return;
    }
    uint8_t op = regs[0x03];
    switch (op >> 5) {
    case 0: cdb_len = 6; break;
    case 1: case 2: cdb_len = 10; break;
    case 5: cdb_len = 12; break;
    default: cdb_len = regs[0x00] >> 4 ? regs[0x00] >> 4 : 6; break;
    }
    memcpy(cdb, &regs[0x03], 12);
    std::vector<uint8_t> in; uint32_t out_len = 0;
    uint8_t st = dev[target]->execute(cdb, cdb_len, nullptr, in, out_len);
    if (log) {
        fprintf(stderr, "[SCSI] id%d CDB", target);
        for (int i = 0; i < cdb_len; i++) fprintf(stderr, " %02x", cdb[i]);
        fprintf(stderr, " -> status %02x in %zu out %u (count %u, %s)\n", st, in.size(), out_len, xfer_left,
                dma_mode() ? "DMA" : "polled");
    }
    aux |= AUX_BSY | AUX_CIP;
    pos = 0;
    if (st == 0 && out_len) {
        phase = DATA_OUT;
        buf.assign(out_len, 0);
        regs[0x0f] = 0;
        update_dbr();
        return;
    }
    if (!in.empty()) {
        if (xfer_left && in.size() > xfer_left) in.resize(xfer_left);
        phase = DATA_IN;
        buf.swap(in);
        regs[0x0f] = st;
        update_dbr();
        return;
    }
    finish(st);
}

// ======================================================================= image naming

static bool parse_zulu(const std::string &fname, int &id, ScsiDevice::Type &type, int &bs) {
    std::string n = fname;
    for (auto &ch : n) ch = (char)toupper((unsigned char)ch);
    if (n.size() < 3) return false;
    std::string pre = n.substr(0, 2);
    if (pre == "HD") type = ScsiDevice::HD;
    else if (pre == "CD") type = ScsiDevice::CD;
    else if (pre == "RE" || pre == "MO") type = ScsiDevice::REMOVABLE;
    else return false;
    if (!isdigit((unsigned char)n[2])) return false;
    id = n[2] - '0';
    if (id > 7) return false;
    bs = 0;
    size_t us = n.find('_');
    size_t dot = n.rfind('.');
    if (us != std::string::npos && (dot == std::string::npos || us < dot)) {
        int v = atoi(n.c_str() + us + 1);
        if (v == 256 || v == 512 || v == 1024 || v == 2048 || v == 4096) bs = v;
    }
    return true;
}

bool Wd33c93::attach(int id, const std::string &path, ScsiDevice::Type type, int bs) {
    if (id < 0 || id > 7) return false;
    auto d = ScsiDevice::open(path, type, bs, type == ScsiDevice::CD || force_readonly);
    if (!d) return false;
    dev[id] = std::move(d);
    return true;
}

std::string Wd33c93::attach_file(const std::string &path, int host_id) {
    namespace fs = std::filesystem;
    int id = 0, bs = 0; ScsiDevice::Type t = ScsiDevice::HD;
    if (!parse_zulu(fs::u8path(path).filename().u8string(), id, t, bs)) {
        // any other name: an image bigger than a floppy (e.g. DISK000.iso from a ZIP/SCSI disk)
        // goes on the bus as a hard disk, on the first free ID
        std::error_code ec;
        auto sz = fs::file_size(fs::u8path(path), ec);
        if (ec || sz <= 2 * 1024 * 1024) return "";
        id = -1;
        for (int i = 0; i < 8 && id < 0; i++) if (i != host_id && !dev[i]) id = i;
        if (id < 0) return "SCSI: no free IDs";
        t = ScsiDevice::HD;
    }
    if (id == host_id) return "ID " + std::to_string(id) + " e o proprio ASR-10";
    if (!attach(id, path, t, bs)) return "cannot open " + path;
    return "SCSI ID " + std::to_string(id) + ": " + dev[id]->describe();
}

std::string Wd33c93::summary() const {
    std::string s;
    for (int i = 0; i < 8; i++) if (dev[i]) {
        if (!s.empty()) s += "  ";
        s += "ID" + std::to_string(i) + (dev[i]->type == ScsiDevice::CD ? ":CD" : ":HD");
    }
    return s.empty() ? "no devices" : s;
}

std::string Wd33c93::attach_dir(const std::string &dir, int host_id) {
    namespace fs = std::filesystem;
    std::string report;
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto &e : fs::directory_iterator(fs::u8path(dir), ec)) if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto &p : files) {
        int id = 0, bs = 0; ScsiDevice::Type t = ScsiDevice::HD;
        if (!parse_zulu(p.filename().u8string(), id, t, bs)) continue;
        if (id == host_id) { report += "ID " + std::to_string(id) + " e o proprio ASR-10, ignorado: " + p.u8string() + "\n"; continue; }
        if (dev[id]) { report += "ID " + std::to_string(id) + " ja ocupado, ignorado: " + p.u8string() + "\n"; continue; }
        if (attach(id, p.u8string(), t, bs)) report += "SCSI ID " + std::to_string(id) + ": " + dev[id]->describe() + "\n";
        else report += "cannot open " + p.u8string() + "\n";
    }
    if (ec) report += "cannot read the folder " + dir + "\n";
    return report;
}
