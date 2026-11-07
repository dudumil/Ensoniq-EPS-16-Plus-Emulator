// ASR-10 front panel / keyboard controller as seen from the main CPU (DUART channel B,
// 31250 baud). The panel micro's firmware is not available, so this models its serial
// protocol as reverse-engineered from the ASR-10 ROM/OS:
//
// CPU -> panel (each byte is acknowledged by the panel with $FF = "ready for next"):
//   $00-$15      set cursor position (0..21) on the 22-character line
//   $20-$5F      character at cursor, cursor advances
//   $66          home / clear line
//   $74 n        indicator n off     $75 n  indicator n on     $76 n  indicator n blinking
//   $60 a        attribute for the following characters (1 = normal, 3 = underlined)
//   $77/$78/$79 n  annunciator n of the left group on/off/blink (LOAD, INST, ... WAVE)
//   $7A/$7B/$7C n  annunciator n of the right group on/off/blink (ENV, ODUB, ... CLOCK)
//   $73 a b      bar graph (input level on the sampling page): b = number of lit cells
//   $62 save cursor (start of a field), $63 back to the saved position, $72 end of field
//   $65 cursor left   $6A cursor right   $67 / $69 edit cursor (underline) on / off at the cursor
//   $71, $7E, $D0+x, $E8, $E9   meaning unknown (no visible effect modelled)
//   $E7, $71     handshake at reset          $FC   query -> panel answers $FC,$01
//   $7F, $FF, $FD keyboard calibration       (answer $FF = OK; $C9 would mean failure)
// panel -> CPU (two bytes each):
//   [0x80|idx][0]     button idx pressed          [idx][0]        button released
//   [0x80|k][vel]     key k (0..60 = C2..C7) down [k][vel]        key up (release velocity)
//   [0x40|k][press]   polyphonic pressure for key k
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <functional>

class Panel {
public:
    int  meter_follow = 0;
    bool eps = false;      // EPS-16 Plus: $73 (meter) takes one argument
    bool rack = false;                     // identify as the rack unit ($FC query answered with $01)

public:
    Panel();
    void reset();
    void rx_from_cpu(uint8_t b);           // byte sent by the main CPU
    std::function<void(uint8_t)> send;     // byte to the main CPU

    // user interface
    void button(int code, bool down);
    void key(int k, int velocity, bool down);
    void pressure(int k, int value);
    std::string text() const { return std::string(disp, 22); }
    std::string attr_line(int t) const;    // '^' where attribute t is set
    static constexpr int COLS = 22;
    char    disp[COLS];
    uint8_t attr[COLS];                    // bit 0 = underlined text ($60 3), bit 1 = edit cursor ($67)
    uint8_t ind[16];                       // indicator states 0 off 1 on 2 blink: 0-7 red "loaded", 8-15 yellow "selected"
    uint8_t ind2[16];                      // left annunciators ($77 on / $78 off / $79 blink), 0 off 1 on 2 blink
    uint8_t ind3[16];                      // right annunciators ($7A on / $7B off / $7C blink)
    std::string annunciators() const;      // lit annunciator legends, e.g. "LOAD* INST BANK | STOP" (* = blinking)
    int     meter = 0, meter_a = 0, meter_peak = 0;        // bar graph ($73 a b): b = level in character cells
    int     cx = 0;
    int     mode60 = 0;
    bool    attr_active = false;           // $60 attribute applies to the text written until $72
    int     field_start = 0, field_end = 0, field_old_end = 0, field_writes = 0;
    bool    field_open = false, field_rewrite = false;
    bool log = false;
    std::deque<uint8_t> trace;             // last bytes received (debug)
    uint64_t updates = 0;                  // increments on every visible change

private:
    int pending = 0;                       // command awaiting its argument
    void put(uint8_t c);
};
