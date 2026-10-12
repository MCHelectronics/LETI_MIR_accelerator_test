#pragma once
// Simple plots drawn with OpenCV and saved as PNG. Labels are ASCII only:
// the built-in OpenCV fonts have no Cyrillic glyphs.
#include <string>
#include <vector>

namespace lab {

struct Series {
    std::vector<float> y;
    unsigned char b, g, r;  // line color
    std::string label;
};

// Line plot of several series sharing the x values.
void save_line_plot(const std::string& path, const std::string& title,
                    const std::vector<float>& x, const std::vector<Series>& series,
                    const std::string& x_label, const std::string& y_label);

// Waterfall: rows[t][k] in dB, x from x_min to x_max, time grows downwards.
void save_waterfall(const std::string& path, const std::string& title,
                    const std::vector<std::vector<float>>& rows, float x_min, float x_max,
                    const std::string& x_label, float seconds_per_row);

}  // namespace lab
