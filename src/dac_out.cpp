// D/A converters (Philips TDA1541A) and the step to the computer's rate - see dac_out.h.
#include "dac_out.h"
#include <cmath>
#include <algorithm>

namespace {
// small deterministic generator: every run (and both emulators) get the same "chips"
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    double uni() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return ((s >> 11) + 0.5) / 9007199254740992.0; }
    double normal() { double u = uni(), v = uni(); return std::sqrt(-2 * std::log(u)) * std::cos(6.283185307179586 * v); }
};
double bessel_i0(double x) {           // for the Kaiser window
    double s = 1, t = 1;
    for (int k = 1; k < 40; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-12 * s) break; }
    return s;
}
const double TAU = 1.8e3 * 2.2e-9;     // I/V converter of the data sheet's circuit: 1.8 kOhm || 2.2 nF
}

// The weight errors of each channel's 16 bit currents, in LSB. The data sheet gives 0.5 LSB
// typical, 1 LSB maximum for the differential and integral linearity errors (TDA1541A/N2): a
// "chip" is drawn at random until its worst error at the major carries is in that range.
void DacOut::make_tables() {
    for (int ch = 0; ch < CH; ch++) {
        double e[16] = {};
        for (int attempt = 0; attempt < 1000; attempt++) {
            Rng r(1541 + ch * 977 + attempt * 31);
            for (int j = 0; j < 16; j++)
                e[j] = j >= 10 ? 0.04 * r.normal()                               // dynamic element matching: very close
                               : 0.18 * std::sqrt(std::ldexp(1.0, j - 9)) * r.normal();   // passive divider
            double worst = 0, sum = 0;
            for (int k = 1; k < 16; k++) { sum += e[k - 1]; worst = std::max(worst, std::fabs(e[k] - sum)); }
            if (worst >= 0.3 && worst <= 0.9) break;
        }
        // analog value of a code (offset binary) = sum of its bits' weights; 0 V at code 0x8000
        double hi[256], lo[256];
        for (int b = 0; b < 256; b++) {
            hi[b] = lo[b] = 0;
            for (int j = 0; j < 8; j++) {
                if (b & (1 << j)) { hi[b] += std::ldexp(1.0, j + 8) + e[j + 8]; lo[b] += std::ldexp(1.0, j) + e[j]; }
            }
        }
        const double mid = hi[0x80] + lo[0];
        for (int b = 0; b < 256; b++) { hi_tab[ch][b] = (float)((hi[b] - mid) / 32768.0); lo_tab[ch][b] = (float)(lo[b] / 32768.0); }
    }
    tabs_made = true;
}

float DacOut::convert(int ch, float x) const {
    // the word the chip gets: 16 bits (lower bits of a longer word are not sent)
    int c = (int)std::floor(x * 32768.0f + 1e-4f);
    c = std::clamp(c, -32768, 32767);
    unsigned ob = (unsigned)(c + 32768);                  // the chip's bits (offset binary inside)
    return hi_tab[ch][ob >> 8] + lo_tab[ch][ob & 255];
}

