// Front-end settings - see settings.h.
#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0; while (i < s.size() && s[i] == ' ') i++;
    return s.substr(i);
}

bool Settings::load(const std::string &file) {
    path = file;
    FILE *f = fopen(file.c_str(), "r");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        int n = atoi(v.c_str());
        if (k == "audio_driver") audio_api = v;
        else if (k == "audio_out") audio_out = v;
        else if (k == "audio_in") audio_in = v;
        else if (k == "sample_rate") rate = (unsigned)n;
        else if (k == "buffer") buffer = (unsigned)n;
        else if (k == "input_left") in_left = n - 1;
        else if (k == "main_out") main_pair = n > 0 ? (n - 1) / 2 : 0;
        else if (k == "aux1_out" || k == "aux2_out" || k == "aux3_out") aux_pair[k[3] - '1'] = v == "off" ? (k[3] == '1' ? -2 : -1) : n > 0 ? (n - 1) / 2 : -1;
        else if (k == "midi_in") { midi_in = v; midi_in_set = true; }
        else if (k == "midi_out") midi_out = v;
        else if (k == "keyboard_channel" && n >= 1 && n <= 16) kbd_channel = n;
        else if (k == "midi_in_to_keyboard_channel") midi_remap = n != 0;
        else if (k == "bus2_to_main") bus_main[0] = n != 0;
        else if (k == "bus3_to_main") bus_main[1] = n != 0;
        else if (k == "show_keyboard") show_keyboard = n != 0;
        else if (k == "scsi_dir") scsi_dir = v;
        else if (k == "app_dir") app_dir = v;
    }
    fclose(f);
    if (in_left < 0) in_left = 0;
    in_right = in_left;
    return true;
}

bool Settings::save() const {
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "# Ensoniq EPS-16 Plus emulator settings\n");
    fprintf(f, "audio_driver=%s\naudio_out=%s\naudio_in=%s\n", audio_api.c_str(), audio_out.c_str(), audio_in.c_str());
    fprintf(f, "sample_rate=%u\nbuffer=%u\n", rate, buffer);
    fprintf(f, "# input channel for the sampler (mono; 1 = first input of the device)\ninput_left=%d\n", in_left + 1);
    fprintf(f, "# first output of each stereo pair (1 = outs 1-2, 3 = outs 3-4 ...); aux1_out: 0 = mixed dry into main, off = silent; aux2/3_out: 0 = no separate output\n");
    fprintf(f, "main_out=%d\n", main_pair * 2 + 1);
    for (int i = 0; i < 3; i++) {
        if (aux_pair[i] == -2) fprintf(f, "aux%d_out=off\n", i + 1);
        else fprintf(f, "aux%d_out=%d\n", i + 1, aux_pair[i] < 0 ? 0 : aux_pair[i] * 2 + 1);
    }
    fprintf(f, "midi_in=%s\nmidi_out=%s\n", midi_in.c_str(), midi_out.c_str());
    fprintf(f, "# MIDI channel played by the on-screen keyboard and wheels\nkeyboard_channel=%d\n", kbd_channel);
    fprintf(f, "# 1 = MIDI In channel messages are sent to keyboard_channel\nmidi_in_to_keyboard_channel=%d\n", midi_remap ? 1 : 0);
    fprintf(f, "# 0 = BUS 2/3 only on the OEX-6 outputs, not in the main mix\nbus2_to_main=%d\nbus3_to_main=%d\n", bus_main[0] ? 1 : 0, bus_main[1] ? 1 : 0);
    fprintf(f, "# OEX-6 outputs: aux1_out = AUX 1, aux2_out = BUS 2, aux3_out = BUS 3\n");
    fprintf(f, "# 1 = show the keyboard strip below the panel (F9 / TECLADO button)\nshow_keyboard=%d\n", show_keyboard ? 1 : 0);
    fprintf(f, "# folder of the SCSI drive images (HD0.img, CD5.iso ...); empty = \"scsi\" next to the program\nscsi_dir=%s\n", scsi_dir.c_str());
    fprintf(f, "# folder of the program (written by it; the VST3 plug-in looks for the ROMs and disks there)\napp_dir=%s\n", app_dir.c_str());
    fclose(f);
    return true;
}
