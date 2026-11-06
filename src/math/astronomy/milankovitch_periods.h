// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_ASTRONOMY_MILANKOVITCH_PERIODS_H
#define ASTER_MATH_ASTRONOMY_MILANKOVITCH_PERIODS_H
#include <array>
namespace aster::astronomy
{
// Waltham (2015) model envelopes, NOT calibrated confidence intervals.
struct PeriodRange
{
    double lower = 0, upper = 0;
    double midpoint() const
    {
        return lower + (upper - lower) / 2;
    }
};
// Component order: obliquity, P1..P4, eccentricity ~405,95,124,99,131 ka.
struct MilankovitchPeriods
{
    std::array<PeriodRange, 10> periodKa;
    PeriodRange lunarDistanceKm, dayHours, meanObliquityDegrees, axialPrecessionKa;
};
// Age in Ma before present, 0..4000. Zero is evaluated by the continuous limit.
MilankovitchPeriods milankovitchPeriods(double ageMa);
} // namespace aster::astronomy
#endif
