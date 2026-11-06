// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/chronology/age_model.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aster::chronology
{
namespace
{
void stop(const std::function<bool()> &cancel, std::size_t i = 0)
{
    if ((i & 1023) == 0 && cancel && cancel())
        throw Cancelled{};
}
void ordered(const std::vector<double> &v, Problem p, std::size_t limit,
             const std::function<bool()> &cancel)
{
    if (v.size() < 2)
        throw Failure(Problem::Shape, "At least two samples are required");
    if (v.size() > limit)
        throw Failure(Problem::Budget, "Chronology sample limit exceeded");
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        stop(cancel, i);
        if (!std::isfinite(v[i]))
            throw Failure(Problem::NumericRange, "Non-finite coordinate", i + 1);
        if (i && !(v[i] > v[i - 1]))
            throw Failure(
                p, "Coordinates must be strictly increasing; no automatic sorting or merging",
                i + 1);
        if (i && !std::isfinite(v[i] - v[i - 1]))
            throw Failure(Problem::NumericRange, "Coordinate interval exceeds finite double range",
                          i + 1);
    }
}
// Avoid overflow/underflow in a*factor/b when the final quotient is representable,
// including on platforms where long double has only double precision.
double positiveRatio(double a, double b, double factor, std::size_t row, double shape = 1)
{
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(factor) || !std::isfinite(shape) ||
        a <= 0 || b <= 0 || factor <= 0 || shape <= 0)
        throw Failure(Problem::NumericRange, "Invalid positive ratio", row);
    int ea, eb, ef, es;
    const double ma = std::frexp(a, &ea), mb = std::frexp(b, &eb), mf = std::frexp(factor, &ef),
                 ms = std::frexp(shape, &es);
    const double value = std::scalbn((ma / mb) * mf * ms, ea - eb + ef + es);
    if (!std::isfinite(value) || value <= 0)
        throw Failure(Problem::NumericRange, "Positive interval or rate is not representable", row);
    return value;
}
} // namespace
RateResult integrateRate(const std::vector<double> &depth, const std::vector<double> &rate,
                         double factor, double firstAge, RateRule rule,
                         const std::function<bool()> &cancel)
{
    stop(cancel);
    if (depth.size() != rate.size() || !std::isfinite(factor) || factor <= 0 ||
        !std::isfinite(firstAge) ||
        (rule != RateRule::LeftConstant && rule != RateRule::LinearRate))
        throw Failure(Problem::Shape,
                      "Invalid paired rate data, units, first age or integration rule");
    ordered(depth, Problem::DepthOrder, maxSamples, cancel);
    for (std::size_t i = 0; i < rate.size(); ++i)
    {
        stop(cancel, i);
        if (!std::isfinite(rate[i]) || rate[i] <= 0)
            throw Failure(Problem::RatePositive,
                          "Every rate, including the final row, must be finite and positive",
                          i + 1);
    }
    RateResult result;
    result.age.resize(depth.size());
    result.age[0] = firstAge;
    result.minimumIncrement = std::numeric_limits<double>::max();
    long double accumulated = firstAge, correction = 0;
    for (std::size_t i = 1; i < depth.size(); ++i)
    {
        stop(cancel, i);
        double denominator = rate[i - 1], shape = 1;
        if (rule == RateRule::LinearRate && rate[i] != rate[i - 1])
        {
            const double high = std::max(rate[i - 1], rate[i]),
                         low = std::min(rate[i - 1], rate[i]);
            const double delta = (high - low) / high;
            // Integral of 1/r(z), NOT inverse of a mean rate or integral of r.
            shape = delta < .5 ? -std::log1p(-delta) / delta
                               : (std::log(high) - std::log(low)) / (1 - low / high);
            denominator = high;
        }
        const double increment =
            positiveRatio(depth[i] - depth[i - 1], denominator, factor, i + 1, shape);
        const long double adjusted = static_cast<long double>(increment) - correction;
        const long double next = accumulated + adjusted;
        correction = (next - accumulated) - adjusted;
        accumulated = next;
        result.age[i] = static_cast<double>(accumulated);
        if (!std::isfinite(result.age[i]) || !(result.age[i] > result.age[i - 1]))
            throw Failure(
                Problem::NumericRange,
                "Age increment overflows or cannot increase the stored age at double precision",
                i + 1);
        result.minimumIncrement = std::min(result.minimumIncrement, increment);
        result.maximumIncrement = std::max(result.maximumIncrement, increment);
    }
    stop(cancel);
    return result;
}
MappingResult mapDepth(const std::vector<double> &depth, const std::vector<double> &d,
                       const std::vector<double> &a, Boundary boundary,
                       const std::function<bool()> &cancel)
{
    stop(cancel);
    if (d.size() != a.size() || (boundary != Boundary::Reject && boundary != Boundary::Linear))
        throw Failure(Problem::Shape, "Invalid age controls or boundary rule");
    ordered(depth, Problem::DepthOrder, maxSamples, cancel);
    ordered(d, Problem::DepthOrder, maxControlPoints, cancel);
    ordered(a, Problem::AgeOrder, maxControlPoints, cancel);
    if (boundary == Boundary::Reject && (depth.front() < d.front() || depth.back() > d.back()))
        throw Failure(Problem::OutsideModel,
                      "Input depth lies outside control-point coverage; extrapolation is disabled");
    MappingResult result;
    result.intervalRate.resize(d.size() - 1);
    for (std::size_t i = 1; i < d.size(); ++i)
    {
        stop(cancel, i);
        result.intervalRate[i - 1] = positiveRatio(d[i] - d[i - 1], a[i] - a[i - 1], 1, i + 1);
    }
    result.age.resize(depth.size());
    std::size_t j = 0;
    for (std::size_t i = 0; i < depth.size(); ++i)
    {
        stop(cancel, i);
        while (j + 2 < d.size() && depth[i] > d[j + 1])
            ++j;
        if (depth[i] < d.front() || depth[i] > d.back())
            ++result.extrapolated;
        if (depth[i] == d[j])
            result.age[i] = a[j];
        else if (depth[i] == d[j + 1])
            result.age[i] = a[j + 1];
        else
        {
            const long double t = (static_cast<long double>(depth[i]) - d[j]) / (d[j + 1] - d[j]);
            const long double value = t >= 0 && t <= 1
                                          ? (1 - t) * a[j] + t * a[j + 1]
                                          : static_cast<long double>(a[j]) + t * (a[j + 1] - a[j]);
            result.age[i] = static_cast<double>(value);
        }
        if (!std::isfinite(result.age[i]) || (i && !(result.age[i] > result.age[i - 1])))
            throw Failure(Problem::NumericRange,
                          "Converted ages overflow or lose strict ordering at double precision",
                          i + 1);
    }
    stop(cancel);
    return result;
}
} // namespace aster::chronology
