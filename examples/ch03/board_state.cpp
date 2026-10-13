// board_state: read-only snapshot of QPSK SDR Lab. Shows the channel mode,
// LO and channel settings, the sample-rate chain inside AD9361, the TX state
// that matters for safety (attenuation, DDS tones), temperatures and supply
// voltages. Nothing is written to the board.
#include <iio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<std::pair<std::string, std::string>> g_rows;  // for --json
int g_warnings = 0;

void row(const std::string& key, const std::string& value) {
    g_rows.emplace_back(key, value);
    std::printf("%-22s %s\n", key.c_str(), value.c_str());
}

void warn(const std::string& text) {
    ++g_warnings;
    std::printf("WARNING: %s\n", text.c_str());
}

// Attribute readers return "n/a" instead of failing: a missing value is
// information too (for example, an XML context has no attribute values).
std::string text_of(char* buf, ssize_t n) {
    if (n <= 0) return "n/a";
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string ch_attr(iio_channel* ch, const char* name) {
    char buf[256] = {};
    return ch ? text_of(buf, iio_channel_attr_read(ch, name, buf, sizeof buf)) : "n/a";
}

std::string dev_attr(iio_device* dev, const char* name) {
    char buf[256] = {};
    return dev ? text_of(buf, iio_device_attr_read(dev, name, buf, sizeof buf)) : "n/a";
}

std::string debug_attr(iio_device* dev, const char* name) {
    char buf[256] = {};
    return dev ? text_of(buf, iio_device_debug_attr_read(dev, name, buf, sizeof buf)) : "n/a";
}

double num(const std::string& s) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end != s.c_str() ? v : NAN;
}

