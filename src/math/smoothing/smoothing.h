// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_SMOOTHING_H
#define ASTER_MATH_SMOOTHING_H
#include <cstddef>
#include <exception>
#include <functional>
#include <vector>
namespace aster::numeric
{
namespace SmoothingLimits
{
inline constexpr std::size_t maxSamples = 1000000;
inline constexpr std::size_t maxWindow = 501;
inline constexpr std::size_t maxWorkTerms = 100000000;
inline constexpr int maxDegree = 5;
} // namespace SmoothingLimits
enum class SmoothingMethod
{
    MovingMean,
    SavitzkyGolay
};
struct SmoothingCancelled final : std::exception
{
};
// Odd centered point window. Mean clips it at edges; SG fits the first/last
// complete window at edges (interp), with no padding or extrapolated samples.
std::vector<double> smoothSeries(const std::vector<double> &y, std::size_t window, int degree,
                                 SmoothingMethod method,
                                 const std::function<bool()> &cancelled = {});
} // namespace aster::numeric
#endif
