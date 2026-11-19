// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/lowess_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "third_party/lowess/Lowess.h"
#include "math/smoothing/lowess_fit.h"
#include "math/sample_scaling.h"
#include <vector>
#include <algorithm>
namespace aster {
TaskDescriptor LowessTask::descriptor()
{
    auto d = task_detail::seriesTransform("LOWESS detrend", taskCategory(TaskCategory::Trend));
    d.version = 4; // Fitting parameters and outputs retain their stored meanings.
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
    if (source->x.size() != source->y.size())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, "X and Y lengths differ"));
    const auto error = science_detail::orderedSeries(*source); if (!error.isEmpty()) return NodeResult::failure(error);
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    const auto count = static_cast<size_t>(source->x.size());
    const size_t neighbours = std::clamp(size_t(m_fraction * count), size_t(2), count);
    if (m_iterations > 0) {
        // Each fit scans a window once for every pass, including the initial ordinary fit.
        constexpr double maxRobustTerms = 1e9;
        const double work = double(count) * neighbours * (1 + m_iterations);
        if (work > maxRobustTerms)
            return NodeResult::failure(ErrorInfo{"LOWESS_WORK_LIMIT",
                {{"samples", double(count)}, {"neighbor_count", double(neighbours)}, {"iterations", m_iterations},
                 {"estimated_terms", work}, {"maximum", maxRobustTerms}},
                "Robust LOWESS exceeds the neighborhood work limit"});
    }
    std::vector<double> x(source->x.begin(), source->x.end()), y(source->y.begin(), source->y.end());
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (!std::isfinite(*bounds.first) || !std::isfinite(*bounds.second))
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, "LOWESS requires finite Y values"));
    const double largest = std::max(std::abs(*bounds.first), std::abs(*bounds.second));
    double yOrigin = 0;
    if ((*bounds.first > 0 || *bounds.second < 0) && std::min(std::abs(*bounds.first),std::abs(*bounds.second)) > largest / 2)
        yOrigin = numeric::sampleMidpoint(*bounds.first,*bounds.second);
    const double yScale = numeric::binarySampleScale(std::max(*bounds.second-yOrigin,yOrigin-*bounds.first));
    for (size_t i=0; i<y.size(); ++i) {
        if (i % 1024 == 0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
        const double delta = y[i]-yOrigin;
        y[i] = delta / yScale;
        if (!std::isfinite(y[i]) || (delta != 0 && y[i] == 0))
            return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation","lowess"},{"point",double(i+1)}},
                "LOWESS working amplitudes cannot represent this data range"));
    }
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
    int percentShown = -1;
    const auto keepGoing = [&](int pass, size_t i) {
        const int percent = int(100.0*(pass*x.size() + i)/((m_iterations+1)*x.size()));
        if (context.progress && percent != percentShown) {
            percentShown = percent;
            context.progress(percent, "progress.lowess_fit");
        }
        return !context.cancellation.isCancelled();
    };
    std::vector<double> fitted(x.size()), residual(x.size());
    const bool useMoments = m_iterations == 0 && neighbours >= 128 && double(x.size()) * neighbours > 2000000;
    if (useMoments) {
        if (!numeric::fitOrdinaryLowess(x, y, neighbours, fitted, [&](size_t i) { return keepGoing(0,i); }))
            return NodeResult::cancelled();
        for (size_t i = 0; i < x.size(); ++i) residual[i] = y[i] - fitted[i];
    } else {
        std::vector<double> robust(x.size());
        CppLowess::TemplatedLowess<std::vector<double>, double> algorithm;
        const int code = algorithm.lowess(x, y, m_fraction, m_iterations, 0.0, fitted, robust, residual, keepGoing);
        if (context.cancellation.isCancelled()) return NodeResult::cancelled();
        if (code != 0) return NodeResult::failure(makeError(ErrorCode::LowessFailure, {{"library_code", code}, {"fraction", m_fraction}, {"iterations", m_iterations}, {"samples", double(x.size())}}));
    }
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    SeriesData detrended = *source, trend = *source;
    // LOWESS residuals need not have zero mean (especially after robust reweighting).
    // y - fitted + mean(fitted) preserves mean(y); adding mean(y) does not.
    long double meanAdjustment = 0;
    if (m_retainMean)
        for (double value : fitted)
            meanAdjustment += static_cast<long double>(value) / fitted.size();
    for (qsizetype i = 0; i < source->y.size(); ++i) {
        if (i % 1024 == 0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
        trend.y[i] = std::fma(fitted[i], yScale, yOrigin);
        detrended.y[i] = m_retainMean
            ? static_cast<double>(std::fma(residual[i]+meanAdjustment, static_cast<long double>(yScale), static_cast<long double>(yOrigin)))
            : residual[i]*yScale;
        if (!std::isfinite(trend.y[i]) || !std::isfinite(detrended.y[i]))
            return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation","lowess"},{"point",double(i+1)}}));
    }
    return NodeResult::success({{"detrended", makeData(std::move(detrended))}, {"trend", makeData(std::move(trend))}},
        {{"fraction", m_fraction}, {"robust_iterations", m_iterations}, {"retain_mean", m_retainMean},
         {"mean_adjustment", m_retainMean ? double(std::fma(meanAdjustment, static_cast<long double>(yScale), static_cast<long double>(yOrigin))) : 0.},
         {"neighbor_count", double(neighbours)}, {"fit_evaluation",useMoments ? "local_moments" : "direct_weights"},
         {"variant", m_iterations > 0 ? "robust_lowess" : "ordinary_lowess"},
         {"reference_implementation", "Acycle detrending.m: LOWESS / rLOWESS; NETLIB local-linear library retained"}});
}
}
