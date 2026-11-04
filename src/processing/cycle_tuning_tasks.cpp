#include "processing/cycle_tuning_tasks.h"
#include "processing/chronology_helpers.h"
#include "processing/task_categories.h"
#include "math/astronomy/milankovitch_periods.h"
#include "math/chronology/cycle_age.h"
namespace aster
{
namespace
{
using namespace chronology_detail;
const QStringList components = {"obliquity", "p1",  "p2",   "p3",  "p4",
                                "e405",      "e95", "e124", "e99", "e131"};
QVector<ParameterSpec> targetParameters()
{
    auto age = task_detail::number("target_age_ma", "Geological age (Ma)", 300, 0, 4000);
    auto component =
        science_detail::text("target_component", "Target component", "obliquity", components);
    age.group = component.group = "target_period";
    age.icon = component.icon = "orbit";
    return {age, component};
}
int componentIndex(const QJsonObject &p)
{
    const int i = components.indexOf(p["target_component"].toString());
    if (i < 0)
        throw std::invalid_argument("Unknown astronomical target component");
    return i;
}
QJsonObject targetReport(const QJsonObject &p, const astronomy::PeriodRange &r)
{
    return {{"model", "Waltham_2015_smooth_and_fluctuating_drag_envelopes"},
            {"doi", "10.2110/jsr.2015.66"},
            {"geological_age_ma", p["target_age_ma"]},
            {"component", p["target_component"]},
            {"period_min_ka", r.lower},
            {"period_mid_ka", r.midpoint()},
            {"period_max_ka", r.upper},
            {"range_meaning", "model_envelope_not_95_percent_confidence_interval"},
            {"orbit_phase_available", false}};
}
TableData rangeTable(double age, const QString &prefix, const astronomy::PeriodRange &r)
{
    return {{"target_age_ma", prefix + "_min", prefix + "_mid", prefix + "_max"},
            {{age, r.lower, r.midpoint(), r.upper}}};
}
} // namespace
TaskDescriptor MilankovitchPeriodTask::descriptor()
{
    auto d = task_detail::seriesTransform("Milankovitch target period",
                                          taskCategory(TaskCategory::Targets));
    d.inputKind.reset();
    d.parameters = targetParameters();
    d.defaultOutput = "target_period";
    d.outputs = {{"target_period", "Target period envelope", DataKind::Table, true},
                 {"lunar_distance", "Earth-Moon distance", DataKind::Table, true},
                 {"earth_day", "Day length", DataKind::Table, true},
                 {"mean_obliquity", "Mean obliquity", DataKind::Table, true},
                 {"axial_precession", "Axial precession", DataKind::Table, true}};
    return d;
}
NodeResult MilankovitchPeriodTask::execute(const TaskInput &, const ExecutionContext &c)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    try
    {
        const double age = m_parameters["target_age_ma"].toDouble(qQNaN());
        const auto model = astronomy::milankovitchPeriods(age);
        const int index = componentIndex(m_parameters);
        const auto period = model.periodKa[std::size_t(index)];
        auto r = NodeResult::success(
            {{"target_period", makeData(rangeTable(age, components[index] + "_ka", period))},
             {"lunar_distance", makeData(rangeTable(age, "lunar_km", model.lunarDistanceKm))},
             {"earth_day", makeData(rangeTable(age, "day_hours", model.dayHours))},
             {"mean_obliquity",
              makeData(rangeTable(age, "obliquity_deg", model.meanObliquityDegrees))},
             {"axial_precession", makeData(rangeTable(age, "axial_ka", model.axialPrecessionKa))}},
            targetReport(m_parameters, period));
        r.warnings << "warning.target_envelope";
        if (index >= 5)
            r.warnings << "warning.target_eccentricity";
        return c.cancellation.isCancelled() ? NodeResult::cancelled() : r;
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(ErrorInfo{"TARGET_PERIOD", {}, QString::fromUtf8(e.what())});
    }
}
TaskDescriptor CycleAgeModelTask::descriptor()
{
    auto d = task_detail::seriesTransform("Cycle-count age model",
                                          taskCategory(TaskCategory::Chronology));
    d.outputs = {{"age_model", "Depth-age model", DataKind::Series, true},
                 {"selected_extrema", "Selected cycle markers", DataKind::Series, true},
                 {"cycle_anchors", "Cycle anchor table", DataKind::Table, true},
                 {"interval_rates", "Interval sedimentation rates", DataKind::Table, true}};
    d.defaultOutput = "age_model";
    d.parameters = units();
    auto source = science_detail::text("cycle_period_source", "Period source", "manual",
                                       {"manual", "model_midpoint"});
    auto period = task_detail::number("cycle_period", "Cycle duration", 41, 1e-9, 1e12);
    source.group = period.group = "cycle_duration";
    period.visibleWhen = {{"cycle_period_source", "manual"}};
    d.parameters << source << period;
    auto targets = targetParameters();
    for (auto &p : targets)
    {
        p.visibleWhen = {{"cycle_period_source", "model_midpoint"}};
        p.group = "cycle_duration";
    }
    d.parameters += targets;
    auto extremum =
        science_detail::text("cycle_extremum", "Cycle marker", "peak", {"peak", "trough"});
    auto relief = task_detail::number("cycle_min_relief", "Minimum local relief", 0, 0, 1e12);
    auto distance =
        task_detail::number("cycle_min_distance", "Minimum depth separation", 0, 0, 1e12);
    extremum.group = relief.group = distance.group = "cycle_markers";
    auto first = science_detail::text("cycle_first_age_mode", "First marker age", "one_period",
                                      {"one_period", "specified"});
    auto age = task_detail::number("cycle_first_age", "Specified first age", 0, -1e12, 1e12);
    age.visibleWhen = {{"cycle_first_age_mode", "specified"}};
    auto extend = science_detail::boolean("cycle_extend_ends", "Extend to input ends", false);
    first.group = age.group = extend.group = "cycle_anchoring";
    d.parameters << extremum << relief << distance << first << age << extend;
    return d;
}
NodeResult CycleAgeModelTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return checked(
        input, c,
        [&](const SeriesData &s)
        {
            const auto depth = m_parameters["chrono_depth_unit"].toString(),
                       age = m_parameters["chrono_age_unit"].toString();
            const auto mismatch = unitMismatch(s.xAxis.unit, depth);
            if (!mismatch.isEmpty())
                return NodeResult::failure(mismatch);
            chronology::CycleOptions o;
            QJsonObject target;
            const bool model = m_parameters["cycle_period_source"] == "model_midpoint";
            if (model)
            {
                const auto calculated =
                    astronomy::milankovitchPeriods(m_parameters["target_age_ma"].toDouble(qQNaN()));
                const auto range = calculated.periodKa[std::size_t(componentIndex(m_parameters))];
                target = targetReport(m_parameters, range);
                o.period = range.midpoint() * 1000 / years(age);
            }
            else
                o.period = m_parameters["cycle_period"].toDouble(qQNaN());
            o.firstAge = m_parameters["cycle_first_age_mode"] == "one_period"
                             ? o.period
                             : m_parameters["cycle_first_age"].toDouble(qQNaN());
            o.troughs = m_parameters["cycle_extremum"] == "trough";
            o.minimumRelief = m_parameters["cycle_min_relief"].toDouble();
            o.minimumDistance = m_parameters["cycle_min_distance"].toDouble();
            o.extendEnds = m_parameters["cycle_extend_ends"].toBool();
            if (c.progress)
                c.progress(5, "progress.cycle_markers");
            auto v = chronology::cycleAge(values(s.x), values(s.y), o,
                                          [&] { return c.cancellation.isCancelled(); });
            SeriesData ages{
                vector(v.modelDepth), vector(v.modelAge), {"Depth", depth}, {"Age", age}};
            SeriesData selected;
            selected.xAxis = {"Depth", depth};
            selected.yAxis = s.yAxis;
            TableData anchors{
                {"sample_index", "depth_" + depth, "marker_value", "age_" + age, "local_relief"},
                {}};
            for (std::size_t k = 0; k < v.indices.size(); ++k)
            {
                if (task_detail::cancelled(c, qsizetype(k)))
                    return NodeResult::cancelled();
                const auto i = qsizetype(v.indices[k]);
                selected.x << s.x[i];
                selected.y << s.y[i];
                anchors.rows.push_back(
                    {double(i + 1), s.x[i], s.y[i], v.anchorAge[k], v.relief[k]});
            }
            TableData intervals{{"depth_start_" + depth, "depth_end_" + depth, "age_start_" + age,
                                 "age_end_" + age, "rate_" + depth + "_" + age},
                                {}};
            for (qsizetype k = 0; k + 1 < ages.x.size(); ++k)
            {
                if (task_detail::cancelled(c, k))
                    return NodeResult::cancelled();
                intervals.rows.push_back({ages.x[k], ages.x[k + 1], ages.y[k], ages.y[k + 1],
                                          v.intervalRate[std::size_t(k)]});
            }
            auto result = NodeResult::success(
                {{"age_model", makeData(std::move(ages))},
                 {"selected_extrema", makeData(std::move(selected))},
                 {"cycle_anchors", makeData(std::move(anchors))},
                 {"interval_rates", makeData(std::move(intervals))}},
                {{"method", "same_phase_cycle_count"},
                 {"period", o.period},
                 {"age_unit", age},
                 {"depth_unit", depth},
                 {"age_reference", m_parameters["chrono_age_reference"]},
                 {"first_marker_age", o.firstAge},
                 {"extremum", o.troughs ? "trough" : "peak"},
                 {"candidates", double(v.candidateCount)},
                 {"selected_markers", double(v.indices.size())},
                 {"plateau_groups", double(v.plateauCount)},
                 {"plateau_rule", "single_middle_sample_left_for_even_plateau"},
                 {"relief_definition",
                  "height_minus_higher_of_adjacent_interpeak_valleys_not_global_prominence"},
                 {"minimum_relief", o.minimumRelief},
                 {"minimum_depth_separation", o.minimumDistance},
                 {"selection_priority", "local_relief_descending_then_original_index"},
                 {"end_extension", o.extendEnds ? "linear_endpoint_secant" : "none"},
                 {"extrapolated_samples", o.extendEnds ? 2 : 0},
                 {"period_source", model ? "model_midpoint" : "manual"},
                 {"target_model", target},
                 {"sorted", false},
                 {"resampled", false},
                 {"filtered", false}});
            result.warnings << "warning.cycle_count_assumption"
                            << "warning.chronology_deterministic";
            if (model)
                result.warnings << "warning.target_envelope" << "warning.cycle_model_midpoint";
            if (o.extendEnds)
                result.warnings << "warning.chronology_extrapolated";
            if (s.xAxis.unit.isEmpty())
                result.warnings << "warning.chronology_declared_units";
            if (c.progress)
                c.progress(100, "progress.cycle_markers");
            return result;
        });
}
} // namespace aster
