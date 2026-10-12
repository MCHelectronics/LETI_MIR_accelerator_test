// Test signal: FM with a 1 kHz tone, deviation 75 kHz, at +200 kHz offset.
#include <cmath>
#include <iostream>

#include "lab/iq_file.hpp"

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: gen_fm OUT.cf32\n"; return 2; }
    constexpr double kPi = 3.14159265358979323846, fs = 1e6, offset = 200e3, fm = 1e3, dev = 75e3;
    std::vector<lab::cf32> x(500000);
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double t = n / fs;
        const double phase = 2 * kPi * offset * t + dev / fm * std::sin(2 * kPi * fm * t);
        x[n] = std::polar(0.5f, static_cast<float>(phase));
    }
    lab::IqMeta meta;
    meta.sample_rate_hz = fs;
    meta.center_frequency_hz = 100e6;
    meta.add("program", "gen_fm (test signal)");
    lab::write_iq(argv[1], x, meta);
}
