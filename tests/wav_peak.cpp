// Prints the dominant frequency of a 16-bit mono WAV file (test helper).
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "lab/dsp.hpp"

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: wav_peak FILE.wav\n"; return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    unsigned char h[44];
    if (!in.read(reinterpret_cast<char*>(h), 44)) { std::cerr << "Bad WAV\n"; return 1; }
    const unsigned rate = h[24] | h[25] << 8 | h[26] << 16 | h[27] << 24;
    std::vector<lab::cf32> x;
    for (unsigned char b[2]; in.read(reinterpret_cast<char*>(b), 2);)
        x.emplace_back(static_cast<std::int16_t>(b[0] | b[1] << 8) / 32768.0f, 0.0f);
    const std::size_t n = 8192;
    if (x.size() < n) { std::cerr << "WAV is shorter than " << n << " samples\n"; return 1; }
    const auto s = lab::averaged_spectrum_db(x, n, 64);
    std::size_t best = n / 2 + 1;
    for (std::size_t k = n / 2 + 1; k < n; ++k) if (s[k] > s[best]) best = k;
    std::printf("%.1f\n", (best - n / 2.0) * rate / n);
}
