// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/lowess_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "third_party/lowess/Lowess.h"
#include <vector>
#include <algorithm>
namespace aster {
TaskDescriptor LowessTask::descriptor()
{
    auto d = task_detail::seriesTransform("LOWESS detrend", taskCategory(TaskCategory::Trend));
    d.version = 4; // Ordinary LOWESS by default; robust reweighting remains explicit.
    d.outputs = {{"detrended", "Detrended series", DataKind::Series, true}, {"trend", "LOWESS trend", DataKind::Series, true}};
    d.defaultOutput = "detrended";
    d.parameters = {task_detail::number("fraction", "Window fraction", 0.35, 0.001, 1),
        task_detail::number("iterations", "Robust iterations (0 = LOWESS, 5 = rLOWESS)", 0, 0, 10, ParameterKind::Integer),
        science_detail::boolean("retain_mean", "Retain original mean", false)};
    return d;
}
LowessTask::LowessTask(const QJsonObject& p) : m_fraction(p["fraction"].toDouble()),
    m_iterations(p["iterations"].toInt()), m_retainMean(p["retain_mean"].toBool()) {}
NodeResult LowessTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto error = science_detail::orderedSeries(*source); if (!error.isEmpty()) return NodeResult::failure(error);
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    std::vector<double> x(source->x.begin(), source->x.end()), y(source->y.begin(), source->y.end());
    // LOWESS is invariant to positive affine transformations of X. Evaluate in
    // bounded coordinates so squared local distances cannot overflow/underflow.
    // Only working coordinates change; published axes retain the user's values.
    const double origin = x.front(), span = x.back() - origin;
    const double scale = std::max(std::abs(x.front()), std::abs(x.back()));
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = std::isfinite(span) ? (x[i] - origin) / span
                                : x[i] / scale - origin / scale;
        if (!std::isfinite(x[i]) || (i && x[i] <= x[i - 1]))
            return NodeResult::failure(makeError(ErrorCode::NumericRange, {},
                "LOWESS working coordinates cannot preserve distinct X values"));
    }
    std::vector<double> fitted(x.size()), robust(x.size()), residual(x.size());
    CppLowess::TemplatedLowess<std::vector<double>, double> algorithm;
    const int code = algorithm.lowess(x, y, m_fraction, m_iterations, 0.0, fitted, robust, residual);
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    if (code != 0) return NodeResult::failure(makeError(ErrorCode::LowessFailure, {{"library_code", code}, {"fraction", m_fraction}, {"iterations", m_iterations}, {"samples", double(source->x.size())}}));
    SeriesData detrended = *source, trend = *source;
    // LOWESS residuals need not have zero mean (especially after robust reweighting).
    // y - fitted + mean(fitted) preserves mean(y); adding mean(y) does not.
    long double meanAdjustment = 0;
    if (m_retainMean)
        for (double value : fitted)
            meanAdjustment += static_cast<long double>(value) / fitted.size();
    for (qsizetype i = 0; i < source->y.size(); ++i) {
        trend.y[i] = fitted[i];
        detrended.y[i] = static_cast<double>(residual[i] + meanAdjustment);
    }
    return NodeResult::success({{"detrended", makeData(std::move(detrended))}, {"trend", makeData(std::move(trend))}},
        {{"fraction", m_fraction}, {"robust_iterations", m_iterations}, {"retain_mean", m_retainMean},
         {"mean_adjustment", static_cast<double>(meanAdjustment)},
         {"neighbor_count", double(std::max(size_t(2), std::min(x.size(), size_t(m_fraction * x.size()))))},
         {"variant", m_iterations > 0 ? "robust_lowess" : "ordinary_lowess"},
         {"reference_implementation", "Acycle detrending.m: LOWESS / rLOWESS; NETLIB local-linear library retained"}});
}
}
