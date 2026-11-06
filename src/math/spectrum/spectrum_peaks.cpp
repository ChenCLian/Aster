// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/spectrum_peaks.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <stdexcept>

namespace spectrum {

double spectrumProminenceThreshold(
    int n, const double* y, const PeakOptions& opt)
{
    if (n < 0 || (n > 0 && y == nullptr))
        throw std::invalid_argument("Invalid spectrum pointer or length");
    if (!std::isfinite(opt.minProminence) || opt.minProminence < 0.0 ||
        !std::isfinite(opt.minWidth) || opt.minWidth < 0.0 ||
        std::isnan(opt.minHeight) || opt.minDistance < 1 || opt.prominenceRadius < 0 ||
        !std::isfinite(opt.relativeProminence) || opt.relativeProminence < 0 || opt.relativeProminence > 1)
        throw std::invalid_argument("Invalid peak options");
    for (int j = 0; j < n; ++j)
        if (!std::isfinite(y[j]))
            throw std::invalid_argument("Spectrum contains NaN or infinity");

    if (!n) return opt.minProminence;
    const auto bounds = std::minmax_element(y, y + n);
    const long double scale = std::max(std::abs((long double)*bounds.first), std::abs((long double)*bounds.second));
    // Normalize before subtraction: MSVC long double has double's exponent range.
    const double range = *bounds.second - *bounds.first;
    const long double relative = std::isfinite(range) ? opt.relativeProminence * range :
        (scale == 0 ? 0 : scale *
         (opt.relativeProminence * (*bounds.second / scale - *bounds.first / scale)));
    const long double floor = 64 * std::numeric_limits<double>::epsilon() * scale;
    const auto threshold = std::max((long double)opt.minProminence, std::max(relative, floor));
    if (threshold > std::numeric_limits<double>::max())
        throw std::invalid_argument("Peak prominence threshold exceeds the numeric range");
    return double(threshold);
}

std::vector<Peak> findSpectrumPeaks(int n, const double* y, const PeakOptions& opt)
{
    const double threshold = spectrumProminenceThreshold(n, y, opt);
    const double minimum = n ? *std::min_element(y, y + n) : 0;
    std::vector<Peak> candidates;
    for (int i = 1; i < n - 1;) {
        if (y[i] <= y[i - 1]) { ++i; continue; }

        // A peak must rise on the left and fall after its (possibly flat) top.
        int end = i;
        while (end < n - 1 && y[end + 1] == y[i]) ++end;
        if (end == n - 1 || y[end + 1] >= y[i]) {
            i = end + 1;
            continue;
        }
        const int p = i + (end - i) / 2;
        i = end + 1;
        const double h = y[p];
        if (h < opt.minHeight) continue;
        if (h - minimum < threshold) continue;

        const int radius = opt.prominenceRadius;
        const int lo = radius == 0 ? 0 : p - (std::min)(p, radius);
        const int hi = radius == 0 ? n - 1 : p + (std::min)(n - 1 - p, radius);

        // Stop only at a HIGHER peak or the window boundary. Equal peaks do not stop us.
        int leftBase = p, rightBase = p;
        for (int j = p - 1; j >= lo && y[j] <= h; --j)
            if (y[j] < y[leftBase]) leftBase = j;
        for (int j = p + 1; j <= hi && y[j] <= h; ++j)
            if (y[j] < y[rightBase]) rightBase = j;

        const double prominence = h - (std::max)(y[leftBase], y[rightBase]);
        if (!std::isfinite(prominence))
            throw std::invalid_argument("Peak prominence exceeds the numeric range");
        if (prominence <= 0 || prominence < threshold) continue;

        double width = 0.0;
        if (prominence > 0.0) {
            const double level = h - 0.5 * prominence;
            int l = p, r = p;
            while (l > leftBase && y[l] > level) --l;
            while (r < rightBase && y[r] > level) ++r;
            double left = l, right = r;
            if (y[l] < level)
                left += (level - y[l]) / (y[l + 1] - y[l]);
            if (y[r] < level)
                right -= (level - y[r]) / (y[r - 1] - y[r]);
            width = right - left;
        }
        if (width >= opt.minWidth)
            candidates.push_back(Peak{p, h, prominence, width});
    }

    // Select the most prominent eligible peak first in every competing cluster.
    std::sort(candidates.begin(), candidates.end(), [](const Peak& a, const Peak& b) {
        if (a.prominence != b.prominence) return a.prominence > b.prominence;
        if (a.height != b.height) return a.height > b.height;
        return a.index < b.index;
    });
    std::set<int> chosen;
    std::vector<Peak> result;
    for (const Peak& peak : candidates) {
        const auto right = chosen.lower_bound(peak.index);
        if (right != chosen.end() && *right - peak.index < opt.minDistance) continue;
        if (right != chosen.begin() && peak.index - *std::prev(right) < opt.minDistance)
            continue;
        chosen.insert(right, peak.index);
        result.push_back(peak);
    }
    std::sort(result.begin(), result.end(), [](const Peak& a, const Peak& b) {
        return a.index < b.index;
    });
    return result;
}

// Same return type as the original AMPD function; indices are zero based.
std::vector<int> findSpectrumPeakIndices(
    int n, const double* y, const PeakOptions& opt)
{
    const auto peaks = findSpectrumPeaks(n, y, opt);
    std::vector<int> indices;
    indices.reserve(peaks.size());
    for (const Peak& peak : peaks) indices.push_back(peak.index);
    return indices;
}

} // namespace spectrum
