// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/coco_task.h"
#include "processing/coco_task_support.h"
#include "processing/chronology_helpers.h"
#include "processing/task_categories.h"
#include "math/chronology/coco_scan.h"
#include "math/chronology/coco_analysis.h"
#include "math/astronomy/milankovitch_periods.h"
#include <QJsonArray>
#include <QRegularExpression>
namespace aster
{
namespace
{
std::vector<double> numbers(const QString &text)
{
    if (text.size() > 2048)
        throw std::length_error("Target list is too long");
    auto fields = text.trimmed().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    std::vector<double> values;
    for (const auto &field : fields)
    {
        bool ok = false;
        const double v = field.toDouble(&ok);
        if (!ok || !std::isfinite(v))
            throw std::invalid_argument("Invalid numeric target list");
        values.push_back(v);
    }
    return values;
}
QJsonArray array(const std::vector<double> &v)
{
    QJsonArray a;
    for (auto x : v)
        a.append(x);
    return a;
}
} // namespace
TaskDescriptor CocoTask::descriptor()
{
    const chronology::CocoOptions defaults;
    const auto list = [](const std::vector<double> &values)
    {
        QStringList parts;
        for (double value : values)
            parts << QString::number(value, 'g', 12);
        return parts.join(", ");
    };
    auto d = task_detail::seriesTransform("COCO fixed-target scan",
                                          taskCategory(TaskCategory::OrbitalMatching));
    d.defaultOutput = "correlation";
    d.outputs = {{"correlation", "Spectral correlation", DataKind::Series, true},
                 {"scan_diagnostics", "Rate grid and eligibility", DataKind::Table, true},
                 {"best_observed", "Observed spectrum at best rate", DataKind::Series, true},
                 {"best_target", "Target spectrum at best rate", DataKind::Series, true},
                 {"target_periods", "Target periods and powers", DataKind::Table, true}};
    auto depth = chronology_detail::units().front();
    auto mode = science_detail::text("coco_target", "Target periods", "manual",
                                     {"manual", "waltham_midpoint"});
    auto periods = science_detail::text("coco_periods", "Periods (ka)", list(defaults.periodsKa));
    periods.visibleWhen = {{"coco_target", "manual"}};
    auto age = task_detail::number("target_age_ma", "Geological age (Ma)", 300, 0, 4000);
    age.visibleWhen = {{"coco_target", "waltham_midpoint"}};
    auto weights =
        science_detail::text("coco_powers", "Relative powers", list(defaults.relativePower));
    mode.group = periods.group = age.group = weights.group = "coco_target";
    auto minimum = task_detail::number("coco_rate_min", "Minimum rate (cm/ka)",
                                       defaults.minimumRate, 1e-9, 1e9);
    auto maximum = task_detail::number("coco_rate_max", "Maximum rate (cm/ka)",
                                       defaults.maximumRate, 1e-9, 1e9);
    auto step =
        task_detail::number("coco_rate_step", "Rate step (cm/ka)", defaults.rateStep, 1e-9, 1e9);
    minimum.group = maximum.group = step.group = "coco_scan";
    auto frequency = task_detail::number("coco_max_frequency", "Maximum frequency (1/ka)",
                                         defaults.maximumFrequency, 1e-9, 1e6);
    auto padding = task_detail::number("coco_padding", "FFT padding factor", defaults.paddingFactor,
                                       1, 4, ParameterKind::Integer);
    auto detrend = science_detail::text(
        "coco_detrend", "Detrending",
        defaults.detrend == chronology::CocoOptions::Detrend::Mean     ? "mean"
        : defaults.detrend == chronology::CocoOptions::Detrend::Linear ? "linear"
                                                                       : "none",
        {"none", "mean", "linear"});
    frequency.group = padding.group = detrend.group = "coco_spectrum";
    d.parameters = {depth,   mode, periods,   age,     weights, minimum,
                    maximum, step, frequency, padding, detrend};
    return d;
}
namespace coco_task_detail
{
chronology::CocoOptions options(const QJsonObject &p)
{
    if (!QStringList{"manual", "waltham_midpoint"}.contains(p["coco_target"].toString()) ||
        !QStringList{"none", "mean", "linear"}.contains(p["coco_detrend"].toString()))
        throw std::invalid_argument("Unknown COCO target or detrending mode");
    chronology::CocoOptions o;
    const bool model = p["coco_target"] == "waltham_midpoint";
    if (model)
    {
        const auto m = astronomy::milankovitchPeriods(p["target_age_ma"].toDouble(qQNaN()));
        o.periodsKa.clear();
        // Three eccentricity, obliquity, and three precession components.
        for (auto i : {5, 7, 6, 0, 1, 2, 4})
            o.periodsKa.push_back(m.periodKa[std::size_t(i)].midpoint());
    }
    else
        o.periodsKa = numbers(p["coco_periods"].toString());
    o.relativePower = numbers(p["coco_powers"].toString());
    o.minimumRate = p["coco_rate_min"].toDouble(qQNaN());
    o.maximumRate = p["coco_rate_max"].toDouble(qQNaN());
    o.rateStep = p["coco_rate_step"].toDouble(qQNaN());
    o.maximumFrequency = p["coco_max_frequency"].toDouble(qQNaN());
    o.paddingFactor = p["coco_padding"].toInt();
    const auto trend = p["coco_detrend"].toString();
    o.detrend = trend == "mean"     ? chronology::CocoOptions::Detrend::Mean
                : trend == "linear" ? chronology::CocoOptions::Detrend::Linear
                                    : chronology::CocoOptions::Detrend::None;
    return o;
}
NodeResult outputs(const SeriesData &s, const QJsonObject &p, const chronology::CocoOptions &o,
                   const chronology::CocoResult &v, double dxMetres)
{
    const auto unit = p["chrono_depth_unit"].toString();
    const auto trend = p["coco_detrend"].toString();
    const bool model = p["coco_target"] == "waltham_midpoint";
    SeriesData score;
    score.xAxis = {"Sedimentation rate", "cm/ka"};
    score.yAxis = {"Spectral correlation", "1"};
    TableData diagnostics{{"rate_cm_ka", "coco_status", "coco_eligible", "coco_correlation",
                           "coco_resolved_targets", "rayleigh_1_ka", "nyquist_1_ka", "coco_bins"},
                          {}};
    std::size_t excluded = 0;
    for (const auto &row : v.rows)
    {
        const bool valid = row.status == chronology::CocoStatus::Eligible;
        if (valid)
        {
            score.x << row.rate;
            score.y << row.correlation;
        }
        else
            ++excluded;
        // A numeric table has no nullable cells: eligibility MUST be read.
        // Zero for an excluded correlation is a placeholder, never a result.
        diagnostics.rows.push_back({row.rate, double(int(row.status)), valid ? 1. : 0.,
                                    valid ? row.correlation : 0., double(row.resolvedTargets),
                                    row.rayleigh, row.nyquist, double(row.bins)});
    }
    SeriesData observed{chronology_detail::vector(v.frequency),
                        chronology_detail::vector(v.observedPower),
                        {"Frequency", "1/ka"},
                        {"Relative power", "1"}};
    auto target = observed;
    target.y = chronology_detail::vector(v.targetPower);
    TableData periods{{"target_period_ka", "target_frequency_1_ka", "target_relative_power"}, {}};
    for (std::size_t i = 0; i < o.periodsKa.size(); ++i)
        periods.rows.push_back({o.periodsKa[i], 1 / o.periodsKa[i], o.relativePower[i]});
    const auto &best = v.rows[v.bestRow];
    auto result = NodeResult::success(
        {{"correlation", makeData(std::move(score))},
         {"scan_diagnostics", makeData(std::move(diagnostics))},
         {"best_observed", makeData(std::move(observed))},
         {"best_target", makeData(std::move(target))},
         {"target_periods", makeData(std::move(periods))}},
        {{"method", "COCO_fixed_target_phase_average_v1"},
         {"reference_doi", "10.1016/j.epsl.2018.08.041"},
         {"numeric_backend", science_detail::numericBackendReport()},
         {"target_definition", "sum_relative_power_times_half_sin_cos_periodograms"},
         {"target_source",
          model ? "Waltham_2015_midpoints_e405_e124_e95_obl_p1_p2_p4" : "user_periods"},
         {"periods_ka", array(o.periodsKa)},
         {"relative_powers", array(o.relativePower)},
         {"geological_age_ma", model ? p["target_age_ma"] : QJsonValue(QJsonValue::Null)},
         {"depth_unit", unit},
         {"rate_unit", "cm/ka"},
         {"depth_step_metres", dxMetres},
         {"samples", double(s.y.size())},
         {"nfft", double(v.fftSize)},
         {"padding_factor", o.paddingFactor},
         {"detrend", trend},
         {"window", "rectangular"},
         {"frequency_band_1_ka", QJsonArray{0, o.maximumFrequency}},
         {"dc_excluded", true},
         {"target_amplitude_adjustment", false},
         {"red_noise_removal", false},
         {"minimum_rate", o.minimumRate},
         {"requested_maximum_rate", o.maximumRate},
         {"actual_last_rate", v.rows.back().rate},
         {"rate_step", o.rateStep},
         {"eligible_rates", double(v.rows.size() - excluded)},
         {"excluded_rates", double(excluded)},
         {"best_rate_cm_ka", best.rate},
         {"best_correlation", best.correlation},
         {"eligibility",
          "full_band_below_Nyquist_all_targets_above_Rayleigh_at_least_8_nonDC_bins"},
         {"status_codes", QJsonObject{{"0", "eligible"},
                                      {"1", "Nyquist_band"},
                                      {"2", "resolution_or_bins"},
                                      {"3", "constant_spectrum"}}},
         {"excluded_correlation_placeholder", 0},
         {"confidence_scope", "descriptive_no_p_value_no_rate_confidence_interval"},
         {"spectra_display_normalization", "each_maximum_to_one_at_best_eligible_rate"},
         {"sorted", false},
         {"resampled", false},
         {"age_model_created", false}});
    result.warnings << "warning.coco_descriptive" << "warning.coco_target";
    if (excluded)
        result.warnings << "warning.coco_exclusions";
    if (model)
        result.warnings << "warning.target_envelope";
    if (s.xAxis.unit.isEmpty())
        result.warnings << "warning.chronology_declared_units";
    return result;
}
NodeResult run(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p,
               std::size_t maxSamples, const Operation &operation)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    const auto *s = dataAs<SeriesData>(input.data);
    if (!s)
        return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    if (std::size_t(s->x.size()) > maxSamples || std::size_t(s->y.size()) > maxSamples)
        return NodeResult::failure(
            ErrorInfo{"COCO_BUDGET",
                      {{"maximum_samples", double(maxSamples)}},
                      "Input exceeds COCO sample limit; no full input copy was made"});
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    double dx = 0;
    const auto spacing = science_detail::uniformSpacing(*s, dx);
    if (!spacing.isEmpty())
        return NodeResult::failure(spacing);
    const auto unit = p["chrono_depth_unit"].toString();
    const auto mismatch = chronology_detail::unitMismatch(s->xAxis.unit, unit);
    if (!mismatch.isEmpty())
        return NodeResult::failure(mismatch);
    try
    {
        const auto o = options(p);
        auto result = operation(*s, dx * chronology_detail::metres(unit), o);
        return c.cancellation.isCancelled() ? NodeResult::cancelled() : result;
    }
    catch (const chronology::CocoCancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const chronology::CocoWorkBudgetError &e)
    {
        return NodeResult::failure(ErrorInfo{
            e.kind == chronology::CocoWorkBudgetError::Kind::Simulations
                ? "COCO_MC_WORK_BUDGET" : "COCO_WINDOW_WORK_BUDGET",
            {{"estimated_work", double(std::ceil(e.estimatedWork))},
             {"maximum_work", double(chronology::CocoAnalysisLimits::maxEvaluationWork)},
             {"requested_iterations", double(e.requestedIterations)},
             {"maximum_iterations", double(e.maximumIterations)}},
            QString::fromUtf8(e.what())});
    }
    catch (const std::length_error &e)
    {
        return NodeResult::failure(ErrorInfo{"COCO_BUDGET", {}, QString::fromUtf8(e.what())});
    }
    catch (const std::domain_error &e)
    {
        return NodeResult::failure(ErrorInfo{"COCO_NO_SCORE", {}, QString::fromUtf8(e.what())});
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(ErrorInfo{"COCO_PARAMETERS", {}, QString::fromUtf8(e.what())});
    }
}
} // namespace coco_task_detail
NodeResult CocoTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return coco_task_detail::run(
        input, c, m_parameters, chronology::CocoLimits::maxSamples,
        [&](const SeriesData &s, double dx, const chronology::CocoOptions &o)
        {
            auto v = chronology::cocoScan(
                chronology_detail::values(s.y), dx, o, [&] { return c.cancellation.isCancelled(); },
                [&](int p)
                {
                    if (c.progress)
                        c.progress(p, "progress.coco_scan");
                });
            return coco_task_detail::outputs(s, m_parameters, o, v, dx);
        });
}
} // namespace aster
