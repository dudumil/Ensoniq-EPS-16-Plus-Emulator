#include "panel.h"
#include <cstdio>
#include <cstring>

Panel::Panel() { reset(); }

void Panel::reset() {
    memset(disp, ' ', sizeof(disp));
    memset(attr, 0, sizeof(attr));
    memset(ind, 0, sizeof(ind)); memset(ind2, 0, sizeof(ind2)); memset(ind3, 0, sizeof(ind3));
    cx = 0; pending = 0; meter = meter_peak = 0; meter_follow = 0; mode60 = 0; attr_active = false;
}

std::string Panel::attr_line(int t) const {
    std::string s(COLS, ' ');
    for (int i = 0; i < COLS; i++) if (attr[i] & (1 << t)) s[i] = t == 1 ? '_' : '^';
    return s;
}

void Panel::put(uint8_t c) {
    if (cx >= 0 && cx < COLS) {
        disp[cx] = (c >= 0x20 && c < 0x7f) ? c : '?';
        // the text attribute set by $60 applies until the end of the field ($72); later writes
        // (e.g. the bar counter updated while a song plays) keep the cell's underline
        if (attr_active) attr[cx] = (uint8_t)((attr[cx] & 2) | ((mode60 & 2) ? 1 : 0));   // bit 1 = edit cursor
        updates++;
    }
    if (cx < COLS) cx++;
    field_writes++;
}

void Panel::rx_from_cpu(uint8_t b) {
    trace.push_back(b); if (trace.size() > 256) trace.pop_front();
    if (meter_follow == 1 && (b > 0x1f || pending)) meter_follow = 0;   // no peak byte this time
    uint8_t reply = 0xff;   // flow-control acknowledge
    if (pending) {
        int cmd = pending; pending = 0;
        if (log && cmd >= 0x77 && cmd <= 0x7c) fprintf(stderr, "[PANEL] %02x %02x  |%.22s|\n", cmd, b, disp);
        switch (cmd) {
        case 0x74: case 0x75: case 0x76: ind[b & 15] = cmd - 0x74; updates++; break;
        case 0x77: case 0x78: case 0x79:          // second indicator bank (meaning not fully known)
            ind2[b & 15] = cmd == 0x77 ? 1 : cmd == 0x78 ? 0 : 2; updates++; break;
        case 0x7a: case 0x7b: case 0x7c:          // third indicator bank: on / off / blink
            ind3[b & 15] = cmd == 0x7a ? 1 : cmd == 0x7b ? 0 : 2; updates++; break;
        case 0x73: if (eps) { meter_a = b; break; }   // EPS-16 Plus: one argument; the levels follow the $F7 sync
                   meter_a = b; pending = 0x173; break;   // first of two arguments
        case 0x173: meter = b; updates++; break;
        case 0x60: mode60 = b; attr_active = true; break;   // character attribute for following text (bit 1 = underline)
        }
    } else if (eps && meter_follow > 0 && b <= 0x1f) {
        // EPS-16 Plus input meter: right after the $F7 sync comes the bar length, and sometimes
        // a second byte, the peak-hold position. Anything else ends it (a later $00-$1F is a
        // cursor position again).
        if (meter_follow == 2) { meter = b; meter_peak = 0; } else meter_peak = b;
        meter_follow--; updates++;
    } else if (b <= 0x1f) {
        cx = b;                                 // cursor position
        field_open = field_rewrite = false;     // a pending field rewrite ($63) is abandoned
    } else if (b < 0x60) {
        put(b);
    } else {
        switch (b) {
        case 0x62:                                  // start of an editable field
            field_start = cx; field_open = true; field_writes = 0; break;
        case 0x63:                                  // rewrite the current field from its start
            field_old_end = field_end; cx = field_start; field_open = true; field_rewrite = true; field_writes = 0; break;
        case 0x72:                                  // end of field
            attr_active = false;
            if (field_open) {
                if (field_rewrite && field_writes > 0)
                    for (int i = cx; i < field_old_end && i < COLS; i++) { disp[i] = ' '; attr[i] = 0; }
                if (!field_rewrite || field_writes > 0) field_end = cx;
                field_open = false; field_rewrite = false;
                updates++;
            }
            break;
        case 0x66: field_start = field_end = 0; field_open = field_rewrite = false;
            memset(disp, ' ', sizeof(disp)); memset(attr, 0, sizeof(attr)); cx = 0; mode60 = 1; meter = meter_peak = 0; meter_follow = 0; updates++; break;
        case 0x60: case 0x73:
        case 0x74: case 0x75: case 0x76:
        case 0x77: case 0x78: case 0x79:
        case 0x7a: case 0x7b: case 0x7c:
            pending = b; break;
        // edit cursor (underline under the character being edited, e.g. a layer on the
        // Edit/Instrument PATCH= page or a letter when renaming)
        case 0x65: if (cx > 0) cx--; break;                                     // cursor left
        case 0x67: if (cx < COLS) attr[cx] |= 2; updates++; break;              // show edit cursor here
        case 0x69: if (cx < COLS) attr[cx] &= ~2; updates++; break;             // remove edit cursor here
        case 0x6a: if (cx < COLS) { attr[cx] &= ~2; cx++; } updates++; break;   // remove it and step right
        case 0x68: if (cx < COLS) { attr[cx] |= 2; cx++; } updates++; break;    // mark this character and step right
                                                    // (the choice in "KEEP = OLD NEW" prompts)
        case 0xfc: reply = 0xfc; break;        // query: answered by $FC,$01 below
        case 0xe7: reply = 0x00; break;
        case 0xea: break;                           // (EPS-16 Plus, sampling meter refresh: no visible effect known)
        case 0xf7: reply = 0xf7; meter_follow = 2; break;        // sync (EPS-16 Plus sampling): the usual $FF, then $F7 echoed
        default:
            if (log) fprintf(stderr, "[PANEL] unhandled %02x\n", b);
            break;
        }
    }
    if (send) {
        if (reply == 0xf7) send(0xff);
        send(reply);
        if (reply == 0xfc) send(rack ? 0x01 : 0x00);
    }
}

