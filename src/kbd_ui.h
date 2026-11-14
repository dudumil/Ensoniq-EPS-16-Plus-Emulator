// "Keyboard" skin of the SDL front end: the top view of an EPS-16 Plus keyboard (with an OEX-6
// output expander on the right), drawn after a photo of one: rear edge, disk well and drive,
// Patch Select and wheels, the front panel, the 61-note keyboard.
//
// Layout is kept in "photo units" (pixels of the photo: the instrument spans x 60..2404,
// y 343..1102). The photo was taken from a little in front, so it is a little short from back to
// front: VS stretches it back to the unit's proportions.
#pragma once
#include "rack_ui.h"
#include <SDL.h>
#include <cstdint>
#include <vector>

class Panel;
class VfdRenderer;

struct KbdState : RackState {
    const bool *keys = nullptr;        // 61 keys, C2..C7
    float pitch = 0.5f, mod = 0;       // wheels 0..1 (pitch: 0.5 = centre)
    bool patch[2] = { false, false };  // Patch Select held
};

class KbdUI {
public:
    static constexpr float U0 = 60, U1 = 2404, V0 = 343, V1 = 1102, VS = 1.10f;
    static constexpr int KEYS = 61;
    explicit KbdUI(float width) : K(width / (U1 - U0)) {}
    ~KbdUI();
    float height() const { return (V1 - V0) * VS * K; }
    float X(float u) const { return (u - U0) * K; }
    float Y(float v) const { return (v - V0) * VS * K; }

    bool init(SDL_Renderer *r);
    void draw(const KbdState &s, VfdRenderer &vfd);

    // hit testing (logical window coordinates); button indices as RackUI (PATCH_L/PATCH_R too)
    int button_at(int x, int y) const;
    RackUI::Area area_at(int x, int y) const;
    int key_at(int x, int y, int *velocity = nullptr) const;
    // travel of the sliders and wheels (window coordinates): value = 1 at the top
    RackUI::Slider volume_slider() const { return travel(709, VOL_TOP, VOL_BOT, 50); }
    RackUI::Slider data_slider() const { return travel(1067, VOL_TOP, VOL_BOT, 50); }
    RackUI::Slider pitch_wheel() const { return travel(240.5f, WHEEL_TOP, WHEEL_BOT, 30); }
    RackUI::Slider mod_wheel() const { return travel(318.5f, WHEEL_TOP, WHEEL_BOT, 30); }

private:
    float K;
    SDL_Renderer *R = nullptr;
    SDL_Texture *art = nullptr, *glow = nullptr;
    struct Btn { int idx; float u0, v0, u1, v1; int look; };   // look: 0 dark, 1 light grey, 2 ribbed (instruments), 3 small square
    std::vector<Btn> buttons;
    static constexpr float VOL_TOP = 502, VOL_BOT = 614;        // centre of the slider knob at the ends
    static constexpr float WHEEL_TOP = 878, WHEEL_BOT = 1006;

    RackUI::Slider travel(float cu, float top, float bot, float w) const { return { X(cu - w / 2), Y(top), w * K, (bot - top) * VS * K }; }
    SDL_FRect rect(float u0, float v0, float u1, float v1) const { return { X(u0), Y(v0), (u1 - u0) * K, (v1 - v0) * VS * K }; }
    void fill(const SDL_FRect &r, uint32_t c, uint8_t a = 255);
    void vgrad(const SDL_FRect &r, uint32_t top, uint32_t bot, uint8_t a = 255);
    void button(const Btn &b, bool pressed);
    void led(float cu, float cv, float w, float h, uint32_t on, uint32_t off, bool lit);
    void slider_knob(float cu, float val);
    void wheel(float cu, float val, bool spring);
    void keyboard(const bool *down);
    void key_shape(int k, float &u0, float &u1, float &v1) const;
};
