// Audio device input/output for the SDL front end.
//
// Two kinds of back end:
//   - RtAudio (WASAPI, DirectSound and ASIO on Windows; CoreAudio on macOS; ALSA, PulseAudio
//     and JACK on Linux, whichever were compiled in): any number of output and input channels,
//     so the main bus and the AUX buses can go to different outputs of a multichannel interface;
//   - SDL (always available): stereo out and stereo in.
//
// The caller supplies two callbacks, called from the audio thread:
//   render(out, channels, frames)   fill `frames` interleaved frames of `channels` outputs
//                                   (output channel 0 = first output of the device)
//   capture(in, channels, frames)   `frames` interleaved frames of the selected input channels
//                                   (in[0] = first selected channel)
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AudioDevice {
    std::string name;
    int outputs = 0, inputs = 0;
    std::vector<unsigned> rates;
    unsigned preferred_rate = 0;
    bool default_out = false, default_in = false;
    unsigned id = 0;                   // back-end specific
};

struct AudioApi {
    std::string id;                    // "sdl", or the RtAudio API name ("wasapi", "asio", "alsa", ...)
    std::string name;                  // for display
};

struct AudioSetup {
    std::string api;                   // AudioApi::id ("" = first available)
    std::string out_dev;               // device name ("" = default)
    std::string in_dev;                // device name ("" = no input, "*" = default input)
    unsigned rate = 0;                 // 0 = the device's preferred rate
    unsigned buffer = 256;             // frames per callback
    int out_channels = 2;              // outputs to open, starting at the device's first output
    int in_first = 0, in_count = 2;    // input channels to open (0-based first channel, count)
};

class AudioIO {
public:
    using Render = std::function<void(float *out, int channels, int frames)>;
    using Capture = std::function<void(const float *in, int channels, int frames)>;

    AudioIO();
    ~AudioIO();
    static std::vector<AudioApi> apis();
    static std::vector<AudioDevice> devices(const std::string &api);

    // opens and starts the streams; on failure returns false with a message in err
    bool open(const AudioSetup &s, Render render, Capture capture, std::string &err);
    void close();
    bool is_open() const;
    bool has_input() const;
    unsigned rate() const { return rate_; }
    unsigned buffer() const { return buffer_; }
    int out_channels() const { return out_ch_; }
    int in_channels() const { return in_ch_; }
    std::string description() const { return desc_; }

    struct Impl;                       // (public so the C callbacks can reach it)
private:
    std::unique_ptr<Impl> d;
    unsigned rate_ = 0, buffer_ = 0;
    int out_ch_ = 0, in_ch_ = 0;
    std::string desc_;
};
