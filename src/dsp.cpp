#include "lab/dsp.hpp"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace lab {
namespace {
constexpr double kPi = 3.14159265358979323846;

float to_db(float power) { return 10.0f * std::log10(std::max(power, 1e-20f)); }

template <typename T>
std::vector<T> decimate_impl(const std::vector<T>& x, const std::vector<float>& h, std::size_t d) {
    if (d == 0 || h.empty()) throw std::invalid_argument("decimate: bad parameters");
    std::vector<T> y;
    y.reserve(x.size() / d + 1);
    // Only the kept outputs are computed: y[m] = sum_k h[k] x[m*d - k].
    for (std::size_t n = 0; n < x.size(); n += d) {
        T acc{};
        const std::size_t taps = std::min(h.size(), n + 1);
        for (std::size_t k = 0; k < taps; ++k) acc += h[k] * x[n - k];
        y.push_back(acc);
    }
    return y;
}
}  // namespace

std::vector<float> hann(std::size_t n) {
    std::vector<float> w(n);
    for (std::size_t k = 0; k < n; ++k)
        w[k] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * k / n));
    return w;
}

Spectrum::Spectrum(std::size_t n) : n_(n), window_(hann(n)) {
    if (n < 16) throw std::invalid_argument("FFT size must be at least 16");
    const double gc = std::accumulate(window_.begin(), window_.end(), 0.0) / n;  // coherent gain
    norm_ = static_cast<float>(1.0 / std::pow(n * gc, 2));
    in_ = fftwf_alloc_complex(n);
    out_ = fftwf_alloc_complex(n);
    plan_ = fftwf_plan_dft_1d(static_cast<int>(n), static_cast<fftwf_complex*>(in_),
                              static_cast<fftwf_complex*>(out_), FFTW_FORWARD, FFTW_ESTIMATE);
}

Spectrum::~Spectrum() {
    fftwf_destroy_plan(static_cast<fftwf_plan>(plan_));
    fftwf_free(in_);
    fftwf_free(out_);
}

void Spectrum::power(const cf32* block, std::vector<float>& out) {
    auto* in = static_cast<fftwf_complex*>(in_);
    auto* spec = static_cast<fftwf_complex*>(out_);
    for (std::size_t k = 0; k < n_; ++k) {
        in[k][0] = block[k].real() * window_[k];
        in[k][1] = block[k].imag() * window_[k];
    }
    fftwf_execute(static_cast<fftwf_plan>(plan_));
    out.resize(n_);
    const std::size_t half = n_ / 2;
    for (std::size_t k = 0; k < n_; ++k) {
        const std::size_t src = (k + half) % n_;  // fftshift: -Fs/2 first
        out[k] = (spec[src][0] * spec[src][0] + spec[src][1] * spec[src][1]) * norm_;
    }
}

std::vector<float> averaged_spectrum_db(const std::vector<cf32>& x, std::size_t nfft,
                                        std::size_t max_blocks) {
    const std::size_t blocks = std::min(max_blocks, x.size() / nfft);
    if (blocks == 0) throw std::invalid_argument("Record is shorter than one FFT block");
    Spectrum fft(nfft);
    std::vector<float> sum(nfft, 0.0f), p;
    for (std::size_t b = 0; b < blocks; ++b) {
        fft.power(&x[b * nfft], p);
        for (std::size_t k = 0; k < nfft; ++k) sum[k] += p[k];
    }
    for (float& v : sum) v = to_db(v / blocks);
    return sum;
}

std::vector<std::vector<float>> waterfall_db(const std::vector<cf32>& x, std::size_t nfft,
                                             std::size_t max_rows) {
    const std::size_t blocks = x.size() / nfft;
    if (blocks == 0) throw std::invalid_argument("Record is shorter than one FFT block");
    const std::size_t rows = std::min(blocks, max_rows);
    Spectrum fft(nfft);
    std::vector<std::vector<float>> image(rows);
    for (std::size_t r = 0; r < rows; ++r) {
        const std::size_t b = r * blocks / rows;  // evenly spaced blocks in time
        fft.power(&x[b * nfft], image[r]);
        for (float& v : image[r]) v = to_db(v);
    }
    return image;
}

std::vector<float> fir_lowpass(std::size_t taps, double cutoff) {
    if (taps % 2 == 0 || cutoff <= 0 || cutoff >= 0.5)
        throw std::invalid_argument("fir_lowpass: odd taps and 0 < cutoff < 0.5 required");
    std::vector<float> h(taps);
    const double mid = (taps - 1) / 2.0;
    double sum = 0;
    for (std::size_t k = 0; k < taps; ++k) {
        const double t = k - mid;
        const double sinc = t == 0 ? 2 * cutoff : std::sin(2 * kPi * cutoff * t) / (kPi * t);
        const double blackman = 0.42 - 0.5 * std::cos(2 * kPi * k / (taps - 1)) +
                                0.08 * std::cos(4 * kPi * k / (taps - 1));
        h[k] = static_cast<float>(sinc * blackman);
        sum += h[k];
    }
    for (float& v : h) v = static_cast<float>(v / sum);  // unity gain at 0 Hz
    return h;
}

std::vector<cf32> decimate(const std::vector<cf32>& x, const std::vector<float>& h, std::size_t d) {
    return decimate_impl(x, h, d);
}
std::vector<float> decimate(const std::vector<float>& x, const std::vector<float>& h, std::size_t d) {
    return decimate_impl(x, h, d);
}

}  // namespace lab
