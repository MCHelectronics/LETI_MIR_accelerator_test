// tx_play: transmits an IQ record from a cyclic TX buffer of QPSK SDR Lab.
// Safety order: nothing is touched without --go; the TX attenuation is set to
// the maximum before any other setting; on exit (also on Ctrl+C) the
// attenuation returns to the maximum, the buffer is destroyed and TX LO is
// powered down. Use only after the GO decision of chapter 1.
#include <ad9361.h>
#include <iio.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "lab/iq_file.hpp"

namespace {
const char* kUsage =
    "Usage: tx_play --uri URI --freq HZ --in FILE.cf32 --tx-gain DB --go\n"
    "               [--seconds T] [--bw HZ] [--channel 1|2]\n"
    "  --tx-gain: -89.75..0 dB (0 = maximum output power)\n"
    "  --go: confirms the GO decision for the cable path (chapter 1)\n";
constexpr double kMaxAttenuation = -89.75;
volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

double number(const std::string& text) {
    std::size_t used = 0;
    const double v = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(v)) throw std::invalid_argument("Bad number: " + text);
    return v;
}

void check(int err, const std::string& what) {
    if (err < 0) throw std::runtime_error(what + " failed, error " + std::to_string(-err));
}
}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> opt = {{"--seconds", "10"}, {"--channel", "1"}};
    bool go = false;
    try {
        for (int k = 1; k < argc; ++k) {
            const std::string key = argv[k];
            if (key == "--go") { go = true; continue; }
            if (k + 1 >= argc || key.rfind("--", 0) != 0) throw std::invalid_argument(kUsage);
            opt[key] = argv[++k];
        }
        for (const char* need : {"--uri", "--freq", "--in", "--tx-gain"})
            if (!opt.count(need)) throw std::invalid_argument(kUsage);
        if (!go) {
            std::cerr << "tx_play: transmission is not confirmed. Check the level budget of chapter 1\n"
                         "and the passive cable path with an attenuator, then add --go.\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "tx_play: " << e.what() << '\n';
        return 2;
    }

    iio_channel* ctl = nullptr;  // TX attenuation of the chosen channel
    iio_channel* lo = nullptr;   // TX LO
    std::unique_ptr<iio_context, decltype(&iio_context_destroy)> ctx(nullptr, iio_context_destroy);
    int code = 0;
    try {
        const double freq = number(opt["--freq"]), gain = number(opt["--tx-gain"]);
        const double seconds = number(opt["--seconds"]);
        const int channel = static_cast<int>(number(opt["--channel"]));
        if (!(gain >= kMaxAttenuation && gain <= 0)) throw std::invalid_argument("--tx-gain must be -89.75..0");
        if (channel != 1 && channel != 2) throw std::invalid_argument("--channel must be 1 or 2");
        if (!(seconds > 0 && seconds <= 600)) throw std::invalid_argument("--seconds must be 0..600");

        lab::IqMeta meta;
        const std::vector<lab::cf32> x = lab::read_iq(opt["--in"], meta);
        for (const auto& v : x)
            if (std::abs(v.real()) > 1 || std::abs(v.imag()) > 1) throw std::runtime_error("Samples exceed full scale 1.0");
        const double rate = meta.sample_rate_hz;
        const double bw = opt.count("--bw") ? number(opt["--bw"]) : rate;

        ctx.reset(iio_create_context_from_uri(opt["--uri"].c_str()));
        if (!ctx) throw std::runtime_error("Cannot open " + opt["--uri"]);
        iio_device* phy = iio_context_find_device(ctx.get(), "ad9361-phy");
        iio_device* tx = iio_context_find_device(ctx.get(), "cf-ad9361-dds-core-lpc");
        if (!phy || !tx) throw std::runtime_error("ad9361-phy / cf-ad9361-dds-core-lpc not found");
        ctl = iio_device_find_channel(phy, channel == 1 ? "voltage0" : "voltage1", true);
        lo = iio_device_find_channel(phy, "altvoltage1", true);  // TX_LO
        if (!ctl || !lo) throw std::runtime_error("TX control channels not found");
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        // 1. Maximum attenuation before anything else.
        check(iio_channel_attr_write_double(ctl, "hardwaregain", kMaxAttenuation), "hardwaregain");
        // 2. Sample rate (and FIR), analog bandwidth, LO.
        check(ad9361_set_bb_rate(phy, static_cast<unsigned long>(rate)), "ad9361_set_bb_rate");
        check(iio_channel_attr_write_longlong(ctl, "rf_bandwidth", static_cast<long long>(bw)), "rf_bandwidth");
        iio_channel_attr_write_bool(lo, "powerdown", false);
        check(iio_channel_attr_write_longlong(lo, "frequency", static_cast<long long>(freq)), "frequency");

        // 3. Cyclic buffer: the hardware repeats it until it is destroyed.
        iio_channel* ci = iio_device_find_channel(tx, channel == 1 ? "voltage0" : "voltage2", true);
        iio_channel* cq = iio_device_find_channel(tx, channel == 1 ? "voltage1" : "voltage3", true);
        if (!ci || !cq) throw std::runtime_error("TX stream channels not found");
        iio_channel_enable(ci);
        iio_channel_enable(cq);
        std::unique_ptr<iio_buffer, decltype(&iio_buffer_destroy)> buf(
            iio_device_create_buffer(tx, x.size(), true), iio_buffer_destroy);
        if (!buf) throw std::runtime_error("Cannot create TX buffer");
        // 12-bit DAC values sit in the upper bits of the 16-bit word: x * 2047 << 4.
        const std::ptrdiff_t step = iio_buffer_step(buf.get());
        auto* pi = static_cast<char*>(iio_buffer_first(buf.get(), ci));
        auto* pq = static_cast<char*>(iio_buffer_first(buf.get(), cq));
        for (const auto& v : x) {
            const std::int16_t i = static_cast<std::int16_t>(std::lround(v.real() * 2047) * 16);
            const std::int16_t q = static_cast<std::int16_t>(std::lround(v.imag() * 2047) * 16);
            iio_channel_convert_inverse(ci, pi, &i);
            iio_channel_convert_inverse(cq, pq, &q);
            pi += step;
            pq += step;
        }
        if (iio_buffer_push(buf.get()) < 0) throw std::runtime_error("TX buffer push failed");

        // 4. Only now open the requested output level.
        check(iio_channel_attr_write_double(ctl, "hardwaregain", gain), "hardwaregain");
        long long f_act = 0, fs_act = 0;
        double g_act = 0;
        iio_channel_attr_read_longlong(lo, "frequency", &f_act);
        iio_channel_attr_read_longlong(ctl, "sampling_frequency", &fs_act);
        iio_channel_attr_read_double(ctl, "hardwaregain", &g_act);
        std::printf("TX%d: f0 = %lld Hz, Fs = %lld S/s, hardwaregain = %.2f dB, %zu samples cyclic\n", channel,
                    f_act, fs_act, g_act, x.size());
        std::printf("Transmitting for %.0f s (Ctrl+C stops earlier)...\n", seconds);
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (!g_stop && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Return to the safe state before the buffer goes away.
        iio_channel_attr_write_double(ctl, "hardwaregain", kMaxAttenuation);
        buf.reset();
        iio_channel_disable(ci);
        iio_channel_disable(cq);
    } catch (const std::exception& e) {
        std::cerr << "tx_play: " << e.what() << '\n';
        code = 1;
    }
    // Safe state on every exit path where the board was opened.
    if (ctl) iio_channel_attr_write_double(ctl, "hardwaregain", kMaxAttenuation);
    if (lo) iio_channel_attr_write_bool(lo, "powerdown", true);
    if (ctx) std::cout << "TX stopped: attenuation " << kMaxAttenuation << " dB, TX LO powered down\n";
    return code;
}
