// Keyboard skin drawing - see kbd_ui.h.
#include "kbd_ui.h"
#include "kbd_art.h"
#include "panel.h"
#include "vfd.h"
#include <cmath>
#include <algorithm>

namespace {
uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { float x = ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t; return (uint32_t)std::clamp(x + 0.5f, 0.0f, 255.0f) << s; };
    return ch(16) | ch(8) | ch(0);
}
uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
float rnd(uint32_t x) { return (hash(x) & 0xffff) / 65535.0f; }

// colours (measured on the photo, a little darker: the photo is lit flat)
const uint32_t BODY = 0x26282c, DECK = 0x232529, PLATE = 0x2a2b2f, STRIP = 0x2b2d32, RECESS = 0x121418,
               CHEEK = 0x2b2f35, OEX = 0x2f2f34, PRINT = 0xd4d5d7, WHITE = 0xececec, DIMPRINT = 0x8d9095, PURPLE = 0x5e3a62;
const uint32_t LEGEND_COLOUR[3] = { PRINT, WHITE, DIMPRINT };

// layout of the 61 keys: 36 white keys from u = 423 to 2338
const float KB_U0 = 423, KB_U1 = 2338, WW = (KB_U1 - KB_U0) / 36, KEY_TOP = 765, WHITE_BOT = 1080, BLACK_BOT = 970, BW = 32.5f;
bool black(int k) { int n = k % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
int white_index(int k) { static const int wi[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 }; return (k / 12) * 7 + wi[k % 12]; }
float black_centre(int k) {     // in white keys from the C of the octave (measured: C# left of centre, D# right...)
    static const float c[12] = { 0, 0.855f, 0, 2.07f, 0, 0, 3.84f, 0, 5.01f, 0, 6.17f, 0 };
    return (k / 12) * 7 + c[k % 12];
}

// ------------------------------------------------------------------ the static art, drawn once
// A CPU canvas in photo units (with VS), anti-aliased by coverage.
struct Canvas {
    int w, h;
    float su, sv;               // texels per photo unit across / down
    std::vector<float> px;      // r, g, b
    Canvas(int w_, int h_, float su_, float sv_) : w(w_), h(h_), su(su_), sv(sv_), px((size_t)w_ * h_ * 3) {}
    float tx(float u) const { return (u - KbdUI::U0) * su; }
    float ty(float v) const { return (v - KbdUI::V0) * sv; }
    void put(int x, int y, uint32_t c, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        a = std::min(a, 1.0f);
        float *p = &px[((size_t)y * w + x) * 3];
        p[0] += (((c >> 16) & 255) - p[0]) * a; p[1] += (((c >> 8) & 255) - p[1]) * a; p[2] += ((c & 255) - p[2]) * a;
    }
    void add(int x, int y, float d) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        float *p = &px[((size_t)y * w + x) * 3];
        for (int i = 0; i < 3; i++) p[i] = std::clamp(p[i] + d, 0.0f, 255.0f);
    }
    static float cover(float a0, float a1, int i) { return std::max(0.0f, std::min(a1, i + 1.0f) - std::max(a0, (float)i)); }
    template <class F> void area(float u0, float v0, float u1, float v1, F f) {   // f(x, y, coverage, ty 0..1)
        float x0 = tx(u0), x1 = tx(u1), y0 = ty(v0), y1 = ty(v1);
        for (int y = (int)std::floor(y0); y < (int)std::ceil(y1); y++) {
            float cy = cover(y0, y1, y);
            for (int x = (int)std::floor(x0); x < (int)std::ceil(x1); x++)
                f(x, y, cy * cover(x0, x1, x), (y + 0.5f - y0) / std::max(1.0f, y1 - y0));
        }
    }
    void rect(float u0, float v0, float u1, float v1, uint32_t c, float a = 1) { area(u0, v0, u1, v1, [&](int x, int y, float k, float) { put(x, y, c, k * a); }); }
    void vgrad(float u0, float v0, float u1, float v1, uint32_t top, uint32_t bot, float a = 1) {
        area(u0, v0, u1, v1, [&](int x, int y, float k, float t) { put(x, y, mix(top, bot, std::clamp(t, 0.0f, 1.0f)), k * a); });
    }
    void grain(float u0, float v0, float u1, float v1, float amp, uint32_t seed) {
        area(u0, v0, u1, v1, [&](int x, int y, float k, float) {
            float n = (rnd((uint32_t)x * 2654435761u ^ (uint32_t)y * 40503u ^ seed) - 0.5f) * amp
                    + (rnd((uint32_t)(x / 3) * 92821u ^ (uint32_t)(y / 3) * 68917u ^ seed) - 0.5f) * amp * 0.6f;
            add(x, y, n * k);
        });
    }
    void line(float ua, float va, float ub, float vb, float width, uint32_t c, float a = 1) {
        float xa = tx(ua), ya = ty(va), xb = tx(ub), yb = ty(vb), hw = std::max(0.5f, width * su * 0.5f);
        float dx = xb - xa, dy = yb - ya, L2 = std::max(1e-6f, dx * dx + dy * dy);
        int x0 = (int)std::floor(std::min(xa, xb) - hw - 1), x1 = (int)std::ceil(std::max(xa, xb) + hw + 1);
        int y0 = (int)std::floor(std::min(ya, yb) - hw - 1), y1 = (int)std::ceil(std::max(ya, yb) + hw + 1);
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float px_ = x + 0.5f, py_ = y + 0.5f, t = std::clamp(((px_ - xa) * dx + (py_ - ya) * dy) / L2, 0.0f, 1.0f);
            float ex = xa + t * dx - px_, ey = ya + t * dy - py_, d = std::sqrt(ex * ex + ey * ey);
            put(x, y, c, std::clamp(hw + 0.5f - d, 0.0f, 1.0f) * a);
        }
    }
    void disc(float cu, float cv, float r, uint32_t c, float a = 1) {      // r in photo units across
        float cx = tx(cu), cy = ty(cv), rx = r * su, ry = r * su;        // round on screen
        for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++) for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            float d = std::sqrt(((x + 0.5f - cx) / rx) * ((x + 0.5f - cx) / rx) + ((y + 0.5f - cy) / ry) * ((y + 0.5f - cy) / ry));
            put(x, y, c, std::clamp((1 - d) * rx + 0.5f, 0.0f, 1.0f) * a);
        }
    }
    void tri(float u1, float v1, float u2, float v2, float u3, float v3, uint32_t c, float a = 1) {   // filled, 4x4 supersampled
        float X[3] = { tx(u1), tx(u2), tx(u3) }, Y[3] = { ty(v1), ty(v2), ty(v3) };
        int x0 = (int)std::floor(std::min({ X[0], X[1], X[2] })), x1 = (int)std::ceil(std::max({ X[0], X[1], X[2] }));
        int y0 = (int)std::floor(std::min({ Y[0], Y[1], Y[2] })), y1 = (int)std::ceil(std::max({ Y[0], Y[1], Y[2] }));
        auto side = [&](float px_, float py_, int i) { int j = (i + 1) % 3; return (X[j] - X[i]) * (py_ - Y[i]) - (Y[j] - Y[i]) * (px_ - X[i]); };
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
                float px_ = x + (sx + 0.5f) / 4, py_ = y + (sy + 0.5f) / 4;
                float a0 = side(px_, py_, 0), a1 = side(px_, py_, 1), a2 = side(px_, py_, 2);
                if ((a0 >= 0 && a1 >= 0 && a2 >= 0) || (a0 <= 0 && a1 <= 0 && a2 <= 0)) n++;
            }
            put(x, y, c, n / 16.0f * a);
        }
    }
    void legend(const KbdLegend &l, uint32_t c) {
        float x0 = tx(l.x), y0 = ty(l.y), x1 = tx(l.x + l.w), y1 = ty(l.y + l.h);
        for (int y = (int)std::floor(y0); y < (int)std::ceil(y1); y++) for (int x = (int)std::floor(x0); x < (int)std::ceil(x1); x++) {
            float sum = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
                float fx = (x + (sx + 0.5f) / 4 - x0) / (x1 - x0) * l.tw, fy = (y + (sy + 0.5f) / 4 - y0) / (y1 - y0) * l.th;
                int ix = (int)fx, iy = (int)fy;
                if (ix < 0 || iy < 0 || ix >= l.tw || iy >= l.th) continue;
                size_t k = (size_t)iy * l.tw + ix;
                uint8_t b = KBD_LEGEND_ALPHA[l.offset + k / 2];
                sum += ((k & 1) ? b >> 4 : b & 15) / 15.0f;
            }
            put(x, y, c, sum / 16);
        }
    }
    // a raised edge: light line on top / left, dark on bottom / right
    void bevel(float u0, float v0, float u1, float v1, uint32_t light, uint32_t dark, float t = 1.5f) {
        rect(u0, v0, u1, v0 + t, light); rect(u0, v0, u0 + t, v1, light);
        rect(u0, v1 - t, u1, v1, dark); rect(u1 - t, v0, u1, v1, dark);
    }
    void screw(float cu, float cv, float r) {
        disc(cu, cv, r + 1.2f, 0x111214); disc(cu, cv, r, 0x3b3d42); disc(cu - r * 0.25f, cv - r * 0.3f, r * 0.55f, 0x4a4c51, 0.6f);
        line(cu - r * 0.7f, cv + r * 0.15f, cu + r * 0.7f, cv - r * 0.15f, r * 0.35f, 0x15161a);
    }
};
}

