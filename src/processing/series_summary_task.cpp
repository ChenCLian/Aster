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
    long double mean = 0, m2 = 0;
    double minimum = source->y[0], maximum = source->y[0];
    for (qsizetype i = 0; i < source->y.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        const long double delta = static_cast<long double>(source->y[i]) - mean;
        mean += delta / (i + 1);
        m2 += delta * (static_cast<long double>(source->y[i]) - mean);
        minimum = std::min(minimum, source->y[i]); maximum = std::max(maximum, source->y[i]);
    }
    const double variance = static_cast<double>(std::max(0.0L, m2 / source->y.size()));
    TableData table{{"count", "mean", "population_variance", "minimum", "maximum"},
        {{static_cast<double>(source->y.size()), static_cast<double>(mean), variance, minimum, maximum}}};
    return NodeResult::success({{"summary", makeData(std::move(table))}});
}

} // namespace aster
