// fm_demod: mono broadcast FM from an IQ record to a WAV file.
// Chain: NCO shift -> low-pass + decimation -> phase-difference discriminator
// -> de-emphasis -> audio low-pass + decimation -> 16-bit WAV.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "lab/dsp.hpp"
#include "lab/iq_file.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
const char* kUsage = "Usage: fm_demod IN.cf32 OUT.wav [--offset HZ] [--deemph US]\n";

void write_wav(const std::string& path, const std::vector<float>& audio, unsigned rate) {
    if (std::filesystem::exists(path)) throw std::runtime_error("Output exists: " + path);
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) out.put(static_cast<char>(v >> 8 * k)); };
    auto u16 = [&](std::uint16_t v) { out.put(static_cast<char>(v)); out.put(static_cast<char>(v >> 8)); };
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(audio.size() * 2);
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);  // PCM, mono, 16 bit
    out.write("data", 4); u32(data_bytes);
    for (float v : audio) u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(32767 * std::clamp(v, -1.0f, 1.0f)))));
    if (!out) throw std::runtime_error("Write failed: " + path);
}

// Level of the 19 kHz stereo pilot above the median of the composite spectrum.
float pilot_db(const std::vector<float>& composite, double rate) {
    const std::size_t n = 8192;
    if (composite.size() < n) return 0;
    std::vector<lab::cf32> c(composite.begin(), composite.end());
    const auto s = lab::averaged_spectrum_db(c, n, 32);
    const auto bin = static_cast<std::size_t>(n / 2 + std::lround(19000.0 * n / rate));
    float peak = -300;
    for (std::size_t k = bin - 3; k <= bin + 3; ++k) peak = std::max(peak, s[k]);
    std::vector<float> sorted(s.begin() + n / 2, s.end());
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    return peak - sorted[sorted.size() / 2];
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc % 2 == 0) throw std::invalid_argument(kUsage);
        double offset = 0, deemph_us = 50;  // 50 us de-emphasis is used in Europe and Russia
        for (int k = 3; k < argc; k += 2) {
            const std::string key = argv[k];
            if (key == "--offset") offset = std::stod(argv[k + 1]);
            else if (key == "--deemph") deemph_us = std::stod(argv[k + 1]);
            else throw std::invalid_argument(kUsage);
        }
        lab::IqMeta meta;
        std::vector<lab::cf32> x = lab::read_iq(argv[1], meta);
        const double fs = meta.sample_rate_hz;
        if (fs < 240e3) throw std::invalid_argument("Need Fs >= 240 kS/s for broadcast FM");
        if (std::abs(offset) > fs / 2 - 100e3) throw std::invalid_argument("Station is outside the record");

        // 1. NCO: move the station from +offset to 0 Hz (chapter 4, digital downmix).
        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] *= std::polar(1.0f, static_cast<float>(-2 * kPi * offset * n / fs));

        // 2. Keep +-100 kHz and decimate to about 240..300 kS/s.
        const auto d1 = std::max<std::size_t>(1, static_cast<std::size_t>(fs / 240e3));
        const double fs1 = fs / d1;
        const auto baseband = lab::decimate(x, lab::fir_lowpass(129, 100e3 / fs), d1);

        // 3. Discriminator: instantaneous frequency, normalized to 75 kHz deviation.
        std::vector<float> composite(baseband.size(), 0.0f);
        for (std::size_t n = 1; n < baseband.size(); ++n)
            composite[n] = static_cast<float>(std::arg(baseband[n] * std::conj(baseband[n - 1])) * fs1 / (2 * kPi) / 75e3);

        // 4. De-emphasis: first-order low-pass with time constant tau.
        const float a = static_cast<float>(std::exp(-1.0 / (fs1 * deemph_us * 1e-6)));
        std::vector<float> audio(composite.size());
        float state = 0;
        for (std::size_t n = 0; n < composite.size(); ++n) audio[n] = state = (1 - a) * composite[n] + a * state;

        // 5. Audio band 15 kHz, rate close to 48 kHz.
        const auto d2 = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(fs1 / 48e3)));
        const auto rate = static_cast<unsigned>(std::lround(fs1 / d2));
        audio = lab::decimate(audio, lab::fir_lowpass(101, 15e3 / fs1), d2);

        // Remove DC (carrier offset) and normalize the peak to 0.9.
        double mean = 0;
        for (float v : audio) mean += v;
        mean /= audio.size();
        float peak = 1e-6f;
        for (float& v : audio) v -= static_cast<float>(mean), peak = std::max(peak, std::abs(v));
        for (float& v : audio) v *= 0.9f / peak;
        write_wav(argv[2], audio, rate);

        std::printf("Fs %.0f -> %.0f (x1/%zu) -> audio %u S/s (x1/%zu), %.2f s\n", fs, fs1, d1, rate, d2,
                    audio.size() / static_cast<double>(rate));
        std::printf("Mean frequency offset of the station: %+.0f Hz\n", mean * 75e3);
        const float pilot = pilot_db(composite, fs1);
        std::printf("Stereo pilot 19 kHz: %.1f dB above the composite noise%s\n", pilot,
                    pilot > 15 ? " (broadcast FM detected)" : "");
        std::cout << "Saved " << argv[2] << '\n';
    } catch (const std::exception& e) {
        std::cerr << "fm_demod: " << e.what() << '\n';
        return 1;
    }
}
