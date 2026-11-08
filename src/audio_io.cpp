// Audio device input/output - see audio_io.h.
#include "audio_io.h"
#include <SDL.h>
#include <algorithm>
#include <cstring>
#ifdef ASR_WITH_RTAUDIO
#include "../third_party/rtaudio/RtAudio.h"
#endif

struct AudioIO::Impl {
    Render render;
    Capture capture;
    // SDL
    SDL_AudioDeviceID sdl_out = 0, sdl_in = 0;
#ifdef ASR_WITH_RTAUDIO
    std::unique_ptr<RtAudio> rt_out, rt_in;     // rt_in only when input and output are different devices
    bool duplex = false;
    std::string last_error;
#endif
    int out_ch = 0, in_ch = 0;
};

AudioIO::AudioIO() : d(new Impl) {}
AudioIO::~AudioIO() { close(); }

std::vector<AudioApi> AudioIO::apis() {
    std::vector<AudioApi> r;
#ifdef ASR_WITH_RTAUDIO
    std::vector<RtAudio::Api> v;
    RtAudio::getCompiledApi(v);
    for (auto a : v) {
        if (a == RtAudio::RTAUDIO_DUMMY) continue;
        r.push_back({ RtAudio::getApiName(a), RtAudio::getApiDisplayName(a) });
    }
#endif
    const char *drv = SDL_GetCurrentAudioDriver();
    r.push_back({ "sdl", std::string("SDL") + (drv ? std::string(" (") + drv + ")" : "") });
    return r;
}

std::vector<AudioDevice> AudioIO::devices(const std::string &api) {
    std::vector<AudioDevice> r;
    if (api == "sdl") {
        for (int cap = 0; cap < 2; cap++) {
            int n = SDL_GetNumAudioDevices(cap);
            for (int i = 0; i < n; i++) {
                const char *nm = SDL_GetAudioDeviceName(i, cap);
                if (!nm) continue;
                auto it = std::find_if(r.begin(), r.end(), [&](const AudioDevice &x) { return x.name == nm; });
                if (it == r.end()) { r.push_back(AudioDevice{}); it = r.end() - 1; it->name = nm; it->id = (unsigned)i; }
                if (cap) it->inputs = 2; else it->outputs = 2;
            }
        }
        for (auto &x : r) { x.rates = { 44100, 48000, 88200, 96000 }; x.preferred_rate = 48000; }
        return r;
    }
#ifdef ASR_WITH_RTAUDIO
    RtAudio::Api a = RtAudio::getCompiledApiByName(api);
    if (a == RtAudio::UNSPECIFIED) return r;
    RtAudio rt(a, [](RtAudioErrorType, const std::string &) {});
    rt.showWarnings(false);
    for (unsigned id : rt.getDeviceIds()) {
        RtAudio::DeviceInfo info = rt.getDeviceInfo(id);
        AudioDevice dv;
        dv.name = info.name; dv.id = id;
        dv.outputs = (int)info.outputChannels; dv.inputs = (int)info.inputChannels;
        dv.rates = info.sampleRates; dv.preferred_rate = info.preferredSampleRate;
        dv.default_out = info.isDefaultOutput; dv.default_in = info.isDefaultInput;
        r.push_back(dv);
    }
#endif
    return r;
}

bool AudioIO::is_open() const {
#ifdef ASR_WITH_RTAUDIO
    if (d->rt_out && d->rt_out->isStreamOpen()) return true;
    if (d->rt_in && d->rt_in->isStreamOpen()) return true;
#endif
    return d->sdl_out != 0 || d->sdl_in != 0;
}

bool AudioIO::has_input() const {
#ifdef ASR_WITH_RTAUDIO
    if ((d->duplex && d->in_ch) || (d->rt_in && d->rt_in->isStreamOpen())) return true;
#endif
    return d->sdl_in != 0;
}

void AudioIO::close() {
    if (d->sdl_out) { SDL_CloseAudioDevice(d->sdl_out); d->sdl_out = 0; }
    if (d->sdl_in) { SDL_CloseAudioDevice(d->sdl_in); d->sdl_in = 0; }
#ifdef ASR_WITH_RTAUDIO
    for (auto *p : { &d->rt_out, &d->rt_in }) {
        if (*p) {
            if ((*p)->isStreamRunning()) (*p)->abortStream();
            if ((*p)->isStreamOpen()) (*p)->closeStream();
            p->reset();
        }
    }
    d->duplex = false;
#endif
    out_ch_ = in_ch_ = 0; rate_ = buffer_ = 0; desc_.clear();
}

