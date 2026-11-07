// Anti-aliased text for the options page (FreeSans atlases from ui_font.h), Latin-1 + UTF-8 input.
#pragma once
#include <SDL.h>
#include <string>
#include <cstdint>

struct UiFace;
class UiText {
public:
    enum Face { SMALL, BODY, BOLD, TITLE, N_FACES };
    bool init(SDL_Renderer *r);
    ~UiText();
    // draws with the top of the line at y; align 0 = left, 1 = centre, 2 = right; returns the width
    float draw(Face f, const std::string &utf8, float x, float y, uint32_t rgb, float spacing = 0, int align = 0, uint8_t alpha = 255);
    float width(Face f, const std::string &utf8, float spacing = 0) const;
    float line(Face f) const;
    // shortens the text with "..." until it fits in w
    std::string fit(Face f, const std::string &utf8, float w, float spacing = 0) const;
private:
    SDL_Renderer *R = nullptr;
    SDL_Texture *tex[N_FACES] = {};
    static const UiFace *face(Face f);
};
