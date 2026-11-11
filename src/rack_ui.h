// Front panel of the EPS-16 Plus rack for the SDL front end: faceplate, buttons, LEDs,
// sliders, display window, floppy drive and the printed legends.
//
// Layout is kept in "panel units" (a 1870 x 515 panel with the rack ears); the positions were
// measured on a photo of the rack front and are written in photo pixels through PU()/PV().
#pragma once
#include <SDL.h>
#include <cstdint>
#include <vector>

class Panel;
class VfdRenderer;

// photo pixel -> panel unit (the faceplate spans x 12..764, y 330..557 on the photo)
constexpr float PSCALE = 2.237f;
constexpr float PU(float px) { return 178 + (px - 12) * PSCALE; }
constexpr float PV(float py) { return 155 + (py - 330) * PSCALE; }

struct RackState {
    const Panel *panel = nullptr;
    uint32_t ticks = 0;
    const bool *held = nullptr;        // held[panel button index], 64 entries
    float volume = 1, data = 0.5f;     // slider positions 0..1 (1 = top)
    bool disk = false, drive_busy = false;
    uint32_t scsi_activity = 0;        // SCSI bus activity counter: the LED over the floppy drive blinks when it changes
    bool power_held = false;
};

class RackUI {
public:
    static constexpr float PW = 1870, PH = 515;          // panel units
    static constexpr float OX = 0, OY = 4;               // where the panel goes in the window
    explicit RackUI(float width) : K(width / PW) {}
    float height() const { return PH * K; }
    float X(float u) const { return OX + (u - 85) * K; }
    float Y(float v) const { return OY + (v - 155) * K; }

    bool init(SDL_Renderer *r);
    ~RackUI();
    void draw(const RackState &s, VfdRenderer &vfd);

    // hit testing (logical window coordinates)
    int button_at(int x, int y) const;                   // panel button index, PATCH_L/PATCH_R or -1
    static constexpr int PATCH_L = 40, PATCH_R = 41;     // Patch Select: a resistor network read on AN1
    enum Area { NONE, POWER, EJECT, DRIVE };
    Area area_at(int x, int y) const;
    struct Slider { float x, y, w, h; };
    Slider volume_slider() const { return slider(VOL_X, VOL_TOP, VOL_BOT); }
    Slider data_slider() const { return slider(DATA_X, DATA_TOP, DATA_BOT); }

private:
    float K;
    uint32_t scsi_seen = 0, scsi_seen_t = 0;   // last activity count and when it changed (LED hold)
    bool scsi_seen_init = false, scsi_hit = false;
    SDL_Renderer *R = nullptr;
    SDL_Texture *plate = nullptr, *glow = nullptr;
    std::vector<SDL_Texture *> legend;

    struct Btn { int idx; float x, y, w, h; };
    std::vector<Btn> buttons;
    static constexpr float VOL_X = PU(85), VOL_TOP = PV(428), VOL_BOT = PV(500);
    static constexpr float DATA_X = PU(653), DATA_TOP = PV(428), DATA_BOT = PV(500);

    SDL_FRect rect(float x, float y, float w, float h) const { return { X(x), Y(y), w * K, h * K }; }
    SDL_FRect prect(float x0, float y0, float x1, float y1) const { return rect(PU(x0), PV(y0), (x1 - x0) * PSCALE, (y1 - y0) * PSCALE); }
    Slider slider(float cx, float top, float bot) const;
    void fill(const SDL_FRect &r, uint32_t c, uint8_t a = 255);
    void vgrad(const SDL_FRect &r, uint32_t top, uint32_t bot);
    void circle(float cx, float cy, float rad, uint32_t c);
    void line(float x1, float y1, float x2, float y2, uint32_t c);
    void led(float cx, float cy, float w, float h, uint32_t on_colour, bool on);
    void button(const Btn &b, bool pressed);
    void slider_knob(float cx, float top, float bot, float val);
};
