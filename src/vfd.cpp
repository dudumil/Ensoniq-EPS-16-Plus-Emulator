// ASR-10 VFD drawing - see vfd.h.
#include "vfd.h"
#include "vfd_art.h"
#include "panel.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {
constexpr int SS = 3;                       // texels per logical pixel
constexpr float CW = 30, CH = 32;           // logical size of one character texture
constexpr float GX = 2, GY = 2;             // glyph origin inside the texture
constexpr float PITCH = 26;                 // character pitch (logical px)
// glyph geometry (logical px), upright before the slant is applied
constexpr float GW = 13.4f, GH = 21.5f, SLANT = 0.11f, T = 1.8f;
constexpr float L = T / 2, Rr = GW - T / 2, Tt = T / 2, B = GH - T / 2, M = GH / 2, C = GW / 2;

const uint32_t LIT = 0x6ff5ea, GHOST = 0x071716, LEG_GHOST = 0x0b2120;

struct Pt { float x, y; };
using Poly = std::vector<Pt>;

Pt shear(Pt p) { return { GX + p.x + (GH - p.y) * SLANT, GY + p.y }; }

// a bar between two points with pointed ends, shortened by `gap` at both ends
Poly bar(Pt p, Pt q, float t, float gap) {
    float dx = q.x - p.x, dy = q.y - p.y, len = std::sqrt(dx * dx + dy * dy);
    dx /= len; dy /= len;
    float nx = -dy, ny = dx, h = t / 2;
    Pt a{p.x + dx * gap, p.y + dy * gap}, b{q.x - dx * gap, q.y - dy * gap};
    Poly r = { a, {a.x + dx * h + nx * h, a.y + dy * h + ny * h}, {b.x - dx * h + nx * h, b.y - dy * h + ny * h},
               b, {b.x - dx * h - nx * h, b.y - dy * h - ny * h}, {a.x + dx * h - nx * h, a.y + dy * h - ny * h} };
    for (auto &v : r) v = shear(v);
    return r;
}

bool inside(const Poly &p, float x, float y) {        // convex polygon, either winding
    int pos = 0, neg = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const Pt &a = p[i], &b = p[(i + 1) % p.size()];
        float c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (c > 0) pos++; else if (c < 0) neg++;
    }
    return !(pos && neg);
}

// segment n (0..14 as in VFD_CHARSET, 15 = underline): coverage at logical point (x, y)
bool covers(int n, float x, float y) {
    static std::vector<Poly> polys;
    if (polys.empty()) {
        const float g = 0.45f, gd = 1.6f, td = 1.55f;
        polys = {
            bar({L, Tt}, {Rr, Tt}, T, g),  bar({Rr, Tt}, {Rr, M}, T, g), bar({Rr, M}, {Rr, B}, T, g),   // a b c
            bar({L, B}, {Rr, B}, T, g),    bar({L, M}, {L, B}, T, g),    bar({L, Tt}, {L, M}, T, g),    // d e f
            bar({L, M}, {C, M}, T, g),     bar({C, M}, {Rr, M}, T, g),                                  // g1 g2
            bar({L, Tt}, {C, M}, td, gd),  bar({C, Tt}, {C, M}, T, g),   bar({Rr, Tt}, {C, M}, td, gd), // h i j
            bar({C, M}, {L, B}, td, gd),   bar({C, M}, {C, B}, T, g),    bar({C, M}, {Rr, B}, td, gd),  // k l m
        };
    }
    if (n < 14) return inside(polys[n], x, y);
    if (n == 14) { Pt c = shear({Rr + 2.4f, B}); return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= 0.95f * 0.95f; }
    return x >= GX - 0.4f && x <= GX + GW + 2.4f && y >= GY + GH + 3.0f && y <= GY + GH + 4.4f;   // underline
}

// box blur of an alpha plane (glow)
std::vector<uint8_t> blur(const std::vector<uint8_t> &a, int w, int h, int rad, int passes) {
    std::vector<float> f(a.begin(), a.end()), t(f.size());
    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            float s = 0; int n = 0;
            for (int k = -rad; k <= rad; k++) { int xx = x + k; if (xx >= 0 && xx < w) { s += f[y * w + xx]; n++; } }
            t[y * w + x] = s / (2 * rad + 1);
        }
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            float s = 0;
            for (int k = -rad; k <= rad; k++) { int yy = y + k; if (yy >= 0 && yy < h) s += t[yy * w + x]; }
            f[y * w + x] = s / (2 * rad + 1);
        }
    }
    std::vector<uint8_t> r(a.size());
    for (size_t i = 0; i < r.size(); i++) r[i] = (uint8_t)std::min(255.0f, f[i] * 2.2f);
    return r;
}

SDL_Texture *alpha_texture(SDL_Renderer *R, const std::vector<uint8_t> &a, int w, int h) {
    std::vector<uint32_t> px(a.size());
    for (size_t i = 0; i < a.size(); i++) px[i] = ((uint32_t)a[i] << 24) | 0xffffff;
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

void color(SDL_Texture *t, uint32_t c, uint8_t a = 255) {
    SDL_SetTextureColorMod(t, c >> 16, (c >> 8) & 255, c & 255);
    SDL_SetTextureAlphaMod(t, a);
}
}

