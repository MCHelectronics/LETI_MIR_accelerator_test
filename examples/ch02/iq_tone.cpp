#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
double number(const char* text) {
    std::size_t used = 0;
    const std::string input(text);
    const double value = std::stod(input, &used);
    if (used != input.size() || !std::isfinite(value))
        throw std::invalid_argument("Expected a finite number: " + input);
    return value;
}

// Write explicitly little-endian IEEE-754, without struct padding assumptions.
void write_float(std::ostream& out, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    std::uint32_t word = 0;
    std::memcpy(&word, &value, sizeof(word));
    unsigned char bytes[4];
    for (unsigned k = 0; k < 4; ++k) bytes[k] = (word >> (8 * k)) & 0xff;
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            std::cerr << "Usage: iq_tone OUTPUT.cf32 FS_HZ TONE_HZ DURATION_S AMPLITUDE\n";
            return 2;
        }
        const double fs = number(argv[2]), f = number(argv[3]);
        const double duration = number(argv[4]), amplitude = number(argv[5]);
        if (!(fs > 0 && duration > 0 && amplitude > 0 && amplitude <= 1) ||
            std::abs(f) >= fs / 2)
            throw std::invalid_argument("Require Fs,T>0, 0<A<=1 and |f|<Fs/2");
        const double count = std::round(fs * duration);
        // Deliberate limit for this small teaching generator (80 MB).
        if (!std::isfinite(count) || count < 1 || count > 10000000)
            throw std::invalid_argument("Require 1..10000000 samples");
        const auto n_samples = static_cast<std::uint64_t>(count);
        const std::string path = argv[1], meta_path = path + ".json";
        if (std::filesystem::exists(path) || std::filesystem::exists(meta_path))
            throw std::runtime_error("Output exists; choose a new filename");
        std::ofstream output, metadata;
        output.exceptions(std::ios::failbit | std::ios::badbit);
        metadata.exceptions(std::ios::failbit | std::ios::badbit);
        output.open(path, std::ios::binary);
        metadata.open(meta_path);
        constexpr double tau = 6.2831853071795864769;
        for (std::uint64_t n = 0; n < n_samples; ++n) {
            const double phase = tau * (f / fs) * static_cast<double>(n);
            write_float(output, static_cast<float>(amplitude * std::cos(phase)));
            write_float(output, static_cast<float>(amplitude * std::sin(phase)));
        }
        output.close();
        metadata << std::setprecision(17)
                 << "{\n  \"format\": \"cf32_le_iq\",\n"
                 << "  \"sample_rate_hz\": " << fs << ",\n"
                 << "  \"tone_frequency_hz\": " << f << ",\n"
                 << "  \"requested_duration_s\": " << duration << ",\n"
                 << "  \"duration_s\": " << n_samples / fs << ",\n"
                 << "  \"sample_count\": " << n_samples << ",\n"
                 << "  \"amplitude_linear\": " << amplitude << "\n}\n";
        metadata.close();
        std::cout << n_samples << " complex samples, " << n_samples * 8 << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << "iq_tone: " << e.what() << '\n';
        return 1;
    }
}
