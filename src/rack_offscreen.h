// Off-screen rendering of the rack front panel (no window, no SDL_Init): SDL's software renderer
// draws RackUI + the VFD into a memory surface whose pixels are handed to another toolkit
// (the VST3 editor uses it). Pixels are 32-bit 0xAARRGGBB, `width() * height()`, row by row.
#pragma once
#include <cstdint>
#include <memory>
#include "rack_ui.h"
#include "vfd.h"

struct SDL_Surface;
struct SDL_Renderer;

class RackOffscreen {
public:
    // width_px = width of the rendered panel in pixels (1400 = 1:1 with the stand-alone window)
    explicit RackOffscreen(int width_px);
    ~RackOffscreen();
    bool ok() const { return renderer != nullptr; }
    int width() const { return w; }
    int height() const { return h; }
    void render(const RackState &s);
    const uint32_t *pixels() const;
    int pitch() const;                      // bytes per row
    RackUI &ui() { return *rack; }

private:
    int w = 0, h = 0;
    SDL_Surface *surface = nullptr;
    SDL_Renderer *renderer = nullptr;
    std::unique_ptr<RackUI> rack;
    std::unique_ptr<VfdRenderer> vfd;
};
