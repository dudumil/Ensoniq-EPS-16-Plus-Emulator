#include "rack_offscreen.h"
#include <SDL.h>
#include <cmath>

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
    if (!vfd->init(renderer) || !rack->init(renderer)) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
}

RackOffscreen::~RackOffscreen() {
    rack.reset(); vfd.reset();          // textures belong to the renderer: free them first
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
}

void RackOffscreen::render(const RackState &s) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, 0x0c, 0x0d, 0x0e, 255);
    SDL_RenderClear(renderer);
    rack->draw(s, *vfd);
    SDL_RenderPresent(renderer);
}

const uint32_t *RackOffscreen::pixels() const { return surface ? (const uint32_t *)surface->pixels : nullptr; }
int RackOffscreen::pitch() const { return surface ? surface->pitch : 0; }
