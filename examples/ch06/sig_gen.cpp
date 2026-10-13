// sig_gen: test signals for the TX->RX experiment (tone, AM, FM) in the
// course IQ format. The record is meant to be played in a cyclic TX buffer,
// so the program checks that it holds an integer number of periods.
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "lab/iq_file.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
const char* kUsage =
    "Usage: sig_gen --type tone|am|fm --out FILE.cf32 [--rate HZ] [--offset HZ]\n"
    "               [--amplitude A] [--mod-freq HZ] [--depth MU] [--deviation HZ] [--samples N]\n";

double number(const std::string& text) {
    std::size_t used = 0;
    const double v = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(v)) throw std::invalid_argument("Bad number: " + text);
    return v;
}

// True if f*N/Fs is an integer: the buffer then wraps without a phase jump.
bool whole_periods(double f, double n, double fs) {
    const double cycles = f * n / fs;
    return std::abs(cycles - std::round(cycles)) < 1e-6;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> opt = {
            {"--rate", "1e6"}, {"--offset", "100e3"}, {"--amplitude", "0.5"}, {"--mod-freq", "1e3"},
            {"--depth", "0.5"}, {"--deviation", "75e3"}, {"--samples", "100000"}};
        for (int k = 1; k < argc; k += 2) {
            if (k + 1 >= argc || std::string(argv[k]).rfind("--", 0) != 0) throw std::invalid_argument(kUsage);
            opt[argv[k]] = argv[k + 1];
        }
        if (!opt.count("--type") || !opt.count("--out")) throw std::invalid_argument(kUsage);
        const std::string type = opt["--type"];
        const double fs = number(opt["--rate"]), fc = number(opt["--offset"]), a = number(opt["--amplitude"]);
        const double fm = number(opt["--mod-freq"]), mu = number(opt["--depth"]), dev = number(opt["--deviation"]);
        const double count = number(opt["--samples"]);
        if (type != "tone" && type != "am" && type != "fm") throw std::invalid_argument(kUsage);
        if (!(fs > 0 && a > 0 && count >= 1000 && count <= 10000000))
            throw std::invalid_argument("Require Fs > 0, A > 0, 1000..10000000 samples");

        // Occupied band: tone = line, AM = fc +- fm, FM = Carson's rule.
        const double half_band = type == "tone" ? 0 : type == "am" ? fm : dev + fm;
        if (std::abs(fc) + half_band >= fs / 2) throw std::invalid_argument("Signal does not fit into +-Fs/2");
        const double peak = type == "am" ? a * (1 + mu) : a;  // largest |x[n]|
        if (peak > 1) throw std::invalid_argument("Peak amplitude above full scale 1.0");
        if (type == "am" && !(mu > 0 && mu <= 1)) throw std::invalid_argument("AM depth must be 0 < mu <= 1");

        const auto n = static_cast<std::size_t>(count);
        std::vector<lab::cf32> x(n);
        const double beta = dev / fm;  // FM modulation index
        for (std::size_t k = 0; k < n; ++k) {
            const double t = k / fs, carrier = 2 * kPi * fc * t, m = 2 * kPi * fm * t;
            double mag = a, phase = carrier;
            if (type == "am") mag = a * (1 + mu * std::cos(m));
            if (type == "fm") phase = carrier + beta * std::sin(m);
            x[k] = lab::cf32(static_cast<float>(mag * std::cos(phase)), static_cast<float>(mag * std::sin(phase)));
        }

        const bool periodic = whole_periods(fc, count, fs) && (type == "tone" || whole_periods(fm, count, fs));
        lab::IqMeta meta;
        meta.sample_rate_hz = fs;
        meta.add("signal", type);
        meta.add("offset_hz", fc);
        meta.add("amplitude_linear", a);
        if (type != "tone") meta.add("modulating_frequency_hz", fm);
        if (type == "am") meta.add("am_depth", mu);
        if (type == "fm") meta.add("fm_deviation_hz", dev);
        meta.add("whole_periods", periodic ? "yes" : "no");
        meta.add("program", "sig_gen");
        lab::write_iq(opt["--out"], x, meta);

        std::printf("%s: %zu samples, %.4f s, offset %+.0f Hz, peak %.3f FS\n", type.c_str(), n, n / fs, fc, peak);
        if (type == "am") std::printf("AM: sidebands at +-%.0f Hz, %.2f dB below the carrier\n", fm, -20 * std::log10(mu / 2));
        if (type == "fm") std::printf("FM: beta = %.1f, Carson bandwidth %.0f Hz\n", beta, 2 * (dev + fm));
        if (!periodic)
            std::printf("WARNING: not a whole number of periods; a cyclic TX buffer will produce a phase jump\n");
    } catch (const std::exception& e) {
        std::cerr << "sig_gen: " << e.what() << '\n';
        return 1;
    }
}
