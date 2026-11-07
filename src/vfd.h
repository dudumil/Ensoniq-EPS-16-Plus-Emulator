// Drawing of the ASR-10 vacuum fluorescent display for the SDL front end:
// 22 slanted 15-segment characters (14 segments + period) with an underline segment each,
// and the 29 annunciator legends in the upper half of the glass.
#pragma once
#include <SDL.h>
#include <cstdint>

class Panel;

class VfdRenderer {
public:
    bool init(SDL_Renderer *r);                 // builds the segment and legend textures
    // x,y = top-left of the glass; s = scale (1 = WIDTH x HEIGHT logical px); glass = draw the
    // dark glass rectangle behind the digits
    void draw(const Panel &p, float x, float y, uint32_t ticks, float s = 1.0f, bool glass = true);
    static constexpr float WIDTH = 590, HEIGHT = 100;
    ~VfdRenderer();

private:
    SDL_Renderer *R = nullptr;
    static constexpr int NSEG = 16;            // 15 character segments + underline
    SDL_Texture *seg[NSEG] = {}, *seg_glow[NSEG] = {};
    SDL_Texture *leg[32] = {}, *leg_glow[32] = {};
    int nleg = 0;
    void cell(const Panel &p, int i, float x, float y, float s);
};
