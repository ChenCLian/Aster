// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/services/series_cleanup.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace aster
{
SeriesCleanupResult cleanSeries(const SeriesData &source, const QString &policy,
                                const CancellationToken &cancel)
{
    SeriesCleanupResult result;
    if (source.x.size() != source.y.size() || source.x.size() < 2)
    {
        result.error = makeError(ErrorCode::InputInvalid, {},
                                 "Cleanup requires at least two paired X/Y samples");
        return result;
    }
    if (!QStringList{"average", "first", "last", "error"}.contains(policy))
    {
        result.error = makeError(ErrorCode::ParameterInvalid, {}, "Unknown duplicate policy");
        return result;
    }
    for (qsizetype i = 0; i < source.x.size(); ++i)
    {
        if (cancel.isCancelled())
        {
            result.cancelled = true;
            return result;
        }
        if (!std::isfinite(source.x[i]) || !std::isfinite(source.y[i]))
        {
            result.error = makeError(ErrorCode::InputInvalid, {{"point", double(i + 1)}},
                "Nonfinite X/Y: choose the reader's skip-invalid-rows policy before running");
            return result;
        }
    }
    std::vector<qsizetype> order(source.x.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](auto a, auto b) { return source.x[a] < source.x[b]; });
    result.data.xAxis = source.xAxis;
    result.data.yAxis = source.yAxis;
    result.data.x.reserve(source.x.size());
    result.data.y.reserve(source.y.size());
    for (size_t i = 0; i < order.size();)
    {
        if (cancel.isCancelled())
        {
            result.cancelled = true;
            return result;
        }
        size_t end = i + 1;
        while (end < order.size() && source.x[order[end]] == source.x[order[i]])
            ++end;
        if (end > i + 1 && policy == "error")
        {
            result.error = makeError(ErrorCode::XDuplicate,
                {{"point_before", double(order[i] + 1)}, {"point", double(order[i + 1] + 1)},
                 {"x_before", source.x[order[i]]}, {"x", source.x[order[i + 1]]},
                 {"duplicate_policy", policy}});
            return result;
        }
        double value = source.y[order[i]];
        if (policy == "last")
            value = source.y[order[end - 1]];
        else if (policy == "average" && end > i + 1)
        {
            // Scale first: Windows long double is double, so an unscaled sum
            // can overflow even when the mean is representable.
            double scale = 0;
            for (size_t j = i; j < end; ++j)
                scale = std::max(scale, std::abs(source.y[order[j]]));
            long double sum = 0;
            if (scale > 0)
                for (size_t j = i; j < end; ++j)
                    sum += static_cast<long double>(source.y[order[j]]) / scale;
            const auto unitMean = std::clamp(sum / (end - i), -1.0L, 1.0L);
            value = static_cast<double>(unitMean * scale);
        }
        result.data.x.push_back(source.x[order[i]]);
        result.data.y.push_back(value);
        for (size_t j = i; j < end; ++j)
            if (order[j] != qsizetype(j))
                result.reordered = true;
        result.mergedRows += end - i - 1;
        i = end;
    }
    if (result.data.x.size() < 2)
        result.error = makeError(ErrorCode::TooFewDistinctX,
                                 {{"actual", double(result.data.x.size())}, {"minimum", 2}});
    return result;
}
} // namespace aster
