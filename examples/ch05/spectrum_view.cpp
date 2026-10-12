// spectrum_view: three views of one IQ record (time, averaged spectrum,
// waterfall) saved as PNG, plus the strongest spectral lines in the terminal.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "lab/dsp.hpp"
#include "lab/iq_file.hpp"
#include "lab/plot.hpp"

namespace {
const char* kUsage =
    "Usage: spectrum_view FILE.cf32 [--nfft N] [--avg M] [--peaks K] [--out PREFIX]\n"
    "                     [--time-ms T] [--rows R]\n";

struct Options {
    std::string file, prefix;
    std::size_t nfft = 4096, avg = 64, peaks = 5, rows = 400;
    double time_ms = 0.2;
};

Options parse(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]).rfind("--", 0) == 0) throw std::invalid_argument(kUsage);
    Options o;
    o.file = argv[1];
    o.prefix = o.file.size() > 5 && o.file.substr(o.file.size() - 5) == ".cf32"
                   ? o.file.substr(0, o.file.size() - 5) : o.file;
    for (int k = 2; k < argc; k += 2) {
        if (k + 1 >= argc) throw std::invalid_argument(kUsage);
        const std::string key = argv[k], value = argv[k + 1];
        if (key == "--nfft") o.nfft = std::stoul(value);
        else if (key == "--avg") o.avg = std::stoul(value);
        else if (key == "--peaks") o.peaks = std::stoul(value);
        else if (key == "--rows") o.rows = std::stoul(value);
        else if (key == "--time-ms") o.time_ms = std::stod(value);
        else if (key == "--out") o.prefix = value;
        else throw std::invalid_argument(kUsage);
    }
    return o;
}

struct Peak { std::size_t bin; float level; };

// Local maxima at least 10 dB above the median (noise) level, strongest first,
// separated by at least 8 bins.
std::vector<Peak> find_peaks(const std::vector<float>& s, std::size_t count) {
    std::vector<float> sorted(s);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const float floor = sorted[sorted.size() / 2];
    std::vector<Peak> all;
    for (std::size_t k = 1; k + 1 < s.size(); ++k)
        if (s[k] > s[k - 1] && s[k] >= s[k + 1] && s[k] > floor + 10) all.push_back({k, s[k]});
    std::sort(all.begin(), all.end(), [](const Peak& a, const Peak& b) { return a.level > b.level; });
    std::vector<Peak> kept;
    for (const Peak& p : all) {
        const bool near = std::any_of(kept.begin(), kept.end(), [&](const Peak& q) {
            return (p.bin > q.bin ? p.bin - q.bin : q.bin - p.bin) < 8; });
        if (!near) kept.push_back(p);
        if (kept.size() == count) break;
    }
    return kept;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        lab::IqMeta meta;
        const std::vector<lab::cf32> x = lab::read_iq(o.file, meta);
        const double fs = meta.sample_rate_hz, f0 = meta.center_frequency_hz;
        const bool rf = std::isfinite(f0);  // absolute RF axis only if f0 is known
        const double df = fs / o.nfft;
        const std::size_t blocks = std::min(o.avg, x.size() / o.nfft);

        std::printf("%zu samples, Fs = %.0f S/s, duration %.3f s\n", x.size(), fs, x.size() / fs);
        std::printf("N = %zu, df = Fs/N = %.1f Hz, block %.3f ms, averaged %zu blocks\n",
                    o.nfft, df, 1e3 * o.nfft / fs, blocks);

        // Averaged spectrum and the frequency of every shifted bin:
        // f_k = f0 + (k - N/2) * Fs/N.
        const std::vector<float> s = lab::averaged_spectrum_db(x, o.nfft, o.avg);
        std::vector<float> axis(o.nfft);
        for (std::size_t k = 0; k < o.nfft; ++k) {
            const double offset = (static_cast<double>(k) - o.nfft / 2.0) * df;
            axis[k] = static_cast<float>(rf ? (f0 + offset) / 1e6 : offset / 1e3);
        }
        const std::string x_label = rf ? "Frequency, MHz" : "Offset from 0 Hz, kHz";

        std::cout << "Strongest lines (offset from f0, level):\n";
        for (const auto& p : find_peaks(s, o.peaks)) {
            const double offset = (static_cast<double>(p.bin) - o.nfft / 2.0) * df;
            std::printf("  %+12.1f Hz  %7.2f dBFS", offset, p.level);
            if (rf) std::printf("   (RF %.6f MHz)", (f0 + offset) / 1e6);
            std::printf("\n");
        }

        // Time view: the first time_ms milliseconds of I and Q.
        const std::size_t nt = std::clamp<std::size_t>(static_cast<std::size_t>(o.time_ms * 1e-3 * fs), 2, x.size());
        std::vector<float> t(nt), i(nt), q(nt);
        for (std::size_t k = 0; k < nt; ++k) t[k] = static_cast<float>(1e3 * k / fs), i[k] = x[k].real(), q[k] = x[k].imag();
        lab::save_line_plot(o.prefix + "_time.png", "Time domain (first samples)", t,
                            {{i, 200, 80, 0, "I"}, {q, 0, 80, 220, "Q"}}, "Time, ms", "Amplitude, FS");

        char title[96];
        std::snprintf(title, sizeof title, "Averaged spectrum: N=%zu, Navg=%zu, Hann", o.nfft, blocks);
        lab::save_line_plot(o.prefix + "_spectrum.png", title, axis, {{s, 160, 60, 0, ""}}, x_label, "dBFS");

        const auto rows = lab::waterfall_db(x, o.nfft, o.rows);
        const float row_time = static_cast<float>((x.size() / o.nfft) * o.nfft / fs / rows.size());
        lab::save_waterfall(o.prefix + "_waterfall.png", "Waterfall", rows, axis.front(), axis.back(),
                            x_label, row_time);
        std::cout << "Images: " << o.prefix << "_{time,spectrum,waterfall}.png\n";
    } catch (const std::exception& e) {
        std::cerr << "spectrum_view: " << e.what() << '\n';
        return 1;
    }
}