void DacOut::make_kernel(double in_rate, double out_rate) {
    kernel_rate_in = in_rate; kernel_rate_out = out_rate;
    const double T = 1.0 / in_rate, dt = T / P;
    const bool tda = model == TDA1541A;
    // band limit of the computer's side (its converter): the analog output up to 0.45 x its
    // rate; a perfect converter has nothing above half the machine's rate
    double fc = 0.45 * out_rate;
    if (!tda) fc = std::min(fc, 0.47 * in_rate);
    const double ZC = 24, Ha = ZC / (2 * fc), beta = 9.0;
    const int M = (int)std::ceil(Ha / dt);
    std::vector<double> aa(2 * M + 1);
    const double i0b = bessel_i0(beta);
    for (int m = -M; m <= M; m++) {
        double t = m * dt, x = 2 * fc * t, s = x == 0 ? 1 : std::sin(M_PI * x) / (M_PI * x);
        double r = t / Ha, w = std::fabs(r) >= 1 ? 0 : bessel_i0(beta * std::sqrt(1 - r * r)) / i0b;
        aa[m + M] = 2 * fc * s * w;
    }
    // response of one sample before the band limit: the hold and the I/V pole (TDA1541A), or a
    // single point (perfect converter)
    std::vector<double> h1;
    if (tda) {
        const int J = (int)std::ceil((T + 14 * TAU) / dt);
        h1.resize(J + 1);
        for (int j = 0; j <= J; j++) {
            double t = (j + 0.5) * dt;
            h1[j] = t < T ? 1 - std::exp(-t / TAU) : (std::exp(T / TAU) - 1) * std::exp(-t / TAU);
            h1[j] *= dt;
        }
    } else h1.assign(1, T);
    const int J = (int)h1.size() - 1;
    std::vector<double> h(J + 2 * M + 1, 0.0);
    for (int j = 0; j <= J; j++) {
        const double a = h1[j];
        if (a == 0) continue;
        for (int m = 0; m <= 2 * M; m++) h[j + m] += a * aa[m];
    }
    // the table covers u = (k - M) / P input samples; pad so that it starts and ends on whole samples
    ka = (M + P - 1) / P; kb = (J + M + P - 1) / P + 1;
    kern.assign((size_t)(ka + kb) * P + 2, 0.0f);
    for (size_t k = 0; k < h.size(); k++) kern[k + (size_t)(ka * P - M)] = (float)h[k];
    // exact gain 1 for a constant signal, at every phase
    double sum = 0;
    for (int ph = 0; ph < P; ph++) for (size_t k = ph; k < kern.size(); k += P) sum += kern[k];
    const float g = (float)(P / sum);
    for (float &v : kern) v *= g;
}

void DacOut::reset() {
    for (auto &c : hist) std::fill(c, c + HIST, 0.0f);
    n = 0; t_next = 0;
}

void DacOut::push(const float *x, bool aux, double in_rate, double out_rate, std::vector<float> &main, std::vector<float> &aux_out) {
    if (!tabs_made) make_tables();
    if (in_rate <= 0 || out_rate <= 0) return;
    if (std::fabs(in_rate - kernel_rate_in) > 1e-6 * in_rate || out_rate != kernel_rate_out) make_kernel(in_rate, out_rate);
    // the converters
    n++;
    const int slot = (int)(n & (HIST - 1));
    if (model == TDA1541A) {
        for (int c = 0; c < CH; c++) {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            const float nz = ((noise & 0xffff) / 65535.0f - 0.5f) * 7.8e-6f;     // 110 dB under full scale
            hist[c][slot] = convert(c, x[c]) + nz;
        }
    } else for (int c = 0; c < CH; c++) hist[c][slot] = x[c];
    // output samples whose kernel has all its input samples
    const double step = in_rate / out_rate;
    const int nch = aux ? CH : 2;
    while (t_next + ka <= (double)n) {
        float y[CH] = {};
        const long i0 = (long)std::ceil(t_next - kb), i1 = (long)std::floor(t_next + ka);
        for (long i = std::max(i0, n - HIST + 1); i <= i1; i++) {
            const double u = t_next - i;                       // input samples after sample i
            const double kp = (u + ka) * P;
            const int k = (int)kp;
            if (k < 0 || k + 1 >= (int)kern.size()) continue;
            const float f = (float)(kp - k), w = kern[k] + (kern[k + 1] - kern[k]) * f;
            const int s = (int)(i & (HIST - 1));
            for (int c = 0; c < nch; c++) y[c] += hist[c][s] * w;
        }
        main.push_back(y[0]); main.push_back(y[1]);
        if (aux) for (int c = 2; c < CH; c++) aux_out.push_back(y[c]);
        t_next += step;
    }
    if (n >= (1L << 24)) { n -= 1L << 24; t_next -= (double)(1L << 24); }   // (a multiple of HIST: the ring is unchanged)
}
