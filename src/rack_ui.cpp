// Rack front panel drawing - see rack_ui.h.
#include "rack_ui.h"
#include "panel_art.h"
#include "panel.h"
#include "vfd.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace {
// colours of the printed legends (see tools/make_panel_art.py)
const uint32_t LEGEND_COLOUR[4] = { 0xd2d3d5, 0xd8b25a, 0x3f9bd0, 0xa7a9ad };
const uint32_t BG = 0x101112;

uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { float x = ((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t; return (uint32_t)(x + 0.5f) << s; };
    return ch(16) | ch(8) | ch(0);
}
// small deterministic noise
uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
float rnd(uint32_t x) { return (hash(x) & 0xffff) / 65535.0f; }
}

RackUI::~RackUI() {
    if (plate) SDL_DestroyTexture(plate);
    if (glow) SDL_DestroyTexture(glow);
    for (auto *t : legend) if (t) SDL_DestroyTexture(t);
}

bool RackUI::init(SDL_Renderer *r) {
    R = r;
    // ---- buttons (panel button index, rectangle measured in photo pixels)
    auto add = [&](int idx, float x0, float y0, float x1, float y1) {
        buttons.push_back({ idx, PU(x0), PV(y0), (x1 - x0) * PSCALE, (y1 - y0) * PSCALE });
    };
    const float cx[3] = { 120, 147.5f, 175 }, ry[4] = { 358, 389.5f, 421, 453 };
    const int par[12] = { 13, 18, 19, 24, 25, 30, 31, 36, 37, 32, 12, 7 };   // Env1..Layer, Sample, Track(0), Effect
    for (int i = 0; i < 12; i++) add(par[i], cx[i % 3], ry[i / 3], cx[i % 3] + 27, ry[i / 3] + 11.5f);
    add(PATCH_L, 120, 501, 147.5f, 519);                     // Patch Select left (analog input AN1, not a panel button)
    add(PATCH_R, 175, 501, 202.5f, 519);                     // Patch Select right
    // mode buttons in line with the instrument buttons below (Load/Cmd/Edit over 1-3, Inst..Effects over 5-8)
    const int mode[3] = { 26, 6, 5 };                        // Load, Cmd, Edit
    for (int i = 0; i < 3; i++) add(mode[i], 257.5f + 27.5f * i, 454, 283.5f + 27.5f * i, 465.5f);
    const int mode2[4] = { 15, 21, 27, 9 };                  // Inst, Seq/Song, System/MIDI, Effects
    for (int i = 0; i < 4; i++) add(mode2[i], 257.5f + 27.5f * (4 + i), 454, 283.5f + 27.5f * (4 + i), 465.5f);
    const int trk[8] = { 2, 8, 14, 20, 4, 34, 28, 22 };
    for (int i = 0; i < 8; i++) add(trk[i], 257.5f + 27.5f * i, 501, 283.5f + 27.5f * i, 519);
    add(3, 534, 358, 561, 369.5f);                              // Record
    add(23, 562.5f, 358, 589.5f, 369.5f);                       // Stop/Cont
    add(29, 591, 358, 618, 369.5f);                             // Play
    add(10, 562.5f, 420, 589.5f, 432);                       // up
    add(16, 535, 452.5f, 561.5f, 465);                       // left
    add(11, 563, 452.5f, 589.5f, 465);                       // down
    add(17, 591, 452.5f, 617.5f, 465);                       // right
    add(33, 535, 501, 561.5f, 519);                       // No/Cancel
    add(35, 590, 501, 616.5f, 519);                       // Yes/Enter

    // ---- brushed anodised faceplate (with the rack ears and their slots cut out)
    const int tw = (int)std::ceil(PW * K * 2), th = (int)std::ceil(PH * K * 2);
    const float ts = tw / PW;                                // texels per panel unit
    std::vector<uint32_t> px((size_t)tw * th);
    std::vector<float> rowv(th);
    for (int y = 0; y < th; y++) rowv[y] = (rnd(y * 7919u) - 0.5f) * 0.9f + (rnd(y / 3 * 104729u) - 0.5f) * 0.6f;
    for (int y = 0; y < th; y++) {
        float v = y / ts;                                    // panel units from the top
        for (int x = 0; x < tw; x++) {
            float u = x / ts + 85;
            bool ear = u < 178 || u > 1860;
            float base = ear ? 40.0f : 36.0f;
            // fine horizontal brushing: a slowly varying streak per row plus a little grain
            float streak = rowv[y] * 3.0f + (rnd(y * 131u + (x >> 5) * 977u) - 0.5f) * 1.2f;
            float grain = (rnd(x * 2654435761u ^ (y * 40503u)) - 0.5f) * 2.0f;
            float l = base + streak + grain;
            // bevels: lit top edge, dark bottom edge, groove between ears and panel
            if (v < 2.5f) l += 26 * (1 - v / 2.5f);
            if (v > PH - 3) l -= 14;
            if (std::fabs(u - 178) < 2.2f || std::fabs(u - 1860) < 2.2f) l -= 12;
            if (std::fabs(u - 181) < 1.2f || std::fabs(u - 1863) < 1.2f) l += 8;
            int c = std::clamp((int)l, 0, 255);
            uint32_t a = 255;
            // rack mounting slots (two per ear)
            const float hx[2] = { 120, 1920 }, hy[2] = { 305, 523 };
            for (float ex : hx) for (float ey : hy) {
                float dx = (u - ex) / 21.0f, dy = (v + 155 - ey) / 12.5f;
                float d = std::fabs(dx) > 0.45f ? std::sqrt((std::fabs(dx) - 0.45f) * (std::fabs(dx) - 0.45f) / (0.55f * 0.55f) + dy * dy) : std::fabs(dy);
                if (d < 1.0f) a = 0;
                else if (d < 1.25f) c = std::max(0, c - 10);
            }
            px[(size_t)y * tw + x] = (a << 24) | (uint32_t)(c << 16) | (uint32_t)(c << 8) | (uint32_t)(c + 2 > 255 ? 255 : c + 2);
        }
    }
    plate = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
    if (!plate) return false;
    SDL_UpdateTexture(plate, nullptr, px.data(), tw * 4);
    SDL_SetTextureBlendMode(plate, SDL_BLENDMODE_BLEND);

    // ---- soft round glow for the LEDs
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

    // ---- legends
    for (const PanelLegend &l : PANEL_LEGENDS) {
        std::vector<uint32_t> a((size_t)l.tw * l.th);
        for (size_t k = 0; k < a.size(); k++) {
            uint8_t b = PANEL_LEGEND_ALPHA[l.offset + k / 2];
            uint32_t v = ((k & 1) ? b >> 4 : b & 15) * 17;
            a[k] = (v << 24) | 0xffffff;
        }
        SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, l.tw, l.th);
        if (!t) return false;
        SDL_UpdateTexture(t, nullptr, a.data(), l.tw * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        uint32_t c = LEGEND_COLOUR[l.colour & 3];
        SDL_SetTextureColorMod(t, c >> 16, (c >> 8) & 255, c & 255);
        legend.push_back(t);
    }
    return true;
}