KbdUI::~KbdUI() {
    if (art) SDL_DestroyTexture(art);
    if (glow) SDL_DestroyTexture(glow);
}

bool KbdUI::init(SDL_Renderer *r) {
    R = r;
    // ---- buttons (panel button index, rectangle in photo units)
    auto add = [&](int idx, float u0, float v0, float u1, float v1, int look = 0) { buttons.push_back({ idx, u0, v0, u1, v1, look }); };
    add(26, 766, 518, 795, 528.5f, 1); add(6, 766, 558, 795, 568.5f, 1); add(5, 766, 598, 795, 608.5f, 1);   // Load, Command, Edit
    const float rows[4] = { 497, 537, 577, 616 };
    add(15, 838, rows[0], 867, rows[0] + 10.5f); add(21, 838, rows[1], 867, rows[1] + 10.5f);                 // Instrument, Seq/Song
    add(27, 838, rows[2], 867, rows[2] + 10.5f); add(9, 838, rows[3], 867, rows[3] + 10.5f);                  // System/MIDI, Effects
    const int pad[9] = { 13, 18, 19, 24, 25, 30, 31, 36, 37 };          // Env 1 .. Layer
    const float cols[3] = { 910, 953, 995 };
    for (int i = 0; i < 9; i++) add(pad[i], cols[i % 3] - 14.5f, rows[i / 3], cols[i % 3] + 14.5f, rows[i / 3] + 10.5f);
    add(12, cols[1] - 14.5f, rows[3], cols[1] + 14.5f, rows[3] + 10.5f);                                      // Track (0)
    add(10, 1150, 497.5f, 1177, 507.5f); add(16, 1121, 525, 1149, 535);                                        // up, left
    add(17, 1178, 525, 1205.5f, 535); add(11, 1150, 552, 1177, 562);                                          // right, down
    add(33, 1121, 615, 1150, 625); add(35, 1177.5f, 615, 1206, 625);                                          // No, Yes
    const int trk[8] = { 2, 8, 14, 20, 4, 34, 28, 22 };
    for (int i = 0; i < 8; i++) add(trk[i], 1241.7f + 45.83f * i, 605, 1285 + 45.83f * i, 630.5f, 2);
    add(7, 1641, 513, 1668.5f, 524.5f); add(32, 1712, 513, 1741.5f, 524.5f);                                  // Effect, Sample
    add(3, 1635, 611.5f, 1662, 621.5f, 1); add(23, 1679, 611.5f, 1706, 621.5f, 1); add(29, 1720.5f, 611.5f, 1748, 621.5f, 1);   // Record, Stop, Play
    add(RackUI::PATCH_L, 179, 817, 199, 838, 3); add(RackUI::PATCH_R, 207, 817, 228, 838, 3);

    // ---- the static art
    const float sc = 2.0f;                                   // texture texels per window pixel
    const int tw = (int)std::ceil((U1 - U0) * K * sc), th = (int)std::ceil(height() * sc);
    Canvas c(tw, th, K * sc, K * VS * sc);
    for (size_t i = 0; i < c.px.size(); i += 3) { c.px[i] = 0x0c; c.px[i + 1] = 0x0d; c.px[i + 2] = 0x0e; }

    // body
    c.rect(60, 343, 2404, 1102, BODY);
    c.grain(60, 343, 2404, 1102, 5, 11);
    // end cheeks (moulded, a little lighter at the outside, chamfered at the front corners)
    c.vgrad(60, 343, 128, 1102, 0x30343a, 0x2a2d33); c.rect(60, 343, 64, 1100, 0x3a3e45);
    c.rect(124, 343, 128, 1102, 0x1a1c20);
    c.vgrad(2340, 343, 2404, 1102, 0x1f2024, 0x1b1c1f); c.rect(2400, 343, 2404, 1100, 0x2a2c31); c.rect(2340, 343, 2344, 1102, 0x141518);
    c.tri(60, 1060, 60, 1102, 102, 1102, 0x0c0d0e); c.tri(2404, 1064, 2404, 1102, 2366, 1102, 0x0c0d0e);
    c.line(60, 1060, 102, 1102, 1.5f, 0x3a3e45);
    // rear deck
    c.vgrad(128, 343, 2340, 452, 0x27292d, DECK);
    c.grain(128, 343, 2340, 452, 4, 21);
    c.rect(60, 343, 2404, 345.5f, 0x3c3f45);                      // back edge catches the light
    c.rect(415, 449, 2340, 452, 0x121315);                        // step down to the panel
    // rear legends' little parts: the triangle, the MIDI line, screws
    c.tri(439, 371, 431, 384, 447, 384, 0xdcdcdc);
    c.line(1503, 381.7f, 1693, 381.7f, 0.9f, DIMPRINT);
    c.line(1958, 386.5f, 2072, 386.5f, 0.8f, DIMPRINT);
    c.screw(437.6f, 398, 5.5f); c.screw(2353, 382, 5.5f);

    // ---- left end: disk well, drive, wheels
    c.rect(128, 452, 415, 1082, 0x2a2e34);
    c.grain(128, 452, 415, 1082, 4, 31);
    // disk well
    c.rect(134, 453, 412, 702, 0x1a1d22);
    c.vgrad(146, 464, 402, 690, 0x0d0f12, 0x15171b);
    c.rect(146, 464, 402, 470, 0x07080a);                        // shadow of the back wall
    c.rect(134, 453, 412, 455, 0x3a3e45);
    c.rect(402, 464, 404, 690, 0x2a2d33);
    // the floppy drive in its recess under the well: the slot, a lip under it
    c.rect(138, 710, 410, 762, 0x16181c);
    c.vgrad(146, 714, 402, 760, 0x1a1c21, 0x111316);
    c.rect(150, 724, 400, 741, 0x040405);                        // the slot
    c.rect(150, 724, 400, 726, 0x000000);
    c.vgrad(150, 741, 400, 748, 0x3e4147, 0x25282d);             // lip under the slot
    c.tri(392, 724, 400, 724, 400, 741, 0x1a1c21);              // its slanted end
    c.rect(138, 710, 410, 712, 0x0a0b0d);
    // wheel well
    c.rect(138, 760, 410, 1082, 0x1d2025);
    c.vgrad(146, 762, 404, 1080, 0x2a2e34, 0x30343a);
    c.rect(146, 762, 404, 767, 0x121418);
    c.rect(146, 762, 150, 1080, 0x1f2227);
    c.grain(146, 762, 404, 1080, 3, 41);
    for (float cu : { 240.5f, 318.5f }) {                        // wheel slots
        c.rect(cu - 15, 868, cu + 15, 1016, 0x101113);
        c.rect(cu - 13, 870, cu + 13, 1014, 0x040405);
        c.rect(cu - 15, 1014, cu + 15, 1016, 0x3c4046);
    }
    for (float u0 : { 179.0f, 207.0f }) c.rect(u0 - 2, 815, u0 + 23, 840.5f, 0x15171a);   // Patch Select recesses
    c.line(262, 1043, 298, 1043, 0.8f, PURPLE); c.line(338, 1043, 392, 1043, 0.8f, PURPLE);
    c.line(148, 1043, 222, 1043, 0.8f, PURPLE);

    // ---- keyboard bed and front rail
    c.rect(408, 755, 2345, 1082, 0x0b0b0c);
    c.rect(KB_U0, 765, KB_U1, 1080, 0x6c6c6a);                   // (seen between the white keys)
    c.vgrad(408, 760, 423, 1080, 0x1d2024, 0x23262b);           // key end blocks
    c.vgrad(2338, 760, 2346, 1080, 0x1b1c20, 0x1f2024);
    c.vgrad(128, 1080, 2340, 1102, 0x2c2e33, 0x1e2024);
    c.rect(128, 1080, 2340, 1082, 0x3a3d42);

    // ---- strip behind the keys (under the panel)
    c.vgrad(415, 688, 2340, 762, 0x2d2f34, 0x292b30);
    c.grain(415, 688, 2340, 762, 4, 51);
    c.rect(415, 688, 2340, 692, 0x141517);                        // shadow under the panel
    c.rect(415, 758, 2340, 763, 0x0e0f11);                        // where the keys go in
    c.screw(432, 722, 5.5f); c.screw(2323, 711, 5.5f);

    // ---- the front panel
    c.rect(415, 452, 1798, 690, 0x1c1d20);
    c.rect(425, 454, 1790, 688, PLATE);
    c.grain(425, 454, 1790, 688, 4, 61);
    c.bevel(425, 454, 1790, 688, 0x3a3c41, 0x18191c, 1.6f);
    // slider frames and slots
    for (float cu : { 709.0f, 1067.0f }) {
        c.rect(cu - 26, 477, cu + 26, 639, 0x0e0f11);
        c.bevel(cu - 26, 477, cu + 26, 639, 0x16171a, 0x34363b, 1.4f);
        c.vgrad(cu - 17, 486, cu + 17, 630, 0x060607, 0x0a0a0b);
        c.rect(cu - 3, 516, cu + 3, 626, 0x000000);
    }
    // display window: dark glass in a bezel, its edge catches the light on the right
    c.rect(1236, 472, 1602, 560, 0x141518);
    c.rect(1239, 475, 1599, 557, 0x040506);
    c.rect(1595.5f, 478, 1598, 554, 0x55575d);
    c.rect(1239, 475, 1599, 477, 0x0a0b0c);
    // the button recesses
    for (const Btn &b : buttons) {
        if (b.look == 3) continue;
        float m = b.look == 2 ? 1.5f : 1.2f;
        c.rect(b.u0 - m, b.v0 - m, b.u1 + m, b.v1 + m * 1.8f, 0x0b0c0d);
    }
    // instrument LED windows
    for (int i = 0; i < 8; i++) {
        float bu = 1241.7f + 45.83f * i;
        c.rect(bu + 2, 584.3f, bu + 18, 589.3f, 0x0b0c0d);
        c.rect(bu + 20, 596.8f, bu + 36, 601.8f, 0x0b0c0d);
    }
    // brackets beside LOADED / SELECTED
    c.line(1242, 579, 1242, 594, 1.0f, PRINT); c.line(1239.5f, 579, 1244.5f, 579, 0.9f, PRINT); c.line(1239.5f, 594, 1244.5f, 594, 0.9f, PRINT);
    c.line(1601, 576, 1601, 600, 1.0f, PRINT); c.line(1598.5f, 576, 1603.5f, 576, 0.9f, PRINT); c.line(1598.5f, 600, 1603.5f, 600, 0.9f, PRINT);
    // cursor arrows (outlined triangles)
    auto otri = [&](float u1, float v1, float u2, float v2, float u3, float v3) {
        c.line(u1, v1, u2, v2, 1.0f, PRINT); c.line(u2, v2, u3, v3, 1.0f, PRINT); c.line(u3, v3, u1, v1, 1.0f, PRINT);
    };
    otri(1163.5f, 480.5f, 1156, 490, 1171, 490);       // up
    otri(1163.5f, 579, 1156, 569.5f, 1171, 569.5f);    // down
    otri(1103, 530, 1112, 523.5f, 1112, 536.5f);       // left
    otri(1222, 530, 1213, 523.5f, 1213, 536.5f);       // right
    // purple double rules along the bottom of the panel
    auto rule = [&](float u0, float u1) { c.line(u0, 663.6f, u1, 663.6f, 0.8f, PURPLE); c.line(u0, 667.2f, u1, 667.2f, 0.8f, PURPLE); };
    rule(612, 668); rule(742, 761); rule(802, 898); rule(951, 1080); rule(1167, 1254);
    for (int i = 0; i < 7; i++) rule(1270 + 45.83f * i, 1302 + 45.83f * i);
    rule(1591, 1678); rule(1764, 1784);

    // ---- OEX-6 output expander, sitting on the right of the panel
    c.rect(1796, 668, 2338, 676, 0x161719, 0.8f);                // its shadow
    c.rect(1798, 441, 2336, 670, 0x141517);
    c.vgrad(1800, 443, 2334, 668, 0x323238, OEX);
    c.grain(1800, 443, 2334, 668, 4, 71);
    c.bevel(1800, 443, 2334, 668, 0x46464d, 0x1a1a1e, 1.4f);
    {   // the diagram, white lines
        const float LW = 1.1f;
        auto L = [&](float u0, float v0, float u1, float v1) { c.line(u0, v0, u1, v1, LW, WHITE); };
        auto arrow_up = [&](float u, float v) { c.tri(u, v - 1, u - 3.2f, v + 6, u + 3.2f, v + 6, WHITE); };
        auto arrow_right = [&](float u, float v) { c.tri(u + 1, v, u - 6, v - 3, u - 6, v + 3, WHITE); };
        // jack legends' underlines
        L(1980, 458, 2054, 458); L(2120, 456, 2193, 456); L(2259, 454, 2333, 454);
        // Aux 1: up from the bus, along, to the jack
        L(2129, 546, 2129, 490.5f); L(2129, 490.5f, 2015, 490.5f); L(2015, 490.5f, 2015, 474); arrow_up(2015, 472);
        // Bus 2: straight up
        L(2156, 570.8f, 2156, 474); arrow_up(2156, 472);
        // Bus 3: up, along, up
        L(2183, 556, 2183, 484); L(2183, 484, 2295, 484); L(2295, 484, 2295, 472); arrow_up(2295, 470);
        // the box
        L(2004, 536, 2327, 536); L(2004, 536, 2004, 641); L(2004, 641, 2327, 641); L(2327, 536, 2327, 641);
        // buses
        for (float v : { 546.0f, 556.0f, 570.8f, 585.4f }) c.disc(2100, v, 1.6f, WHITE);
        L(2100, 546, 2129, 546);
        L(2100, 556, 2214, 556); L(2100, 570.8f, 2214, 570.8f); L(2100, 585.4f, 2236, 585.4f);
        // FX processor box with its two input switches and the mixer
        L(2198.5f, 539, 2269, 539); L(2198.5f, 539, 2198.5f, 597); L(2198.5f, 597, 2269, 597); L(2269, 539, 2269, 597);
        L(2214, 556, 2224, 548); L(2214, 570.8f, 2224, 562.8f);               // switches (open)
        c.disc(2228, 556, 1.4f, WHITE); c.disc(2228, 570.8f, 1.4f, WHITE);
        L(2230, 556, 2240, 560); L(2240, 560, 2247, 570); arrow_right(2249.5f, 571.5f);
        L(2230, 570.8f, 2246, 573.5f);
        L(2236, 585.4f, 2247, 577);
        c.disc(2253, 574, 4.0f, WHITE); c.disc(2253, 574, 2.9f, OEX);
        L(2251, 574, 2255, 574); L(2253, 572, 2253, 576);
        L(2257, 574, 2288, 574); L(2288, 574, 2288, 562);
        c.line(2282, 562, 2294, 562, LW, WHITE); L(2282, 562, 2288, 551); L(2294, 562, 2288, 551);   // volume (triangle)
        L(2288, 551, 2288, 538); L(2288, 538, 2282, 533); L(2288, 538, 2294, 533);              // to L / R
        // wavesample: VOL triangle, PAN box, L/R and the output switch
        L(2007.5f, 558.5f, 2007.5f, 585); L(2007.5f, 558.5f, 2029, 571.7f); L(2007.5f, 585, 2029, 571.7f);
        L(2029, 571.7f, 2040.5f, 571.7f);
        L(2040.8f, 562, 2063, 562); L(2040.8f, 581.5f, 2063, 581.5f); L(2040.8f, 562, 2040.8f, 581.5f); L(2063, 562, 2063, 581.5f);
        for (float v : { 567.5f, 571.7f, 576.0f }) { L(2063, v, 2073, v); arrow_right(2075, v); }
        c.disc(2081, 571.7f, 1.4f, WHITE); L(2082, 571, 2094, 564);
        // the little keyboard at the bottom of the box, and two wheels
        c.rect(2041, 598.5f, 2302, 635.4f, WHITE);
        for (int i = 0; i <= 49; i++) c.line(2041 + i * 261.0f / 49, 598.5f, 2041 + i * 261.0f / 49, 635.4f, 0.6f, 0x1c1c1e);
        for (int k = 0; k < 84; k++) if (black(k)) {
            float u = 2041 + (white_index(k) + (black_centre(k) - white_index(k))) * 261.0f / 49;
            c.rect(u - 1.6f, 598.5f, u + 1.6f, 622, 0x1c1c1e);
        }
        c.rect(2009, 605, 2014, 629, WHITE); c.rect(2018, 605, 2023, 629, WHITE);
        c.rect(2010, 606, 2013, 628, OEX); c.rect(2019, 606, 2022, 628, OEX);
    }

    // ---- printed legends
    for (const KbdLegend &l : KBD_LEGENDS) c.legend(l, LEGEND_COLOUR[l.colour % 3]);

    std::vector<uint32_t> out((size_t)tw * th);
    for (size_t i = 0; i < out.size(); i++) {
        const float *p = &c.px[i * 3];
        out[i] = 0xff000000u | ((uint32_t)std::clamp(p[0], 0.0f, 255.0f) << 16) | ((uint32_t)std::clamp(p[1], 0.0f, 255.0f) << 8) | (uint32_t)std::clamp(p[2], 0.0f, 255.0f);
    }
    art = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
    if (!art) return false;
    SDL_UpdateTexture(art, nullptr, out.data(), tw * 4);
    SDL_SetTextureScaleMode(art, SDL_ScaleModeLinear);

    // ---- soft glow for the LEDs
    const int gs = 32;
    std::vector<uint32_t> g(gs * gs);
    for (int y = 0; y < gs; y++) for (int x = 0; x < gs; x++) {
        float dx = (x + 0.5f) / gs * 2 - 1, dy = (y + 0.5f) / gs * 2 - 1, d = std::sqrt(dx * dx + dy * dy);
        float a = d >= 1 ? 0 : (1 - d) * (1 - d);
        g[y * gs + x] = ((uint32_t)(a * 255) << 24) | 0xffffff;
    }
    glow = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, gs, gs);
    if (!glow) return false;
    SDL_UpdateTexture(glow, nullptr, g.data(), gs * 4);
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
    return true;
}

