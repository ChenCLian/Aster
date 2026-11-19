// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/band_power_task.h"
#include "processing/science_helpers.h"
#include "processing/task_categories.h"
#include "processing/task_helpers.h"
#include "model/frequency_units.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aster
{
double observedPsdResolution(const InputOrigin &origin)
{
    const auto &r = origin.report;
    if (origin.method == "database_spectrum" && origin.port == "data")
        return r["observed_psd_rayleigh"].toDouble();
    if (origin.port == "mean_spectrum" &&
        (origin.method == "evolution_fft" || origin.method == "evolution_mtm") &&
        r["normalization"] == "physical_PSD_no_column_normalization")
        return r["rayleigh_frequency"].toDouble();
    if (origin.port != "spectrum") return 0;
    if (origin.method == "mtm" || origin.method == "mtm_ar1_simulation")
        return r["rayleigh_frequency"].toDouble();
    if (origin.method == "ar1_global")
        return positiveReciprocal(r["samples"].toDouble() * r["sample_spacing"].toDouble());
    if (origin.method == "fft_spectrum" || origin.method == "welch_spectrum")
        return positiveReciprocal(r["segment_points"].toDouble() * r["spacing"].toDouble());
    return 0;
}

TaskDescriptor BandPowerTask::descriptor()
{
    TaskDescriptor d;
    d.label = "Period-band power integration";
    d.category = taskCategory(TaskCategory::PowerSpectrum);
    d.inputKind = DataKind::Spectrum;
    d.readsInputMetadata = true;
    d.inputPortKeys = {"spectrum", "mean_spectrum", "data"};
    d.outputs = {{"band_power", "Integrated band power", DataKind::Table, true}};
    d.defaultOutput = "band_power";
    auto axis = science_detail::text("band_axis", "Enter bounds as", "period", {"period", "frequency"});
    auto reference = science_detail::text("band_reference", "Reference band", "available", {"available", "custom"});
    auto bound = [](const char *key, const char *label) {
        return task_detail::number(key, label, 0, 0, std::numeric_limits<double>::max());
    };
    auto low = bound("band_min", "Target lower bound"), high = bound("band_max", "Target upper bound");
    auto refLow = bound("band_ref_min", "Reference lower bound"), refHigh = bound("band_ref_max", "Reference upper bound");
    refLow.visibleWhen = refHigh.visibleWhen = {{"band_reference", "custom"}};
    d.parameters = {axis, low, high, reference, refLow, refHigh};
    return d;
}

namespace
{
struct Band { double low, high; };
Band frequencyBand(double low, double high, bool period)
{
    return period ? Band{positiveReciprocal(high), positiveReciprocal(low)} : Band{low, high};
}
bool valid(Band b)
{
    return std::isfinite(b.low) && std::isfinite(b.high) && b.low >= 0 && b.low < b.high;
}
// Integrate the piecewise linear PSD on the original frequency axis, including partial end intervals.
long double section(double x0, double x1, double p0, double p1, Band band)
{
    const auto left = std::max(x0, band.low), right = std::min(x1, band.high);
    if (left >= right) return 0;
    const auto interpolate = [&](double x) -> long double {
        const long double t = (static_cast<long double>(x) - x0) / (x1 - x0);
        return (1 - t) * p0 + t * p1;
    };
    return (right - left) * (0.5L * interpolate(left) + 0.5L * interpolate(right));
}
}
NodeResult BandPowerTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    if (c.cancellation.isCancelled()) return NodeResult::cancelled();
    const auto *s = dataAs<SpectrumData>(input.data);
    const auto rayleigh = observedPsdResolution(input.origin);
    if (!s || !std::isfinite(rayleigh) || rayleigh <= 0)
        return NodeResult::failure(ErrorInfo{"BAND_PSD_INPUT", {}, "Choose an observed power spectral density output"});
    const auto &f = s->frequency, &p = s->power;
    if (f.size() < 2 || p.size() != f.size())
        return NodeResult::failure(makeError(ErrorCode::TooFewSamples, {{"minimum", 2}, {"actual", double(f.size())}}));
    const bool period = m_parameters["band_axis"] == "period";
    auto target = frequencyBand(m_parameters["band_min"].toDouble(), m_parameters["band_max"].toDouble(), period);
    const Band available{f.front(), f.back()};
    auto reference = m_parameters["band_reference"] == "custom"
        ? frequencyBand(m_parameters["band_ref_min"].toDouble(), m_parameters["band_ref_max"].toDouble(), period)
        : available;
    if (!valid(target) || !valid(reference))
        return NodeResult::failure(ErrorInfo{"BAND_BOUNDS", {}, "Enter ordered, finite bounds; period limits must both be positive"});
    // Snap only round-off at a boundary, such as 1 / (1 / Nyquist).
    auto close = [](double a, double b) {
        return std::abs(a - b) <= 8 * std::numeric_limits<double>::epsilon() * std::max(std::abs(a), std::abs(b));
    };
    if (close(reference.low, available.low)) reference.low = available.low;
    if (close(reference.high, available.high)) reference.high = available.high;
    if (close(target.low, reference.low)) target.low = reference.low;
    if (close(target.high, reference.high)) target.high = reference.high;
    if (!valid(reference) || !valid(target) || reference.low < available.low || reference.high > available.high ||
        target.low < reference.low || target.high > reference.high)
        return NodeResult::failure(ErrorInfo{"BAND_COVERAGE", {{"frequency_min", available.low}, {"frequency_max", available.high}},
                                           "The spectrum must cover the reference, and the reference must cover the target"});
    const double width = target.high - target.low;
    if ((width < rayleigh && !close(width, rayleigh)) ||
        (target.low > 0 && target.low < rayleigh && !close(target.low, rayleigh)))
        return NodeResult::failure(ErrorInfo{"BAND_RESOLUTION", {{"rayleigh_frequency", rayleigh},
            {"target_frequency_min", target.low}, {"target_frequency_max", target.high}}, "The record or analysis window is too short for this band"});
    long double targetPower = 0, referencePower = 0;
    for (qsizetype i = 1; i < f.size(); ++i) {
        if (task_detail::cancelled(c, i)) return NodeResult::cancelled();
        if (!(f[i] > f[i-1]) || !std::isfinite(f[i]) || !std::isfinite(f[i-1]) || f[i-1] < 0 ||
            !std::isfinite(p[i-1]) || !std::isfinite(p[i]) || p[i-1] < 0 || p[i] < 0)
            return NodeResult::failure(makeError(ErrorCode::InputInvalid, {{"point", double(i+1)}}, "Invalid PSD grid or power"));
        targetPower += section(f[i-1], f[i], p[i-1], p[i], target);
        referencePower += section(f[i-1], f[i], p[i-1], p[i], reference);
    }
    if (c.cancellation.isCancelled()) return NodeResult::cancelled();
    if (referencePower == 0)
        return NodeResult::failure(ErrorInfo{"BAND_ZERO_POWER", {}, "Reference band has zero power"});
    const double a = double(targetPower), b = double(referencePower), fraction = double(targetPower / referencePower);
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(fraction) || b == 0 || (targetPower > 0 && a == 0))
        return NodeResult::failure(ErrorInfo{"BAND_NUMERIC", {}, "Integrated power is outside the representable range"});
    const auto unit = s->frequencyAxis.unit;
    const QString suffix = unit.isEmpty() ? QString() : " (" + unit + ")";
    TableData table{{"Target frequency min" + suffix, "Target frequency max" + suffix,
                     "Reference frequency min" + suffix, "Reference frequency max" + suffix,
                     "Band power", "Reference power", "Power fraction", "Power (%)"},
                    {{target.low, target.high, reference.low, reference.high, a, b, fraction, 100 * fraction}}};
    return NodeResult::success({{"band_power", makeData(std::move(table))}},
        {{"method", "piecewise_linear_PSD_trapezoids"}, {"source_method", input.origin.method},
         {"source_port", input.origin.port}, {"frequency_unit", unit}, {"period_unit", periodUnitForFrequency(unit)},
         {"psd_unit", s->powerAxis.unit}, {"rayleigh_frequency", rayleigh}, {"target_power", a},
         {"reference_power", b}, {"fraction", fraction}, {"frequency_min", target.low}, {"frequency_max", target.high},
         {"reference_min", reference.low}, {"reference_max", reference.high}});
}
}
