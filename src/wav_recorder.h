#pragma once
// Real-time recording of the sequencer's output to a WAV file (File > Record Song to WAV...).
// The user chooses a folder; the recorder is then armed and waits. When the sequencer starts (the PLAY
// annunciator of the display lights) the main stereo output is written to a file, from 100 ms before
// the start; when the sequencer stops (end of the song, or STOP) it goes on until the sound has died
// away (reverb tails; 10 s at most) and the file gets the name of the song / sequence: the first 12
// characters of the Seq/Song page ("NAME........ BAR=.." / "... STEP=.."), remembered whenever that page
// is shown (reading it from the display works with any OS version; if the page was never shown the
// file gets a generic name).
// process() runs in the emulation thread; the other calls are made with the machine locked.
#include "u8file.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

class WavRecorder {
public:
    enum State { IDLE, ARMED, RECORDING, TAIL };
    State state = IDLE;
    std::string folder;             // destination
    std::string fallback_name = "Recording";
    std::string event;              // set when something worth telling the user happens (taken by the UI)
    std::string last_file;

    ~WavRecorder() { finish(); }

    void arm(const std::string &dir) { finish(); folder = dir; pre.clear(); state = ARMED; }
    // armed: back to idle; recording: close the file now
    void stop() { if (state == ARMED) { state = IDLE; event = "WAV recording cancelled"; } else finish(); }
    bool active() const { return state != IDLE; }
    double seconds() const { return rate ? (double)frames / rate : 0; }
    const std::string &name() const { return rec_name.empty() ? seq_name : rec_name; }

    std::string status() const {
        char b[160];
        if (state == ARMED) return "WAV recording armed" + (seq_name.empty() ? std::string() : " for \"" + seq_name + "\"") + ": press PLAY on the sequencer (Ctrl+W cancels)";
        int s = (int)seconds();
        snprintf(b, sizeof b, "Recording \"%s\"  %d:%02d%s", shown_name().c_str(), s / 60, s % 60, state == TAIL ? "  (sound dying away...)" : "");
        return b;
    }

    // stereo: interleaved frames of the main output; disp: the 22 display characters;
    // playing: the sequencer is running
    void process(const float *stereo, size_t n, int sample_rate, bool playing, const char *disp) {
        // the Seq/Song page: "SEQUENCE 01  BAR=00.01" (a sequence) or " ** SONG **  STEP=01" (a song)
        if (disp && disp[12] == ' ' && (!memcmp(disp + 13, "BAR=", 4) || !memcmp(disp + 13, "STEP=", 5))) {
            std::string nm;
            for (int i = 0; i < 12; i++) if (disp[i] != '*') nm += disp[i];     // "** SONG **" -> "SONG"
            while (!nm.empty() && nm.back() == ' ') nm.pop_back();
            size_t a = nm.find_first_not_of(' ');
            if (a != std::string::npos) seq_name = nm.substr(a);
        }
        if (state == IDLE) return;
        if (state == ARMED) {
            if (!playing) {                         // keep the last 100 ms
                pre.insert(pre.end(), stereo, stereo + n * 2);
                size_t keep = (size_t)(sample_rate / 10) * 2;
                if (pre.size() > keep) pre.erase(pre.begin(), pre.end() - keep);
                return;
            }
            if (!open(sample_rate)) { state = IDLE; pre.clear(); return; }
            rec_name = seq_name;
            write(pre.data(), pre.size() / 2); pre.clear();
            state = RECORDING;
        }
        if (sample_rate != rate) { finish(); event = "WAV recording stopped: the audio sample rate changed"; return; }
        write(stereo, n);
        if (rec_name.empty()) rec_name = seq_name;  // not known at the start: the first one seen
        if (state == RECORDING) {
            if (!playing) { state = TAIL; tail = 0; quiet = 0; }
        } else {                                     // TAIL
            if (playing) { state = RECORDING; return; }   // Continue (or the PLAY annunciator blinking)
            float pk = 0;
            for (size_t i = 0; i < n * 2; i++) pk = std::max(pk, std::fabs(stereo[i]));
            quiet = pk < 1e-4f ? quiet + n : 0;
            tail += n;
            if (quiet >= (size_t)rate * 4 / 10 || tail >= (size_t)rate * 10) finish();
        }
    }

