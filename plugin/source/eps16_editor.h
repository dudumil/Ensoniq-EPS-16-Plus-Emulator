// Plug-in editor: the rack front panel (drawn off screen with SDL's software renderer and shown
// as a bitmap in a VSTGUI frame) and nothing else: disks go in by drag and drop,
// reset and eject are the panel's own buttons, and the settings are the stand-alone program's.
#pragma once
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/vstgui.h"
#include "eps16_plugin.h"
#include "rack_offscreen.h"
#include <memory>

namespace Eps16Vst {

class RackView;

class Editor : public VSTGUIEditor, public VSTGUI::IControlListener {
public:
    explicit Editor(Plugin *p);
    ~Editor() override;
    bool PLUGIN_API open(void *parent, const VSTGUI::PlatformType &type) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl *control) override;
    Plugin *plugin() { return plug; }

    static constexpr int W = 1120;          // logical size of the panel (points)

private:
    void tick();
    Plugin *plug;
    RackView *rack = nullptr;
    VSTGUI::CTextLabel *status = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
    uint32_t last_status = ~0u, status_until = 0;
    int settings_ticks = 0;
};

} // namespace Eps16Vst
