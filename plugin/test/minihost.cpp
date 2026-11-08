// Minimal offline VST3 host for testing the EPS-16 Plus plug-in: loads the bundle, runs it for N
// seconds at 44.1 kHz in 256-frame blocks, optionally sends notes, writes the Main output to a WAV
// and prints the real-time factor.
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknownimpl.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static void write_wav(const char *path, const std::vector<float> &lr, int rate) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    uint32_t n = (uint32_t)lr.size(), bytes = n * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(bytes);
    for (float x : lr) { int v = (int)std::lround(x * 32767.f); v = v > 32767 ? 32767 : v < -32768 ? -32768 : v; w16((uint16_t)(int16_t)v); }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 4) { printf("minihost plugin.vst3 out.wav seconds [note_at_s]\n"); return 1; }
    std::string err;
    auto mod = VST3::Hosting::Module::create(argv[1], err);
    if (!mod) { printf("load: %s\n", err.c_str()); return 1; }
    auto factory = mod->getFactory();
    IPtr<IComponent> comp;
    for (auto &ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass) { comp = factory.createInstance<IComponent>(ci.ID()); break; }
    if (!comp) { printf("no component\n"); return 1; }
    static HostApplication host;
    comp->initialize(&host);
    FUnknownPtr<IAudioProcessor> proc(comp);
    const int block = 256, rate = 44100;
    ProcessSetup setup{ kRealtime, kSample32, block, (double)rate };
    proc->setupProcessing(setup);
    comp->activateBus(kAudio, kOutput, 0, true);
    comp->setActive(true);
    proc->setProcessing(true);
    HostProcessData data;
    data.prepare(*comp, block, kSample32);
    EventList events;
    data.inputEvents = &events;
    double seconds = atof(argv[3]), note_at = argc > 4 ? atof(argv[4]) : -1;
    long blocks = (long)(seconds * rate / block);
    std::vector<float> out;
    auto t0 = std::chrono::steady_clock::now();
    for (long b = 0; b < blocks; b++) {
        events.clear();
        double t = (double)b * block / rate;
        if (note_at >= 0 && t >= note_at && t - note_at < (double)block / rate) {
            Event e{}; e.type = Event::kNoteOnEvent; e.noteOn.channel = 0; e.noteOn.pitch = 60; e.noteOn.velocity = 0.8f; e.noteOn.noteId = -1;
            events.addEvent(e);
        }
        if (note_at >= 0 && t >= note_at + 1.0 && t - note_at - 1.0 < (double)block / rate) {
            Event e{}; e.type = Event::kNoteOffEvent; e.noteOff.channel = 0; e.noteOff.pitch = 60; e.noteOff.velocity = 0.5f; e.noteOff.noteId = -1;
            events.addEvent(e);
        }
        data.numSamples = block;
        proc->process(data);
        float **ch = data.outputs[0].channelBuffers32;
        for (int i = 0; i < block; i++) { out.push_back(ch[0][i]); out.push_back(ch[1][i]); }
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%.1f s of audio in %.2f s -> %.1fx real time\n", seconds, el, seconds / el);
    double peak = 0; for (float x : out) peak = std::max(peak, (double)std::fabs(x));
    printf("peak %.4f\n", peak);
    write_wav(argv[2], out, rate);
    proc->setProcessing(false);
    comp->setActive(false);
    data.unprepare();
    comp->terminate();
    return 0;
}
