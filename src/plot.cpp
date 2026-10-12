#include "lab/plot.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace lab {
namespace {
const cv::Scalar kBlack(0, 0, 0), kGrid(225, 225, 225);
constexpr int kW = 1000, kH = 520, kLeft = 90, kRight = 30, kTop = 50, kBottom = 70;

// "Nice" tick step: 1, 2 or 5 times a power of ten.
double tick_step(double span, int target) {
    const double raw = span / target, mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * mag) return m * mag;
    return 10 * mag;
}

std::string fmt(double v, double step) {
    char text[32];
    const int digits = std::max(0, -static_cast<int>(std::floor(std::log10(step))));
    std::snprintf(text, sizeof text, "%.*f", digits, v);
    return text;
}

void text(cv::Mat& img, const std::string& s, cv::Point at, double scale = 0.5) {
    cv::putText(img, s, at, cv::FONT_HERSHEY_SIMPLEX, scale, kBlack, 1, cv::LINE_AA);
}

// Axes with grid; returns the plot rectangle. x and y ranges in data units.
cv::Rect axes(cv::Mat& img, const std::string& title, double x0, double x1, double y0, double y1,
              const std::string& xl, const std::string& yl, bool y_grid = true) {
    const cv::Rect area(kLeft, kTop, kW - kLeft - kRight, kH - kTop - kBottom);
    const double sx = tick_step(x1 - x0, 8), sy = tick_step(y1 - y0, 6);
    for (double v = std::ceil(x0 / sx) * sx; v <= x1 + 1e-9 * sx; v += sx) {
        const int px = area.x + static_cast<int>((v - x0) / (x1 - x0) * area.width);
        cv::line(img, {px, area.y}, {px, area.br().y}, kGrid);
        text(img, fmt(v, sx), {px - 20, area.br().y + 20}, 0.45);
    }
    if (y_grid)
        for (double v = std::ceil(y0 / sy) * sy; v <= y1 + 1e-9 * sy; v += sy) {
            const int py = area.br().y - static_cast<int>((v - y0) / (y1 - y0) * area.height);
            cv::line(img, {area.x, py}, {area.br().x, py}, kGrid);
            text(img, fmt(v, sy), {8, py + 5}, 0.45);
        }
    cv::rectangle(img, area, kBlack);
    text(img, title, {kLeft, 22}, 0.65);
    text(img, xl, {area.x + area.width / 2 - 60, kH - 15});
    text(img, yl, {8, kTop - 6});
    return area;
}

void save(const std::string& path, const cv::Mat& img) {
    if (!cv::imwrite(path, img)) throw std::runtime_error("Cannot write " + path);
}
}  // namespace

void save_line_plot(const std::string& path, const std::string& title, const std::vector<float>& x,
                    const std::vector<Series>& series, const std::string& x_label,
                    const std::string& y_label) {
    if (x.size() < 2 || series.empty()) throw std::invalid_argument("Nothing to plot");
    float lo = series[0].y[0], hi = lo;
    for (const auto& s : series)
        for (float v : s.y) lo = std::min(lo, v), hi = std::max(hi, v);
    const float pad = std::max(0.05f * (hi - lo), 1e-3f);
    lo -= pad, hi += pad;
    cv::Mat img(kH, kW, CV_8UC3, cv::Scalar(255, 255, 255));
    const cv::Rect a = axes(img, title, x.front(), x.back(), lo, hi, x_label, y_label);
    int labelled = 0;
    for (const auto& s : series) {
        std::vector<cv::Point> pts;
        for (std::size_t k = 0; k < x.size(); ++k)
            pts.emplace_back(a.x + static_cast<int>((x[k] - x.front()) / (x.back() - x.front()) * a.width),
                             a.br().y - static_cast<int>((s.y[k] - lo) / (hi - lo) * a.height));
        cv::polylines(img, pts, false, cv::Scalar(s.b, s.g, s.r), 1, cv::LINE_AA);
        labelled += !s.label.empty();
    }
    if (labelled) {  // legend on a white box, drawn over the curves
        cv::rectangle(img, {a.br().x - 158, a.y + 4}, {a.br().x - 6, a.y + 8 + 20 * labelled},
                      cv::Scalar(255, 255, 255), cv::FILLED);
        cv::rectangle(img, {a.br().x - 158, a.y + 4}, {a.br().x - 6, a.y + 8 + 20 * labelled}, kGrid);
        int legend_y = a.y + 20;
        for (const auto& s : series) {
            if (s.label.empty()) continue;
            cv::line(img, {a.br().x - 150, legend_y - 5}, {a.br().x - 120, legend_y - 5}, cv::Scalar(s.b, s.g, s.r), 2);
            text(img, s.label, {a.br().x - 112, legend_y});
            legend_y += 20;
        }
    }
    save(path, img);
}

void save_waterfall(const std::string& path, const std::string& title,
                    const std::vector<std::vector<float>>& rows, float x_min, float x_max,
                    const std::string& x_label, float seconds_per_row) {
    if (rows.empty()) throw std::invalid_argument("Nothing to plot");
    // Color range: from the median level (noise) to the strongest bin.
    std::vector<float> all;
    for (const auto& r : rows) all.insert(all.end(), r.begin(), r.end());
    std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
    const float lo = all[all.size() / 2] - 5, hi = *std::max_element(all.begin(), all.end());
    cv::Mat levels(static_cast<int>(rows.size()), static_cast<int>(rows[0].size()), CV_8UC1);
    for (int t = 0; t < levels.rows; ++t)
        for (int k = 0; k < levels.cols; ++k) {
            const float u = (rows[t][k] - lo) / std::max(hi - lo, 1e-3f);
            levels.at<unsigned char>(t, k) = static_cast<unsigned char>(255 * std::clamp(u, 0.0f, 1.0f));
        }
    cv::Mat color;
    cv::applyColorMap(levels, color, cv::COLORMAP_VIRIDIS);
    cv::Mat img(kH, kW, CV_8UC3, cv::Scalar(255, 255, 255));
    const cv::Rect a = axes(img, title, x_min, x_max, 0, 1, x_label, "", false);
    cv::resize(color, img(a), a.size(), 0, 0, cv::INTER_AREA);
    cv::rectangle(img, a, kBlack);
    char note[96];
    std::snprintf(note, sizeof note, "time down: %.3g s total, %.3g ms/row; color %.0f..%.0f dBFS",
                  rows.size() * seconds_per_row, 1e3 * seconds_per_row, lo, hi);
    text(img, note, {kLeft, kTop - 4}, 0.45);
    save(path, img);
}

}  // namespace lab
