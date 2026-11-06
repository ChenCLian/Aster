// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_CHRONOLOGY_AGE_MODEL_H
#define ASTER_MATH_CHRONOLOGY_AGE_MODEL_H
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>
#include <exception>
namespace aster::chronology
{
inline constexpr std::size_t maxSamples = 1000000, maxControlPoints = 100000;
enum class Problem
{
    Shape,
    DepthOrder,
    AgeOrder,
    RatePositive,
    OutsideModel,
    NumericRange,
    Budget,
    CycleAnchors
};
struct Failure final : std::runtime_error
{
    Problem problem;
    std::size_t row; // One-based point/ending interval, or zero when not applicable.
    Failure(Problem p, const char *message, std::size_t at = 0)
        : std::runtime_error(message), problem(p), row(at)
    {
    }
};
struct Cancelled final : std::exception
{
};
enum class RateRule
{
    LeftConstant,
    LinearRate
};
enum class Boundary
{
    Reject,
    Linear
};
struct RateResult
{
    std::vector<double> age;
    double minimumIncrement = 0, maximumIncrement = 0;
};
struct MappingResult
{
    std::vector<double> age, intervalRate;
    std::size_t extrapolated = 0;
};
// factor converts depth/rate into the requested age unit. X stays in its
// original unit. Positive rates, strictly increasing depth; no row cleanup.
RateResult integrateRate(const std::vector<double> &depth, const std::vector<double> &rate,
                         double factor, double firstAge, RateRule rule,
                         const std::function<bool()> &cancel = {});
// Controls and query depth use the SAME unit. Ages must strictly increase
// with depth. Piecewise-linear interpolation, optional endpoint-secant extrapolation.
MappingResult mapDepth(const std::vector<double> &depth, const std::vector<double> &controlDepth,
                       const std::vector<double> &controlAge, Boundary boundary,
                       const std::function<bool()> &cancel = {});
} // namespace aster::chronology
#endif