// ------------------------------------------------------------------ primitives
void KbdUI::fill(const SDL_FRect &r, uint32_t c, uint8_t a) {
    SDL_SetRenderDrawBlendMode(R, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, a);
    SDL_RenderFillRectF(R, &r);
}
void KbdUI::vgrad(const SDL_FRect &r, uint32_t top, uint32_t bot, uint8_t a) {
    int n = std::max(1, (int)std::ceil(r.h));
    for (int i = 0; i < n; i++) {
        float y0 = r.y + r.h * i / n, y1 = r.y + r.h * (i + 1) / n;
        fill({ r.x, y0, r.w, y1 - y0 }, mix(top, bot, (i + 0.5f) / n), a);
    }
}
void KbdUI::led(float cu, float cv, float w, float h, uint32_t on, uint32_t off, bool lit) {
    SDL_FRect r = rect(cu - w / 2, cv - h / 2, cu + w / 2, cv + h / 2);
    if (!lit) { vgrad(r, mix(off, 0xffffff, 0.12f), off); fill({ r.x, r.y, r.w, K * 0.8f }, mix(off, 0xffffff, 0.3f)); return; }
    vgrad(r, mix(on, 0xffffff, 0.45f), on);
    float gw = w * 2.6f * K, gh = h * 6.0f * K;
    SDL_FRect g{ r.x + r.w / 2 - gw / 2, r.y + r.h / 2 - gh / 2, gw, gh };
    SDL_SetTextureColorMod(glow, on >> 16, (on >> 8) & 255, on & 255);
    SDL_SetTextureAlphaMod(glow, 140);
    SDL_RenderCopyF(R, glow, nullptr, &g);
}
void KbdUI::button(const Btn &b, bool pressed) {
    SDL_FRect r = rect(b.u0, b.v0, b.u1, b.v1);
    float d = pressed ? K * 1.3f : 0;
    SDL_FRect f{ r.x, r.y + d, r.w, r.h - d * 0.4f };
    if (b.look == 3) {                       // Patch Select: small square keys in the wheel well
        fill({ r.x - K, r.y - K, r.w + 2 * K, r.h + 3 * K }, 0x0d0e10);
        vgrad(f, pressed ? 0x2e3237 : 0x3d4248, pressed ? 0x26292e : 0x30343a);
        fill({ f.x, f.y, f.w, K * 1.2f }, pressed ? 0x3a3e44 : 0x50555c);
        fill({ f.x, f.y + f.h - K * 1.4f, f.w, K * 1.4f }, 0x1a1c20);
        return;
    }
    if (b.look == 1) {                       // Load / Command / Edit: lighter grey caps
        vgrad(f, pressed ? 0x44484d : 0x575c62, pressed ? 0x383b40 : 0x464a50);
        fill({ f.x, f.y, f.w, K * 1.3f }, pressed ? 0x50545a : 0x6c7178);
    } else if (b.look == 2) {                // instrument / track buttons: black with ridges across
        vgrad(f, pressed ? 0x141518 : 0x1d1e22, pressed ? 0x0e0f11 : 0x141517);
        fill({ f.x, f.y, f.w, K * 1.2f }, pressed ? 0x24262a : 0x34363b);
        for (int i = 0; i < 13; i++) {
            float y = f.y + f.h * (0.14f + 0.74f * i / 12);
            fill({ f.x + K * 2.5f, y, f.w - K * 5, K * 0.55f }, 0x08090a, 200);
            fill({ f.x + K * 2.5f, y + K * 0.55f, f.w - K * 5, K * 0.45f }, pressed ? 0x1e2024 : 0x2a2c31, 200);
        }
    } else {
        vgrad(f, pressed ? 0x24272b : 0x34383d, pressed ? 0x1b1d21 : 0x25282d);
        fill({ f.x, f.y, f.w, K * 1.3f }, pressed ? 0x2e3136 : 0x4a4e55);
    }
    fill({ f.x, f.y + f.h - K * 1.6f, f.w, K * 1.6f }, 0x0d0e10);
    fill({ f.x, f.y, K * 0.9f, f.h }, b.look == 1 ? 0x5a5f66 : 0x2c2f34);
    fill({ f.x + f.w - K * 0.9f, f.y, K * 0.9f, f.h }, 0x121316);
}
void KbdUI::slider_knob(float cu, float val) {
    float cv = VOL_BOT - (VOL_BOT - VOL_TOP) * std::clamp(val, 0.0f, 1.0f);
    SDL_FRect sh = rect(cu - 19, cv - 12, cu + 19, cv + 15);
    fill(sh, 0x000000, 150);
    SDL_FRect cap = rect(cu - 19, cv - 13.5f, cu + 19, cv + 13.5f);
    vgrad(cap, 0x2c2e33, 0x0f1012);
    // ridges on the upper half, a white line across, the lower half plain
    for (int i = 0; i < 5; i++) {
        SDL_FRect rr = rect(cu - 18, cv - 12.5f + i * 2.3f, cu + 18, cv - 11.6f + i * 2.3f);
        fill(rr, 0x3a3c42);
    }
    fill(rect(cu - 18, cv - 0.6f, cu + 18, cv + 1.2f), 0xe8e8e8);
    fill(rect(cu - 18, cv + 1.2f, cu + 18, cv + 2.2f), 0x050505);
    fill(rect(cu - 19, cv - 13.5f, cu - 18, cv + 13.5f), 0x3a3c42);
}
void KbdUI::wheel(float cu, float val, bool spring) {
    // seen from above the wheel is a dark rim in its slot, rounded off towards both ends; its
    // raised grip moves along it (the pitch wheel's grip sits in the middle when it is centred)
    (void)spring;
    const float top = 871, bot = 1013, span = bot - top, mid = (top + bot) / 2;
    for (int i = 0; i < 60; i++) {
        float v0 = top + span * i / 60, v1 = top + span * (i + 1) / 60, a = ((v0 + v1) / 2 - mid) / (span / 2);
        float w = 9.5f * std::sqrt(std::max(0.0f, 1 - a * a * 0.92f));
        float light = std::max(0.0f, 1 - std::fabs(a + 0.25f) * 1.3f);           // lit from the back a little
        fill(rect(cu - w - 1.5f, v0, cu + w * 0.6f, v1 + 0.3f), mix(0x15161a, 0x3a3e45, light));
        fill(rect(cu - w - 1.5f, v0, cu - w, v1 + 0.3f), mix(0x24262b, 0x5a5e66, light));   // the rim's edge catches the light
    }
    float gv = bot - 10 - (span - 20) * std::clamp(val, 0.0f, 1.0f);
    float a = (gv - mid) / (span / 2), w = 10.5f * std::sqrt(std::max(0.05f, 1 - a * a * 0.92f));
    vgrad(rect(cu - w - 2, gv - 7, cu + w * 0.7f, gv + 7), 0x50545c, 0x1c1e22);
    fill(rect(cu - w - 2, gv - 7, cu + w * 0.7f, gv - 6), 0x6e737c);
    fill(rect(cu - w - 2, gv + 6, cu + w * 0.7f, gv + 7.5f), 0x050506);
    fill(rect(cu - 13, 1009, cu + 13, 1014), 0x050506);                       // the slot's front lip
}
void KbdUI::key_shape(int k, float &u0, float &u1, float &v1) const {
    if (!black(k)) { u0 = KB_U0 + white_index(k) * WW + 0.45f; u1 = u0 + WW - 0.9f; v1 = WHITE_BOT; }
    else { float c = KB_U0 + black_centre(k) * WW; u0 = c - BW / 2; u1 = c + BW / 2; v1 = BLACK_BOT; }
}
void KbdUI::keyboard(const bool *down) {
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < KEYS; k++) {
            if (black(k) != (pass == 1)) continue;
            bool d = down && down[k];
            float u0, u1, v1;
            key_shape(k, u0, u1, v1);
            if (!black(k)) {
                SDL_FRect r = rect(u0, KEY_TOP, u1, v1);
                vgrad(r, d ? 0xbcbcb8 : 0xdedede, d ? 0xd2d2ce : 0xf1f1ee);
                fill(rect(u0, KEY_TOP, u1, KEY_TOP + (d ? 10 : 5)), d ? 0x88888a : 0xaaaaa8);      // shadow of the strip
                fill(rect(u0, v1 - 9, u1, v1), d ? 0xc2c2be : 0xe3e3e0);                           // front edge
                fill(rect(u0, v1 - 9, u1, v1 - 8), d ? 0xa6a6a2 : 0xcbcbc8);
                fill(rect(u1 - 0.7f, KEY_TOP, u1, v1), 0xa9a9a6);
                fill(rect(u0, KEY_TOP, u0 + 0.7f, v1), 0xf8f8f6);
            } else {
                SDL_FRect r = rect(u0, KEY_TOP, u1, v1);
                fill(rect(u0 - 1, KEY_TOP, u1 + 2.5f, v1 + 5), 0x000000, 120);                  // shadow on the white keys
                vgrad(r, d ? 0x2a2b2f : 0x1a1b1e, d ? 0x1c1d20 : 0x0c0c0e);
                fill(rect(u0 + 3, KEY_TOP, u0 + 6.5f, v1 - 14), d ? 0x4a4c52 : 0x3a3c41);            // highlight down the left side
                fill(rect(u0 + 3, v1 - 16, u1 - 3, v1 - 3), d ? 0x34363b : 0x26272b);                 // the sloping front
                fill(rect(u0 + 3, v1 - 16, u1 - 3, v1 - 15), d ? 0x55575d : 0x46484e);
            }
        }
}

