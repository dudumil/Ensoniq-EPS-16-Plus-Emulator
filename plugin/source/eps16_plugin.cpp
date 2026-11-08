#include "eps16_plugin.h"
#include "version.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ustring.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>

#ifdef EPS16_WITH_GUI
#include "eps16_editor.h"
#endif

namespace Eps16Vst {

static const FUID kPluginUID(0x3E9D1A74, 0x52C64B1E, 0x8F07D2A3, 0x61B5C90E);

Plugin::Plugin() { midi.reserve(512); }
Plugin::~Plugin() = default;

tresult PLUGIN_API Plugin::queryInterface(const TUID iid, void **obj) {
    QUERY_INTERFACE(iid, obj, IMidiMapping::iid, IMidiMapping)
    return SingleComponentEffect::queryInterface(iid, obj);
}

tresult PLUGIN_API Plugin::initialize(FUnknown *context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // inputs: stereo audio for sampling (side chain); MIDI
    addAudioInput(STR16("Entrada (sampling)"), SpeakerArr::kStereo, kAux, BusInfo::kDefaultActive);
    addEventInput(STR16("MIDI In"), 16);
    // outputs: Main + the OEX-6 Output Expander (AUX 1, BUS 2, BUS 3, off until the host activates them)
    addAudioOutput(STR16("Main"), SpeakerArr::kStereo, kMain, BusInfo::kDefaultActive);
    addAudioOutput(STR16("AUX 1"), SpeakerArr::kStereo, kAux, 0);
    addAudioOutput(STR16("BUS 2"), SpeakerArr::kStereo, kAux, 0);
    addAudioOutput(STR16("BUS 3"), SpeakerArr::kStereo, kAux, 0);
    addEventOutput(STR16("MIDI Out"), 16);

    parameters.addParameter(STR16("Volume"), STR16("%"), 0, volume, ParameterInfo::kCanAutomate, kVolume);
    parameters.addParameter(STR16("Relogio MIDI do host"), nullptr, 1, 0, ParameterInfo::kCanAutomate, kMidiClock);
    parameters.addParameter(STR16("Canal MIDI (0 = do host)"), nullptr, 16, midi_ch / 16.0, 0, kMidiChannel);
    for (int ch = 0; ch < 16; ch++)
        for (int n = 0; n < kMidiPerChannel; n++) {
            char name[48];
            if (n < 128) snprintf(name, sizeof name, "MIDI CC%d ch%d", n, ch + 1);
            else snprintf(name, sizeof name, "MIDI %s ch%d", n == 128 ? "Aftertouch" : n == 129 ? "Pitch Bend" : "Program", ch + 1);
            String128 t; Steinberg::UString(t, 128).fromAscii(name);
            parameters.addParameter(t, nullptr, 0, n == 129 ? 0.5 : 0, ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly,
                                    kMidiBase + ch * kMidiPerChannel + n);
        }

    namespace fs = std::filesystem;
    std::string res = (fs::u8path(eps16_module_dir()) / ".." / "Resources" / "roms").lexically_normal().u8string();
    eng.boot(res, eps16_user_dir());
    eng.analog(Eps16::AN_VOLUME, (uint16_t)(volume * 1023));
    return kResultOk;
}

tresult PLUGIN_API Plugin::terminate() {
    eng.save_floppy();
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API Plugin::setActive(TBool state) {
    if (!state) eng.save_floppy();
    return kResultOk;
}

tresult PLUGIN_API Plugin::setupProcessing(ProcessSetup &setup) {
    eng.set_sample_rate(setup.sampleRate);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API Plugin::canProcessSampleSize(int32 size) {
    return size == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Plugin::setBusArrangements(SpeakerArrangement *inputs, int32 numIns,
                                              SpeakerArrangement *outputs, int32 numOuts) {
    for (int32 i = 0; i < numIns; i++) if (inputs[i] != SpeakerArr::kStereo) return kResultFalse;
    for (int32 i = 0; i < numOuts; i++) if (outputs[i] != SpeakerArr::kStereo) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

// ------------------------------------------------------------------ MIDI in
static inline uint8_t to7(double v) { int x = (int)std::lround(v * 127.0); return (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x); }

void Plugin::collect_midi(ProcessData &data) {
    if (IParameterChanges *pc = data.inputParameterChanges) {
        for (int32 i = 0; i < pc->getParameterCount(); i++) {
            IParamValueQueue *q = pc->getParameterData(i);
            if (!q) continue;
            ParamID id = q->getParameterId();
            int32 n = q->getPointCount();
            int32 off; ParamValue v;
            if (id == kVolume && n > 0 && q->getPoint(n - 1, off, v) == kResultOk) {
                volume = v; eng.analog(Eps16::AN_VOLUME, (uint16_t)(v * 1023));
            } else if (id == kMidiClock && n > 0 && q->getPoint(n - 1, off, v) == kResultOk) {
                clock_on = v > 0.5;
            } else if (id == kMidiChannel && n > 0 && q->getPoint(n - 1, off, v) == kResultOk) {
                midi_ch = (int)std::lround(v * 16); eng.midi_channel = midi_ch;
            } else if (id >= kMidiBase && id < kMidiBase + 16 * kMidiPerChannel) {
                int idx = (int)(id - kMidiBase), ch = idx / kMidiPerChannel, c = idx % kMidiPerChannel;
                for (int32 p = 0; p < n; p++) {
                    if (q->getPoint(p, off, v) != kResultOk) continue;
                    EngineMidi e{ off, {0, 0, 0}, 0 };
                    if (c < 128) { e.bytes[0] = (uint8_t)(0xB0 | ch); e.bytes[1] = (uint8_t)c; e.bytes[2] = to7(v); e.len = 3; }
                    else if (c == kAfterTouch) { e.bytes[0] = (uint8_t)(0xD0 | ch); e.bytes[1] = to7(v); e.len = 2; }
                    else if (c == kPitchBend) {
                        int b = (int)std::lround(v * 16383.0); b = b < 0 ? 0 : b > 16383 ? 16383 : b;
                        e.bytes[0] = (uint8_t)(0xE0 | ch); e.bytes[1] = (uint8_t)(b & 127); e.bytes[2] = (uint8_t)(b >> 7); e.len = 3;
                    } else { e.bytes[0] = (uint8_t)(0xC0 | ch); e.bytes[1] = to7(v); e.len = 2; }
                    midi.push_back(e);
                }
            }
        }
    }
    if (IEventList *ev = data.inputEvents) {
        for (int32 i = 0; i < ev->getEventCount(); i++) {
            Event e;
            if (ev->getEvent(i, e) != kResultOk) continue;
            EngineMidi m{ e.sampleOffset, {0, 0, 0}, 0 };
            switch (e.type) {
            case Event::kNoteOnEvent: {
                int ch = e.noteOn.channel & 15;
                uint8_t vel = to7(e.noteOn.velocity);
                m.bytes[0] = (uint8_t)((vel ? 0x90 : 0x80) | ch); m.bytes[1] = (uint8_t)(e.noteOn.pitch & 127);
                m.bytes[2] = vel ? vel : 64; m.len = 3; midi.push_back(m); break;
            }
            case Event::kNoteOffEvent:
                m.bytes[0] = (uint8_t)(0x80 | (e.noteOff.channel & 15)); m.bytes[1] = (uint8_t)(e.noteOff.pitch & 127);
                m.bytes[2] = to7(e.noteOff.velocity); m.len = 3; midi.push_back(m); break;
            case Event::kPolyPressureEvent:
                m.bytes[0] = (uint8_t)(0xA0 | (e.polyPressure.channel & 15)); m.bytes[1] = (uint8_t)(e.polyPressure.pitch & 127);
                m.bytes[2] = to7(e.polyPressure.pressure); m.len = 3; midi.push_back(m); break;
            case Event::kDataEvent:
                if (e.data.type == DataEvent::kMidiSysEx && e.data.bytes) {
                    for (uint32 k = 0; k < e.data.size; k += 3) {
                        EngineMidi s{ e.sampleOffset, {0, 0, 0}, 0 };
                        for (uint32 j = 0; j < 3 && k + j < e.data.size; j++) s.bytes[s.len++] = e.data.bytes[k + j];
                        midi.push_back(s);
                    }
                }
                break;
            default: break;
            }
        }
    }
}

// host transport -> MIDI clock / start / stop / song position (only when the parameter is on)
void Plugin::midi_clock(ProcessData &data) {
    ProcessContext *ctx = data.processContext;
    bool playing = clock_on && ctx && (ctx->state & ProcessContext::kPlaying) &&
                   (ctx->state & ProcessContext::kTempoValid) && (ctx->state & ProcessContext::kProjectTimeMusicValid);
    if (!playing) {
        if (was_playing) { midi.push_back(EngineMidi{ 0, {0xFC, 0, 0}, 1 }); was_playing = false; }
        return;
    }
    double qpos = ctx->projectTimeMusic;                       // quarters at the start of the block
    double q_per_sample = ctx->tempo / 60.0 / processSetup.sampleRate;
    if (!was_playing || std::fabs(qpos - next_tick) > 1.0 / 12) {     // (re)start or jump
        int sixteenths = (int)std::floor(std::max(0.0, qpos) * 4.0);
        EngineMidi spp{ 0, {0xF2, (uint8_t)(sixteenths & 127), (uint8_t)((sixteenths >> 7) & 127)}, 3 };
        midi.push_back(spp);
        midi.push_back(EngineMidi{ 0, {(uint8_t)(sixteenths == 0 ? 0xFA : 0xFB), 0, 0}, 1 });
        next_tick = std::ceil(qpos * 24.0 - 1e-9) / 24.0;
        was_playing = true;
    }
    double end = qpos + q_per_sample * data.numSamples;
    while (next_tick < end) {
        int off = (int)std::floor((next_tick - qpos) / q_per_sample);
        midi.push_back(EngineMidi{ std::max(0, off), {0xF8, 0, 0}, 1 });
        next_tick += 1.0 / 24.0;
    }
}

// ------------------------------------------------------------------ MIDI out
void Plugin::send_midi_out(ProcessData &data) {
    std::vector<uint8_t> bytes = eng.take_midi_out();
    IEventList *out = data.outputEvents;
    for (uint8_t b : bytes) {
        if (b >= 0xF8) continue;                         // real time: not forwarded
        if (b & 0x80) {
            if (b == 0xF0) { in_sysex = true; continue; }
            if (b == 0xF7 || b >= 0xF0) { in_sysex = false; out_status = 0; continue; }
            in_sysex = false; out_status = b; out_count = 0; continue;
        }
        if (in_sysex || !out_status) continue;
        out_data[out_count++] = b;
        int need = ((out_status & 0xF0) == 0xC0 || (out_status & 0xF0) == 0xD0) ? 1 : 2;
        if (out_count < need) continue;
        out_count = 0;
        if (!out) continue;
        Event e{};
        e.busIndex = 0; e.sampleOffset = 0; e.ppqPosition = 0; e.flags = Event::kIsLive;
        int ch = out_status & 15;
        switch (out_status & 0xF0) {
        case 0x90:
            if (out_data[1]) { e.type = Event::kNoteOnEvent; e.noteOn.channel = (int16)ch; e.noteOn.pitch = out_data[0];
                               e.noteOn.velocity = out_data[1] / 127.f; e.noteOn.noteId = -1; break; }
            [[fallthrough]];
        case 0x80:
            e.type = Event::kNoteOffEvent; e.noteOff.channel = (int16)ch; e.noteOff.pitch = out_data[0];
            e.noteOff.velocity = (out_status & 0xF0) == 0x80 ? out_data[1] / 127.f : 0.5f; e.noteOff.noteId = -1; break;
        case 0xA0:
            e.type = Event::kPolyPressureEvent; e.polyPressure.channel = (int16)ch; e.polyPressure.pitch = out_data[0];
            e.polyPressure.pressure = out_data[1] / 127.f; e.polyPressure.noteId = -1; break;
        case 0xB0:
            e.type = Event::kLegacyMIDICCOutEvent; e.midiCCOut.channel = (int8)ch; e.midiCCOut.controlNumber = out_data[0];
            e.midiCCOut.value = (int8)out_data[1]; e.midiCCOut.value2 = 0; break;
        case 0xC0:
            e.type = Event::kLegacyMIDICCOutEvent; e.midiCCOut.channel = (int8)ch; e.midiCCOut.controlNumber = kCtrlProgramChange;
            e.midiCCOut.value = (int8)out_data[0]; e.midiCCOut.value2 = 0; break;
        case 0xD0:
            e.type = Event::kLegacyMIDICCOutEvent; e.midiCCOut.channel = (int8)ch; e.midiCCOut.controlNumber = kAfterTouch;
            e.midiCCOut.value = (int8)out_data[0]; e.midiCCOut.value2 = 0; break;
        case 0xE0:
            e.type = Event::kLegacyMIDICCOutEvent; e.midiCCOut.channel = (int8)ch; e.midiCCOut.controlNumber = kPitchBend;
            e.midiCCOut.value = (int8)out_data[0]; e.midiCCOut.value2 = (int8)out_data[1]; break;
        default: continue;
        }
        out->addEvent(e);
    }
}

// ------------------------------------------------------------------ audio
tresult PLUGIN_API Plugin::process(ProcessData &data) {
    midi.clear();
    collect_midi(data);
    if (data.numSamples <= 0) return kResultOk;
    midi_clock(data);
    std::stable_sort(midi.begin(), midi.end(), [](const EngineMidi &a, const EngineMidi &b) { return a.offset < b.offset; });

    float *out[8] = {};
    bool aux_active[3] = {};
    for (int32 b = 0; b < data.numOutputs && b < 4; b++) {
        AudioBusBuffers &bus = data.outputs[b];
        if (bus.numChannels < 2 || !bus.channelBuffers32) continue;
        out[b * 2] = bus.channelBuffers32[0]; out[b * 2 + 1] = bus.channelBuffers32[1];
        bus.silenceFlags = 0;
        if (b > 0) aux_active[b - 1] = audioOutputs.size() > (size_t)b && audioOutputs[b]->isActive();
    }
    const float *in_l = nullptr, *in_r = nullptr;
    if (data.numInputs > 0 && data.inputs[0].numChannels >= 2 && data.inputs[0].channelBuffers32 &&
        !audioInputs.empty() && audioInputs[0]->isActive()) {
        in_l = data.inputs[0].channelBuffers32[0]; in_r = data.inputs[0].channelBuffers32[1];
    }
    eng.process(out, in_l, in_r, data.numSamples, midi, aux_active);
    send_midi_out(data);
    return kResultOk;
}

// ------------------------------------------------------------------ state
static const int32 kStateMagic = 0x45505331;   // "EPS1"

tresult PLUGIN_API Plugin::getState(IBStream *state) {
    IBStreamer s(state, kLittleEndian);
    s.writeInt32(kStateMagic);
    s.writeInt32(3);
    s.writeDouble(volume);
    s.writeInt32(clock_on ? 1 : 0);
    s.writeStr8(eng.floppy_path().c_str());
    auto sc = eng.scsi_paths();
    s.writeInt32((int32)sc.size());
    for (auto &p : sc) s.writeStr8(p.c_str());
    s.writeStr8(eng.scsi_dir().c_str());            // version 2: SCSI folder
    s.writeInt32(midi_ch);                          // version 3: MIDI channel
    return kResultOk;
}

tresult PLUGIN_API Plugin::setState(IBStream *state) {
    IBStreamer s(state, kLittleEndian);
    int32 magic = 0, ver = 0, clk = 0, n = 0;
    if (!s.readInt32(magic) || magic != kStateMagic || !s.readInt32(ver)) return kResultFalse;
    double vol = volume;
    s.readDouble(vol); s.readInt32(clk);
    std::string floppy;
    if (char8 *f = s.readStr8()) { floppy = f; delete[] f; }
    std::vector<std::string> scsi;
    if (s.readInt32(n)) for (int32 i = 0; i < n && i < 8; i++) {
        char8 *p = s.readStr8(); scsi.push_back(p ? std::string(p) : std::string()); delete[] p;
    }
    std::string sdir;
    if (ver >= 2) if (char8 *d = s.readStr8()) { sdir = d; delete[] d; }
    int32 mch = 9;
    if (ver >= 3) s.readInt32(mch);
    midi_ch = mch < 0 ? 0 : mch > 16 ? 16 : mch; eng.midi_channel = midi_ch;
    setParamNormalized(kMidiChannel, midi_ch / 16.0);
    volume = vol; clock_on = clk != 0;
    setParamNormalized(kVolume, volume);
    setParamNormalized(kMidiClock, clock_on ? 1 : 0);
    eng.analog(Eps16::AN_VOLUME, (uint16_t)(volume * 1023));
    eng.restore(floppy, scsi, sdir);
    return kResultOk;
}

// ------------------------------------------------------------------ controller side
tresult PLUGIN_API Plugin::getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber cc, ParamID &id) {
    if (busIndex != 0 || channel < 0 || channel > 15 || cc < 0 || cc >= kMidiPerChannel) return kResultFalse;
    id = kMidiBase + channel * kMidiPerChannel + cc;
    return kResultTrue;
}

void Plugin::set_midi_channel(int ch) {
    midi_ch = ch < 0 ? 0 : ch > 16 ? 16 : ch;
    eng.midi_channel = midi_ch;
    double v = midi_ch / 16.0;
    beginEdit(kMidiChannel); setParamNormalized(kMidiChannel, v); performEdit(kMidiChannel, v); endEdit(kMidiChannel);
}

void Plugin::edit_volume(double v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    beginEdit(kVolume); setParamNormalized(kVolume, v); performEdit(kVolume, v); endEdit(kVolume);
}

IPlugView *PLUGIN_API Plugin::createView(FIDString name) {
#ifdef EPS16_WITH_GUI
    if (name && strcmp(name, ViewType::kEditor) == 0) return new Editor(this);
#else
    (void)name;
#endif
    return nullptr;
}

} // namespace Eps16Vst

// ------------------------------------------------------------------ factory
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Eps16Vst::kPluginUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               0,                                   // single component: not distributable
               Steinberg::Vst::PlugType::kInstrumentSampler,
               FULL_VERSION_STR,
               kVstVersionString,
               Eps16Vst::Plugin::createInstance)
END_FACTORY