    // close the file being recorded and give it its name
    void finish() {
        if (!f) { if (state != IDLE && state != ARMED) state = IDLE; return; }
        // drop the silence at the end, but for 50 ms
        uint64_t keep = std::min<uint64_t>(frames, last_loud + (uint64_t)rate / 20);
        long data_bytes = (long)(keep * 2 * 3);
        uint8_t h[44];
        header(h, (uint32_t)data_bytes);
        fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fflush(f);
        fclose(f); f = nullptr;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::resize_file(fs::u8path(tmp_path), (uintmax_t)(44 + data_bytes), ec);
        ec.clear();
        std::string base = sanitize(rec_name.empty() ? (seq_name.empty() ? fallback_name : seq_name) : rec_name);
        fs::path dir = fs::u8path(folder), dst = dir / fs::u8path(base + ".wav");
        for (int i = 2; fs::exists(dst, ec) && i < 1000; i++) dst = dir / fs::u8path(base + " (" + std::to_string(i) + ").wav");
        fs::rename(fs::u8path(tmp_path), dst, ec);
        char b[64]; int s = (int)((double)keep / rate + 0.5);
        snprintf(b, sizeof b, "  (%d:%02d)", s / 60, s % 60);
        last_file = ec ? tmp_path : dst.u8string();
        event = "Recorded: " + last_file + b;
        state = IDLE; frames = 0; rec_name.clear();
    }

private:
    FILE *f = nullptr;
    std::string tmp_path, seq_name, rec_name;
    std::vector<float> pre;
    std::vector<uint8_t> buf;
    int rate = 0;
    uint64_t frames = 0, last_loud = 0;
    size_t tail = 0, quiet = 0;

    std::string shown_name() const { const std::string &n = name(); return n.empty() ? fallback_name : n; }
    static std::string sanitize(std::string s) {
        for (auto &c : s) if ((unsigned char)c < 0x20 || strchr("\\/:*?\"<>|", c)) c = '_';
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        return s.empty() ? "Recording" : s;
    }
    void header(uint8_t *h, uint32_t data_bytes) const {
        auto p32 = [&](int o, uint32_t v) { h[o] = (uint8_t)v; h[o + 1] = (uint8_t)(v >> 8); h[o + 2] = (uint8_t)(v >> 16); h[o + 3] = (uint8_t)(v >> 24); };
        auto p16 = [&](int o, uint16_t v) { h[o] = (uint8_t)v; h[o + 1] = (uint8_t)(v >> 8); };
        memcpy(h, "RIFF", 4); p32(4, 36 + data_bytes); memcpy(h + 8, "WAVEfmt ", 8);
        p32(16, 16); p16(20, 1); p16(22, 2); p32(24, (uint32_t)rate); p32(28, (uint32_t)rate * 6); p16(32, 6); p16(34, 24);
        memcpy(h + 36, "data", 4); p32(40, data_bytes);
    }
    bool open(int sample_rate) {
        namespace fs = std::filesystem;
        tmp_path = (fs::u8path(folder) / "recording-in-progress.wav.part").u8string();
        f = u8fopen(tmp_path.c_str(), "wb+");
        if (!f) { event = "Cannot write in " + folder; return false; }
        rate = sample_rate; frames = 0; last_loud = 0;
        uint8_t h[44]; header(h, 0); fwrite(h, 1, 44, f);
        return true;
    }
    void write(const float *s, size_t n) {          // 24-bit PCM
        if (!f || !n) return;
        buf.resize(n * 6);
        for (size_t i = 0; i < n * 2; i++) {
            float v = std::max(-1.0f, std::min(1.0f, s[i]));
            int32_t q = (int32_t)std::lrint(v * 8388607.0f);
            buf[i * 3] = (uint8_t)q; buf[i * 3 + 1] = (uint8_t)(q >> 8); buf[i * 3 + 2] = (uint8_t)(q >> 16);
            if (std::fabs(v) >= 1e-4f) last_loud = frames + i / 2 + 1;
        }
        fwrite(buf.data(), 1, buf.size(), f);
        frames += n;
    }
};
