#pragma once
// Minimal DSP toolkit of the course: windowed FFT (FFTW3), averaged spectrum,
// waterfall rows, FIR low-pass design and decimation.
#include <cstddef>
#include <vector>

#include "lab/iq_file.hpp"

namespace lab {

// Hann window of length n.
std::vector<float> hann(std::size_t n);

// Windowed FFT of length n. Output is fftshift-ed: bin 0 is -Fs/2.
// Levels are in dBFS for a complex tone: amplitude A gives 20*log10(A)
// (normalization |X|/(N*Gc), see chapter 4).
class Spectrum {
public:
    explicit Spectrum(std::size_t n);
    ~Spectrum();
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;

    std::size_t size() const { return n_; }
    // Power |X|^2/(N*Gc)^2 of one block of n samples, fftshift-ed.
    void power(const cf32* block, std::vector<float>& out);

private:
    std::size_t n_;
    std::vector<float> window_;
    float norm_;     // 1/(N*Gc)^2
    void* in_;       // fftwf_complex*
    void* out_;      // fftwf_complex*
    void* plan_;     // fftwf_plan
};

// Average of up to max_blocks consecutive spectra, in dBFS.
std::vector<float> averaged_spectrum_db(const std::vector<cf32>& x, std::size_t nfft,
                                        std::size_t max_blocks);

// One dBFS row per consecutive block, at most max_rows rows (evenly spaced).
std::vector<std::vector<float>> waterfall_db(const std::vector<cf32>& x, std::size_t nfft,
                                             std::size_t max_rows);

// Windowed-sinc low-pass FIR, cutoff as a fraction of the sample rate (0..0.5).
std::vector<float> fir_lowpass(std::size_t taps, double cutoff);

// Filter with h and keep every d-th output sample.
std::vector<cf32> decimate(const std::vector<cf32>& x, const std::vector<float>& h, std::size_t d);
std::vector<float> decimate(const std::vector<float>& x, const std::vector<float>& h, std::size_t d);

}  // namespace lab
