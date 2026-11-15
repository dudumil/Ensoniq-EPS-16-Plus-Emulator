// The machine's D/A converters and the step to the computer's sample rate.
//
// The ASR-10 and the EPS-16 Plus convert with Philips TDA1541A DACs: one for the main outputs
// and one for each bus of the OEX-6 output expander (4 chips, 8 channels). Model of one channel:
//   - the TDA1541A (data sheet, February 1991): 16-bit two's complement words (time-multiplexed
//     I2S mode); a longer word loses its low bits. The output current is the sum of the bits'
//     currents: the 6 upper bits from three 2-bit dynamic element matching dividers (very
//     accurate), the 10 lower bits from an emitter scaled passive divider; each chip has its own
//     small weight errors, within the data sheet's linearity (0.5 LSB typical, 1 LSB maximum).
//     Noise 110 dB under full scale. No oversampling filter: the output holds each word for a
//     whole sample period (zero-order hold);
//   - the analog section of the EPS-16 Plus main board (see dac_out.cpp): the I/V converter,
//     the 3rd-order low-pass that makes up for the hold's droop and removes the images, the
//     coupling capacitor. The ASR-10's is taken to be the same (no schematic of it is known).
// The analog output is then band-limited to the computer's rate and sampled there (polyphase,
// windowed sinc).
// IDEAL: a perfect converter (no 16-bit step, no hold, no analog section): only the
// band-limited resampling.
#pragma once
#include <cstdint>
#include <vector>

class DacOut {
public:
    enum Model { IDEAL = 0, TDA1541A = 1 };
    static const int CH = 8;            // main L/R, then the three expander buses (L/R each)

    void set_model(Model m) { if (m != model) { model = m; kernel_rate_in = 0; } }
    Model get_model() const { return model; }
    // one frame from the machine: x[0..7] with 1.0 = 16-bit full scale; `aux` = also make the
    // expander channels. Output frames go to main (L/R) and aux (6 channels) at out_rate.
    void push(const float *x, bool aux, double in_rate, double out_rate, std::vector<float> &main, std::vector<float> &aux_out);
    void reset();

private:
    Model model = TDA1541A;
    // per channel: the analog value of each 16-bit code, from two tables (high and low byte)
    float hi_tab[CH][256], lo_tab[CH][256];
    bool tabs_made = false;
    void make_tables();
    float convert(int ch, float x) const;

    // kernel: the continuous response to one input sample (hold, I/V pole, band limit), sampled
    // at P phases per input sample over [-A, B] input samples
    static const int P = 256;
    std::vector<float> kern;
    int ka = 0, kb = 0;                 // support, in input samples
    double kernel_rate_in = 0, kernel_rate_out = 0;
    void make_kernel(double in_rate, double out_rate);
    struct Cached { double in, out; Model model; std::vector<float> kern; int ka, kb; };
    std::vector<Cached> cache;
    float hp_x[CH] = {}, hp_y[CH] = {};    // the coupling capacitor after the I/V converter

    // history of the analog values (input samples)
    static const int HIST = 256;        // power of two, > A + B
    float hist[CH][HIST] = {};
    long n = 0;                         // index of the newest input sample
    double t_next = 0;                  // time of the next output sample, in input samples
    uint32_t noise = 22222;
};
