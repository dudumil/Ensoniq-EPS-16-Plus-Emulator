#include "rack_offscreen.h"
#include <SDL.h>
#include <cmath>
#include <algorithm>

RackOffscreen::RackOffscreen(int width_px) {
    rack.reset(new RackUI((float)width_px));
    w = width_px;
    h = (int)std::ceil(rack->height() + RackUI::OY * width_px / 1400.0f + 2);
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    renderer = SDL_CreateSoftwareRenderer(surface);
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    vfd.reset(new VfdRenderer);
    if (!vfd->init(renderer) || !rack->init(renderer) || !rack->prescale(*vfd)) { rack.reset(); vfd.reset(); SDL_DestroyRenderer(renderer); renderer = nullptr; }
}

RackOffscreen::~RackOffscreen() {
    rack.reset(); vfd.reset();          // textures belong to the renderer: free them first
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
}

// Only the regions that changed are drawn again (with the renderer clipped to each, the whole
// panel is drawn: the faceplate, then everything on it, so each region comes out as in a full
// redraw). A full redraw of the 2x panel takes tens of milliseconds in the software renderer.
bool RackOffscreen::render(const RackState &s) {
    if (!renderer) return false;
    regions.clear();
    if (!have_prev) regions.push_back({ 0, 0, w, h });
    else rack->changed_regions(prev, s, regions);
    if (regions.empty()) { dirty_ = { 0, 0, 0, 0 }; return false; }
    // clamp to the surface; many regions: their bounding box
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (auto &r : regions) {
        const int ax = std::max(0, r.x), ay = std::max(0, r.y), bx = std::min(w, r.x + r.w), by = std::min(h, r.y + r.h);
        r = { ax, ay, std::max(0, bx - ax), std::max(0, by - ay) };
        if (r.w > 0 && r.h > 0) { x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by); }
    }
    if (x1 <= x0 || y1 <= y0) { dirty_ = { 0, 0, 0, 0 }; return false; }
    dirty_ = { x0, y0, x1 - x0, y1 - y0 };
    if (regions.size() > 8) { regions.clear(); regions.push_back(dirty_); }
    for (const SDL_Rect &r : regions) {
        if (r.w <= 0 || r.h <= 0) continue;
        SDL_RenderSetClipRect(renderer, &r);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0x0c, 0x0d, 0x0e, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        rack->draw(s, *vfd);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_RenderPresent(renderer);
    // remember what was drawn
    prev = s;
    prev_panel = *s.panel; prev.panel = &prev_panel;
    for (int i = 0; i < 64; i++) prev_held[i] = s.held && s.held[i];
    prev.held = prev_held;
    have_prev = true;
    return true;
}

const uint32_t *RackOffscreen::pixels() const { return surface ? (const uint32_t *)surface->pixels : nullptr; }
int RackOffscreen::pitch() const { return surface ? surface->pitch : 0; }
