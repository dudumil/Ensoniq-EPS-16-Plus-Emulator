#include "vfd.h"
#include "panel.h"
#include <SDL.h>
#include <cstring>
#include <cstdio>
int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO);
    const float s = 3.0f;
    int W = (int)(VfdRenderer::WIDTH * s), H = (int)(VfdRenderer::HEIGHT * s);
    SDL_Window *w = SDL_CreateWindow("x", 0, 0, W, H, 0);
    SDL_Renderer *R = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    VfdRenderer v; v.init(R);
    Panel p; p.reset();
    memset(p.disp, ' ', sizeof p.disp);
    const char *t = argv[1]; memcpy(p.disp + atoi(argv[2]), t, strlen(t));
    SDL_SetRenderDrawColor(R, 3, 4, 5, 255); SDL_RenderClear(R);
    v.draw(p, 0, 0, 0, s, false);
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(R, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
    SDL_SaveBMP(sf, argv[3]);
    return 0;
}
