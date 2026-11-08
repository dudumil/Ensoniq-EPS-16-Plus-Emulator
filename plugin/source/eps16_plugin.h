// VST3 instrument: Ensoniq EPS-16 Plus emulator (single component: processor and controller in one).
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "eps16_engine.h"
#include <atomic>

namespace Eps16Vst {

using namespace Steinberg;
using namespace Steinberg::Vst;

enum ParamIds : ParamID {
    kVolume = 0,          // front panel Volume slider
    kMidiClock = 1,       // send the host tempo/transport to the EPS as MIDI clock
    kMidiBase = 1000,     // hidden parameters fed by the host's MIDI controllers: kMidiBase + ch*131 + n
};
static const int kMidiPerChannel = 131;          // CC 0..127, aftertouch, pitch bend, program change

class Plugin : public SingleComponentEffect, public IMidiMapping {
public:
    Plugin();
    ~Plugin() override;
    static FUnknown *createInstance(void *) { return (IAudioProcessor *)new Plugin; }

    // IComponent / IAudioProcessor
    tresult PLUGIN_API initialize(FUnknown *context) SMTG_OVERRIDE;
    tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 size) SMTG_OVERRIDE;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                          SpeakerArrangement *outputs, int32 numOuts) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData &data) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream *state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream *state) SMTG_OVERRIDE;

    // IEditController
    IPlugView *PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;

    // IMidiMapping
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber cc,
                                                   ParamID &id) SMTG_OVERRIDE;

    OBJ_METHODS(Plugin, SingleComponentEffect)
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) SMTG_OVERRIDE;
    REFCOUNT_METHODS(SingleComponentEffect)

    Eps16Engine &engine() { return eng; }
    // editor -> Volume parameter (keeps the host informed)
    void edit_volume(double v);

private:
    void collect_midi(ProcessData &data);
    void midi_clock(ProcessData &data);
    void send_midi_out(ProcessData &data);

    Eps16Engine eng;
    std::vector<EngineMidi> midi;
    double volume = 0.98;
    bool clock_on = false;
    // MIDI clock state
    bool was_playing = false;
    double next_tick = 0;                 // next 1/24-quarter position (in quarters)
    // running status for the MIDI output parser
    uint8_t out_status = 0; uint8_t out_data[2] = {}; int out_count = 0; bool in_sysex = false;
};

} // namespace Eps16Vst
