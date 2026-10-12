// rx_capture: one contiguous RX recording from QPSK SDR Lab into a cf32 file.
// Receive only: no TX buffer is created and TX settings are not changed,
// except the common sample-rate/FIR setup done by ad9361_set_bb_rate().
#include <ad9361.h>
#include <iio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "lab/iq_file.hpp"

namespace {
const char* kUsage =
    "Usage: rx_capture --uri URI --freq HZ --out FILE.cf32 [--rate HZ] [--bw HZ]\n"
    "                  [--gain DB|slow_attack|fast_attack|hybrid] [--samples N] [--channel 1|2]\n";

// Parses "--key value" pairs; every option takes exactly one value.
std::map<std::string, std::string> parse(int argc, char** argv) {
    std::map<std::string, std::string> opt = {
        {"--rate", "1e6"}, {"--gain", "40"}, {"--samples", "1000000"}, {"--channel", "1"}};
    for (int k = 1; k < argc; k += 2) {
        const std::string key = argv[k];
        if (k + 1 >= argc || key.rfind("--", 0) != 0) throw std::invalid_argument(kUsage);
        opt[key] = argv[k + 1];
    }
    for (const char* need : {"--uri", "--freq", "--out"})
        if (!opt.count(need)) throw std::invalid_argument(kUsage);
    if (!opt.count("--bw")) opt["--bw"] = opt["--rate"];
    return opt;
}

double number(const std::string& text) {
    std::size_t used = 0;
    const double v = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(v)) throw std::invalid_argument("Bad number: " + text);
    return v;
}

void check(int err, const std::string& what) {
    if (err < 0) throw std::runtime_error(what + " failed, error " + std::to_string(-err));
}

long long read_ll(iio_channel* ch, const char* attr) {
    long long v = 0;
    check(iio_channel_attr_read_longlong(ch, attr, &v), std::string("read ") + attr);
    return v;
}

std::string read_str(iio_channel* ch, const char* attr) {
    char text[64] = {};
    check(static_cast<int>(iio_channel_attr_read(ch, attr, text, sizeof text)), std::string("read ") + attr);
    return text;
}

