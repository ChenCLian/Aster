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
    d.outputs = {{"power_map", "Power spectral density map", DataKind::Grid, true},
                 {"mean_spectrum", "Mean window spectrum", DataKind::Spectrum, true}};
    d.defaultOutput = "power_map";
    d.parameters = {
        task_detail::number("evo_window", "Window samples", 128, 8,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        task_detail::number("evo_step", "Step samples", 16, 1,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        task_detail::number("evo_nfft", "FFT samples (0 = window)", 0, 0,
                            double(numeric::EvolutionLimits::maxFftSamples),
                            ParameterKind::Integer),
        science_detail::text("evo_trend", "Local detrending", "mean", {"mean", "linear"}),
        task_detail::number("evo_frequency_ratio", "Maximum / Nyquist", 1, .001, 1)};
    if (mtm)
        d.parameters.push_back(task_detail::number("evo_nw", "Time-bandwidth NW", 2, 1, 32));
    else
        d.parameters.push_back(
            science_detail::text("evo_taper", "FFT window", "hann", {"hann", "rectangular"}));
    for (auto &p : d.parameters)
    {
        p.group =
            (p.key == "evo_window" || p.key == "evo_step") ? "moving_window" : "spectral_estimator";
    }
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
    o.maximumFrequencyRatio = p["evo_frequency_ratio"].toDouble();
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
        report["normalization"] = "physical_PSD_no_column_normalization";
        report["confidence_scope"] = "descriptive_map_no_significance_test";
        report["mean_spectrum_semantics"] =
            "arithmetic_mean_of_window_PSDs_not_full_record_estimate";
        report["adaptive_unconverged_windows"] = r.unconvergedWindows;
        report["resampled"] = false;
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
