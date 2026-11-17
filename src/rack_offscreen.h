// Off-screen rendering of the rack front panel (no window, no SDL_Init): SDL's software renderer
// draws RackUI + the VFD into a memory surface whose pixels are handed to another toolkit
// (the VST3 editor uses it). Pixels are 32-bit 0xAARRGGBB, `width() * height()`, row by row.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "rack_ui.h"
#include "vfd.h"
#include "panel.h"

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
    // draws what changed since the last render (all of it the first time, or after invalidate());
    // returns false when nothing changed. dirty() = the rectangle that was redrawn.
    bool render(const RackState &s);
    void invalidate() { have_prev = false; }
    SDL_Rect dirty() const { return dirty_; }
    const uint32_t *pixels() const;
    int pitch() const;                      // bytes per row
    RackUI &ui() { return *rack; }

private:
    int w = 0, h = 0;
    SDL_Surface *surface = nullptr;
    SDL_Renderer *renderer = nullptr;
    std::unique_ptr<RackUI> rack;
    std::unique_ptr<VfdRenderer> vfd;
    // the state last drawn (with its own copies of the panel and the held buttons)
    bool have_prev = false;
    RackState prev;
    Panel prev_panel;
    bool prev_held[64] = {};
    SDL_Rect dirty_{ 0, 0, 0, 0 };
    std::vector<SDL_Rect> regions;
};
