#include "processing/task_categories.h"
#include "processing/preprocess_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "processing/services/series_cleanup.h"
#include <gsl/gsl_spline.h>
#include <gsl/gsl_errno.h>
#include <limits>
#include <memory>

namespace aster {
TaskDescriptor PreprocessTask::descriptor()
{
    auto d = task_detail::seriesTransform("Sort, deduplicate and resample", taskCategory(TaskCategory::Sampling));
    d.version = 4; // Acycle datapreproc defaults: median interval and linear interpolation.
    d.parameters = {science_detail::text("duplicate_policy", "Duplicate X", "average", {"average", "first", "last", "error"}),
        science_detail::text("spacing_mode", "Spacing", "median", {"none", "mean", "median", "min", "max", "fixed"}),
        task_detail::number("spacing", "Fixed interval", 1, 1e-12, 1e12),
        science_detail::text("interpolation", "Interpolation", "linear", {"linear", "cubic", "akima"})};
    for(auto& p:d.parameters) {
        p.group=p.key=="duplicate_policy" ? "cleanup" : "resampling";
        if(p.key=="spacing")p.visibleWhen={{"spacing_mode","fixed"}};
    }
    return d;
}
PreprocessTask::PreprocessTask(const QJsonObject& p) : m_duplicates(p["duplicate_policy"].toString()),
    m_mode(p["spacing_mode"].toString()), m_interpolation(p["interpolation"].toString()), m_spacing(p["spacing"].toDouble()) {}
NodeResult PreprocessTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    auto cleaned=cleanSeries(*source,m_duplicates,context.cancellation);
    if(cleaned.cancelled)return NodeResult::cancelled();
    if(!cleaned.error.isEmpty())return NodeResult::failure(cleaned.error);
    auto data=std::move(cleaned.data);
    const auto duplicates=cleaned.mergedRows;
    QJsonObject report{{"input_rows", static_cast<double>(source->x.size())}, {"merged_rows", static_cast<double>(duplicates)},
        {"reordered",cleaned.reordered}, {"duplicate_policy",m_duplicates}, {"spacing_mode",m_mode}, {"interpolation",m_interpolation},
        {"reference_implementation","Acycle datapreproc / acycleInterpolateSeries; explicit preprocessing only"}};
    if (m_mode != "none") {
        QVector<double> differences;
        for (qsizetype i = 1; i < data.x.size(); ++i) differences.push_back(data.x[i] - data.x[i-1]);
        double spacing = m_spacing;
        if (m_mode == "mean") spacing = (data.x.back() - data.x.front()) / (data.x.size() - 1);
        if (m_mode == "min") spacing = *std::min_element(differences.begin(), differences.end());
        if (m_mode == "max") spacing = *std::max_element(differences.begin(), differences.end());
        if (m_mode == "median") {
            std::sort(differences.begin(), differences.end());
            const auto n = differences.size(); spacing = n % 2 ? differences[n/2] : 0.5 * differences[n/2-1] + 0.5 * differences[n/2];
        }
        const double steps = (data.x.back() - data.x.front()) / spacing;
        if (!std::isfinite(steps) || steps < 1 || steps > 999999) return NodeResult::failure(makeError(ErrorCode::ResampleGrid, {{"spacing", spacing}, {"range", data.x.back() - data.x.front()}, {"estimated_intervals", steps}, {"minimum", 2}, {"maximum", 1000000}}));
        const auto ulp = [](double v) {
            v = std::abs(v);
            return std::nextafter(v, std::numeric_limits<double>::infinity())-v;
        };
        const auto nearest = std::round(steps);
        const bool endpointOnGrid = std::abs(steps-nearest) <= 8*ulp(std::max(1.0,steps));
        auto count = static_cast<qsizetype>(endpointOnGrid ? nearest : std::floor(steps)) + 1;
        if (count < 2) return NodeResult::failure(makeError(ErrorCode::ResampleGrid, {{"spacing", spacing}, {"range", data.x.back() - data.x.front()}, {"actual", double(count)}}));
        science_detail::initializeGsl();
        const gsl_interp_type* type = m_interpolation == "linear" ? gsl_interp_linear : m_interpolation == "akima" ? gsl_interp_akima : gsl_interp_cspline;
        if (data.x.size() < gsl_interp_type_min_size(type)) return NodeResult::failure(makeError(ErrorCode::InterpolationSamples, {{"actual", double(data.x.size())}, {"minimum", double(gsl_interp_type_min_size(type))}, {"interpolation", m_interpolation}}));
        std::unique_ptr<gsl_spline, decltype(&gsl_spline_free)> spline(gsl_spline_alloc(type, data.x.size()), gsl_spline_free);
        std::unique_ptr<gsl_interp_accel, decltype(&gsl_interp_accel_free)> accel(gsl_interp_accel_alloc(), gsl_interp_accel_free);
        if (!spline || !accel)
            return NodeResult::failure(makeError(ErrorCode::AllocationFailure, {{"operation", "interpolation"}}));
        const int setupCode = gsl_spline_init(spline.get(), data.x.data(), data.y.data(), data.x.size());
        if (setupCode != GSL_SUCCESS)
            return NodeResult::failure(makeError(ErrorCode::InterpolationFailure, {{"operation", "initialization"}, {"library_code", setupCode}, {"interpolation", m_interpolation}}, QString::fromUtf8(gsl_strerror(setupCode))));
        SeriesData output; output.xAxis = data.xAxis; output.yAxis = data.yAxis;
        output.x.reserve(count); output.y.reserve(count);
        for (qsizetype i = 0; i < count; ++i) {
            if (i % 1024 == 0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
            double x = data.x.front() + i * spacing; double y = 0;
            if (endpointOnGrid && i+1 == count) {
                const double tolerance = 16*std::max({ulp(data.x.front()),ulp(data.x.back()),
                    ulp(data.x.back()-data.x.front()),ulp(x),ulp(i*spacing)});
                if (std::abs(x-data.x.back()) > tolerance)
                    return NodeResult::failure(makeError(ErrorCode::ResampleResolution, {{"spacing",spacing},{"x",x}}));
                x = data.x.back();
            }
            // A valid interval/count does not guarantee a representable floating-point grid.
            // Reject collapsed adjacent coordinates instead of passing duplicate X downstream.
            if (!std::isfinite(x) || x < data.x.front() || x > data.x.back() ||
                (!output.x.isEmpty() && (!(x > output.x.back()) || std::abs((x-output.x.back())-spacing) > 1e-8*spacing)))
                return NodeResult::failure(makeError(ErrorCode::ResampleResolution,
                    {{"output_point", double(i + 1)}, {"x", x}, {"spacing", spacing},
                     {"x_before", output.x.isEmpty() ? data.x.front() : output.x.back()}}));
            const int evalCode = gsl_spline_eval_e(spline.get(), x, accel.get(), &y);
            if (evalCode != GSL_SUCCESS) return NodeResult::failure(makeError(ErrorCode::InterpolationFailure, {{"point", double(i + 1)}, {"x", x}, {"library_code", evalCode}, {"interpolation", m_interpolation}}, QString::fromUtf8(gsl_strerror(evalCode))));
            output.x.push_back(x); output.y.push_back(y);
        }
        data = std::move(output); report["sample_spacing"] = spacing;
    }
    report["output_rows"] = static_cast<double>(data.x.size());
    return NodeResult::success({{"series", makeData(std::move(data))}}, report);
}
}
