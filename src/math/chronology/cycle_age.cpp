#include "math/chronology/cycle_age.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
namespace aster::chronology
{
namespace
{
void stop(const std::function<bool()> &cancel, std::size_t i = 0)
{
    if ((i & 1023) == 0 && cancel && cancel())
        throw Cancelled{};
}
} // namespace
CycleResult cycleAge(const std::vector<double> &x, const std::vector<double> &y,
                     const CycleOptions &o, const std::function<bool()> &cancel)
{
    stop(cancel);
    if (x.size() != y.size() || !std::isfinite(o.period) || o.period <= 0 ||
        !std::isfinite(o.firstAge) || !std::isfinite(o.minimumRelief) || o.minimumRelief < 0 ||
        !std::isfinite(o.minimumDistance) || o.minimumDistance < 0)
        throw Failure(Problem::Shape,
                      "Need paired data, positive period and nonnegative selection thresholds");
    if (x.size() < 3)
        throw Failure(Problem::CycleAnchors,
                      "Need at least three paired samples and two interior markers");
    if (x.size() > maxSamples)
        throw Failure(Problem::Budget, "Cycle-count input exceeds sample limit");
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        stop(cancel, i);
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
            throw Failure(Problem::NumericRange, "Non-finite cycle data", i + 1);
        if (i && !(x[i] > x[i - 1]))
            throw Failure(Problem::DepthOrder, "Cycle depth must strictly increase", i + 1);
    }
    if (!std::isfinite(x.back() - x.front()))
        throw Failure(Problem::NumericRange, "Depth span is not representable");
    auto height = [&](std::size_t i) { return o.troughs ? -y[i] : y[i]; };
    std::vector<std::size_t> peaks;
    CycleResult result;
    for (std::size_t start = 0; start < x.size();)
    {
        stop(cancel, start);
        std::size_t end = start;
        while (end + 1 < x.size() && y[end + 1] == y[start])
        {
            ++end;
            stop(cancel, end);
        }
        if (start > 0 && end + 1 < x.size() && height(start) > height(start - 1) &&
            height(end) > height(end + 1))
        {
            peaks.push_back(start + (end - start) / 2);
            if (end > start)
                ++result.plateauCount;
        }
        start = end + 1;
    }
    result.candidateCount = peaks.size();
    if (peaks.size() < 2)
        throw Failure(Problem::CycleAnchors,
                      "At least two same-phase interior extrema are required");
    std::vector<double> valleys(peaks.size() + 1), relief(peaks.size());
    for (std::size_t k = 0; k <= peaks.size(); ++k)
    {
        stop(cancel, k);
        const auto a = k == 0 ? 0 : peaks[k - 1], b = k == peaks.size() ? x.size() - 1 : peaks[k];
        double low = height(a);
        for (auto i = a; i <= b; ++i)
        {
            stop(cancel, i);
            low = std::min(low, height(i));
        }
        valleys[k] = low;
    }
    std::vector<std::size_t> candidates;
    for (std::size_t k = 0; k < peaks.size(); ++k)
    {
        stop(cancel, k);
        relief[k] = height(peaks[k]) - std::max(valleys[k], valleys[k + 1]);
        if (!std::isfinite(relief[k]))
            throw Failure(Problem::NumericRange, "Cycle relief overflows", peaks[k] + 1);
        if (relief[k] >= o.minimumRelief)
            candidates.push_back(k);
    }
    // Stable strength-first suppression; ties prefer the earlier original index.
    if (o.minimumDistance > 0)
    {
        std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b)
                  { return relief[a] != relief[b] ? relief[a] > relief[b] : peaks[a] < peaks[b]; });
        stop(cancel);
        std::set<std::size_t> accepted;
        for (std::size_t n = 0; n < candidates.size(); ++n)
        {
            stop(cancel, n);
            const auto k = candidates[n];
            auto next = accepted.lower_bound(k);
            if (next != accepted.end() && x[peaks[*next]] - x[peaks[k]] < o.minimumDistance)
                continue;
            if (next != accepted.begin() &&
                x[peaks[k]] - x[peaks[*std::prev(next)]] < o.minimumDistance)
                continue;
            accepted.insert(k);
        }
        candidates.assign(accepted.begin(), accepted.end());
    }
    if (candidates.size() < 2)
        throw Failure(Problem::CycleAnchors,
                      "Fewer than two cycles remain after the explicit thresholds");
    if (candidates.size() + (o.extendEnds ? 2 : 0) > maxControlPoints)
        throw Failure(Problem::Budget, "Cycle model exceeds control-point limit");
    for (std::size_t n = 0; n < candidates.size(); ++n)
    {
        stop(cancel, n);
        auto k = candidates[n];
        const double age = std::fma(double(n), o.period, o.firstAge);
        if (!std::isfinite(age) || (n && !(age > result.anchorAge.back())))
            throw Failure(Problem::NumericRange, "Cycle ages overflow or lose resolution",
                          peaks[k] + 1);
        result.indices.push_back(peaks[k]);
        result.relief.push_back(relief[k]);
        result.anchorAge.push_back(age);
        result.modelDepth.push_back(x[peaks[k]]);
    }
    result.modelAge = result.anchorAge;
    if (o.extendEnds)
    {
        const auto ends = mapDepth({x.front(), x.back()}, result.modelDepth, result.modelAge,
                                   Boundary::Linear, cancel);
        result.modelDepth.insert(result.modelDepth.begin(), x.front());
        result.modelAge.insert(result.modelAge.begin(), ends.age.front());
        result.modelDepth.push_back(x.back());
        result.modelAge.push_back(ends.age.back());
    }
    // Reuse beta29 age-model validation and interval-rate calculation.
    auto mapped =
        mapDepth(result.modelDepth, result.modelDepth, result.modelAge, Boundary::Reject, cancel);
    result.intervalRate = std::move(mapped.intervalRate);
    stop(cancel);
    return result;
}
} // namespace aster::chronology
