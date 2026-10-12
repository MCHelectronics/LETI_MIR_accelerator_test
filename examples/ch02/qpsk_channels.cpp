#include <iio.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {
bool inspect(iio_context* context, const char* name, bool output) {
    auto* device = iio_context_find_device(context, name);
    std::cout << (output ? "TX: " : "RX: ") << name << '\n';
    if (!device) {
        std::cerr << "  Device is absent\n";
        return false;
    }
    bool complete = true;
    for (unsigned n = 0; n < 4; ++n) {
        const std::string id = "voltage" + std::to_string(n);
        auto* channel = iio_device_find_channel(device, id.c_str(), output);
        const bool found = channel && iio_channel_is_scan_element(channel);
        complete = complete && found;
        std::cout << "  " << id << " (RF" << n / 2 + 1
                  << ((n % 2) ? " Q): " : " I): ");
        if (!found) {
            std::cout << "MISSING scan element\n";
            continue;
        }
        const auto* format = iio_channel_get_data_format(channel);
        std::cout << "index=" << iio_channel_get_index(channel)
                  << " bits=" << format->bits << " storage=" << format->length
                  << " shift=" << format->shift
                  << " signed=" << format->is_signed
                  << " big_endian=" << format->is_be << '\n';
    }
    return complete;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: qpsk_channels ip:ADDRESS | usb:BUS.ADDRESS.INTERFACE | xml:FILE\n";
        return 2;
    }
    unsigned major = 0, minor = 0;
    char tag[8] = {};
    iio_library_get_version(&major, &minor, tag);
    std::cout << "libiio " << major << '.' << minor << " (" << tag << ")\n";
    std::unique_ptr<iio_context, decltype(&iio_context_destroy)> context(
        iio_create_context_from_uri(argv[1]), iio_context_destroy);
    if (!context) {
        std::cerr << "Cannot open context: " << std::strerror(errno) << '\n';
        return 1;
    }
    const bool phy = iio_context_find_device(context.get(), "ad9361-phy") != nullptr;
    std::cout << "ad9361-phy: " << (phy ? "present" : "MISSING") << '\n';
    const bool rx = inspect(context.get(), "cf-ad9361-lpc", false);
    const bool tx = inspect(context.get(), "cf-ad9361-dds-core-lpc", true);
    if (!(phy && rx && tx)) {
        std::cerr << "Expected AD936x 2RX2TX layout is NOT exposed. Check firmware/context.\n";
        return 1;
    }
    std::cout << "2RX2TX scan layout is present. No buffers or RF transmission started.\n"
              << "This is NOT a throughput or simultaneous-stream test.\n";
}