std::string utc_now() {
    char text[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return text;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        auto opt = parse(argc, argv);
        const double freq = number(opt["--freq"]), rate = number(opt["--rate"]), bw = number(opt["--bw"]);
        const double count = number(opt["--samples"]);
        const int channel = static_cast<int>(number(opt["--channel"]));
        if (channel != 1 && channel != 2) throw std::invalid_argument("--channel must be 1 or 2");
        if (!(count >= 1024 && count <= 16777216)) throw std::invalid_argument("--samples: 1024..16777216");
        const auto n = static_cast<std::size_t>(count);

        std::unique_ptr<iio_context, decltype(&iio_context_destroy)> ctx(
            iio_create_context_from_uri(opt["--uri"].c_str()), iio_context_destroy);
        if (!ctx) throw std::runtime_error("Cannot open " + opt["--uri"]);
        iio_device* phy = iio_context_find_device(ctx.get(), "ad9361-phy");
        iio_device* rx = iio_context_find_device(ctx.get(), "cf-ad9361-lpc");
        if (!phy || !rx) throw std::runtime_error("ad9361-phy / cf-ad9361-lpc not found");

        // Control channels of ad9361-phy: RX LO and the gain/bandwidth of the chosen RX.
        iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);
        iio_channel* ctl = iio_device_find_channel(phy, channel == 1 ? "voltage0" : "voltage1", false);
        if (!lo || !ctl) throw std::runtime_error("RX control channels not found");

        // 1. Sample rate: libad9361 also loads the FIR filter needed below ~2.08 MS/s.
        check(ad9361_set_bb_rate(phy, static_cast<unsigned long>(rate)), "ad9361_set_bb_rate");
        // 2. Analog bandwidth and centre frequency.
        check(iio_channel_attr_write_longlong(ctl, "rf_bandwidth", static_cast<long long>(bw)), "rf_bandwidth");
        check(iio_channel_attr_write_longlong(lo, "frequency", static_cast<long long>(freq)), "frequency");
        // 3. Gain: a number means manual gain, otherwise an AGC mode.
        const std::string gain = opt["--gain"];
        const bool manual = std::isdigit(static_cast<unsigned char>(gain[0])) || gain[0] == '-';
        check(static_cast<int>(iio_channel_attr_write(ctl, "gain_control_mode", manual ? "manual" : gain.c_str())),
              "gain_control_mode");
        if (manual)
            check(iio_channel_attr_write_double(ctl, "hardwaregain", number(gain)), "hardwaregain");

        // 4. Read back what the device actually set.
        const long long f_act = read_ll(lo, "frequency"), fs_act = read_ll(ctl, "sampling_frequency");
        const long long bw_act = read_ll(ctl, "rf_bandwidth");
        const std::string mode_act = read_str(ctl, "gain_control_mode");
        double gain_act = 0;
        check(iio_channel_attr_read_double(ctl, "hardwaregain", &gain_act), "read hardwaregain");
        std::cout << "f0 = " << f_act << " Hz (requested " << static_cast<long long>(freq) << ")\n"
                  << "Fs = " << fs_act << " S/s, RF bandwidth = " << bw_act << " Hz\n"
                  << "gain: " << mode_act << ", " << gain_act << " dB\n";

        // 5. Stream channels: I and Q of the chosen receiver.
        iio_channel* ci = iio_device_find_channel(rx, channel == 1 ? "voltage0" : "voltage2", false);
        iio_channel* cq = iio_device_find_channel(rx, channel == 1 ? "voltage1" : "voltage3", false);
        if (!ci || !cq) throw std::runtime_error("RX stream channels not found");
        const iio_data_format* fmt = iio_channel_get_data_format(ci);
        if (fmt->length != 16 || fmt->bits > 16) throw std::runtime_error("Unexpected sample format");
        const float full_scale = static_cast<float>(1 << (fmt->bits - 1));  // 2048 for 12 bits
        iio_channel_enable(ci);
        iio_channel_enable(cq);

        // 6. One buffer for the whole record keeps it contiguous. The first
        //    refill is discarded: it may contain data from before the setup settled.
        std::unique_ptr<iio_buffer, decltype(&iio_buffer_destroy)> buf(
            iio_device_create_buffer(rx, n, false), iio_buffer_destroy);
        if (!buf) throw std::runtime_error("Cannot create RX buffer");
        // A refill waits for the whole record: allow its duration plus a margin.
        iio_context_set_timeout(ctx.get(), static_cast<unsigned>(2000 + 1500.0 * n / fs_act));
        for (int pass = 0; pass < 2; ++pass)
            check(static_cast<int>(iio_buffer_refill(buf.get())), "RX refill");

        // 7. Convert hardware words to complex<float>, full scale = 1.0.
        std::vector<lab::cf32> x;
        x.reserve(n);
        std::size_t clipped = 0;
        double power = 0;
        const std::ptrdiff_t step = iio_buffer_step(buf.get());
        const auto* end = static_cast<const char*>(iio_buffer_end(buf.get()));
        const auto* pi = static_cast<const char*>(iio_buffer_first(buf.get(), ci));
        const auto* pq = static_cast<const char*>(iio_buffer_first(buf.get(), cq));
        for (; pi < end && x.size() < n; pi += step, pq += step) {
            std::int16_t i, q;
            iio_channel_convert(ci, &i, pi);  // sign extension and shift by the driver format
            iio_channel_convert(cq, &q, pq);
            const int limit = static_cast<int>(full_scale) - 1;
            if (std::abs(i) >= limit || std::abs(q) >= limit) ++clipped;
            x.emplace_back(i / full_scale, q / full_scale);
            power += std::norm(x.back());
        }
        iio_channel_disable(ci);
        iio_channel_disable(cq);

        const double rms_db = 10 * std::log10(std::max(power / x.size(), 1e-20));
        std::printf("%zu samples, RMS %.1f dBFS, clipped %zu (%.2f %%)\n", x.size(), rms_db, clipped,
                    100.0 * clipped / x.size());
        if (clipped > x.size() / 1000) std::cout << "WARNING: ADC overload, reduce the gain\n";

        // 8. Samples and sidecar metadata.
        lab::IqMeta meta;
        meta.sample_rate_hz = static_cast<double>(fs_act);
        meta.center_frequency_hz = static_cast<double>(f_act);
        meta.add("rf_bandwidth_hz", static_cast<double>(bw_act));
        meta.add("rx_channel", channel);
        meta.add("gain_control_mode", mode_act);
        meta.add("hardwaregain_db", gain_act);
        meta.add("full_scale", "1.0 = " + std::to_string(static_cast<int>(full_scale)) + " ADC counts");
        meta.add("rms_dbfs", rms_db);
        meta.add("clipped_samples", static_cast<double>(clipped));
        meta.add("contiguous", "yes: single buffer, no gaps");
        meta.add("uri", opt["--uri"]);
        meta.add("utc_start_approx", utc_now());
        meta.add("program", "rx_capture");
        lab::write_iq(opt["--out"], x, meta);
        std::cout << "Saved " << opt["--out"] << " and " << opt["--out"] << ".json\n";
    } catch (const std::exception& e) {
        std::cerr << "rx_capture: " << e.what() << '\n';
        return 1;
    }
}
