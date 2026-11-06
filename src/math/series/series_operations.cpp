// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/series/series_operations.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aster::numeric
{
namespace
{
void stop(const std::function<bool()> &cancelled)
{
    if (cancelled && cancelled())
        throw SeriesCancelled{};
}
void validate(const std::vector<double> &a, const std::function<bool()> &cancel)
{
    if (a.empty())
        throw std::invalid_argument("Empty series");
    if (a.size() > SeriesOperationLimits::maxSamples)
        throw std::length_error("Series exceeds the sample budget");
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        if (!std::isfinite(a[i]))
            throw std::invalid_argument("Series contains NaN or infinity");
    }
}
} // namespace
AmplitudeResult centerSeries(const std::vector<double> &y, bool standardize, int ddof,
                             const std::function<bool()> &cancel)
{
    validate(y, cancel);
    if (ddof < 0 || ddof > 1 || (standardize && y.size() <= std::size_t(ddof)))
        throw std::invalid_argument("Insufficient samples for the chosen standard deviation");
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (*bounds.first == *bounds.second)
    {
        if (standardize)
            throw std::domain_error("Constant series cannot be standardized");
        return {std::vector<double>(y.size(), 0), y.front(), 1};
    }
    const double magnitude = std::max(std::abs(*bounds.first), std::abs(*bounds.second));
    // Work in [-1,1] to avoid overflowing mean or sum of squared deviations,
    // including MSVC where long double has the same precision as double.
    long double mean = 0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        mean += (y[i] / magnitude - mean) / (i + 1);
    }
    long double variance = 0;
    if (standardize)
        for (std::size_t i = 0; i < y.size(); ++i)
        {
            if ((i & 4095) == 0)
                stop(cancel);
            const long double centered = y[i] / magnitude - mean;
            variance += centered * centered / (y.size() - ddof);
        }
    const auto sd = standardize ? std::sqrt(variance) : 1.L;
    if (standardize && !(sd > 0))
        throw std::domain_error("Standard deviation is zero");
    AmplitudeResult result;
    result.mean = static_cast<double>(mean * magnitude);
    result.scale = standardize ? static_cast<double>(sd * magnitude) : 1;
    if (!std::isfinite(result.mean) || !std::isfinite(result.scale) || result.scale <= 0)
        throw std::overflow_error("Mean or standard deviation cannot be represented");
    result.values.reserve(y.size());
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        const long double centered = y[i] / magnitude - mean;
        const double v = static_cast<double>(standardize ? centered / sd : centered * magnitude);
        if (!std::isfinite(v))
            throw std::overflow_error("Centered value overflows; rescale the input");
        result.values.push_back(v);
    }
    return result;
}
SamplingSummary inspectSampling(const std::vector<double> &x, double gapFactor,
                                const std::function<bool()> &cancel)
{
    validate(x, cancel);
    if (x.size() < 2 || !std::isfinite(gapFactor) || gapFactor <= 1)
        throw std::invalid_argument("Need at least two samples and gap factor > 1");
    SamplingSummary r;
    std::vector<double> sorted = x, positive;
    std::sort(sorted.begin(), sorted.end());
    stop(cancel);
    for (std::size_t i = 1; i < sorted.size(); ++i)
        r.duplicates += sorted[i] == sorted[i - 1];
    positive.reserve(x.size() - 1);
    for (std::size_t i = 1; i < x.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        const auto d = x[i] - x[i - 1];
        if (!std::isfinite(d))
            throw std::overflow_error("Sampling interval overflow");
        if (d > 0)
            positive.push_back(d);
        else if (d == 0)
            ++r.equalAdjacent;
        else
            ++r.descending;
    }
    r.strictlyIncreasing = r.descending == 0 && r.equalAdjacent == 0;
    if (!positive.empty())
    {
        std::sort(positive.begin(), positive.end());
        stop(cancel);
        const auto n = positive.size();
        r.minimumPositive = positive.front();
        r.maximumPositive = positive.back();
        r.medianPositive =
            n % 2 ? positive[n / 2] : .5 * positive[n / 2 - 1] + .5 * positive[n / 2];
        for (std::size_t i = 1; i < x.size(); ++i)
        {
            if ((i & 4095) == 0)
                stop(cancel);
            const double d = x[i] - x[i - 1];
            // Divide before comparison so gapFactor*median cannot overflow.
            if (d > 0 && d / r.medianPositive > gapFactor)
            {
                ++r.gapCount;
                if (r.gaps.size() < SeriesOperationLimits::detailRows)
                    r.gaps.push_back({i, d});
            }
        }
    }
    if (r.strictlyIncreasing)
    {
        r.expectedSpacing = (x.back() - x.front()) / (x.size() - 1);
        if (!std::isfinite(r.expectedSpacing) || r.expectedSpacing <= 0)
            throw std::overflow_error("Mean sample spacing cannot be represented");
        for (std::size_t i = 1; i < x.size(); ++i)
            r.relativeDeviation =
                std::max(r.relativeDeviation,
                         std::abs((x[i] - x[i - 1]) - r.expectedSpacing) / r.expectedSpacing);
        r.uniform = r.relativeDeviation <= SeriesOperationLimits::uniformRelativeTolerance;
    }
    stop(cancel);
    return r;
}
} // namespace aster::numeric
