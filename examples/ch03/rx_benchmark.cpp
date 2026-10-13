// rx_benchmark: sustained RX throughput of QPSK SDR Lab over the current link
// (USB or Ethernet). For every sample rate it streams for a few seconds,
// measures how many samples per second reach the PC and reads the overflow
// flag of the RX DMA in the FPGA. Receive only: no TX buffer is created;
// only the common sample rate is changed and restored at the end.
#include <ad9361.h>
#include <iio.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const char* kUsage =
    "Usage: rx_benchmark --uri URI [--rates HZ,HZ,...] [--channels 1|2|both]\n"
    "                    [--seconds T] [--buffer N]\n";
// AXI ADC core status register: bit 2 = RX DMA overflow, samples lost.
// Writing the bits back clears them (see libiio examples/iio_adi_xflow_check.c).
constexpr std::uint32_t kStatusReg = 0x80000088;
constexpr std::uint32_t kOverflow = 0x4;

double number(const std::string& text) {
    std::size_t used = 0;
    const double v = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(v)) throw std::invalid_argument("Bad number: " + text);
    return v;
}

void check(long err, const std::string& what) {
    if (err < 0) throw std::runtime_error(what + " failed, error " + std::to_string(-err));
}

struct Point {
    double fs;       // requested sample rate, S/s
    int channels;    // 1 or 2 receivers
    double got;      // samples per second per channel that reached the PC
    double mbytes;   // payload, MB/s
    bool overflow;   // the FPGA had to drop samples
};

Point measure(iio_context* ctx, iio_device* phy, iio_device* rx, double fs, int channels, std::size_t n,
              double seconds) {
    check(ad9361_set_bb_rate(phy, static_cast<unsigned long>(fs)), "ad9361_set_bb_rate");
    // RX1 = voltage0/1 (I/Q), RX2 = voltage2/3.
    for (int k = 0; k < 4; ++k) {
        iio_channel* ch = iio_device_find_channel(rx, ("voltage" + std::to_string(k)).c_str(), false);
        if (!ch) throw std::runtime_error("RX stream channel voltage" + std::to_string(k) + " not found");
        if (k < 2 * channels)
            iio_channel_enable(ch);
        else
            iio_channel_disable(ch);
    }
    iio_context_set_timeout(ctx, static_cast<unsigned>(3000 + 2000.0 * n / fs));
    std::unique_ptr<iio_buffer, decltype(&iio_buffer_destroy)> buf(
        iio_device_create_buffer(rx, n, false), iio_buffer_destroy);
    if (!buf) throw std::runtime_error("Cannot create RX buffer");

    // The first block only starts the flow; the clock starts after it.
    check(iio_buffer_refill(buf.get()), "RX refill");
    check(iio_device_reg_write(rx, kStatusReg, kOverflow), "clear overflow flag");
    const auto t0 = std::chrono::steady_clock::now();
    double bytes = 0, elapsed = 0;
    do {
        const ssize_t got = iio_buffer_refill(buf.get());
        check(got, "RX refill");
        bytes += static_cast<double>(got);
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    } while (elapsed < seconds);
    std::uint32_t status = 0;
    check(iio_device_reg_read(rx, kStatusReg, &status), "read overflow flag");

    const double sample_bytes = 4.0 * channels;  // 16-bit I + 16-bit Q per receiver
    return {fs, channels, bytes / sample_bytes / elapsed, bytes / elapsed / 1e6, (status & kOverflow) != 0};
}
}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> opt = {{"--rates", "1e6,2.5e6,5e6,7.5e6,10e6,15e6,20e6,30.72e6"},
                                              {"--channels", "both"},
                                              {"--seconds", "3"},
                                              {"--buffer", "262144"}};
    try {
        for (int k = 1; k < argc; k += 2) {
            if (k + 1 >= argc || std::string(argv[k]).rfind("--", 0) != 0) throw std::invalid_argument(kUsage);
            opt[argv[k]] = argv[k + 1];
        }
        if (!opt.count("--uri")) throw std::invalid_argument(kUsage);
        std::vector<double> rates;
        std::stringstream list(opt["--rates"]);
        for (std::string item; std::getline(list, item, ',');) {
            const double fs = number(item);
            if (!(fs >= 0.52e6 && fs <= 30.72e6)) throw std::invalid_argument("Rates must be 0.52e6..30.72e6");
            rates.push_back(fs);
        }
        const std::string which = opt["--channels"];
        if (which != "1" && which != "2" && which != "both") throw std::invalid_argument(kUsage);
        const double seconds = number(opt["--seconds"]), buffer = number(opt["--buffer"]);
        if (!(seconds >= 0.5 && seconds <= 60)) throw std::invalid_argument("--seconds: 0.5..60");
        if (!(buffer >= 4096 && buffer <= 4194304)) throw std::invalid_argument("--buffer: 4096..4194304");

        std::unique_ptr<iio_context, decltype(&iio_context_destroy)> ctx(
            iio_create_context_from_uri(opt["--uri"].c_str()), iio_context_destroy);
        if (!ctx) throw std::runtime_error("Cannot open " + opt["--uri"]);
        iio_device* phy = iio_context_find_device(ctx.get(), "ad9361-phy");
        iio_device* rx = iio_context_find_device(ctx.get(), "cf-ad9361-lpc");
        if (!phy || !rx) throw std::runtime_error("ad9361-phy / cf-ad9361-lpc not found");
        iio_channel* ctl = iio_device_find_channel(phy, "voltage0", false);
        long long fs_initial = 0;
        check(iio_channel_attr_read_longlong(ctl, "sampling_frequency", &fs_initial), "read sampling_frequency");

        std::printf("link %s, buffer %.0f samples, %.1f s per point\n", opt["--uri"].c_str(), buffer, seconds);
        std::printf("RX  Fs, MS/s  received, MS/s  payload, MB/s  overflow  result\n");
        std::vector<int> sets = which == "1" ? std::vector<int>{1} : which == "2" ? std::vector<int>{2}
                                                                                  : std::vector<int>{1, 2};
        int code = 0;
        try {
            for (int channels : sets)
                for (double fs : rates) {
                    const Point p = measure(ctx.get(), phy, rx, fs, channels, static_cast<std::size_t>(buffer),
                                            seconds);
                    const bool ok = !p.overflow && p.got > 0.99 * p.fs;
                    std::printf("%d   %10.3f  %14.3f  %13.1f  %-8s  %s\n", p.channels, p.fs / 1e6, p.got / 1e6,
                                p.mbytes, p.overflow ? "yes" : "no", ok ? "continuous" : "SAMPLES LOST");
                }
        } catch (const std::exception& e) {
            std::cerr << "rx_benchmark: " << e.what() << '\n';
            code = 1;
        }
        // Leave the board at the sample rate it had before the test.
        ad9361_set_bb_rate(phy, static_cast<unsigned long>(fs_initial));
        std::printf("Sample rate restored to %.6f MS/s\n", fs_initial / 1e6);
        return code;
    } catch (const std::exception& e) {
        std::cerr << "rx_benchmark: " << e.what() << '\n';
        return 1;
    }
}
