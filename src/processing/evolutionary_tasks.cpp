// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/evolutionary_tasks.h"
#include "processing/task_helpers.h"
#include "processing/task_categories.h"
#include "processing/science_helpers.h"
#include "math/spectrum/evolutionary_spectrum.h"
#include <new>

namespace aster
{
namespace
{
TaskDescriptor evolutionDescriptor(bool mtm)
{
    auto d = task_detail::seriesTransform(mtm ? "Evolutionary MTM" : "Evolutionary FFT",
                                          taskCategory(TaskCategory::Evolution));
    d.outputs = {{"power_map", "Power map", DataKind::Grid, true},
                 {"mean_spectrum", "Mean window spectrum", DataKind::Spectrum, true}};
    d.defaultOutput = "power_map";
    d.parameters = {
        science_detail::text("evo_window_mode", "Window definition", mtm ? "samples" : "span_percent", {"samples", "span_percent"}),
        task_detail::number("evo_window_percent", "Window / input span (%)", 20, .01, 100),
        task_detail::number("evo_window", "Window samples", 128, 8,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        science_detail::text("evo_step_mode", "Step definition", mtm ? "samples" : "target_windows", {"samples", "overlap", "target_windows"}),
        task_detail::number("evo_target_windows", "Target window count", 500, 2,
                            double(numeric::EvolutionLimits::maxWindows), ParameterKind::Integer),
        task_detail::number("evo_overlap_percent", "Window overlap (%)", 99.2, 0, 99.99),
        task_detail::number("evo_step", "Step samples", 16, 1,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        science_detail::text("evo_fft_mode", "FFT length definition", mtm ? "length" : "multiple", {"length", "multiple"}),
        task_detail::number("evo_padding", "FFT length / window samples", 4, 1, 32, ParameterKind::Integer),
        task_detail::number("evo_nfft", "FFT samples (0 = window)", 0, 0,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        science_detail::text("evo_trend", "Local detrending", mtm ? "mean" : "linear", {"mean", "linear"}),
        task_detail::number("frequency_max", "Maximum frequency (0 = automatic)", 0, 0, 1e100),
        task_detail::number("evo_frequency_ratio", "Legacy maximum / Nyquist", 1, .001, 1),
        science_detail::boolean("evo_normalize", "Normalize each window by its maximum power", !mtm),
        science_detail::boolean("evo_amplitude", "Default map: amplitude (square root of power)", !mtm)};
    if (mtm)
        d.parameters.push_back(task_detail::number("evo_nw", "Time-bandwidth NW", 2, 1, 32));
    else
        d.parameters.push_back(
            science_detail::text("evo_taper", "FFT window", "rectangular", {"hann", "rectangular"}));
    for (auto &p : d.parameters)
    {
        p.hideWhenDefault = p.key == "evo_frequency_ratio";
        p.group =
            (p.key.startsWith("evo_window") || p.key.startsWith("evo_step") || p.key == "evo_overlap_percent" || p.key == "evo_target_windows")
                ? "moving_window" : "spectral_estimator";
        if (p.key == "evo_window") p.visibleWhen = {{"evo_window_mode", "samples"}};
        if (p.key == "evo_window_percent") p.visibleWhen = {{"evo_window_mode", "span_percent"}};
        if (p.key == "evo_target_windows") p.visibleWhen = {{"evo_step_mode", "target_windows"}};
        if (p.key == "evo_step") p.visibleWhen = {{"evo_step_mode", "samples"}};
        if (p.key == "evo_overlap_percent") p.visibleWhen = {{"evo_step_mode", "overlap"}};
        if (p.key == "evo_nfft") p.visibleWhen = {{"evo_fft_mode", "length"}};
        if (p.key == "evo_padding") p.visibleWhen = {{"evo_fft_mode", "multiple"}};
    }
    if (!mtm)
        d.prepareParameters = [](const QJsonObject &supplied) {
            auto p = supplied;
            // Older snapshots/API calls can contain explicit sample counts but no
            // mode keys. Preserve these fixed settings instead of ignoring them
            // under the new data-relative defaults. Explicit modes always win.
            if (!p.contains("evo_window_mode") && p.contains("evo_window")) p["evo_window_mode"] = "samples";
            if (!p.contains("evo_step_mode") && p.contains("evo_step")) p["evo_step_mode"] = "samples";
            if (!p.contains("evo_fft_mode") && p.contains("evo_nfft")) p["evo_fft_mode"] = "length";
            return p;
        };
    return d;
}
NodeResult run(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p, bool mtm)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    const auto *s = dataAs<SeriesData>(input.data);
    if (!s)
        return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    double dt = 0;
    const auto spacingError = science_detail::uniformSpacing(*s, dt);
    if (!spacingError.isEmpty())
        return NodeResult::failure(spacingError);
    numeric::EvolutionOptions o;
    o.method = mtm ? numeric::EvolutionMethod::Mtm : numeric::EvolutionMethod::Fft;
    o.windowSamples = p["evo_window"].toInt();
    o.stepSamples = p["evo_step"].toInt();
    o.fftLength = p["evo_nfft"].toInt();
    // Span mode uses the observed endpoint span, as in LAH window/dt.
    // Do not silently enlarge a short window: the numeric validator reports it.
    if (p["evo_window_mode"].toString() == "span_percent")
        o.windowSamples = std::size_t(std::llround((s->x.size() - 1) * p["evo_window_percent"].toDouble(20) / 100));
    if (p["evo_step_mode"].toString() == "overlap")
        o.stepSamples = std::size_t(std::max(1LL, std::llround(o.windowSamples * (1 - p["evo_overlap_percent"].toDouble(99.2) / 100))));
    if (p["evo_step_mode"].toString() == "target_windows")
    {
        // Integer ceiling keeps the full-window count at or below the target.
        // Oversized windows are left for the numeric validator to reject.
        const auto count = std::size_t(s->x.size());
        const auto available = count > o.windowSamples ? count - o.windowSamples : 0;
        const auto intervals = std::size_t(std::max(2, p["evo_target_windows"].toInt(500)) - 1);
        o.stepSamples = std::max(std::size_t(1), available / intervals + (available % intervals != 0));
    }
    if (p["evo_fft_mode"].toString() == "multiple")
        o.fftLength = o.windowSamples * std::size_t(p["evo_padding"].toInt(4));
    o.maximumFrequencyRatio = p["evo_frequency_ratio"].toDouble();
    o.maximumFrequency = p["frequency_max"].toDouble();
    o.normalizeWindows = p["evo_normalize"].toBool();
    o.nw = mtm ? p["evo_nw"].toDouble() : 2;
    o.trend = p["evo_trend"].toString() == "linear" ? numeric::SegmentTrend::Linear
                                                    : numeric::SegmentTrend::Mean;
    o.window = p["evo_taper"].toString() == "rectangular" ? numeric::FftWindow::Rectangular
                                                          : numeric::FftWindow::Hann;
    const QJsonObject details{{"input_samples", double(s->x.size())},
                              {"window_samples", double(o.windowSamples)},
                              {"step_samples", double(o.stepSamples)},
                              {"nfft", double(o.fftLength)},
                              {"spacing", dt},
                              {"nw", o.nw}};
    try
    {
        if (p["evo_step_mode"].toString() == "target_windows" &&
            o.windowSamples >= 8 && o.windowSamples <= std::size_t(s->x.size()) &&
            o.stepSamples > o.windowSamples)
        {
            auto context = details;
            const auto available = std::size_t(s->x.size()) - o.windowSamples;
            context["target_windows"] = p["evo_target_windows"].toInt(500);
            context["minimum_target_windows"] = double(1 + available / o.windowSamples +
                                                       (available % o.windowSamples != 0));
            return NodeResult::failure(ErrorInfo{"EVOLUTION_TARGET_WINDOWS", context,
                "Target count would require gaps between windows"});
        }
        if (std::size_t(s->y.size()) > numeric::EvolutionLimits::maxInputSamples)
            throw std::length_error("Input exceeds the evolutionary spectrum sample budget");
        auto r = numeric::evolutionarySpectrum(
            std::vector<double>(s->y.begin(), s->y.end()), dt, o,
            [&] { return c.cancellation.isCancelled(); },
            [&](int n)
            {
                if (c.progress)
                    c.progress(n, "progress.evolutionary_spectrum");
            });
        GridData grid;
        grid.x.reserve(qsizetype(r.centers.size()));
        // Use actual observed endpoints, avoiding cancellation of large origins.
        for (std::size_t w = 0; w < r.centers.size(); ++w)
        {
            const auto first = qsizetype(w * o.stepSamples);
            const double center =
                .5 * s->x[first] + .5 * s->x[first + qsizetype(o.windowSamples) - 1];
            if (!std::isfinite(center) || (!grid.x.isEmpty() && center <= grid.x.back()))
                throw std::overflow_error(
                    "Window centers are not representable as increasing doubles");
            grid.x.push_back(center);
        }
        grid.y = QVector<double>(r.frequency.begin(), r.frequency.end());
        grid.values = QVector<double>(r.power.begin(), r.power.end());
        grid.columns = grid.x.size();
        grid.rows = grid.y.size();
        grid.xAxis = s->xAxis;
        grid.yAxis = {"Frequency", s->xAxis.unit.isEmpty() ? QString() : "1/" + s->xAxis.unit};
        grid.valueAxis = {"Power spectral density",
                          s->yAxis.unit.isEmpty() || s->xAxis.unit.isEmpty()
                              ? QString()
                              : "(" + s->yAxis.unit + ")^2 * (" + s->xAxis.unit + ")"};
        if (o.normalizeWindows) grid.valueAxis = {"Relative power", "1"};
        SpectrumData mean{grid.y, QVector<double>(r.meanPower.begin(), r.meanPower.end()),
                          grid.yAxis, grid.valueAxis};
        auto report = details;
        report["nfft"] = double(r.fftLength);
        report["windows"] = double(r.centers.size());
        report["window_span"] = (o.windowSamples - 1) * dt;
        report["fourier_duration"] = o.windowSamples * dt;
        report["window_step"] = o.stepSamples * dt;
        report["rayleigh_frequency"] = 1 / (o.windowSamples * dt);
        report["unused_tail_samples"] = double(r.unusedTail);
        report["local_detrending"] = p["evo_trend"];
        report["estimator"] = mtm ? "adaptive_MTM_symmetric_DPSS" : "one_sided_FFT_periodogram";
        report["taper"] = mtm ? "DPSS" : p["evo_taper"].toString();
        report["grid_layout"] = "row_frequency_column_window_center";
        report["boundary"] = "full_windows_only_no_edge_padding";
        report["normalization"] = o.normalizeWindows ? "per_window_full_spectrum_maximum"
                                                       : "physical_PSD_no_column_normalization";
        report["zero_power_windows"] = double(r.zeroPowerWindows);
        report["nyquist_frequency"] = .5 / dt;
        report["requested_frequency_max"] = o.maximumFrequency;
        report["output_frequency_max"] = r.frequency.back();
        report["frequency_limit_mode"] = o.maximumFrequency > 0 ? "absolute"
                                            : o.maximumFrequencyRatio < 1 ? "legacy_ratio" : "nyquist";
        report["frequency_step"] = 1 / (r.fftLength * dt);
        report["confidence_scope"] = "descriptive_map_no_significance_test";
        report["mean_spectrum_semantics"] = o.normalizeWindows
            ? "arithmetic_mean_of_window_relative_power_not_physical_PSD"
            : "arithmetic_mean_of_window_PSDs_not_full_record_estimate";
        report["adaptive_unconverged_windows"] = r.unconvergedWindows;
        report["resampled"] = false;
        report["window_mode"] = p["evo_window_mode"].toString("samples");
        report["step_mode"] = p["evo_step_mode"].toString("samples");
        if (p["evo_step_mode"].toString() == "target_windows")
            report["target_windows"] = p["evo_target_windows"].toInt(500);
        report["fft_mode"] = p["evo_fft_mode"].toString("length");
        report["actual_overlap_percent"] = 100 * (1 - double(o.stepSamples) / o.windowSamples);
        report["map_display_amplitude"] = p["evo_amplitude"].toBool();
        if (mtm)
            report["taper_plan_builds"] = 1;
        auto result = NodeResult::success({{"power_map", makeData(std::move(grid))},
                                           {"mean_spectrum", makeData(std::move(mean))}},
                                          report);
        result.warnings << "warning.evolution_descriptive";
        if (r.unconvergedWindows)
            result.warnings << "warning.evolution_unconverged";
        return result;
    }
    catch (const numeric::EvolutionCancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const std::length_error &e)
    {
        return NodeResult::failure(
            ErrorInfo{"EVOLUTION_BUDGET", details, QString::fromUtf8(e.what())});
    }
    catch (const std::invalid_argument &e)
    {
        return NodeResult::failure(
            ErrorInfo{"EVOLUTION_PARAMETERS", details, QString::fromUtf8(e.what())});
    }
    catch (const std::overflow_error &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::NumericRange, details, QString::fromUtf8(e.what())));
    }
    catch (const std::exception &e)
    {
        return c.cancellation.isCancelled()
                   ? NodeResult::cancelled()
                   : NodeResult::failure(
                         ErrorInfo{"EVOLUTION_FAILED", details, QString::fromUtf8(e.what())});
    }
}
} // namespace
TaskDescriptor EvolutionFftTask::descriptor()
{
    return evolutionDescriptor(false);
}
TaskDescriptor EvolutionMtmTask::descriptor()
{
    return evolutionDescriptor(true);
}
NodeResult EvolutionFftTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return run(i, c, m_parameters, false);
}
NodeResult EvolutionMtmTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return run(i, c, m_parameters, true);
}
} // namespace aster
