#include "eps16_editor.h"
#include "efe_disk.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include <cstring>
#include <chrono>
#include <algorithm>
#include <functional>
#include <filesystem>

namespace Eps16Vst {

using namespace VSTGUI;

static uint32_t now_ms() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ drop target
class RackDrop : public NonAtomicReferenceCounted, public DropTargetAdapter {
public:
    explicit RackDrop(Plugin *p) : plug(p) {}
    DragOperation onDragEnter(DragEventData d) override { return has_file(d) ? DragOperation::Copy : DragOperation::None; }
    DragOperation onDragMove(DragEventData d) override { return has_file(d) ? DragOperation::Copy : DragOperation::None; }
    bool onDrop(DragEventData d) override {
        bool any = false;
        std::vector<std::string> efes;   // instrument files dropped together go on one floppy
        for (uint32_t i = 0; i < d.drag->getCount(); i++) {
            const void *buf = nullptr; IDataPackage::Type t;
            d.drag->getData(i, buf, t);
            if (t != IDataPackage::kFilePath || !buf) continue;
            std::string f = (const char *)buf;
            if (efe::is_single_file(f)) efes.push_back(f); else plug->engine().insert_disk(f);
            any = true;
        }
        if (!efes.empty()) plug->engine().insert_files(efes);
        return any;
    }
private:
    static bool has_file(DragEventData d) {
        for (uint32_t i = 0; d.drag && i < d.drag->getCount(); i++) if (d.drag->getDataType(i) == IDataPackage::kFilePath) return true;
        return false;
    }
    Plugin *plug;
};

// ------------------------------------------------------------------ rack view
class RackView : public CView {
public:
    RackView(const CRect &r, Plugin *p, int render_px)
        : CView(r), plug(p), off(render_px) {
        bmp = makeOwned<CBitmap>(CPoint(r.getWidth(), r.getHeight()), (double)render_px / r.getWidth());
        drop = makeOwned<RackDrop>(p);
    }
    bool ready() const { return off.ok() && bmp && bmp->getPlatformBitmap(); }

    void update(const EngineSnapshot &snap) {
        if (!ready()) return;
        bool held[64] = {};
        if (held_button >= 0 && held_button < 64) held[held_button] = true;
        if (arrows_both) held[10] = held[11] = true;
        if (rec_latched) held[3] = true;
        RackState rs;
        rs.panel = &snap.panel; rs.ticks = now_ms(); rs.held = held;
        rs.volume = (float)volume_value(); rs.data = (float)data;
        rs.disk = snap.disk; rs.drive_busy = snap.drive_busy;
        rs.power_held = power_held;
        off.render(rs);
        copy_pixels();
        invalid();
    }

    void draw(CDrawContext *ctx) override {
        if (bmp) bmp->draw(ctx, getViewSize());
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent &e) override {
        CPoint p = to_px(e.mousePosition);
        RackUI &ui = off.ui();
        int bi = ui.button_at((int)p.x, (int)p.y);
        Eps16Engine &eng = plug->engine();
        if (!e.buttonState.isLeft()) {
            // right click on an arrow = both arrows: the OS jumps to the middle of the range
            if (e.buttonState.isRight() && (bi == 10 || bi == 11)) {
                arrows_both = true; eng.button(10, true); eng.button(11, true); e.consumed = true;
            }
            return;
        }
        if (bi == 3) { rec_latched = !rec_latched; eng.button(3, rec_latched); }   // RECORD latches
        else if (bi >= 0) { held_button = bi; eng.button(bi, true); }
        else {
            RackUI::Area a = ui.area_at((int)p.x, (int)p.y);
            if (a == RackUI::POWER) { power_held = true; power_t = now_ms(); }
            else if (a == RackUI::EJECT) eng.eject_disk();
            else if (a == RackUI::DRIVE) { if (on_drive) on_drive(); }
            else if (hit(ui.volume_slider(), p)) { drag = 1; drag_to(p); }
            else if (hit(ui.data_slider(), p)) { drag = 2; drag_to(p); }
        }
        e.consumed = true;
    }
    void onMouseMoveEvent(MouseMoveEvent &e) override {
        if (drag) { drag_to(to_px(e.mousePosition)); e.consumed = true; }
    }
    void onMouseUpEvent(MouseUpEvent &e) override {
        Eps16Engine &eng = plug->engine();
        if (power_held) {
            power_held = false;
            if (now_ms() - power_t >= 700) eng.power_reset();
        }
        if (arrows_both) { eng.button(11, false); eng.button(10, false); arrows_both = false; }
        if (held_button >= 0) { eng.button(held_button, false); held_button = -1; }
        drag = 0;
        e.consumed = true;
    }
    void onMouseCancelEvent(MouseCancelEvent &e) override {
        if (held_button >= 0) { plug->engine().button(held_button, false); held_button = -1; }
        drag = 0; power_held = false;
        e.consumed = true;
    }
    void onMouseWheelEvent(MouseWheelEvent &e) override {      // wheel = up / down arrows
        if (e.deltaY == 0) return;
        int idx = e.deltaY > 0 ? 10 : 11;
        plug->engine().button(idx, true); plug->engine().button(idx, false);
        e.consumed = true;
    }
    SharedPointer<IDropTarget> getDropTarget() override { return drop; }