std::string fmt(const char* pattern, double v) {
    if (std::isnan(v)) return "n/a";
    char buf[64];
    std::snprintf(buf, sizeof buf, pattern, v);
    return buf;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += (c == '\n' || c == '\t') ? ' ' : c;
    }
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && !(argc == 4 && std::string(argv[2]) == "--json")) {
        std::cerr << "Usage: board_state URI [--json FILE]\n";
        return 2;
    }
    std::unique_ptr<iio_context, decltype(&iio_context_destroy)> ctx(
        iio_create_context_from_uri(argv[1]), iio_context_destroy);
    if (!ctx) {
        std::cerr << "board_state: cannot open " << argv[1] << '\n';
        return 1;
    }
    iio_context* c = ctx.get();
    iio_device* phy = iio_context_find_device(c, "ad9361-phy");
    iio_device* dds = iio_context_find_device(c, "cf-ad9361-dds-core-lpc");
    iio_device* xadc = iio_context_find_device(c, "xadc");
    if (!phy) {
        std::cerr << "board_state: ad9361-phy not found\n";
        return 1;
    }
    auto ctx_attr = [c](const char* name) {
        const char* v = iio_context_get_attr_value(c, name);
        return std::string(v ? v : "n/a");
    };

    // 1. Identity: firmware and the transceiver model the driver runs.
    row("uri", argv[1]);
    row("firmware", ctx_attr("fw_version"));
    row("kernel", ctx_attr("local,kernel"));
    row("transceiver", ctx_attr("ad9361-phy,model"));
    const std::string two = debug_attr(phy, "adi,2rx-2tx-mode-enable");
    row("channel_mode", two == "1" ? "2RX2TX" : two == "0" ? "1RX1TX" : "n/a");
    const std::string fdd = debug_attr(phy, "adi,frequency-division-duplex-mode-enable");
    row("duplex", fdd == "1" ? "FDD" : fdd == "0" ? "TDD" : "n/a");
    row("ensm_mode", dev_attr(phy, "ensm_mode"));
    if (two == "0") warn("firmware exposes only one RX and one TX channel");

    // 2. Local oscillators: one for both receivers, one for both transmitters.
    iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    iio_channel* tx_lo = iio_device_find_channel(phy, "altvoltage1", true);
    row("rx_lo_mhz", fmt("%.6f", num(ch_attr(rx_lo, "frequency")) / 1e6));
    row("tx_lo_mhz", fmt("%.6f", num(ch_attr(tx_lo, "frequency")) / 1e6) +
                         (ch_attr(tx_lo, "powerdown") == "1" ? " (powered down)" : " (on)"));

    // 3. Sample-rate chain: BBPLL -> ADC/DAC -> half-band stages -> FIR -> Fs.
    row("rx_path_rates", dev_attr(phy, "rx_path_rates"));
    row("tx_path_rates", dev_attr(phy, "tx_path_rates"));
    row("fir", dev_attr(phy, "filter_fir_config"));

    // 4. Per-channel settings of ad9361-phy (control, not samples).
    for (int k = 0; k < 2; ++k) {
        const std::string id = "voltage" + std::to_string(k), rx = "rx" + std::to_string(k + 1);
        const std::string tx = "tx" + std::to_string(k + 1);
        iio_channel* r = iio_device_find_channel(phy, id.c_str(), false);
        iio_channel* t = iio_device_find_channel(phy, id.c_str(), true);
        row(rx + "_gain", ch_attr(r, "gain_control_mode") + ", " + ch_attr(r, "hardwaregain"));
        row(rx + "_bw_mhz", fmt("%.3f", num(ch_attr(r, "rf_bandwidth")) / 1e6));
        row(rx + "_port", ch_attr(r, "rf_port_select"));
        row(rx + "_rssi", ch_attr(r, "rssi"));
        row(tx + "_gain", ch_attr(t, "hardwaregain"));
        row(tx + "_bw_mhz", fmt("%.3f", num(ch_attr(t, "rf_bandwidth")) / 1e6));
    }
    iio_channel* rx1 = iio_device_find_channel(phy, "voltage0", false);
    row("fs_msps", fmt("%.6f", num(ch_attr(rx1, "sampling_frequency")) / 1e6));

    // 5. DDS tone generators of the TX core: they can transmit without any
    //    buffer from the PC, so a non-zero scale is a safety finding.
    if (dds) {
        std::string active;
        for (unsigned k = 0; k < iio_device_get_channels_count(dds); ++k) {
            iio_channel* ch = iio_device_get_channel(dds, k);
            const char* name = iio_channel_get_name(ch);
            if (!name || iio_channel_is_scan_element(ch)) continue;  // only altvoltageN tones
            const double scale = num(ch_attr(ch, "scale"));
            if (ch_attr(ch, "raw") == "1" && scale > 0)
                active += std::string(active.empty() ? "" : ", ") + name + "=" + fmt("%.3f", scale);
        }
        row("dds_tones", active.empty() ? "off (scale 0)" : active);
        if (!active.empty()) warn("DDS tones are enabled: TX outputs a signal without a buffer");
    }

    // 6. Temperatures and supplies.
    const double t_trx = num(ch_attr(iio_device_find_channel(phy, "temp0", false), "input")) / 1000;
    row("ad9361_temp_c", fmt("%.1f", t_trx));
    if (xadc) {
        iio_channel* t = iio_device_find_channel(xadc, "temp0", false);
        const double t_soc =
            (num(ch_attr(t, "raw")) + num(ch_attr(t, "offset"))) * num(ch_attr(t, "scale")) / 1000;
        row("zynq_temp_c", fmt("%.1f", t_soc));
        if (t_soc > 80) warn("Zynq temperature above 80 C: check cooling");
        // XADC channels are found by name: voltage0 is "vccint" and so on.
        for (const char* rail : {"vccint", "vccaux", "vccbram", "vccpint", "vccpaux", "vccoddr"}) {
            iio_channel* v = iio_device_find_channel(xadc, rail, false);
            row(std::string(rail) + "_v", fmt("%.3f", num(ch_attr(v, "raw")) * num(ch_attr(v, "scale")) / 1000));
        }
    }

    if (argc == 4) {
        std::ofstream out(argv[3]);
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
        out << "{\n  \"utc\": \"" << stamp << "\",\n  \"program\": \"board_state\",\n";
        for (std::size_t k = 0; k < g_rows.size(); ++k)
            out << "  \"" << g_rows[k].first << "\": \"" << json_escape(g_rows[k].second) << '"'
                << (k + 1 < g_rows.size() ? ",\n" : "\n");
        out << "}\n";
        if (!out) {
            std::cerr << "board_state: cannot write " << argv[3] << '\n';
            return 1;
        }
        std::cout << "Saved " << argv[3] << '\n';
    }
    std::cout << (g_warnings ? "Warnings: " + std::to_string(g_warnings) : "No warnings") << '\n';
}
