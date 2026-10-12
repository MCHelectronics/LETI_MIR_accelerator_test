#include "lab/iq_file.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace lab {
namespace {
constexpr const char* kFormat = "cf32_le_iq";

std::string number_text(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream text;
    text << std::setprecision(17) << value;
    return text.str();
}

std::string quoted(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

// Minimal reader for the flat JSON objects written by this course.
std::string json_value(const std::string& json, const std::string& key) {
    const auto at = json.find('"' + key + '"');
    if (at == std::string::npos) return {};
    auto pos = json.find(':', at);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos < json.size() && json[pos] == '"') {
        const auto end = json.find('"', pos + 1);
        return json.substr(pos + 1, end - pos - 1);
    }
    auto end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '\n' && json[end] != '}') ++end;
    return json.substr(pos, end - pos);
}
}  // namespace

void IqMeta::add(const std::string& key, double value) { extra.emplace_back(key, number_text(value)); }
void IqMeta::add(const std::string& key, const std::string& value) { extra.emplace_back(key, quoted(value)); }

void write_iq(const std::string& path, const std::vector<cf32>& samples, IqMeta meta) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    const std::string meta_path = path + ".json";
    if (std::filesystem::exists(path) || std::filesystem::exists(meta_path))
        throw std::runtime_error("Output exists; choose a new filename: " + path);
    std::ofstream data(path, std::ios::binary);
    if (!data) throw std::runtime_error("Cannot create " + path);
    // Explicit little-endian bytes, independent of the host byte order.
    for (const cf32& s : samples) {
        for (float v : {s.real(), s.imag()}) {
            std::uint32_t word;
            std::memcpy(&word, &v, 4);
            const unsigned char bytes[4] = {
                static_cast<unsigned char>(word), static_cast<unsigned char>(word >> 8),
                static_cast<unsigned char>(word >> 16), static_cast<unsigned char>(word >> 24)};
            data.write(reinterpret_cast<const char*>(bytes), 4);
        }
    }
    if (!data) throw std::runtime_error("Write failed: " + path);
    meta.sample_count = samples.size();
    std::ofstream json(meta_path);
    json << "{\n  \"format\": \"" << kFormat << "\",\n"
         << "  \"sample_rate_hz\": " << number_text(meta.sample_rate_hz) << ",\n"
         << "  \"center_frequency_hz\": " << number_text(meta.center_frequency_hz) << ",\n"
         << "  \"sample_count\": " << meta.sample_count;
    for (const auto& [key, value] : meta.extra) json << ",\n  \"" << key << "\": " << value;
    json << "\n}\n";
    if (!json) throw std::runtime_error("Write failed: " + meta_path);
}

std::vector<cf32> read_iq(const std::string& path, IqMeta& meta) {
    std::ifstream json_file(path + ".json");
    if (!json_file) throw std::runtime_error("Missing metadata " + path + ".json");
    const std::string json((std::istreambuf_iterator<char>(json_file)), {});
    if (json_value(json, "format") != kFormat)
        throw std::runtime_error("Unsupported format in " + path + ".json");
    meta.sample_rate_hz = std::stod(json_value(json, "sample_rate_hz"));
    const std::string fc = json_value(json, "center_frequency_hz");
    meta.center_frequency_hz = (fc.empty() || fc == "null") ? NAN : std::stod(fc);
    meta.sample_count = std::stoull(json_value(json, "sample_count"));

    const auto bytes = std::filesystem::file_size(path);
    if (bytes != meta.sample_count * 8)
        throw std::runtime_error("File size " + std::to_string(bytes) + " B does not match " +
                                 std::to_string(meta.sample_count) + " samples x 8 B");
    std::ifstream data(path, std::ios::binary);
    std::vector<cf32> samples(meta.sample_count);
    std::vector<unsigned char> raw(bytes);
    data.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes));
    if (!data) throw std::runtime_error("Read failed: " + path);
    for (std::size_t n = 0; n < samples.size(); ++n) {
        float v[2];
        for (int k = 0; k < 2; ++k) {
            const unsigned char* b = &raw[8 * n + 4 * k];
            const std::uint32_t word = b[0] | b[1] << 8 | b[2] << 16 | std::uint32_t(b[3]) << 24;
            std::memcpy(&v[k], &word, 4);
        }
        samples[n] = {v[0], v[1]};
    }
    return samples;
}

}  // namespace lab
