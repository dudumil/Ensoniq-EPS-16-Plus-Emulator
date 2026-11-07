// Front-end settings (audio, MIDI), stored as "key=value" lines in asr10.cfg.
#pragma once
#include <string>

struct Settings {
    // audio
    std::string audio_api;             // "" = first available driver
    std::string audio_out;             // device name, "" = default
    std::string audio_in;              // device name, "" = no input
    unsigned rate = 0;                 // 0 = device default
    unsigned buffer = 256;
    int in_left = 0, in_right = 0;     // input channel (0-based) of the sampler (mono: in_right = in_left)
    int main_pair = 0;                 // output pair for the main outputs (0 = outs 1-2)
    int aux_pair[3] = { -1, -1, -1 };  // AUX1, BUS2, BUS3 output pair; -1 = none (AUX1: mixed into main), -2 = AUX1 off
    // MIDI (port names, "" = none)
    std::string midi_in, midi_out;
    bool midi_in_set = false;          // false: use the first input port, like earlier versions
    int kbd_channel = 9;
    bool midi_remap = true;            // MIDI In channel messages go to kbd_channel
    bool bus_main[2] = { true, true }; // BUS 2/3 also reach the main mix (through the ESP)               // MIDI channel of the on-screen keyboard and wheels (1-16)
    // window
    bool show_keyboard = true;         // strip with the wheels, Patch Select and the keyboard

    std::string path;                  // file the settings were loaded from / are saved to
    bool load(const std::string &file);
    bool save() const;
};
