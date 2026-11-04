#include "processing/series_utility_tasks.h"
#include "processing/task_categories.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/series/series_operations.h"
#include <new>

namespace aster
{
namespace
{
ErrorInfo inputError(const TaskInput &input)
{
    if (!dataAs<SeriesData>(input.data))
        return makeError(ErrorCode::InputType, {{"expected", "Series"}});
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return makeError(ErrorCode::InputInvalid, {}, invalid);
    if (std::size_t(dataAs<SeriesData>(input.data)->x.size()) >
        numeric::SeriesOperationLimits::maxSamples)
        return {"SERIES_BUDGET",
                {{"input_samples", double(dataAs<SeriesData>(input.data)->x.size())},
                 {"maximum_samples", double(numeric::SeriesOperationLimits::maxSamples)}},
                "Input exceeds the sample budget"};
    return {};
}
template <class Operation>
NodeResult guarded(const TaskInput &input, const ExecutionContext &c, Operation operation)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    const auto error = inputError(input);
    if (!error.isEmpty())
        return NodeResult::failure(error);
    try
    {
        return operation(*dataAs<SeriesData>(input.data));
    }
    catch (const numeric::SeriesCancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const std::domain_error &e)
    {
        return NodeResult::failure(
            ErrorInfo{"STANDARDIZE_CONSTANT", {}, QString::fromUtf8(e.what())});
    }
    catch (const std::overflow_error &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::NumericRange, {}, QString::fromUtf8(e.what())));
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::ParameterInvalid, {}, QString::fromUtf8(e.what())));
    }
}
NodeResult amplitude(const TaskInput &i, const ExecutionContext &c, bool standardize, int ddof)
{
    return guarded(i, c,
                   [&](const SeriesData &source)
                   {
                       const auto r = numeric::centerSeries(
                           std::vector<double>(source.y.begin(), source.y.end()), standardize, ddof,
                           [&] { return c.cancellation.isCancelled(); });
                       auto result = source;
                       result.y = QVector<double>(r.values.begin(), r.values.end());
                       if (standardize)
                           result.yAxis.unit = "1";
                       return NodeResult::success(
                           {{"series", makeData(std::move(result))}},
                           {{"operation", standardize ? "z_score" : "demean"},
                            {"mean", r.mean},
                            {"scale", r.scale},
                            {"ddof", standardize ? ddof : 0},
                            {"input_rows", double(source.y.size())},
                            {"resampled", false}});
                   });
}
} // namespace
TaskDescriptor DemeanTask::descriptor()
{
    return task_detail::seriesTransform("Remove mean", taskCategory(TaskCategory::Amplitude));
}
TaskDescriptor StandardizeTask::descriptor()
{
    auto d = task_detail::seriesTransform("Standardize (Z-score)",
                                          taskCategory(TaskCategory::Amplitude));
    d.parameters = {science_detail::text("standardize_sd", "Standard deviation", "sample",
                                         {"sample", "population"})};
    d.parameters[0].group = "amplitude_transform";
    return d;
}
NodeResult DemeanTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return amplitude(i, c, false, 0);
}
NodeResult StandardizeTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return amplitude(i, c, true, m_parameters["standardize_sd"].toString() == "population" ? 0 : 1);
}
TaskDescriptor SelectIntervalTask::descriptor()
{
    auto d = task_detail::seriesTransform("Select or remove interval",
                                          taskCategory(TaskCategory::Selection));
    d.parameters = {
        science_detail::text("interval_action", "Operation", "keep", {"keep", "remove"}),
        task_detail::number("interval_lower", "Lower X bound", 0, -1e100, 1e100),
        task_detail::number("interval_upper", "Upper X bound", 1, -1e100, 1e100)};
    for (auto &p : d.parameters)
        p.group = "selection_interval";
    return d;
}
NodeResult SelectIntervalTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return guarded(
        input, c,
        [&](const SeriesData &source)
        {
            const double lower = m_parameters["interval_lower"].toDouble(qQNaN()),
                         upper = m_parameters["interval_upper"].toDouble(qQNaN());
            const bool keep = m_parameters["interval_action"].toString() == "keep";
            if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper)
                return NodeResult::failure(ErrorInfo{"SELECTION_INTERVAL",
                                                     {{"lower", lower}, {"upper", upper}},
                                                     "Require lower <= upper"});
            SeriesData output;
            output.xAxis = source.xAxis;
            output.yAxis = source.yAxis;
            for (qsizetype i = 0; i < source.x.size(); ++i)
            {
                if (task_detail::cancelled(c, i))
                    return NodeResult::cancelled();
                const bool inside = source.x[i] >= lower && source.x[i] <= upper;
                if (inside == keep)
                {
                    output.x.push_back(source.x[i]);
                    output.y.push_back(source.y[i]);
                }
            }
            if (output.x.isEmpty())
                return NodeResult::failure(
                    ErrorInfo{"SELECTION_EMPTY", {}, "No samples remain in the selected output"});
            const auto count = output.x.size();
            auto result = NodeResult::success({{"series", makeData(std::move(output))}},
                                              {{"operation", keep ? "keep" : "remove"},
                                               {"lower", lower},
                                               {"upper", upper},
                                               {"bounds", "inclusive"},
                                               {"input_rows", double(source.x.size())},
                                               {"output_rows", double(count)},
                                               {"removed_rows", double(source.x.size() - count)},
                                               {"x_unit", source.xAxis.unit},
                                               {"preserved_input_order", true},
                                               {"resampled", false}});
            if (!keep && count < source.x.size())
                result.warnings << "warning.selection_gap";
            return result;
        });
}
TaskDescriptor SamplingCheckTask::descriptor()
{
    auto d = task_detail::seriesTransform("Sampling and gap inspection",
                                          taskCategory(TaskCategory::Quality));
    d.outputs +=
        QVector<OutputSpec>{{"sampling_intervals", "Sampling intervals", DataKind::Table, true},
                            {"sampling_gaps", "Large sampling gaps", DataKind::Table, true},
                            {"sampling_summary", "Sampling summary", DataKind::Table, true}};
    d.parameters = {task_detail::number("gap_factor", "Gap / median interval", 3, 1.01, 1000000)};
    d.parameters[0].group = "sampling_quality";
    return d;
}
NodeResult SamplingCheckTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return guarded(
        input, c,
        [&](const SeriesData &source)
        {
            const auto r =
                numeric::inspectSampling(std::vector<double>(source.x.begin(), source.x.end()),
                                         m_parameters["gap_factor"].toDouble(),
                                         [&] { return c.cancellation.isCancelled(); });
            TableData intervals{{"right_point_1based", "left_x", "right_x", "spacing"}, {}};
            for (qsizetype i = 1;
                 i < std::min(qsizetype(numeric::SeriesOperationLimits::detailRows + 1),
                              source.x.size());
                 ++i)
                intervals.rows.push_back(
                    {double(i + 1), source.x[i - 1], source.x[i], source.x[i] - source.x[i - 1]});
            TableData gaps = intervals;
            gaps.rows.clear();
            for (const auto &gap : r.gaps)
            {
                const auto i = qsizetype(gap.rightIndex);
                gaps.rows.push_back({double(i + 1), source.x[i - 1], source.x[i], gap.spacing});
            }
            TableData summary{{"samples", "duplicate_x", "descending_pairs", "equal_pairs",
                               "strictly_increasing", "uniform", "large_gaps"},
                              {{double(source.x.size()), double(r.duplicates), double(r.descending),
                                double(r.equalAdjacent), r.strictlyIncreasing ? 1. : 0.,
                                r.uniform ? 1. : 0., double(r.gapCount)}}};
            auto result = NodeResult::success(
                {{"series", input.data},
                 {"sampling_intervals", makeData(std::move(intervals))},
                 {"sampling_gaps", makeData(std::move(gaps))},
                 {"sampling_summary", makeData(std::move(summary))}},
                {{"strictly_increasing", r.strictlyIncreasing},
                 {"uniform", r.uniform},
                 {"duplicate_x", double(r.duplicates)},
                 {"descending_pairs", double(r.descending)},
                 {"equal_pairs", double(r.equalAdjacent)},
                 {"large_gap_count", double(r.gapCount)},
                 {"positive_spacing_available", r.medianPositive > 0},
                 {"median_positive_spacing", r.medianPositive},
                 {"minimum_positive_spacing", r.minimumPositive},
                 {"maximum_positive_spacing", r.maximumPositive},
                 {"mean_spacing_available", r.strictlyIncreasing},
                 {"mean_spacing", r.expectedSpacing},
                 {"maximum_relative_deviation", r.relativeDeviation},
                 {"uniform_relative_tolerance",
                  numeric::SeriesOperationLimits::uniformRelativeTolerance},
                 {"gap_factor", m_parameters["gap_factor"]},
                 {"x_unit", source.xAxis.unit},
                 {"detail_limit", double(numeric::SeriesOperationLimits::detailRows)},
                 {"interval_details_truncated",
                  std::size_t(source.x.size()) > numeric::SeriesOperationLimits::detailRows + 1},
                 {"gap_details_truncated", r.gapCount > r.gaps.size()},
                 {"data_modified", false}});
            if (!r.strictlyIncreasing)
                result.warnings << "warning.sampling_unordered";
            else if (!r.uniform)
                result.warnings << "warning.sampling_nonuniform";
            if (r.gapCount)
                result.warnings << "warning.sampling_gap";
            return result;
        });
}
} // namespace aster
