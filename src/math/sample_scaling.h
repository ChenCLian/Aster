// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_SAMPLE_SCALING_H
#define ASTER_MATH_SAMPLE_SCALING_H
#include <algorithm>
#include <cmath>

namespace aster::numeric
{
// Finite ordered bounds. Subtract this origin before normalizing so that
// representable differences on top of a large baseline survive centering.
inline double sampleMidpoint(double low, double high)
{
    return (low < 0 && high > 0) ? (low + high) / 2 : low + (high - low) / 2;
}

// An exact power of two, including for subnormal data. Division changes only
// the exponent of observations within range. Unit scale handles a zero range.
inline double binarySampleScale(double magnitude)
{
    return magnitude == 0 ? 1 : std::scalbn(1.0, std::ilogb(magnitude));
}

struct CenteredSampleScale
{
    double origin, scale;
    double unit(double value) const { return (value - origin) / scale; }
};

inline CenteredSampleScale centeredSampleScale(double low, double high)
{
    const double origin = sampleMidpoint(low, high);
    return {origin, binarySampleScale(std::max(high - origin, origin - low))};
}
}
#endif