VfdRenderer::~VfdRenderer() {
    for (auto *t : seg) if (t) SDL_DestroyTexture(t);
    for (auto *t : seg_glow) if (t) SDL_DestroyTexture(t);
    for (auto *t : leg) if (t) SDL_DestroyTexture(t);
    for (auto *t : leg_glow) if (t) SDL_DestroyTexture(t);
}

bool VfdRenderer::init(SDL_Renderer *r) {
    R = r;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    const int w = (int)(CW * SS), h = (int)(CH * SS);
    for (int n = 0; n < NSEG; n++) {
        std::vector<uint8_t> a(w * h);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int c = 0;
            for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++)
                c += covers(n, (x + (sx + 0.5f) / 4) / SS, (y + (sy + 0.5f) / 4) / SS);
            a[y * w + x] = (uint8_t)(c * 255 / 16);
        }
        seg[n] = alpha_texture(R, a, w, h);
        seg_glow[n] = alpha_texture(R, blur(a, w, h, 3, 2), w, h);
        if (!seg[n] || !seg_glow[n]) return false;
    }
    nleg = (int)(sizeof(VFD_LEGENDS) / sizeof(VFD_LEGENDS[0]));
    for (int i = 0; i < nleg && i < 32; i++) {
        const VfdLegend &l = VFD_LEGENDS[i];
        const int pad = 3 * SS;               // room for the glow
        const int tw = l.tw + 2 * pad, th = l.th + 2 * pad;
        std::vector<uint8_t> a(tw * th, 0);
        for (int y = 0; y < l.th; y++) for (int x = 0; x < l.tw; x++) {
            int k = y * l.tw + x;
            uint8_t b = VFD_LEGEND_ALPHA[l.offset + k / 2];
            a[(y + pad) * tw + x + pad] = (uint8_t)(((k & 1) ? b >> 4 : b & 15) * 17);
        }
        leg[i] = alpha_texture(R, a, tw, th);
        leg_glow[i] = alpha_texture(R, blur(a, tw, th, 3, 2), tw, th);
        if (!leg[i] || !leg_glow[i]) return false;
    }
    return true;
}

void VfdRenderer::cell(const Panel &p, int i, float x, float y, float s) {
    unsigned char ch = (unsigned char)p.disp[i];
    uint16_t mask = (ch >= 0x20 && ch < 0x60) ? VFD_CHARSET[ch - 0x20] : 0;
    if (ch == ' ' && (i < p.meter || (p.meter_peak > 0 && i == p.meter_peak - 1))) mask = 0x1236;   // level meter (b c e f i l), with the peak-hold cell
    uint32_t m = mask | ((p.attr[i] & 3) ? 1u << 15 : 0);
    SDL_FRect dst{x - GX * s, y - GY * s, CW * s, CH * s};
    for (int n = 0; n < NSEG; n++) {
        if (m & (1u << n)) continue;
        color(seg[n], GHOST); SDL_RenderCopyF(R, seg[n], nullptr, &dst);
    }
    for (int n = 0; n < NSEG; n++) {
        if (!(m & (1u << n))) continue;
        color(seg_glow[n], LIT, 110); SDL_SetTextureBlendMode(seg_glow[n], SDL_BLENDMODE_ADD);
        SDL_RenderCopyF(R, seg_glow[n], nullptr, &dst);
        color(seg[n], LIT); SDL_RenderCopyF(R, seg[n], nullptr, &dst);
    }
}

void VfdRenderer::draw(const Panel &p, float x, float y, uint32_t ticks, float s, bool glass) {
    const bool blink = (ticks / 250) & 1;
    if (glass) {
        SDL_FRect g{x, y, WIDTH * s, HEIGHT * s};
        SDL_SetRenderDrawColor(R, 0x04, 0x07, 0x08, 255); SDL_RenderFillRectF(R, &g);
        SDL_SetRenderDrawColor(R, 0x50, 0x58, 0x5f, 255); SDL_RenderDrawRectF(R, &g);
    }
    const float x0 = x + 14 * s, ly = y + 9 * s, cy = y + 61 * s;    // character 0, first legend row, character top
    // annunciators
    for (int i = 0; i < nleg; i++) {
        const VfdLegend &l = VFD_LEGENDS[i];
        int st = (l.group == 0 ? p.ind2 : p.ind3)[l.index & 15] % 3;
        bool on = st == 1 || (st == 2 && blink);
        const float pad = 3;
        SDL_FRect d{x0 + (l.x - pad) * s, ly + (l.y - pad) * s, (l.w + 2 * pad) * s, (l.h + 2 * pad) * s};
        if (on) {
            color(leg_glow[i], LIT, 80); SDL_SetTextureBlendMode(leg_glow[i], SDL_BLENDMODE_ADD);
            SDL_RenderCopyF(R, leg_glow[i], nullptr, &d);
            color(leg[i], LIT);
        } else color(leg[i], LEG_GHOST);
        SDL_RenderCopyF(R, leg[i], nullptr, &d);
    }
    // characters
    for (int i = 0; i < Panel::COLS; i++) cell(p, i, x0 + i * PITCH * s, cy, s);
}