    std::function<void()> on_drive;

private:
    double volume_value() { return plug->getParamNormalized(kVolume); }
    CPoint to_px(CPoint where) const {
        CPoint p = where - getViewSize().getTopLeft();
        double k = (double)off.width() / getViewSize().getWidth();
        return CPoint(p.x * k, p.y * k);
    }
    static bool hit(const RackUI::Slider &s, CPoint p) {
        double m = s.w;                      // the knob overhangs the travel
        return p.x >= s.x - 4 && p.x < s.x + s.w + 4 && p.y >= s.y - m && p.y < s.y + s.h + m;
    }
    void drag_to(CPoint p) {
        RackUI::Slider s = drag == 1 ? off.ui().volume_slider() : off.ui().data_slider();
        double v = 1.0 - (p.y - s.y) / s.h;
        v = v < 0 ? 0 : v > 1 ? 1 : v;
        if (drag == 1) plug->edit_volume(v), plug->engine().analog(Eps16::AN_VOLUME, (uint16_t)(v * 1023));
        else { data = v; plug->engine().analog(Eps16::AN_DATA, (uint16_t)(280 + v * (985 - 280))); }
    }
    void copy_pixels() {
        auto pb = bmp->getPlatformBitmap();
        auto acc = pb->lockPixels(true);
        if (!acc) return;
        uint8_t *dst = acc->getAddress();
        uint32_t bpr = acc->getBytesPerRow();
        int w = std::min<int>(off.width(), (int)pb->getSize().x), h = std::min<int>(off.height(), (int)pb->getSize().y);
        auto fmt = acc->getPixelFormat();
        for (int y = 0; y < h; y++) {
            const uint32_t *src = (const uint32_t *)((const uint8_t *)off.pixels() + (size_t)y * off.pitch());
            uint8_t *d = dst + (size_t)y * bpr;
            for (int x = 0; x < w; x++, d += 4) {
                uint32_t c = src[x];
                uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c, a = 255;
                switch (fmt) {
                case IPlatformBitmapPixelAccess::kARGB: d[0] = a; d[1] = r; d[2] = g; d[3] = b; break;
                case IPlatformBitmapPixelAccess::kRGBA: d[0] = r; d[1] = g; d[2] = b; d[3] = a; break;
                case IPlatformBitmapPixelAccess::kABGR: d[0] = a; d[1] = b; d[2] = g; d[3] = r; break;
                case IPlatformBitmapPixelAccess::kBGRA: d[0] = b; d[1] = g; d[2] = r; d[3] = a; break;
                }
            }
        }
    }

