#include "processing/task_categories.h"
#include "processing/linear_detrend_task.h"
#include "processing/task_helpers.h"
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
    if (source->x.size() < 2) return NodeResult::failure(makeError(ErrorCode::TooFewSamples, {{"actual", double(source->x.size())}, {"minimum", 2}}));
    // Centered incremental means and long-double accumulators avoid naive sums
    // of squared large coordinates. This is ordinary least-squares, not LOWESS.
    long double meanX = 0, meanY = 0;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        meanX += (static_cast<long double>(source->x[i]) - meanX) / (i + 1);
        meanY += (static_cast<long double>(source->y[i]) - meanY) / (i + 1);
    }
    long double xx = 0, xy = 0;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        const auto dx = static_cast<long double>(source->x[i]) - meanX;
        xx += dx * dx;
        xy += dx * (static_cast<long double>(source->y[i]) - meanY);
    }
    if (!(xx > 0) || !std::isfinite(xx) || !std::isfinite(xy))
        return NodeResult::failure(makeError(ErrorCode::DegenerateX, {{"samples", double(source->x.size())}}));
    const long double slope = xy / xx;
    const double intercept = static_cast<double>(meanY - slope * meanX);
    if (!std::isfinite(static_cast<double>(slope)) || !std::isfinite(intercept))
        return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation", "linear_fit"}}));
    SeriesData trend = *source, detrended = *source;
    for (qsizetype i = 0; i < source->x.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        trend.y[i] = static_cast<double>(meanY + slope * (static_cast<long double>(source->x[i]) - meanX));
        detrended.y[i] = source->y[i] - trend.y[i];
    }
    return NodeResult::success({{"detrended", makeData(std::move(detrended))}, {"trend", makeData(std::move(trend))}},
                              {{"slope", static_cast<double>(slope)}, {"intercept", intercept}});
}

} // namespace aster