// ---------------------------------------------------------------- SDL back end
static void sdl_out_cb(void *ud, Uint8 *stream, int len) {
    auto *d = (AudioIO::Render *)ud;
    int frames = len / (int)(2 * sizeof(float));
    (*d)((float *)stream, 2, frames);
}
static void sdl_in_cb(void *ud, Uint8 *stream, int len) {
    auto *d = (AudioIO::Capture *)ud;
    int frames = len / (int)(2 * sizeof(float));
    (*d)((const float *)stream, 2, frames);
}

#ifdef ASR_WITH_RTAUDIO
static int rt_cb(void *out, void *in, unsigned frames, double, RtAudioStreamStatus, void *ud) {
    auto *d = (AudioIO::Impl *)ud;
    if (in && d->in_ch && d->capture) d->capture((const float *)in, d->in_ch, (int)frames);
    if (out && d->out_ch) d->render((float *)out, d->out_ch, (int)frames);
    return 0;
}
static int rt_in_cb(void *, void *in, unsigned frames, double, RtAudioStreamStatus, void *ud) {
    auto *d = (AudioIO::Impl *)ud;
    if (in && d->in_ch && d->capture) d->capture((const float *)in, d->in_ch, (int)frames);
    return 0;
}
#endif

bool AudioIO::open(const AudioSetup &s, Render render, Capture capture, std::string &err) {
    close();
    d->render = std::move(render);
    d->capture = std::move(capture);
    std::string api = s.api;
    if (api.empty()) { auto a = apis(); api = a.empty() ? "sdl" : a[0].id; }

    if (api == "sdl") {
        SDL_AudioSpec want{}, have{};
        want.freq = s.rate ? (int)s.rate : 48000; want.format = AUDIO_F32SYS; want.channels = 2;
        want.samples = (Uint16)std::max(64u, std::min(8192u, s.buffer));
        want.callback = sdl_out_cb; want.userdata = &d->render;
        d->sdl_out = SDL_OpenAudioDevice(s.out_dev.empty() ? nullptr : s.out_dev.c_str(), 0, &want, &have,
                                         SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
        if (!d->sdl_out) { err = std::string("SDL: ") + SDL_GetError(); return false; }
        rate_ = (unsigned)have.freq; buffer_ = have.samples; out_ch_ = d->out_ch = 2;
        if (!s.in_dev.empty()) {
            SDL_AudioSpec cw{}, ch{};
            cw.freq = have.freq; cw.format = AUDIO_F32SYS; cw.channels = 2; cw.samples = have.samples;
            cw.callback = sdl_in_cb; cw.userdata = &d->capture;
            d->sdl_in = SDL_OpenAudioDevice(s.in_dev == "*" ? nullptr : s.in_dev.c_str(), 1, &cw, &ch, 0);   // same rate as the output
            if (d->sdl_in) in_ch_ = d->in_ch = 2;
            else err = std::string("input: ") + SDL_GetError();
        }
        SDL_PauseAudioDevice(d->sdl_out, 0);
        if (d->sdl_in) SDL_PauseAudioDevice(d->sdl_in, 0);
        char b[200]; snprintf(b, sizeof b, "SDL %s, %u Hz, buffer %u", SDL_GetCurrentAudioDriver(), rate_, buffer_);
        desc_ = b;
        return true;
    }

#ifdef ASR_WITH_RTAUDIO
    RtAudio::Api a = RtAudio::getCompiledApiByName(api);
    if (a == RtAudio::UNSPECIFIED) { err = "unknown audio driver: " + api; return false; }
    auto make = [&](std::unique_ptr<RtAudio> &p) {
        p.reset(new RtAudio(a, [this](RtAudioErrorType t, const std::string &m) {
            if (t != RTAUDIO_WARNING) d->last_error = m;
        }));
        p->showWarnings(false);
    };
    make(d->rt_out);
    // find the devices by name
    RtAudio::DeviceInfo oinfo{}, iinfo{};
    bool have_out = false, have_in = false;
    for (unsigned id : d->rt_out->getDeviceIds()) {
        RtAudio::DeviceInfo info = d->rt_out->getDeviceInfo(id);
        if (!have_out && info.outputChannels > 0 && (s.out_dev.empty() ? info.isDefaultOutput : info.name == s.out_dev)) { oinfo = info; have_out = true; }
        if (!have_in && !s.in_dev.empty() && info.inputChannels > 0 && (s.in_dev == "*" ? info.isDefaultInput : info.name == s.in_dev)) { iinfo = info; have_in = true; }
    }
    if (!have_in && s.in_dev == "*") {           // "*" = default input; none flagged: the first one
        for (unsigned id : d->rt_out->getDeviceIds()) {
            RtAudio::DeviceInfo info = d->rt_out->getDeviceInfo(id);
            if (info.inputChannels > 0) { iinfo = info; have_in = true; break; }
        }
    }
    if (!have_out && s.out_dev.empty()) {        // no default flagged: first device with outputs
        for (unsigned id : d->rt_out->getDeviceIds()) {
            RtAudio::DeviceInfo info = d->rt_out->getDeviceInfo(id);
            if (info.outputChannels > 0) { oinfo = info; have_out = true; break; }
        }
    }
    if (!have_out) { err = "output device not found: " + (s.out_dev.empty() ? std::string("(default)") : s.out_dev); close(); return false; }
    if (!s.in_dev.empty() && !have_in) err = "input device not found: " + s.in_dev;

    unsigned rate = s.rate;
    if (!rate) rate = oinfo.preferredSampleRate ? oinfo.preferredSampleRate : oinfo.currentSampleRate ? oinfo.currentSampleRate : 48000;
    RtAudio::StreamParameters op;
    op.deviceId = oinfo.ID;
    op.nChannels = (unsigned)std::max(1, std::min(s.out_channels, (int)oinfo.outputChannels));
    op.firstChannel = 0;
    RtAudio::StreamParameters ip;
    if (have_in) {
        int first = std::max(0, std::min(s.in_first, (int)iinfo.inputChannels - 1));
        int cnt = std::max(1, std::min(s.in_count, (int)iinfo.inputChannels - first));
        ip.deviceId = iinfo.ID; ip.nChannels = (unsigned)cnt; ip.firstChannel = (unsigned)first;
    }
    d->duplex = have_in && iinfo.ID == oinfo.ID;
    if (have_in && !d->duplex && a == RtAudio::WINDOWS_ASIO) {
        err = "with ASIO the input must be the same device as the output";
        have_in = false;
    }
    RtAudio::StreamOptions opt;
    opt.flags = RTAUDIO_SCHEDULE_REALTIME | RTAUDIO_MINIMIZE_LATENCY;
    opt.streamName = "EPS-16 Plus";
    unsigned frames = s.buffer ? s.buffer : 256;
    d->out_ch = (int)op.nChannels;
    d->in_ch = d->duplex ? (int)ip.nChannels : 0;
    if (d->rt_out->openStream(&op, d->duplex ? &ip : nullptr, RTAUDIO_FLOAT32, rate, &frames, rt_cb, d.get(), &opt) != RTAUDIO_NO_ERROR) {
        err = d->last_error.empty() ? std::string("cannot open the audio output") : d->last_error;
        close(); return false;
    }
    rate_ = d->rt_out->getStreamSampleRate(); if (!rate_) rate_ = rate;
    buffer_ = frames; out_ch_ = d->out_ch; in_ch_ = d->in_ch;
    if (have_in && !d->duplex) {                 // separate input device, same rate
        make(d->rt_in);
        unsigned f2 = frames;
        RtAudio::StreamOptions o2 = opt; o2.streamName = "ASR-10 in";
        d->in_ch = (int)ip.nChannels;
        if (d->rt_in->openStream(nullptr, &ip, RTAUDIO_FLOAT32, rate_, &f2, rt_in_cb, d.get(), &o2) != RTAUDIO_NO_ERROR) {
            err = "input: " + d->last_error;
            d->rt_in.reset(); d->in_ch = 0;
        } else in_ch_ = d->in_ch;
    }
    if (d->rt_out->startStream() != RTAUDIO_NO_ERROR) { err = d->last_error; close(); return false; }
    if (d->rt_in && d->rt_in->startStream() != RTAUDIO_NO_ERROR) { err = "input: " + d->last_error; d->rt_in->closeStream(); d->rt_in.reset(); in_ch_ = d->in_ch = 0; }
    long lat = d->rt_out->getStreamLatency();
    char b[300];
    snprintf(b, sizeof b, "%s: %s, %u Hz, buffer %u (%.1f ms)%s", RtAudio::getApiDisplayName(a).c_str(), oinfo.name.c_str(),
             rate_, buffer_, 1000.0 * (lat > 0 ? lat : buffer_) / rate_, in_ch_ ? ", with input" : "");
    desc_ = b;
    return true;
#else
    err = "este executavel so tem audio SDL";
    return false;
#endif
}