    Plugin *plug;
    RackOffscreen off;
    SharedPointer<CBitmap> bmp;
    SharedPointer<RackDrop> drop;
    int held_button = -1, drag = 0;
    bool rec_latched = false, power_held = false, arrows_both = false;
    uint32_t power_t = 0;
    double data = 0.5;
};

// ------------------------------------------------------------------ editor
enum { kTagFloppy = 1, kTagEject, kTagScsi, kTagScsiOff, kTagScsiDir, kTagReset, kTagClock, kTagMidiCh };

static int rack_height() {
    static int h = 0;
    if (!h) { RackUI r((float)Editor::W); h = (int)(r.height() + RackUI::OY * Editor::W / 1400.0f + 2); }
    return h;
}

Editor::Editor(Plugin *p) : VSTGUIEditor(p), plug(p) {
    ViewRect vr(0, 0, W, rack_height() + BAR);
    setRect(vr);
}

Editor::~Editor() = default;

bool PLUGIN_API Editor::open(void *parent, const PlatformType &type) {
    if (frame) return false;
    CRect size(0, 0, W, rack_height() + BAR);
    frame = new CFrame(size, this);
    frame->setBackgroundColor(CColor(12, 13, 14));
    frame->open(parent, type);

    rack = new RackView(CRect(0, 0, W, rack_height()), plug, W * 2);    // 2x pixels: sharp on Retina
    rack->on_drive = [this]() { choose_file(false); };
    frame->addView(rack);

    // bottom bar
    double y0 = rack_height() + 4, h = BAR - 8, x = 8;
    auto button = [&](const char *t, int tag, double w) {
        auto *b = new CTextButton(CRect(x, y0, x + w, y0 + h), this, tag, t);
        b->setFont(kNormalFontSmall);
        b->setRoundRadius(3);
        frame->addView(b);
        x += w + 6;
        return b;
    };
    button("Disquete...", kTagFloppy, 90);
    button("Ejetar", kTagEject, 60);
    button("Disco SCSI...", kTagScsi, 100);
    button("Desligar SCSI", kTagScsiOff, 104);
    button("Pasta SCSI...", kTagScsiDir, 96);
    button("Reset", kTagReset, 56);
    auto *clk = new CTextButton(CRect(x, y0, x + 150, y0 + h), this, kTagClock, "Relogio MIDI do host", CTextButton::kOnOffStyle);
    clk->setFont(kNormalFontSmall); clk->setRoundRadius(3);
    clk->setValue((float)plug->getParamNormalized(kMidiClock));
    frame->addView(clk);
    x += 156;
    // host MIDI -> channel (the OS disk is set to MIDI MULTI, instrument 1 on channel 9)
    auto *mc = new COptionMenu(CRect(x, y0, x + 118, y0 + h), this, kTagMidiCh);
    mc->addEntry("MIDI: canais do host");
    for (int c = 1; c <= 16; c++) mc->addEntry(("MIDI -> canal " + std::to_string(c)).c_str());
    mc->setCurrent(plug->engine().midi_channel.load());
    mc->setFont(kNormalFontSmall);
    mc->setFontColor(CColor(220, 220, 220)); mc->setBackColor(CColor(40, 42, 46)); mc->setFrameColor(CColor(90, 92, 96));
    frame->addView(mc);
    x += 124;
    status = new CTextLabel(CRect(x + 4, y0, W - 8, y0 + h), "");
    status->setFont(kNormalFontSmall);
    status->setFontColor(CColor(170, 200, 230));
    status->setBackColor(CColor(0, 0, 0, 0));
    status->setFrameColor(CColor(0, 0, 0, 0));
    status->setHoriAlign(kLeftText);
    frame->addView(status);

    timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) { tick(); }, 33, true);
    tick();
    return true;
}

void PLUGIN_API Editor::close() {
    if (timer) { timer->stop(); timer = nullptr; }
    if (frame) { frame->forget(); frame = nullptr; }
    rack = nullptr; status = nullptr;
}

void Editor::tick() {
    if (!rack) return;
    EngineSnapshot s = plug->engine().snapshot();
    rack->update(s);
    if (status && s.status_serial != last_status) {
        last_status = s.status_serial;
        std::string line = s.status;
        if (!s.floppy.empty()) line = "[" + std::filesystem::u8path(s.floppy).filename().u8string() + "]  " + line;
        status->setText(line.c_str());
    }
}

void Editor::choose_file(bool scsi) {
    if (!frame) return;
    auto *sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
    if (!sel) return;
    sel->setTitle(scsi ? "Imagem de disco SCSI" : "Imagem de disquete, instrumento .efe ou disco SCSI");
    sel->setInitialDirectory(eps16_user_dir().c_str());
    Plugin *p = plug;
    sel->run([p, scsi](CNewFileSelector *s) {
        if (s->getNumSelectedFiles() == 0) return;
        std::string f = s->getSelectedFile(0);
        if (scsi) p->engine().attach_scsi(f); else p->engine().insert_disk(f);
    });
    sel->forget();
}

void Editor::choose_scsi_dir() {
    if (!frame) return;
    auto *sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectDirectory);
    if (!sel) return;
    sel->setTitle("Pasta das drives SCSI (HD0.img, CD5.iso ...)");
    std::string cur = plug->engine().scsi_dir();
    sel->setInitialDirectory((cur.empty() ? eps16_user_dir() : cur).c_str());
    Plugin *p = plug;
    sel->run([p](CNewFileSelector *s) {
        if (s->getNumSelectedFiles() == 0) return;
        p->engine().set_scsi_dir(s->getSelectedFile(0), true);
    });
    sel->forget();
}

void Editor::valueChanged(CControl *c) {
    int tag = c->getTag();
    if (tag == kTagClock) {
        double v = c->getValue() > 0.5f ? 1.0 : 0.0;
        plug->beginEdit(kMidiClock); plug->setParamNormalized(kMidiClock, v); plug->performEdit(kMidiClock, v); plug->endEdit(kMidiClock);
        return;
    }
    if (tag == kTagMidiCh) { plug->set_midi_channel((int)(c->getValue() + 0.5f)); return; }
    if (c->getValue() < 0.5f) return;               // kick buttons: act on press only
    Eps16Engine &eng = plug->engine();
    switch (tag) {
    case kTagFloppy: choose_file(false); break;
    case kTagEject: eng.eject_disk(); break;
    case kTagScsi: choose_file(true); break;
    case kTagScsiOff: eng.detach_scsi(); break;
    case kTagScsiDir: choose_scsi_dir(); break;
    case kTagReset: eng.power_reset(); break;
    }
}

} // namespace Eps16Vst
