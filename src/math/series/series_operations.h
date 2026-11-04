#ifndef ASTER_MATH_SERIES_OPERATIONS_H
#define ASTER_MATH_SERIES_OPERATIONS_H
#include <functional>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <exception>
namespace aster::numeric
{
namespace SeriesOperationLimits
{
inline constexpr std::size_t maxSamples = 1000000;
inline constexpr std::size_t detailRows = 4096;
inline constexpr double uniformRelativeTolerance = 1e-6;
} // namespace SeriesOperationLimits
struct SeriesCancelled final : std::exception
{
};
struct AmplitudeResult
{
    std::vector<double> values;
    double mean = 0, scale = 1;
};
// Standardization uses (Y-mean)/SD, ddof 0 or 1; demean preserves Y units.
AmplitudeResult centerSeries(const std::vector<double> &y, bool standardize, int ddof,
                             const std::function<bool()> &cancelled = {});
struct Gap
{
    std::size_t rightIndex;
    double spacing;
};
struct SamplingSummary
{
    std::size_t duplicates = 0, descending = 0, equalAdjacent = 0, gapCount = 0;
    double medianPositive = 0, minimumPositive = 0, maximumPositive = 0;
    double expectedSpacing = 0, relativeDeviation = 0;
    bool strictlyIncreasing = false, uniform = false;
    std::vector<Gap> gaps; // Bounded details; gapCount counts the entire input
};
SamplingSummary inspectSampling(const std::vector<double> &x, double gapFactor,
                                const std::function<bool()> &cancelled = {});
} // namespace aster::numeric
#endif
