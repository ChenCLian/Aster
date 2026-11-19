// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/series_summary_task.h"
#include "processing/task_helpers.h"
#include <algorithm>
#include <cmath>

namespace aster {
using namespace task_detail;
TaskDescriptor SeriesSummaryTask::descriptor()
{
    auto d = seriesTransform("Series summary", taskCategory(TaskCategory::Summary));
    d.outputs = {{"summary", "Summary table", DataKind::Table, true}};
    d.defaultOutput = "summary";
    return d;
}
NodeResult SeriesSummaryTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    if (source->y.isEmpty()) return NodeResult::failure(makeError(ErrorCode::TooFewSamples, {{"actual", 0}, {"minimum", 1}}));
    double minimum = source->y[0], maximum = source->y[0];
    for (qsizetype i = 0; i < source->y.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        if (!std::isfinite(source->y[i])) return NodeResult::failure(makeError(ErrorCode::NumericRange));
        minimum = std::min(minimum, source->y[i]); maximum = std::max(maximum, source->y[i]);
    }
    double mean = minimum, variance = 0;
    if (minimum != maximum) {
        // Exact power-of-two scaling also works where long double is binary64.
        int exponent = 0;
        std::frexp(std::max(std::abs(minimum), std::abs(maximum)), &exponent);
        const long double origin = std::scalbn(static_cast<long double>(source->y.front()), -exponent);
        long double offsetMean = 0, m2 = 0;
        for (qsizetype i = 0; i < source->y.size(); ++i) {
            if (cancelled(context, i)) return NodeResult::cancelled();
            const long double offset = std::scalbn(static_cast<long double>(source->y[i]), -exponent) - origin;
            const long double delta = offset - offsetMean;
            offsetMean += delta / (i + 1);
            m2 += delta * (offset - offsetMean);
        }
        mean = static_cast<double>(std::scalbn(origin + offsetMean, exponent));
        variance = static_cast<double>(std::scalbn(m2 / source->y.size(), 2 * exponent));
        if (!std::isfinite(mean) || !std::isfinite(variance) || variance <= 0)
            return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation", "population_variance"}},
                "Population variance of the nonconstant series is outside the representable range; rescale Y"));
    }
    TableData table{{"count", "mean", "population_variance", "minimum", "maximum"},
        {{static_cast<double>(source->y.size()), mean, variance, minimum, maximum}}};
    return NodeResult::success({{"summary", makeData(std::move(table))}});
}

} // namespace aster