// ------------------------------------------------------------------ hit testing
int KbdUI::button_at(int x, int y) const {
    for (const Btn &b : buttons) {
        SDL_FRect r = rect(b.u0 - 3, b.v0 - 3, b.u1 + 3, b.v1 + 3);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return b.idx;
    }
    return -1;
}
RackUI::Area KbdUI::area_at(int x, int y) const {
    auto in = [&](SDL_FRect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    if (in(rect(640, 368, 690, 394))) return RackUI::POWER;          // the POWER legend on the rear edge
    if (in(rect(366, 740, 394, 750))) return RackUI::EJECT;
    if (in(rect(138, 710, 410, 760))) return RackUI::DRIVE;
    return RackUI::NONE;
}
int KbdUI::key_at(int x, int y, int *velocity) const {
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < KEYS; k++) {
            if (black(k) != (pass == 0)) continue;
            float u0, u1, v1;
            key_shape(k, u0, u1, v1);
            SDL_FRect r = rect(u0 - 0.6f, KEY_TOP, u1 + 0.6f, v1);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                if (velocity) *velocity = std::clamp(30 + (int)(97 * (y - r.y) / std::max(1.0f, r.h)), 1, 127);   // lower on the key = louder
                return k;
            }
        }
    return -1;
}

// ------------------------------------------------------------------ the instrument
void KbdUI::draw(const KbdState &s, VfdRenderer &vfd) {
    const Panel &p = *s.panel;
    SDL_FRect all{ X(U0), Y(V0), (U1 - U0) * K, height() };
    SDL_RenderCopyF(R, art, nullptr, &all);

    // ---- display
    {
        SDL_FRect w = rect(1239, 475, 1599, 557);
        float vs = w.w * 0.97f / VfdRenderer::WIDTH, vh = VfdRenderer::HEIGHT * vs;
        vfd.draw(p, w.x + w.w * 0.035f, w.y + (w.h - vh) * 0.55f, s.ticks, vs, false);
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < 10; i++) {                        // a faint reflection on the upper half of the glass
            SDL_SetRenderDrawColor(R, 255, 255, 255, (Uint8)(9 - i * 0.8f));
            SDL_FRect h{ w.x, w.y + i * w.h / 20, w.w, w.h / 20 };
            SDL_RenderFillRectF(R, &h);
        }
    }
    // ---- sliders
    slider_knob(709, s.volume);
    slider_knob(1067, s.data);
    // ---- buttons
    for (const Btn &b : buttons) {
        bool held = s.held && b.idx < 64 && s.held[b.idx];
        if (b.idx == RackUI::PATCH_L) held = s.patch[0];
        if (b.idx == RackUI::PATCH_R) held = s.patch[1];
        button(b, held);
    }
    // ---- instrument LEDs: upper row LOADED (red), lower row SELECTED (amber); a blinking LED is
    //      lit while the blinking annunciators are dark
    const bool led_blink = !((s.ticks / 250) & 1);
    for (int i = 0; i < 8; i++) {
        int r = p.ind[i] % 3, yl = p.ind[8 + i] % 3;
        bool ron = r == 1 || (r == 2 && led_blink), yon = yl == 1 || (yl == 2 && led_blink);
        float bu = 1241.7f + 45.83f * i;
        led(bu + 10, 586.8f, 14.5f, 3.6f, 0xff2a1a, 0x5a4a42, ron);
        led(bu + 28, 599.3f, 14.5f, 3.6f, 0xffa020, 0x6a4f2c, yon);
    }
    // ---- drive: the disk in the slot, the activity LED, the eject button
    if (s.disk) {
        fill(rect(156, 727, 388, 739), 0x2a2c30);
        fill(rect(156, 727, 388, 729), 0x55585e);
    }
    led(352, 744.5f, 6, 2.4f, 0xffa020, 0x4a3e30, s.drive_busy);
    {
        SDL_FRect e = rect(370, 742.5f, 390, 747.5f);
        fill({ e.x - K, e.y - K, e.w + 2 * K, e.h + 2 * K }, 0x0a0b0c);
        vgrad(e, 0x4a4e55, 0x2c2f34);
    }
    // ---- wheels and keys
    wheel(240.5f, s.pitch, true);
    wheel(318.5f, s.mod, false);
    keyboard(s.keys);
}
