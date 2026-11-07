#include "ui_text.h"
#include "ui_font.h"
#include <vector>

namespace {
// UTF-8 -> Latin-1 code points (others mapped to something close, or '?')
std::vector<int> decode(const std::string &s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        int cp, n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) { cp = ((c & 0x1f) << 6) | (s[i + 1] & 0x3f); n = 2; }
        else if ((c & 0xf0) == 0xe0 && i + 2 < s.size()) { cp = ((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f); n = 3; }
        else if ((c & 0xf8) == 0xf0 && i + 3 < s.size()) { cp = 0x10000; n = 4; }
        else { cp = c; n = 1; }      // stray Latin-1 byte
        i += n;
        if (cp == 0x2022 || cp == 0x00b7) cp = 0xb7;
        else if (cp == 0x2013 || cp == 0x2014) cp = '-';
        else if (cp == 0x2192) cp = '>';
        else if (cp == 0x2190) cp = '<';
        else if (cp == 0x2026) { out.push_back('.'); out.push_back('.'); cp = '.'; }
        else if (cp == 0x2018 || cp == 0x2019) cp = '\'';
        else if (cp == 0x201c || cp == 0x201d) cp = '"';
        if (cp < 32 || (cp > 126 && cp < 160) || cp > 255) cp = '?';
        out.push_back(cp);
    }
    return out;
}
int index_of(int cp) { return cp < 127 ? cp - 32 : 95 + (cp - 160); }
}

const UiFace *UiText::face(Face f) {
    static const UiFace *faces[N_FACES] = { &UI_SMALL, &UI_BODY, &UI_BOLD, &UI_TITLE };
    return faces[f];
}

bool UiText::init(SDL_Renderer *r) {
    R = r;
    for (int i = 0; i < N_FACES; i++) {
        const UiFace *fc = face((Face)i);
        std::vector<uint32_t> px((size_t)fc->atlas_w * fc->atlas_h);
        for (size_t k = 0; k < px.size(); k++) {
            uint8_t b = fc->alpha[k / 2];
            uint32_t a = ((k & 1) ? b >> 4 : b & 15) * 17;
            px[k] = (a << 24) | 0xffffff;
        }
        tex[i] = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, fc->atlas_w, fc->atlas_h);
        if (!tex[i]) return false;
        SDL_UpdateTexture(tex[i], nullptr, px.data(), fc->atlas_w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex[i], SDL_ScaleModeLinear);
    }
    return true;
}

UiText::~UiText() { for (auto *t : tex) if (t) SDL_DestroyTexture(t); }

float UiText::line(Face f) const { return face(f)->line / 2.0f; }

float UiText::width(Face f, const std::string &s, float spacing) const {
    const UiFace *fc = face(f);
    float w = 0; int n = 0;
    for (int cp : decode(s)) { w += fc->glyphs[index_of(cp)].adv / 2.0f + spacing; n++; }
    return n ? w - spacing : 0;
}

float UiText::draw(Face f, const std::string &s, float x, float y, uint32_t rgb, float spacing, int align, uint8_t alpha) {
    const UiFace *fc = face(f);
    float w = width(f, s, spacing);
    if (align == 1) x -= w / 2; else if (align == 2) x -= w;
    SDL_SetTextureColorMod(tex[f], rgb >> 16, (rgb >> 8) & 255, rgb & 255);
    SDL_SetTextureAlphaMod(tex[f], alpha);
    float cx = x;
    for (int cp : decode(s)) {
        const UiGlyph &g = fc->glyphs[index_of(cp)];
        if (cp != ' ') {
            SDL_Rect src{ g.x, g.y, g.w, g.h };
            SDL_FRect dst{ cx + g.ox / 2.0f, y + g.oy / 2.0f, g.w / 2.0f, g.h / 2.0f };
            SDL_RenderCopyF(R, tex[f], &src, &dst);
        }
        cx += g.adv / 2.0f + spacing;
    }
    return w;
}

std::string UiText::fit(Face f, const std::string &s, float w, float spacing) const {
    if (width(f, s, spacing) <= w) return s;
    std::string t = s;
    while (!t.empty()) {
        // drop one UTF-8 character
        size_t k = t.size() - 1;
        while (k > 0 && ((unsigned char)t[k] & 0xc0) == 0x80) k--;
        t.erase(k);
        if (width(f, t + "...", spacing) <= w) return t + "...";
    }
    return "...";
}