// ------------------------------------------------------------------ primitives
void RackUI::fill(const SDL_FRect &r, uint32_t c, uint8_t a) {
    SDL_SetRenderDrawBlendMode(R, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, a);
    SDL_RenderFillRectF(R, &r);
}
void RackUI::vgrad(const SDL_FRect &r, uint32_t top, uint32_t bot) {
    int n = std::max(1, (int)std::ceil(r.h));
    for (int i = 0; i < n; i++) {
        float y0 = r.y + r.h * i / n, y1 = r.y + r.h * (i + 1) / n;
        fill({ r.x, y0, r.w, y1 - y0 }, mix(top, bot, (i + 0.5f) / n));
    }
}
void RackUI::circle(float cx, float cy, float rad, uint32_t c) {
    int n = std::max(2, (int)std::ceil(rad * 2));
    for (int i = 0; i < n; i++) {
        float y = -rad + (i + 0.5f) * 2 * rad / n, hw = std::sqrt(std::max(0.0f, rad * rad - y * y));
        fill({ cx - hw, cy + y - rad / n, 2 * hw, 2 * rad / n }, c);
    }
}
void RackUI::line(float x1, float y1, float x2, float y2, uint32_t c) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(R, c >> 16, (c >> 8) & 255, c & 255, 255);
    SDL_RenderDrawLineF(R, X(x1), Y(y1), X(x2), Y(y2));
}
void RackUI::led(float cx, float cy, float w, float h, uint32_t on_colour, bool on) {
    SDL_FRect r = rect(cx - w / 2, cy - h / 2, w, h);
    fill({ r.x - K * 1.5f, r.y - K * 1.5f, r.w + K * 3, r.h + K * 3 }, 0x0a0a0b);   // bezel
    if (!on) { vgrad(r, mix(on_colour, 0x000000, 0.80f), mix(on_colour, 0x000000, 0.88f)); return; }
    vgrad(r, mix(on_colour, 0xffffff, 0.35f), on_colour);
    float gw = w * 3.2f * K, gh = h * 4.5f * K;
    SDL_FRect g{ X(cx) - gw / 2, Y(cy) - gh / 2, gw, gh };
    SDL_SetTextureColorMod(glow, on_colour >> 16, (on_colour >> 8) & 255, on_colour & 255);
    SDL_SetTextureAlphaMod(glow, 150);
    SDL_RenderCopyF(R, glow, nullptr, &g);
}
void RackUI::button(const Btn &b, bool pressed) {
    SDL_FRect r = rect(b.x, b.y, b.w, b.h);
    float d = pressed ? K * 1.2f : 0;
    fill({ r.x - K, r.y - K, r.w + 2 * K, r.h + 3.5f * K }, 0x060607);            // recess and shadow
    SDL_FRect f{ r.x, r.y + d, r.w, r.h };
    // Load, Cmd, Edit, Record, Stop, Play and Patch Select have light grey caps on the EPS-16 Plus rack
    const bool light = b.idx == 26 || b.idx == 6 || b.idx == 5 || b.idx == 3 || b.idx == 23 || b.idx == 29 || b.idx == PATCH_L || b.idx == PATCH_R;
    if (light) {
        vgrad(f, pressed ? 0x55575b : 0x6e7074, pressed ? 0x444649 : 0x56585c);
        fill({ f.x, f.y, f.w, K * 1.3f }, pressed ? 0x606266 : 0x8a8c90);
    } else {
        vgrad(f, pressed ? 0x1f2022 : 0x313236, pressed ? 0x141516 : 0x1b1c1e);
        fill({ f.x, f.y, f.w, K * 1.3f }, pressed ? 0x2a2b2e : 0x4a4c51);             // lit top edge
    }
    fill({ f.x, f.y + f.h - K * 1.6f, f.w, K * 1.6f }, 0x0e0f10);                 // lower edge
    fill({ f.x, f.y, K, f.h }, 0x26272a);
    fill({ f.x + f.w - K, f.y, K, f.h }, 0x111214);
}
RackUI::Slider RackUI::slider(float cx, float top, float bot) const {
    return { X(cx - 28), Y(top), 56 * K, (bot - top) * K };
}
void RackUI::slider_knob(float cx, float top, float bot, float val) {
    // slot
    SDL_FRect s = rect(cx - 6, top - 22, 12, bot - top + 44);   // the knob never sticks out of the slot
    fill({ s.x - K, s.y - K, s.w + 2 * K, s.h + 2 * K }, 0x2a2b2e);
    fill(s, 0x030303);
    // cap
    float cy = bot - (bot - top) * std::clamp(val, 0.0f, 1.0f);
    SDL_FRect c = rect(cx - 27, cy - 20, 54, 40);
    fill({ c.x, c.y + K * 2, c.w, c.h + K * 2 }, 0x050505);
    vgrad(c, 0x3a3b3f, 0x141517);
    fill({ c.x, c.y, c.w, K * 1.4f }, 0x5a5c61);
    SDL_FRect mid = rect(cx - 22, cy - 1.6f, 44, 3.2f);
    fill(mid, 0xe8e8e8);
}

