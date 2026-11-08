// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/cross_spectrum_task.h"
#include "processing/task_helpers.h"
#include "processing/task_categories.h"
#include "processing/science_helpers.h"
#include "math/spectrum/cross_spectrum.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
namespace aster
{
namespace
{
const QStringList columns{"frequency",  "cross_magnitude", "coherence", "phase_degrees",
                          "cross_real", "cross_imaginary", "auto_a",    "auto_b"};
NodeResult failure(const QString &code, const QString &message, QJsonObject context = {})
{
    return NodeResult::failure(ErrorInfo{code, context, message});
}
QJsonObject axis(const AxisInfo &a)
{
    return {{"name", a.name}, {"unit", a.unit}};
}
const TableData *resultTable(const NodeResult &result, QString &error)
{
    const auto data = result.outputs.value("cross_spectrum");
    const auto *table = dataAs<TableData>(data);
    const auto units = result.report["column_units"].toArray();
    const double nfft = result.report["nfft"].toDouble();
    if (result.state != NodeState::Succeeded || !table || !validateData(data).isEmpty() ||
        table->columns != columns || result.report["cross_spectrum_schema"].toInt() != 1 ||
        units.size() != columns.size() || nfft < 8 || nfft > numeric::CrossSpectrumLimits::maxFft ||
        std::floor(nfft) != nfft || table->rows.size() != qsizetype(nfft) / 2 + 1 ||
        result.report["display_reduced"].toBool())
    {
        error = "Complete cross-spectrum table and matching report are required";
        return nullptr;
    }
    for (const auto &unit : units)
        if (!unit.isString())
        {
            error = "Invalid cross-spectrum column units";
            return nullptr;
        }
    return table;
}
} // namespace
TaskDescriptor CrossSpectrumTask::descriptor()
{
    auto d = task_detail::seriesTransform("Welch cross spectrum",
                                          taskCategory(TaskCategory::PowerSpectrum));
    d.key = "cross_spectrum";
    d.version = 2; // Exact zero DC after rectangular-window detrending.
    d.pairedInput = true;
    d.catalogVisible = true;
    d.outputs = {{"cross_spectrum", "Cross-spectral estimates", DataKind::Table, true}};
    d.defaultOutput = "cross_spectrum";
    d.parameters = {
        task_detail::number("psd_window", "Segment points", 256, 8,
                            numeric::CrossSpectrumLimits::maxFft, ParameterKind::Integer),
        task_detail::number("psd_overlap", "Overlap points", 128, 0,
                            numeric::CrossSpectrumLimits::maxFft - 1, ParameterKind::Integer),
        task_detail::number("psd_nfft", "FFT points", 0, 0, numeric::CrossSpectrumLimits::maxFft,
                            ParameterKind::Integer),
        science_detail::text("psd_taper", "Window", "hann", {"hann", "rectangular"}),
        science_detail::text("psd_trend", "Detrending", "mean", {"none", "mean", "linear"})};
    for (auto &p : d.parameters)
        p.group = p.key == "psd_window" || p.key == "psd_overlap" ? "welch_segments"
                                                                  : "spectral_estimator";
    return d;
}
ValidationResult registerCrossSpectrum(TaskRegistry &registry)
{
    return registry.add(CrossSpectrumTask::descriptor(), [](const QJsonObject &p)
                        { return std::make_unique<CrossSpectrumTask>(p); });
}
NodeResult CrossSpectrumTask::execute(const TaskInput &input, const ExecutionContext &ctx)
{
    if (ctx.cancellation.isCancelled())
        return NodeResult::cancelled();
    try
    {
        TaskRegistry registry;
        registerCrossSpectrum(registry);
        const auto params = registry.normalize("cross_spectrum", m_parameters);
        if (!params.validation.ok())
            return NodeResult::failure(
                makeError(ErrorCode::ParameterInvalid, {}, params.validation.message()));
        const auto p = params.parameters;
        const SeriesData *series[2]{dataAs<SeriesData>(input.data),
                                    dataAs<SeriesData>(input.second)};
        double dt[2]{};
        for (int i = 0; i < 2; ++i)
        {
            const QString role = i ? "B" : "A";
            if (!series[i])
                return NodeResult::failure(
                    makeError(ErrorCode::InputType, {{"role", role}, {"expected", "Series"}}));
            if (std::size_t(series[i]->y.size()) > numeric::CrossSpectrumLimits::maxSamples)
                return failure("CROSS_BUDGET", "Input exceeds the cross-spectrum sample limit",
                               {{"role", role}});
            const auto invalid = validateData(i ? input.second : input.data);
            if (!invalid.isEmpty())
                return NodeResult::failure(
                    makeError(ErrorCode::InputInvalid, {{"role", role}}, invalid));
            auto err = science_detail::uniformSpacing(*series[i], dt[i]);
            if (!err.isEmpty())
            {
                err.context["role"] = role;
                return NodeResult::failure(err);
            }
            if (ctx.cancellation.isCancelled())
                return NodeResult::cancelled();
        }
        const auto &a = *series[0];
        const auto &b = *series[1];
        // Equal numeric coordinates cannot establish equal age origins. Require
        // equal declared X name AND unit; never reinterpret BP as before J2000.
        if (a.xAxis.name.trimmed() != b.xAxis.name.trimmed() ||
            a.xAxis.unit.trimmed() != b.xAxis.unit.trimmed())
            return failure("CROSS_X_AXIS", "Paired X axes must declare the same name and unit",
                           {{"A", axis(a.xAxis)}, {"B", axis(b.xAxis)}});
        if (a.x.size() != b.x.size())
            return failure("CROSS_ALIGNMENT", "Paired series must have the same aligned X grid",
                           {{"samples_a", double(a.x.size())}, {"samples_b", double(b.x.size())}});
        const double tolerance = std::min(dt[0], dt[1]) * 1e-6;
        for (qsizetype i = 0; i < a.x.size(); ++i)
        {
            if (!(i & 4095) && ctx.cancellation.isCancelled())
                return NodeResult::cancelled();
            if (std::abs(a.x[i] - b.x[i]) > tolerance)
                return failure("CROSS_ALIGNMENT",
                               "Paired X coordinates differ; align explicitly before analysis",
                               {{"point", double(i + 1)},
                                {"x_a", a.x[i]},
                                {"x_b", b.x[i]},
                                {"tolerance", tolerance}});
        }
        numeric::CrossSpectrumOptions o;
        o.segment = p["psd_window"].toInt();
        o.overlap = p["psd_overlap"].toInt();
        o.nfft = p["psd_nfft"].toInt();
        // Explain combinations using the actual paired input before entering
        // the estimator. Do not silently change the user's segment or overlap.
        const QJsonObject sizes{{"samples", double(a.y.size())},
                                {"segment", double(o.segment)},
                                {"overlap", double(o.overlap)},
                                {"nfft", double(o.nfft)}};
        if (o.segment > std::size_t(a.y.size()))
            return failure("CROSS_SEGMENT_LENGTH", "Segment exceeds paired input length", sizes);
        if (o.overlap >= o.segment)
            return failure("CROSS_OVERLAP", "Overlap must be smaller than segment length", sizes);
        if (o.nfft && o.nfft < o.segment)
            return failure("CROSS_NFFT", "FFT length must be zero (automatic) or at least the segment length", sizes);
        const auto segments = 1 + (std::size_t(a.y.size()) - o.segment) / (o.segment - o.overlap);
        if (segments < 2)
        {
            auto context = sizes;
            context["segments"] = double(segments);
            context["required_samples"] = double(2 * o.segment - o.overlap);
            return failure("CROSS_SEGMENTS", "Coherence requires at least two complete segments", context);
        }
        o.window = p["psd_taper"] == "hann" ? numeric::CrossWindow::Hann
                                            : numeric::CrossWindow::Rectangular;
        o.detrend = p["psd_trend"] == "none"     ? numeric::CrossDetrend::None
                    : p["psd_trend"] == "linear" ? numeric::CrossDetrend::Linear
                                                 : numeric::CrossDetrend::Mean;
        const auto r = numeric::crossSpectrum(
            std::vector<double>(a.y.begin(), a.y.end()),
            std::vector<double>(b.y.begin(), b.y.end()), dt[0], o,
            [&] { return ctx.cancellation.isCancelled(); },
            [&](int n)
            {
                if (ctx.progress)
                    ctx.progress(n, "progress.cross_spectrum");
            });
        TableData table;
        table.columns = columns;
        table.rows.reserve(qsizetype(r.frequency.size()));
        table.defined.reserve(qsizetype(r.frequency.size()));
        qsizetype missingC = 0, missingP = 0;
        for (std::size_t k = 0; k < r.frequency.size(); ++k)
        {
            if (!(k & 4095) && ctx.cancellation.isCancelled())
                return NodeResult::cancelled();
            table.rows.push_back({r.frequency[k], r.magnitude[k], r.coherence[k], r.phaseDegrees[k],
                                  r.real[k], r.imaginary[k], r.autoA[k], r.autoB[k]});
            table.defined.push_back({1, 1, r.coherenceDefined[k], r.phaseDefined[k], 1, 1, 1, 1});
            missingC += !r.coherenceDefined[k];
            missingP += !r.phaseDefined[k];
        }
        const auto density = [&](const QString &u, const QString &v)
        {
            return u.isEmpty() || v.isEmpty() || a.xAxis.unit.isEmpty()
                       ? QString()
                       : "(" + u + ") * (" + v + ") * (" + a.xAxis.unit + ")";
        };
        const QString crossUnit = density(a.yAxis.unit, b.yAxis.unit);
        const QJsonArray units{a.xAxis.unit.isEmpty() ? QString() : "1/(" + a.xAxis.unit + ")",
                               crossUnit,
                               "1",
                               "deg",
                               crossUnit,
                               crossUnit,
                               density(a.yAxis.unit, a.yAxis.unit),
                               density(b.yAxis.unit, b.yAxis.unit)};
        auto result = NodeResult::success(
            {{"cross_spectrum", makeData(std::move(table))}},
            {{"cross_spectrum_schema", 1},
             {"estimator", "Welch_mean"},
             {"parameters", p},
             {"spacing", dt[0]},
             {"samples", double(a.y.size())},
             {"segment_points", double(o.segment)},
             {"overlap_points", double(o.overlap)},
             {"segments", double(r.segments)},
             {"nfft", double(r.nfft)},
             {"unused_tail_samples", double(r.unusedTail)},
             {"taper", p["psd_taper"] == "hann" ? "periodic_hann" : "rectangular"},
             {"detrending", p["psd_trend"]},
             {"normalization", "one_sided_density"},
             {"cross_convention", "conj(A)*B"},
             {"phase_unit", "deg"},
             {"phase_axis_direction", "increasing_numeric_X"},
             {"alignment", "same_uniform_grid"},
             {"alignment_tolerance", tolerance},
             {"resampled", false},
             {"column_units", units},
             {"x_axis", axis(a.xAxis)},
             {"y_axis_a", axis(a.yAxis)},
             {"y_axis_b", axis(b.yAxis)},
             {"undefined_coherence_bins", double(missingC)},
             {"undefined_phase_bins", double(missingP)},
             {"confidence_scope", "descriptive_no_significance_test"}});
        // Preserve immutable pair/selected-output provenance supplied by the coordinator.
        if (ctx.sourceMetadata.contains("batch_pair"))
            result.report["batch_pair"] = ctx.sourceMetadata["batch_pair"];
        return result;
    }
    catch (const numeric::CrossSpectrumCancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const std::length_error &e)
    {
        return failure("CROSS_BUDGET", QString::fromUtf8(e.what()));
    }
    catch (const std::invalid_argument &e)
    {
        return failure("CROSS_PARAMETERS", QString::fromUtf8(e.what()));
    }
    catch (const std::overflow_error &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::NumericRange, {}, QString::fromUtf8(e.what())));
    }
    catch (const std::exception &e)
    {
        return failure("CROSS_FAILED", QString::fromUtf8(e.what()));
    }
}
CrossCsvResult crossSpectrumCsvData(const NodeResult &result)
{
    CrossCsvResult out;
    const auto *table = resultTable(result, out.error);
    if (!table)
        return out;
    auto labeled = *table;
    const auto units = result.report["column_units"].toArray();
    for (qsizetype c = 0; c < labeled.columns.size(); ++c)
        if (!units[c].toString().isEmpty())
            labeled.columns[c] += " (" + units[c].toString() + ")";
    out.data = makeData(std::move(labeled));
    return out;
}
CrossCurveResult crossSpectrumCurves(const NodeResult &result, const QString &column)
{
    CrossCurveResult out;
    const auto *t = resultTable(result, out.error);
    if (!t)
        return out;
    const auto units = result.report["column_units"].toArray();
    const int c = int(columns.indexOf(column));
    if (c <= 0)
    {
        out.error = "Unknown cross-spectrum metric";
        return out;
    }
    SeriesData current;
    current.xAxis = {"Frequency", units[0].toString()};
    current.yAxis = {column, units[c].toString()};
    const auto flush = [&]
    {
        if (!current.x.isEmpty())
        {
            out.paths.push_back(current);
            current.x.clear();
            current.y.clear();
        }
    };
    double previous = -1;
    for (qsizetype r = 0; r < t->rows.size(); ++r)
    {
        const auto &row = t->rows[r];
        if (!t->isDefined(r, 0) || row[0] < 0 || row[0] <= previous)
        {
            out.paths.clear();
            out.error = "Invalid cross-spectrum frequency grid";
            return out;
        }
        previous = row[0];
        if (!t->isDefined(r, c))
        {
            flush();
            continue;
        }
        if (column == "phase_degrees" && !current.y.isEmpty() &&
            std::abs(row[c] - current.y.back()) > 180)
            flush();
        current.x.push_back(row[0]);
        current.y.push_back(row[c]);
    }
    flush();
    return out;
}
} // namespace aster