// Panel -> CPU messages (two bytes each), see header
void Panel::button(int code, bool down) {
    if (!send) return;
    send((down ? 0x80 : 0x00) | (code & 0x3f));   // like keys: bit 7 set = pressed
    send(0x00);
}
void Panel::key(int k, int velocity, bool down) {
    if (!send) return;
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    send((down ? 0x80 : 0x00) | (k & 0x3f));   // keys: bit7 set = key down
    send(velocity);
}
void Panel::pressure(int k, int value) {
    if (!send) return;
    send(0x40 | (k & 0x3f));
    send(value & 0x7f);
}

// Annunciator legends by index, as wired on the ASR-10 display (index -> legend). Mapped by
// watching which index the OS lights on each page; "" = no legend on the glass.
static const char *const ANN_LEFT[16]  = { "", "MACRO", "SEQ", "MIDI", "SONG", "EDIT", "FILTER", "BANK",
                                           "WAVE", "LFO", "PITCH", "LAYER", "SYSTEM", "CMD", "INST", "LOAD" };
static const char *const ANN_RIGHT[16] = { "", "BAR", "SONG", "REC", "SEQ", "TRACK", "REP", "BEAT",
                                           "", "CLOCK", "STEP", "STOP", "PLAY", "AMP", "ODUB", "ENV" };

std::string Panel::annunciators() const {
    std::string s;
    auto add = [&](const char *const *names, const uint8_t *st) {
        for (int i = 15; i >= 0; i--) if (st[i] % 3) {
            if (!s.empty() && s.back() != ' ') s += ' ';
            s += names[i][0] ? names[i] : ("#" + std::to_string(i)).c_str();
            if (st[i] % 3 == 2) s += '*';
        }
    };
    add(ANN_LEFT, ind2);
    s += s.empty() ? "| " : " | ";
    add(ANN_RIGHT, ind3);
    return s;
}