// ------------------------------------------------------------------ hit testing
int RackUI::button_at(int x, int y) const {
    for (const Btn &b : buttons) {
        SDL_FRect r = rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return b.idx;
    }
    return -1;
}
RackUI::Area RackUI::area_at(int x, int y) const {
    auto in = [&](SDL_FRect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    if (in(prect(33, 418, 63, 472))) return POWER;
    if (in(prect(721, 375, 734, 402))) return EJECT;
    if (in(prect(681.5f, 352.5f, 737.5f, 540))) return DRIVE;
    return NONE;
}

// ------------------------------------------------------------------ the panel
void RackUI::draw(const RackState &s, VfdRenderer &vfd) {
    const Panel &p = *s.panel;
    const bool blink = (s.ticks / 250) & 1;
    // faceplate
    SDL_FRect pl{ X(85), Y(155), PW * K, PH * K };
    SDL_RenderCopyF(R, plate, nullptr, &pl);
    // screw heads in the ear slots are not drawn: the slots show what is behind the rack

    // ---- power switch, phones jack, volume
    {   // rocker switch
        SDL_FRect o = prect(35, 420, 61, 470);
        fill({ o.x - K * 2, o.y - K * 2, o.w + K * 4, o.h + K * 4 }, 0x2c2d30);
        fill(o, 0x060606);
        SDL_FRect top = prect(37.5f, 422.5f, 58.5f, 445), bot = prect(37.5f, 445, 58.5f, 467.5f);
        if (s.power_held) { vgrad(top, 0x1a1b1d, 0x222326); vgrad(bot, 0x2a2b2e, 0x101113); }
        else { vgrad(top, 0x3a3b3f, 0x202124); vgrad(bot, 0x141516, 0x0c0c0d); }
        fill(prect(37.5f, 444.5f, 58.5f, 445.2f), 0x050505);
    }
    {
        float jx = X(PU(49)), jy = Y(PV(509));
        circle(jx, jy, 17 * K, 0x6f7175);
        circle(jx, jy, 14 * K, 0xa9abb0);
        circle(jx, jy, 10 * K, 0x2a2b2d);
        circle(jx, jy, 7 * K, 0x020202);
    }
    slider_knob(VOL_X, VOL_TOP, VOL_BOT, s.volume);

    // ---- buttons
    for (const Btn &b : buttons) button(b, s.held && s.held[b.idx & 63]);

    // ---- instrument LEDs (bicolour): red = loaded, amber = selected. A blinking LED (e.g. the
    //      instrument being loaded) is lit while the blinking annunciators on the display are dark
    const bool led_blink = !blink;
    for (int i = 0; i < 8; i++) {
        int r = p.ind[i] % 3, yl = p.ind[8 + i] % 3;
        bool ron = r == 1 || (r == 2 && led_blink), yon = yl == 1 || (yl == 2 && led_blink);
        led(PU(270.5f + 27.5f * i), PV(489), 13, 9, yon ? 0xffa020 : 0xff2a1a, ron || yon);
    }
    // purple rules: Page / track numbers / Data Entry, Sequencer
    {
        const uint32_t PURPLE = 0x6c3a72;
        auto hr = [&](float x0, float x1, float y) { fill(prect(x0, y - 0.35f, x1, y + 0.35f), PURPLE); };
        hr(60, 143, 531); hr(179, 265, 531);
        for (int i = 0; i < 7; i++) hr(275 + 27.5f * i, 293.5f + 27.5f * i, 531);
        hr(468, 470, 531); hr(470, 547, 531); hr(606, 672.5f, 531);
        hr(534, 550, 379.5f); hr(602, 672.5f, 379.5f);
    }
    // arrow triangles (outlines) above / beside the cursor buttons
    auto tri = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
        x1 = PU(x1); x2 = PU(x2); x3 = PU(x3); y1 = PV(y1); y2 = PV(y2); y3 = PV(y3);
        SDL_Color c{ 0xd2, 0xd3, 0xd5, 255 }, b{ 0x1a, 0x1b, 0x1d, 255 };
        float cx = (x1 + x2 + x3) / 3, cy = (y1 + y2 + y3) / 3, sh = 0.6f;
        SDL_Vertex v[6] = {
            { { X(x1), Y(y1) }, c, {0, 0} }, { { X(x2), Y(y2) }, c, {0, 0} }, { { X(x3), Y(y3) }, c, {0, 0} },
            { { X(cx + (x1 - cx) * sh), Y(cy + (y1 - cy) * sh) }, b, {0, 0} },
            { { X(cx + (x2 - cx) * sh), Y(cy + (y2 - cy) * sh) }, b, {0, 0} },
            { { X(cx + (x3 - cx) * sh), Y(cy + (y3 - cy) * sh) }, b, {0, 0} } };
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
        SDL_RenderGeometry(R, nullptr, v, 3, nullptr, 0);
        SDL_RenderGeometry(R, nullptr, v + 3, 3, nullptr, 0);
    };
    tri(576, 411.5f, 571.5f, 416.5f, 580.5f, 416.5f);      // up
    tri(576, 448, 571.5f, 443, 580.5f, 443);               // down
    tri(542.5f, 443.5f, 547.5f, 439, 547.5f, 448);         // left
    tri(607.5f, 443.5f, 602.5f, 439, 602.5f, 448);         // right

    // ---- display window: smoked glass with the VFD behind it
    {
        SDL_FRect w = prect(227.5f, 362.5f, 506.5f, 425);
        fill({ w.x - K * 2, w.y - K * 2, w.w + K * 4, w.h + K * 4 }, 0x2b2c2f);
        fill(w, 0x030405);
        float vs = (w.w - 34 * K) / VfdRenderer::WIDTH;
        float vh = VfdRenderer::HEIGHT * vs;
        vfd.draw(p, w.x + 17 * K, w.y + (w.h - vh) / 2, s.ticks, vs, false);
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < 12; i++) {
            SDL_SetRenderDrawColor(R, 255, 255, 255, (Uint8)(10 - i * 0.8f));
            SDL_FRect h{ w.x, w.y + i * w.h / 24, w.w, w.h / 24 };
            SDL_RenderFillRectF(R, &h);
        }
    }

    // ---- data entry slider
    slider_knob(DATA_X, DATA_TOP, DATA_BOT, s.data);

    // ---- floppy drive (mounted vertically)
    {
        fill(prect(680.5f, 351.5f, 738.5f, 541), 0x2c2d31);
        fill(prect(681.5f, 352.5f, 737.5f, 540), 0x070708);
        vgrad(prect(684, 355, 735, 537.5f), 0x1e1f22, 0x18191b);
        fill(prect(690, 360, 718, 532), 0x101113);           // slot recess
        fill(prect(698, 362.5f, 709, 527), 0x050506);        // the disk opening
        if (s.disk) {
            vgrad(prect(700, 365, 707, 524), 0x8f9196, 0x6c6e72);
            fill(prect(700, 365, 701, 524), 0xb0b2b6);
        }
        // eject button
        fill(prect(721, 375, 734, 402), 0x0c0c0d);
        vgrad(prect(722, 376, 733, 401), 0x34353a, 0x26272b);
        fill(prect(722, 376, 733, 377), 0x55565b);
        // activity LED
        fill(prect(724.5f, 478, 731, 492), 0x1a1b1d);
        led(PU(727.75f), PV(485), 6, 20, 0xffa020, s.drive_busy);
    }

    // ---- SCSI activity LED over the floppy drive: lit for a moment after each bus activity
    if (!scsi_seen_init) { scsi_seen = s.scsi_activity; scsi_seen_init = true; }
    if (s.scsi_activity != scsi_seen) { scsi_seen = s.scsi_activity; scsi_seen_t = s.ticks; scsi_hit = true; }
    const bool scsi_lit = scsi_hit && (uint32_t)(s.ticks - scsi_seen_t) < 70;
    led(PU(709.5f), PV(340), 14, 8, 0xff2a1a, scsi_lit);

    // ---- legends
    int i = 0;
    for (const PanelLegend &l : PANEL_LEGENDS) {
        SDL_FRect d{ X(l.x), Y(l.y), l.w * K, l.h * K };
        SDL_RenderCopyF(R, legend[i++], nullptr, &d);
    }
    (void)BG;
}
