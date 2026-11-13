#include "eps16_editor.h"
#include "efe_disk.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/cfileselector.h"
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

// disk images (.img, Giebler .ede/.eda, SCSI images) go in the drive / on the SCSI bus; instrument
// files (.efe/.efa) given together go on one temporary floppy
static void put_in_drive(Plugin *plug, const std::vector<std::string> &files) {
    std::vector<std::string> efes;
    for (auto &f : files) { if (efe::is_single_file(f)) efes.push_back(f); else plug->engine().insert_disk(f); }
    if (!efes.empty()) plug->engine().insert_files(efes);
}

// ------------------------------------------------------------------ drop target
class RackDrop : public NonAtomicReferenceCounted, public DropTargetAdapter {
public:
    explicit RackDrop(Plugin *p) : plug(p) {}
    DragOperation onDragEnter(DragEventData d) override { return has_file(d) ? DragOperation::Copy : DragOperation::None; }
    DragOperation onDragMove(DragEventData d) override { return has_file(d) ? DragOperation::Copy : DragOperation::None; }
    bool onDrop(DragEventData d) override {
        bool any = false;
        std::vector<std::string> files;
        for (uint32_t i = 0; i < d.drag->getCount(); i++) {
            const void *buf = nullptr; IDataPackage::Type t;
            d.drag->getData(i, buf, t);
            if (t != IDataPackage::kFilePath || !buf) continue;
            files.push_back((const char *)buf);
            any = true;
        }
        put_in_drive(plug, files);
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
        rs.disk = snap.disk; rs.drive_busy = snap.drive_busy; rs.scsi_activity = snap.scsi_activity; rs.scsi_writes = snap.scsi_writes;
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

static int rack_height() {
    static int h = 0;
    if (!h) { RackUI r((float)Editor::W); h = (int)(r.height() + RackUI::OY * Editor::W / 1400.0f + 2); }
    return h;
}

Editor::Editor(Plugin *p) : VSTGUIEditor(p), plug(p) {
    ViewRect vr(0, 0, W, rack_height());
    setRect(vr);
}

Editor::~Editor() = default;

bool PLUGIN_API Editor::open(void *parent, const PlatformType &type) {
    if (frame) return false;
    CRect size(0, 0, W, rack_height());
    frame = new CFrame(size, this);
    frame->setBackgroundColor(CColor(12, 13, 14));
    frame->open(parent, type);

    rack = new RackView(CRect(0, 0, W, rack_height()), plug, W * 2);    // 2x pixels: sharp on Retina
    // a click on the drive slot opens a file chooser (dropping files on the panel works too, where
    // the host passes drops on to plug-ins)
    rack->on_drive = [this]() {
        CNewFileSelector *sel = frame ? CNewFileSelector::create(frame, CNewFileSelector::kSelectFile) : nullptr;
        if (!sel) { plug->engine().message("Drop a disk image (.img, .ede, .eda) or an instrument (.efe/.efa) on the panel to put it in the drive"); return; }
        sel->setTitle("Put a disk image or instrument files in the drive");
        sel->setAllowMultiFileSelection(true);
        sel->addFileExtension(CFileExtension("Disk images and instruments", "img"));
        sel->addFileExtension(CFileExtension("Giebler disk image (EPS / EPS-16 Plus / ASR)", "ede"));
        sel->addFileExtension(CFileExtension("Giebler disk image (ASR-10 HD)", "eda"));
        sel->addFileExtension(CFileExtension("Ensoniq instrument file", "efe"));
        sel->addFileExtension(CFileExtension("Ensoniq instrument file (ASR)", "efa"));
        sel->addFileExtension(CNewFileSelector::getAllFilesExtension());
        Plugin *pl = plug;
        sel->run([pl](CNewFileSelector *fs) {
            std::vector<std::string> files;
            for (uint32_t i = 0; i < fs->getNumSelectedFiles(); i++) if (const char *f = fs->getSelectedFile(i)) files.push_back(f);
            put_in_drive(pl, files);
        });
        sel->forget();
    };
    frame->addView(rack);

    // messages (disk inserted, errors...) appear for a few seconds over the bottom of the panel
    status = new CTextLabel(CRect(10, rack_height() - 30, W - 10, rack_height() - 8), "");
    status->setFont(kNormalFontSmall);
    status->setFontColor(CColor(200, 225, 245));
    status->setBackColor(CColor(0, 0, 0, 190));
    status->setFrameColor(CColor(0, 0, 0, 0));
    status->setHoriAlign(kLeftText);
    status->setTextInset(CPoint(8, 0));
    status->setVisible(false);
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
    if (++settings_ticks >= 60) { settings_ticks = 0; plug->engine().check_settings(); }   // ~2 s
    EngineSnapshot s = plug->engine().snapshot();
    rack->update(s);
    const std::string &err = plug->engine().error();
    if (status && !err.empty()) {                   // the machine could not start: say why, for good
        if (!status->isVisible() || status->getText() != err.c_str()) { status->setText(err.c_str()); status->setVisible(true); }
    } else if (status && s.status_serial != last_status) {
        last_status = s.status_serial;
        if (!s.status.empty()) { status->setText(s.status.c_str()); status->setVisible(true); status_until = now_ms() + 5000; }
    } else if (status && status->isVisible() && now_ms() > status_until) status->setVisible(false);
}

void Editor::valueChanged(CControl *) {}

} // namespace Eps16Vst
