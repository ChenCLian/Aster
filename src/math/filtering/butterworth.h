// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_BUTTERWORTH_H
#define ASTER_MATH_BUTTERWORTH_H
#include <cstddef>
#include <exception>
#include <functional>
#include <vector>
namespace aster::filtering
{
namespace ButterworthLimits
{
inline constexpr std::size_t maxSamples = 1000000;
inline constexpr int maxOrder = 10;
inline constexpr double minimumNyquistRatio = 1e-4;
} // namespace ButterworthLimits
enum class PassType
{
    Low,
    High
};
struct ButterworthResult
{
    std::vector<double> filtered, frequency, gain;
    std::size_t padding = 0;
};
struct ButterworthCancelled final : std::exception
{
};
// Bilinear Butterworth, SOS direct form II transposed, steady initial states,
// odd reflection 3*(order+1) samples, forward/reverse passes. No demeaning.
// cutoff is the single-pass -3 dB frequency; combined gain there is 0.5.
ButterworthResult butterworth(const std::vector<double> &y, double spacing, double cutoff,
                              int order, PassType type,
                              const std::function<bool()> &cancelled = {});
} // namespace aster::filtering
#endif
