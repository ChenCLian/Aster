// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/coco_analysis_tasks.h"
#include "processing/coco_task.h"
#include "processing/coco_task_support.h"
#include "processing/chronology_helpers.h"
#include "math/chronology/coco_analysis.h"
#include <QJsonArray>
namespace aster
{
TaskDescriptor CocoMonteCarloTask::descriptor()
{
    auto d = CocoTask::descriptor();
    d.label = "COCO Monte Carlo test";
    d.defaultOutput = "scan_adjusted_p";
    d.outputs += {{"pointwise_p", "Pointwise p values", DataKind::Series, true},
                  {"scan_adjusted_p", "Scan-adjusted p values", DataKind::Series, true},
                  {"null_maxima", "Null scan maxima", DataKind::Series, true},
                  {"mc_summary", "Monte Carlo summary", DataKind::Table, true}};
    const chronology::CocoMonteCarloOptions defaults;
    auto model = science_detail::text(
        "coco_null", "Null model",
        defaults.nullModel == chronology::CocoMonteCarloOptions::Null::FixedAr1 ? "fixed_ar1"
                                                                             : "random_peaks",
        {"fixed_ar1", "random_peaks"});
    auto rho = task_detail::number("coco_rho", "Fixed AR1 coefficient", defaults.rho, -.999, .999);
    rho.visibleWhen = {{"coco_null", "fixed_ar1"}};
    auto peaks = task_detail::number(
        "coco_null_peaks", "Reference peak count", double(defaults.peakCount), 0,
        double(chronology::CocoAnalysisLimits::maxPeaks), ParameterKind::Integer);
    peaks.visibleWhen = {{"coco_null", "random_peaks"}};
    auto count = task_detail::number(
        "coco_simulations", "Simulations", double(defaults.simulations), 19,
        double(chronology::CocoAnalysisLimits::maxSimulations), ParameterKind::Integer);
    auto seed = task_detail::number("coco_seed", "Random seed", double(defaults.seed), 0,
                                    2147483647, ParameterKind::Integer);
    auto alpha = task_detail::number("coco_alpha", "Test level", defaults.alpha, .0001, .5);
    model.group = rho.group = peaks.group = count.group = seed.group = alpha.group =
        "coco_monte_carlo";
    d.parameters += {model, rho, peaks, count, seed, alpha};
    return d;
}
TaskDescriptor CocoEvolutionTask::descriptor()
{
    auto d = CocoTask::descriptor();
    d.label = "Evolutionary COCO";
    d.defaultOutput = "correlation_map";
    d.outputs = {{"correlation_map", "Depth–rate correlation map", DataKind::Grid, true},
                 {"best_rate", "Best candidate by window", DataKind::Series, true},
                 {"best_correlation", "Best correlation by window", DataKind::Series, true},
                 {"window_diagnostics", "Window diagnostics", DataKind::Table, true},
                 {"rate_diagnostics", "Rate eligibility", DataKind::Table, true},
                 {"target_periods", "Target periods and powers", DataKind::Table, true}};
    const chronology::CocoEvolutionOptions defaults;
    auto window =
        task_detail::number("ecoco_window", "Window samples", double(defaults.windowSamples), 16,
                            double(chronology::CocoLimits::maxSamples), ParameterKind::Integer);
    auto step =
        task_detail::number("ecoco_step", "Step samples", double(defaults.stepSamples), 1,
                            double(chronology::CocoLimits::maxSamples), ParameterKind::Integer);
    window.group = step.group = "moving_window";
    d.parameters += {window, step};
    return d;
}
NodeResult CocoMonteCarloTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return coco_task_detail::run(
        input, c, m_parameters, chronology::CocoLimits::maxSamples,
        [&](const SeriesData &s, double dx, const chronology::CocoOptions &o)
        {
            const auto &p = m_parameters;
            chronology::CocoMonteCarloOptions mc;
            const auto model = p["coco_null"].toString();
            if (model != "fixed_ar1" && model != "random_peaks")
                throw std::invalid_argument("Unknown COCO null model");
            mc.nullModel = model == "fixed_ar1"
                               ? chronology::CocoMonteCarloOptions::Null::FixedAr1
                               : chronology::CocoMonteCarloOptions::Null::RandomPeaks;
            mc.simulations = std::size_t(p["coco_simulations"].toInt());
            mc.rho = p["coco_rho"].toDouble(qQNaN());
            mc.alpha = p["coco_alpha"].toDouble(qQNaN());
            mc.peakCount = std::size_t(p["coco_null_peaks"].toInt());
            const double seed = p["coco_seed"].toDouble(qQNaN());
            if (!std::isfinite(seed) || seed < 0 || seed > 2147483647 || seed != std::floor(seed))
                throw std::invalid_argument("Invalid random seed");
            mc.seed = std::uint64_t(seed);
            const auto v = chronology::cocoMonteCarlo(
                chronology_detail::values(s.y), dx, o, mc,
                [&] { return c.cancellation.isCancelled(); },
                [&](int n)
                {
                    if (c.progress)
                        c.progress(n, "progress.coco_monte_carlo");
                });
            auto result = coco_task_detail::outputs(s, p, o, v.observed, dx);
            SeriesData point{chronology_detail::vector(v.rates),
                             chronology_detail::vector(v.pointwiseP),
                             {"Sedimentation rate", "cm/ka"},
                             {"Pointwise p", "1"}};
            auto adjusted = point;
            adjusted.y = chronology_detail::vector(v.maximum.adjustedP);
            adjusted.yAxis = {"Scan-adjusted p", "1"};
            SeriesData nulls;
            nulls.xAxis = {"Simulation", "1"};
            nulls.yAxis = {"Maximum positive correlation", "1"};
            for (std::size_t i = 0; i < v.maximum.nullMaxima.size(); ++i)
            {
                nulls.x << double(i + 1);
                nulls.y << v.maximum.nullMaxima[i];
            }
            TableData summary{
                {"mc_simulations", "mc_seed", "mc_model_code", "mc_rho", "mc_peak_count",
                 "mc_alpha", "mc_observed_maximum", "mc_global_p", "mc_critical_value",
                 "mc_p_resolution"},
                {{double(mc.simulations), double(mc.seed), model == "fixed_ar1" ? 0. : 1.,
                  model == "fixed_ar1" ? mc.rho : 0., double(v.peaks), mc.alpha,
                  v.maximum.observedMaximum, v.maximum.globalP, v.maximum.criticalValue,
                  1. / (mc.simulations + 1)}}};
            result.outputs.insert("pointwise_p", makeData(std::move(point)));
            result.outputs.insert("scan_adjusted_p", makeData(std::move(adjusted)));
            result.outputs.insert("null_maxima", makeData(std::move(nulls)));
            result.outputs.insert("mc_summary", makeData(std::move(summary)));
            auto &report = result.report;
            report["method"] = "COCO_fixed_target_monte_carlo_v1";
            report["confidence_scope"] =
                model == "fixed_ar1"
                    ? "one_fixed_rate_grid_under_independently_specified_stationary_Gaussian_AR1_"
                      "null"
                    : "random_peak_spectral_reference_not_general_time_series_significance";
            report["null_model"] = model;
            report["null_rho"] =
                model == "fixed_ar1" ? QJsonValue(mc.rho) : QJsonValue(QJsonValue::Null);
            report["rho_estimated"] = false;
            report["peak_count"] = double(v.peaks);
            report["peak_count_automatic"] = model == "random_peaks" && mc.peakCount == 0;
            report["random_peak_width_rayleigh"] = 2;
            report["simulations"] = double(mc.simulations);
            report["seed"] = double(mc.seed);
            report["generator"] = "mt19937_64_high53_uniform_fixed_BoxMuller_v1";
            report["alpha"] = mc.alpha;
            report["global_p"] = v.maximum.globalP;
            report["observed_maximum"] = v.maximum.observedMaximum;
            report["critical_value"] = v.maximum.criticalValue;
            report["p_resolution"] = 1. / (mc.simulations + 1);
            report["family_size"] = double(v.rates.size());
            report["evidence"] = "max(0,Pearson_r); constant_comparison_band=0";
            report["p_definition"] = "(1+count(null>=observed))/(B+1); conservative_near_ties";
            report["family"] = "all_geometrically_eligible_rates; identical_each_replicate";
            report["null_maximum_adjustment"] = true;
            report["upstream_pipeline_simulated"] = false;
            report["target_cache_cells"] = double(v.cacheCells);
            result.warnings.removeAll("warning.coco_descriptive");
            result.warnings << (model == "fixed_ar1" ? "warning.coco_fixed_null"
                                                     : "warning.coco_random_reference")
                            << "warning.coco_scan_test";
            return result;
        });
}
NodeResult CocoEvolutionTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return coco_task_detail::run(
        input, c, m_parameters, chronology::CocoAnalysisLimits::maxInputSamples,
        [&](const SeriesData &s, double dx, const chronology::CocoOptions &o)
        {
            const auto &p = m_parameters;
            chronology::CocoEvolutionOptions e;
            e.windowSamples = std::size_t(p["ecoco_window"].toInt());
            e.stepSamples = std::size_t(p["ecoco_step"].toInt());
            const auto v = chronology::cocoEvolution(
                chronology_detail::values(s.y), dx, o, e,
                [&] { return c.cancellation.isCancelled(); },
                [&](int n)
                {
                    if (c.progress)
                        c.progress(n, "progress.ecoco");
                });
            GridData grid;
            grid.y = chronology_detail::vector(v.rates);
            grid.values = chronology_detail::vector(v.correlation);
            TableData windows{{"window_start_depth", "window_end_depth", "window_center_depth",
                               "rate_cm_ka", "coco_best_correlation"},
                              {}};
            for (std::size_t w = 0; w < v.centers.size(); ++w)
            {
                const auto first = qsizetype(w * e.stepSamples),
                           last = first + qsizetype(e.windowSamples) - 1;
                const double center = .5 * s.x[first] + .5 * s.x[last];
                if (!std::isfinite(center) || (!grid.x.isEmpty() && center <= grid.x.back()))
                    throw std::overflow_error("Unrepresentable window centers");
                grid.x << center;
                windows.rows.push_back(
                    {s.x[first], s.x[last], center, v.bestRate[w], v.bestCorrelation[w]});
            }
            grid.columns = grid.x.size();
            grid.rows = grid.y.size();
            grid.xAxis = s.xAxis;
            if (grid.xAxis.unit.isEmpty())
                grid.xAxis.unit = p["chrono_depth_unit"].toString();
            grid.yAxis = {"Sedimentation rate", "cm/ka"};
            grid.valueAxis = {"Spectral correlation", "1"};
            SeriesData rates{grid.x, chronology_detail::vector(v.bestRate), grid.xAxis, grid.yAxis};
            SeriesData scores{grid.x, chronology_detail::vector(v.bestCorrelation), grid.xAxis,
                              grid.valueAxis};
            TableData diagnostics{{"rate_cm_ka", "coco_status", "coco_eligible",
                                   "coco_resolved_targets", "rayleigh_1_ka", "nyquist_1_ka",
                                   "coco_bins"},
                                  {}};
            std::size_t excluded = 0;
            for (const auto &row : v.rateGrid)
            {
                const bool valid = row.status == chronology::CocoStatus::Eligible;
                if (!valid)
                    ++excluded;
                diagnostics.rows.push_back({row.rate, double(int(row.status)), valid ? 1. : 0.,
                                            double(row.resolvedTargets), row.rayleigh, row.nyquist,
                                            double(row.bins)});
            }
            TableData periods{
                {"target_period_ka", "target_frequency_1_ka", "target_relative_power"}, {}};
            for (std::size_t i = 0; i < o.periodsKa.size(); ++i)
                periods.rows.push_back({o.periodsKa[i], 1 / o.periodsKa[i], o.relativePower[i]});
            QJsonObject report{
                {"method", "eCOCO_fixed_target_phase_average_v1"},
                {"reference_doi", "10.1016/j.epsl.2018.08.041"},
                {"numeric_backend", science_detail::numericBackendReport()},
                {"parameters", p},
                {"samples", double(s.y.size())},
                {"window_samples", double(e.windowSamples)},
                {"step_samples", double(e.stepSamples)},
                {"windows", double(v.centers.size())},
                {"depth_step_metres", dx},
                {"window_span_metres", (e.windowSamples - 1) * dx},
                {"fourier_duration_metres", e.windowSamples * dx},
                {"nfft", double(v.fftSize)},
                {"unused_tail_samples", double(v.unusedTail)},
                {"eligible_rates", double(v.rates.size())},
                {"excluded_rates", double(excluded)},
                {"target_cache_cells", double(v.cacheCells)},
                {"target_plan_builds", 1},
                {"grid_layout", "row_rate_column_window_center"},
                {"boundary", "full_windows_only_no_edge_padding"},
                {"confidence_scope", "descriptive_map_no_pointwise_or_global_significance"},
                {"target_definition", "sum_relative_power_times_half_sin_cos_periodograms"},
                {"best_rate_semantics",
                 "independent_raw_correlation_argmax_each_window; no_path_smoothing_or_age_model"},
                {"constant_window_policy",
                 "fail_with_window_index; no_zero_fill_or_silent_omission"},
                {"target_amplitude_adjustment", false},
                {"red_noise_removal", false},
                {"resampled", false},
                {"sorted", false},
                {"age_model_created", false}};
            auto result =
                NodeResult::success({{"correlation_map", makeData(std::move(grid))},
                                     {"best_rate", makeData(std::move(rates))},
                                     {"best_correlation", makeData(std::move(scores))},
                                     {"window_diagnostics", makeData(std::move(windows))},
                                     {"rate_diagnostics", makeData(std::move(diagnostics))},
                                     {"target_periods", makeData(std::move(periods))}},
                                    report);
            result.warnings << "warning.ecoco_descriptive" << "warning.coco_target";
            if (excluded)
                result.warnings << "warning.coco_exclusions";
            if (s.xAxis.unit.isEmpty())
                result.warnings << "warning.chronology_declared_units";
            if (p["coco_target"] == "waltham_midpoint")
                result.warnings << "warning.target_envelope";
            if (v.unusedTail)
                result.warnings << "warning.ecoco_tail";
            return result;
        });
}
} // namespace aster
