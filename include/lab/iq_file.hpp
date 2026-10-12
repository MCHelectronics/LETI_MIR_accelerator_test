#pragma once
// IQ file in the course format: cf32 little-endian, interleaved I,Q,
// plus a JSON sidecar "<file>.json" with the metadata.
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lab {

using cf32 = std::complex<float>;

struct IqMeta {
    double sample_rate_hz = 0;
    double center_frequency_hz = NAN;  // NaN: generated in baseband
    std::uint64_t sample_count = 0;
    // Additional "key": value pairs, written in this order.
    // Values are stored ready for JSON: numbers as text, strings quoted.
    std::vector<std::pair<std::string, std::string>> extra;

    void add(const std::string& key, double value);
    void add(const std::string& key, const std::string& value);
};

// Writes samples and sidecar. Refuses to overwrite existing files.
void write_iq(const std::string& path, const std::vector<cf32>& samples, IqMeta meta);

// Reads samples and the main sidecar fields. Throws on size or format mismatch.
std::vector<cf32> read_iq(const std::string& path, IqMeta& meta);

}  // namespace lab
