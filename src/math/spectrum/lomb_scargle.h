// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_LOMB_SCARGLE_H
#define ASTER_MATH_LOMB_SCARGLE_H
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>
#include <exception>
namespace aster::numeric
{
namespace LombLimits
{
inline constexpr std::size_t maxSamples = 1000000, maxFrequencies = 65536, maxWorkTerms = 50000000;
inline constexpr double rankTolerance = 1e-12, minCycles = 1e-6, maxCycles = 1e8;
} // namespace LombLimits
struct LombOptions
{
    double minimumFrequency = 0, maximumFrequency = 0; // 0: explicit automatic grid rule.
    int samplesPerPeak = 5;
};
struct LombResult
{
    std::vector<double> frequency, power;
    double duration = 0, medianSpacing = 0, requestedMaximum = 0, frequencyStep = 0;
    std::size_t rankOneBins = 0, rankZeroBins = 0;
};
struct LombCancelled final : std::exception
{
};
// Equal-weight floating-mean fit: y=a*cos(2*pi*f*x)+b*sin(2*pi*f*x)+c.
// Power=(chi0-chi_f)/chi0, dimensionless. Not PSD, probability or FAP.
// Ordered finite X; no resampling, sorting or gap filling. Constant Y rejected.
LombResult lombScargle(const std::vector<double> &x, const std::vector<double> &y,
                       const LombOptions &options = {}, const std::function<bool()> &cancelled = {},
                       const std::function<void(int)> &progress = {});
} // namespace aster::numeric
#endif
