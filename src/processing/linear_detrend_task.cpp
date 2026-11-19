// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/linear_detrend_task.h"
#include "processing/task_helpers.h"
#include "math/sample_scaling.h"
#include <algorithm>
#include <cmath>

namespace aster {
using namespace task_detail;
TaskDescriptor LinearDetrendTask::descriptor()
{
    auto d = seriesTransform("Linear detrend", taskCategory(TaskCategory::Trend));
    d.outputs = {{"detrended", "Detrended series", DataKind::Series, true},
                 {"trend", "Fitted trend", DataKind::Series, true}};
    d.defaultOutput = "detrended";
    return d;
}
NodeResult LinearDetrendTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    if (source->x.size() < 2) return NodeResult::failure(makeError(ErrorCode::TooFewSamples, {{"actual", double(source->x.size())}, {"minimum", 2}}));
    const auto bx = std::minmax_element(source->x.begin(), source->x.end());
    const auto by = std::minmax_element(source->y.begin(), source->y.end());
    const auto x = numeric::centeredSampleScale(*bx.first, *bx.second);
    const auto y = numeric::centeredSampleScale(*by.first, *by.second);
    long double meanX = 0, meanY = 0;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        meanX += x.unit(source->x[i]);
        meanY += y.unit(source->y[i]);
    }
    meanX /= source->x.size();
    meanY /= source->y.size();
    long double xx = 0, xy = 0;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        const auto dx = x.unit(source->x[i]) - meanX;
        xx += dx * dx;
        xy += dx * (y.unit(source->y[i]) - meanY);
    }
    if (!(xx > 0) || !std::isfinite(xx) || !std::isfinite(xy))
        return NodeResult::failure(makeError(ErrorCode::DegenerateX, {{"samples", double(source->x.size())}}));
    const long double unitSlope = xy / xx;
    const long double slope = std::scalbn(unitSlope, std::ilogb(y.scale) - std::ilogb(x.scale));
    const auto atOrigin = std::fma(std::fma(-unitSlope, meanX, meanY),
                                   static_cast<long double>(y.scale), static_cast<long double>(y.origin));
    const double intercept = static_cast<double>(std::fma(-slope, static_cast<long double>(x.origin), atOrigin));
    if (!std::isfinite(static_cast<double>(slope)) || (unitSlope != 0 && static_cast<double>(slope) == 0) || !std::isfinite(intercept))
        return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation", "linear_fit"}}));
    SeriesData trend = *source, detrended = *source;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        const auto fit = std::fma(unitSlope, x.unit(source->x[i]) - meanX, meanY);
        trend.y[i] = static_cast<double>(std::fma(fit, static_cast<long double>(y.scale), static_cast<long double>(y.origin)));
        // Compute the residual before putting the large baseline back into the displayed trend.
        detrended.y[i] = static_cast<double>((y.unit(source->y[i]) - fit) * y.scale);
        if (!std::isfinite(trend.y[i]) || !std::isfinite(detrended.y[i]))
            return NodeResult::failure(makeError(ErrorCode::NumericRange,
                {{"operation", "linear_detrend"}, {"point", double(i + 1)}}));
    }
    return NodeResult::success({{"detrended", makeData(std::move(detrended))}, {"trend", makeData(std::move(trend))}},
                              {{"slope", static_cast<double>(slope)}, {"intercept", intercept}});
}

} // namespace aster
