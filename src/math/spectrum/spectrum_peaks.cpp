// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/spectrum_peaks.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>

namespace spectrum {
namespace {
void checkCancelled(const std::function<bool()>& cancelled)
{
    if (cancelled && cancelled()) throw std::runtime_error("Cancellation requested");
}

// Each leaf summarizes 32 consecutive bins. Whole ranges can be skipped when
// locating a higher peak or a half-prominence crossing, even for equal peaks.
class SpectrumRanges {
    static constexpr size_t blockSize = 32;
    struct Bounds {
        double low = std::numeric_limits<double>::infinity();
        double high = -std::numeric_limits<double>::infinity();
    };
    const double* y;
    size_t leaves = 1;
    std::vector<Bounds> tree;

    double minimum(size_t node, size_t begin, size_t end, size_t lo, size_t hi) const
    {
        if (hi <= begin || end <= lo) return std::numeric_limits<double>::infinity();
        if (lo <= begin && end <= hi) return tree[node].low;
        if (end - begin == blockSize)
            return *std::min_element(y + std::max(begin, lo), y + std::min(end, hi));
        const auto mid = begin + (end - begin) / 2;
        return std::min(minimum(2 * node, begin, mid, lo, hi),
                        minimum(2 * node + 1, mid, end, lo, hi));
    }

public:
    enum class Test { Higher, AtMost };
    enum class Direction { FromLeft, FromRight };

    SpectrumRanges(const double* values, size_t n, const std::function<bool()>& cancelled) : y(values)
    {
        const auto blocks = (n + blockSize - 1) / blockSize;
        while (leaves < blocks) leaves *= 2;
        tree.resize(2 * leaves);
        for (size_t b = 0; b < blocks; ++b) {
            if (b % 256 == 0) checkCancelled(cancelled);
            const auto limits = std::minmax_element(y + b * blockSize, y + std::min(n, (b + 1) * blockSize));
            tree[leaves + b] = {*limits.first, *limits.second};
        }
        for (size_t j = leaves - 1; j > 0; --j)
            tree[j] = {std::min(tree[2*j].low, tree[2*j+1].low),
                       std::max(tree[2*j].high, tree[2*j+1].high)};
    }

    double minimum(int lo, int hi) const
    {
        return minimum(1, 0, leaves * blockSize, lo, hi);
    }

    int nearest(int lo, int hi, double level, Test test, Direction direction) const
    {
        return nearest(1, 0, leaves * blockSize, lo, hi, level, test, direction);
    }

private:
    int nearest(size_t node, size_t begin, size_t end, size_t lo, size_t hi,
                double level, Test test, Direction direction) const
    {
        if (hi <= begin || end <= lo) return -1;
        const auto& bounds = tree[node];
        if (test == Test::Higher ? bounds.high <= level : bounds.low > level) return -1;
        if (end - begin == blockSize) {
            const auto left = std::max(begin, lo), right = std::min(end, hi);
            for (size_t j = left; j < right; ++j) {
                const auto i = direction == Direction::FromLeft ? j : right - 1 - (j - left);
                if (test == Test::Higher ? y[i] > level : y[i] <= level) return static_cast<int>(i);
            }
            return -1;
        }
        const auto mid = begin + (end - begin) / 2;
        if (direction == Direction::FromLeft) {
            const auto hit = nearest(2 * node, begin, mid, lo, hi, level, test, direction);
            if (hit >= 0) return hit;
            return nearest(2 * node + 1, mid, end, lo, hi, level, test, direction);
        }
        const auto hit = nearest(2 * node + 1, mid, end, lo, hi, level, test, direction);
        if (hit >= 0) return hit;
        return nearest(2 * node, begin, mid, lo, hi, level, test, direction);
    }
};
} // namespace

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

std::vector<Peak> findSpectrumPeaks(int n, const double* y, const PeakOptions& opt,
                                    const std::function<bool()>& cancelled)
{
    checkCancelled(cancelled);
    const double threshold = spectrumProminenceThreshold(n, y, opt);
    const double minimum = n ? *std::min_element(y, y + n) : 0;
    std::vector<Peak> candidates;
    std::optional<SpectrumRanges> ranges;
    using Direction = SpectrumRanges::Direction;
    using Test = SpectrumRanges::Test;
    for (int i = 1; i < n - 1;) {
        if (i % 1024 < 2) checkCancelled(cancelled);
        if (y[i] <= y[i - 1]) { ++i; continue; }

        // A peak must rise on the left and fall after its (possibly flat) top.
        int end = i;
        while (end < n - 1 && y[end + 1] == y[i]) {
            ++end;
            if (end % 1024 == 0) checkCancelled(cancelled);
        }
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

        if (!ranges) ranges.emplace(y, size_t(n), cancelled);
        // Equal-height peaks share the same basin. Only a strictly higher bin
        // cuts off the search before the requested radius.
        const int leftHigher = ranges->nearest(lo, p, h, Test::Higher, Direction::FromRight);
        const int rightHigher = ranges->nearest(p + 1, hi + 1, h, Test::Higher, Direction::FromLeft);
        const int left = leftHigher < 0 ? lo : leftHigher + 1;
        const int right = rightHigher < 0 ? hi + 1 : rightHigher;
        const double prominence = h - std::max(ranges->minimum(left, p + 1),
                                               ranges->minimum(p, right));
        if (!std::isfinite(prominence))
            throw std::invalid_argument("Peak prominence exceeds the numeric range");
        if (prominence <= 0 || prominence < threshold) continue;

        const double level = h - 0.5 * prominence;
        const int l = ranges->nearest(left, p + 1, level, Test::AtMost, Direction::FromRight);
        const int r = ranges->nearest(p, right, level, Test::AtMost, Direction::FromLeft);
        double leftEdge = l, rightEdge = r;
        if (y[l] < level) leftEdge += (level - y[l]) / (y[l + 1] - y[l]);
        if (y[r] < level) rightEdge -= (level - y[r]) / (y[r - 1] - y[r]);
        const double width = rightEdge - leftEdge;
        if (width >= opt.minWidth)
            candidates.push_back(Peak{p, h, prominence, width});
    }

    checkCancelled(cancelled);
    if (opt.minDistance == 1) return candidates; // Already ordered by bin.

    // Select the most prominent eligible peak first in every competing cluster.
    std::sort(candidates.begin(), candidates.end(), [](const Peak& a, const Peak& b) {
        if (a.prominence != b.prominence) return a.prominence > b.prominence;
        if (a.height != b.height) return a.height > b.height;
        return a.index < b.index;
    });
    std::set<int> chosen;
    std::vector<Peak> result;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i % 1024 == 0) checkCancelled(cancelled);
        const auto& peak = candidates[i];
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
