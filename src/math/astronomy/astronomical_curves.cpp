// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/astronomy/astronomical_curves.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aster::astronomy {
namespace {
void validate(const OrbitalElements& o)
{
    if (!std::isfinite(o.eccentricity) || o.eccentricity < 0 || o.eccentricity >= 1 ||
        !std::isfinite(o.obliquity) || o.obliquity < 0 || o.obliquity > pi / 2 ||
        !std::isfinite(o.perihelion))
        throw std::invalid_argument("Invalid orbital elements");
}
void addStandardized(std::vector<double>& out, const std::vector<double>& values, double weight)
{
    if (weight == 0) return; // An unused constant component need not be standardized.
    // Center before summation. On platforms where long double is binary64,
    // sum(values)/N can differ from an exactly constant input by one ULP.
    // Offsets keep that variance exactly zero without an arbitrary cutoff
    // that would also reject genuinely small orbital variations.
    const long double origin = values.front();
    long double meanOffset = 0;
    for (double v : values) meanOffset += static_cast<long double>(v) - origin;
    meanOffset /= values.size();
    long double squares = 0;
    for (double v : values) {
        const long double d = (static_cast<long double>(v) - origin) - meanOffset;
        squares += d * d;
    }
    const long double sd = std::sqrt(squares / (values.size() - 1));
    if (!(sd > 0) || !std::isfinite(sd))
        throw std::invalid_argument("ETP has a constant or invalid weighted component");
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] += static_cast<double>(weight * ((static_cast<long double>(values[i]) - origin) - meanOffset) / sd);
        if (!std::isfinite(out[i])) throw std::invalid_argument("ETP exceeds numeric range");
    }
}
}
double climaticPrecession(const OrbitalElements& o)
{
    validate(o);
    return o.eccentricity * std::sin(o.perihelion);
}
double dailyMeanInsolation(const OrbitalElements& o, double latitude, double longitude, double solar)
{
    validate(o);
    if (!std::isfinite(latitude) || std::abs(latitude) > pi / 2 ||
        !std::isfinite(longitude) || !std::isfinite(solar) || solar <= 0)
        throw std::invalid_argument("Invalid insolation parameters");
    longitude = std::remainder(longitude, 2 * pi);
    const double sinDelta = std::sin(o.obliquity) * std::sin(longitude);
    const double a = std::sin(latitude) * sinDelta;
    const double b = std::cos(latitude) * std::sqrt(std::max(0.0, 1 - sinDelta * sinDelta));
    // Stable at both poles and on the polar circles; avoid tan(phi) overflow.
    double dailyCosine;
    if (a >= b) dailyCosine = std::max(0.0, a);       // polar day
    else if (a <= -b) dailyCosine = 0;               // polar night
    else {
        const double h = std::acos(std::clamp(-a / b, -1.0, 1.0));
        dailyCosine = (h * a + b * std::sin(h)) / pi;
    }
    // Solar perihelion longitude = heliocentric pibar + pi.
    const double inverseRadius = (1 - o.eccentricity * std::cos(longitude - o.perihelion)) /
                                 (1 - o.eccentricity * o.eccentricity);
    const double result = solar * inverseRadius * inverseRadius * dailyCosine;
    if (!std::isfinite(result)) throw std::invalid_argument("Insolation exceeds numeric range");
    return std::max(0.0, result);
}
std::vector<double> etp(const std::vector<OrbitalElements>& elements, double we, double wt, double wp)
{
    if (elements.size() < 2 || !std::isfinite(we) || !std::isfinite(wt) || !std::isfinite(wp))
        throw std::invalid_argument("ETP requires at least two samples and finite weights");
    std::vector<double> out(elements.size(), 0), e, t, p;
    e.reserve(elements.size()); t.reserve(elements.size()); p.reserve(elements.size());
    for (const auto& o : elements) {
        validate(o); e.push_back(o.eccentricity); t.push_back(o.obliquity);
        p.push_back(climaticPrecession(o));
    }
    addStandardized(out, e, we); addStandardized(out, t, wt); addStandardized(out, p, wp);
    return out;
}
}
