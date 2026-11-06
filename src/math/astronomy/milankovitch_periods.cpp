// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/astronomy/milankovitch_periods.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aster::astronomy
{
namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
PeriodRange checked(double lo, double hi)
{
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo <= 0 || hi < lo)
        throw std::domain_error("Invalid or unresolved Milankovitch model bounds");
    return {lo, hi};
}
PeriodRange invert(double lo, double hi)
{
    const auto r = checked(lo, hi);
    return checked(1296 / r.upper, 1296 / r.lower);
}
double exponentialMean(double x)
{
    return x == 0 ? 1 : -std::expm1(-x) / x;
}
double smoothDrag(double age, double tau, double present, double wholeHistory)
{
    const double weight = exponentialMean(4500 / tau);
    const double baseline = (wholeHistory - weight * present) / (1 - weight);
    return baseline + (present - baseline) * exponentialMean(age / tau);
}
double meanTilt(double a)
{
    return (((5.991817e-9 * a - 7.656218e-6) * a + .003838065) * a - .8430214) * a + 84.23171;
}
} // namespace
MilankovitchPeriods milankovitchPeriods(double age)
{
    if (!std::isfinite(age) || age < 0 || age > 4000)
        throw std::invalid_argument("Model age must be finite and in 0..4000 Ma");
    // Independent implementation of the published smooth/fluctuating drag envelopes.
    const double stochasticMean = (46 * 6.93 - 22) / (45 * std::exp(1.48 / std::sqrt(45.)));
    const double count = 1 + age / 100;
    const double dragLow =
        std::min(smoothDrag(age, 100, 20, 6.77), 5.69 + 1.225 * (-std::expm1(-age / 2103)));
    const double dragHigh =
        std::max(smoothDrag(age, 1000, 22, 6.93),
                 (22 + count * stochasticMean * std::exp(1.48 / std::sqrt(count))) / (1 + count));
    const double presentPower = std::pow(384399000., 6.5);
    auto distance = [&](double drag)
    {
        const double powered = presentPower - 6.5 * 3.15576e50 * age * drag;
        if (!(powered > 0))
            throw std::domain_error("Lunar distance envelope reaches a singularity");
        return 1e-6 * std::pow(powered, 1 / 6.5); // thousand km internally
    };
    const auto d = checked(distance(dragHigh), distance(dragLow));
    const auto tilt = checked(meanTilt(d.lower), meanTilt(d.upper));
    auto rotation = [](double a, double ob)
    {
        return (3.397618e9 - 7.26061057334 * std::sqrt(4.0334319e14 * a)) /
               (8.04e12 * std::cos(ob * pi / 180));
    };
    const auto omega = checked(rotation(d.upper, tilt.upper), rotation(d.lower, tilt.lower));
    auto k = [](double a, double ob, double w)
    { return 3.989917e8 * w * (73500 / (a * a * a) + .000594103125029) * std::cos(ob * pi / 180); };
    const auto frequency =
        checked(k(d.upper, tilt.upper, omega.lower), k(d.lower, tilt.lower, omega.upper));
    MilankovitchPeriods r;
    r.lunarDistanceKm = checked(1000 * d.lower, 1000 * d.upper);
    r.dayHours = checked(2 * pi * (1 + 1 / 365.24) / (3600 * omega.upper),
                         2 * pi * (1 + 1 / 365.24) / (3600 * omega.lower));
    r.meanObliquityDegrees = tilt;
    r.axialPrecessionKa = invert(frequency.lower, frequency.upper);
    r.periodKa[0] = invert(frequency.lower - 18.848 - .066, frequency.upper - 18.848 + .066);
    constexpr double drift[4] = {4.257482, 7.453, 17.916, 17.368};
    constexpr double spread[4] = {.00003, .019, .2, .2};
    for (int i = 0; i < 4; ++i)
        r.periodKa[1 + i] =
            invert(frequency.lower + drift[i] - spread[i], frequency.upper + drift[i] + spread[i]);
    // Table 1 nominal eccentricity-frequency envelopes; no age-varying orbital phases.
    constexpr double eccentricity[5] = {3.196, 13.66, 10.46, 13.11, 9.92};
    for (int i = 0; i < 5; ++i)
    {
        const double uncertainty = i == 0 ? .019 : .2;
        r.periodKa[5 + i] = invert(eccentricity[i] - uncertainty, eccentricity[i] + uncertainty);
    }
    return r;
}
} // namespace aster::astronomy
