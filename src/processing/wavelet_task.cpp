// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/wavelet_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/statistics/significance.h"
#include "math/wavelet/cwt_support.h"
#include <stdexcept>
#include "third_party/wavelib/wavelib.h"
#include <memory>

namespace aster {
TaskDescriptor WaveletTask::descriptor()
{
    TaskDescriptor d; d.version = 3; d.label = "Continuous wavelet transform"; d.category = taskCategory(TaskCategory::Wavelet);
    d.inputKind = DataKind::Series; d.defaultOutput = "power";
    d.outputs = {{"power", "Wavelet power", DataKind::Grid, true}, {"coi", "Cone of influence", DataKind::Series, true},
                 {"raw_power", "Raw wavelet power", DataKind::Grid, false},
                 {"scale_corrected_power", "Scale-corrected wavelet power", DataKind::Grid, false},
                 {"significance_ratio", "Power / nominal 95% AR1 threshold", DataKind::Grid, false}};
    d.parameters = {science_detail::text("method", "Wavelet convention", "acycle_cwt", {"acycle_cwt", "torrence_compo"}),
        science_detail::text("mother", "Mother wavelet", "morlet", {"morlet", "paul", "dog"}),
        task_detail::number("parameter", "Wavelet parameter", 6, 1, 20),
        task_detail::number("dj", "Log2 scale step", 0.1, 0.02, 1),
        science_detail::boolean("significance", "Compute nominal local 95% AR1 threshold", true)};
    return d;
}
WaveletTask::WaveletTask(const QJsonObject& p) : m_mother(p["mother"].toString()), m_method(p["method"].toString()), m_parameter(p["parameter"].toDouble()),
    m_dj(p["dj"].toDouble()), m_significance(p["significance"].toBool()) {}
NodeResult WaveletTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    if (source->x.size() < 6) return NodeResult::failure(makeError(ErrorCode::TooFewSamples, {{"actual", double(source->x.size())}, {"minimum", 6}}));
    if (source->x.size() > 1000000) return NodeResult::failure(makeError(ErrorCode::WaveletSamplesLimit, {{"actual", double(source->x.size())}, {"maximum", 1000000}}));
    double dt = 0; const auto error = science_detail::uniformSpacing(*source, dt);
    if (!error.isEmpty()) return NodeResult::failure(error);
    if (m_mother == "morlet" && m_parameter < 5)
        return NodeResult::failure(makeError(ErrorCode::WaveletMorlet, {{"parameter", m_parameter}, {"minimum", 5}}));
    if ((m_mother != "morlet" && std::floor(m_parameter) != m_parameter) ||
        (m_mother == "dog" && std::fmod(m_parameter, 2) != 0))
        return NodeResult::failure(makeError(ErrorCode::WaveletOrder, {{"mother", m_mother}, {"parameter", m_parameter}}));
    const int n = static_cast<int>(source->x.size());
    try {
        const bool acycle = m_method == "acycle_cwt";
        const auto geometry = acycle ? numeric::acycleWaveletGeometry(m_mother,m_parameter,n,m_dj)
                                     : numeric::waveletGeometry(m_mother,m_parameter,n,m_dj);
        const int scales = geometry.count;
        const auto cells = static_cast<qint64>(n)*scales;
        const auto estimatedBytes = cells*(acycle ? 72 : 48)+static_cast<qint64>(n)*128;
        if (scales < 1 || cells > 16000000 || estimatedBytes > 512LL*1024*1024)
            return NodeResult::failure(makeError(ErrorCode::WaveletGridLimit, {{"samples", n}, {"scales", scales},
                {"cells", double(n)*scales}, {"maximum", 16000000}, {"dj", m_dj}}));
        if (context.cancellation.isCancelled()) return NodeResult::cancelled();
        const auto signal = numeric::centeredUnitSignal(source->y.size(),source->y.data());
        if (acycle && !(signal.sampleVariance > 0))
            return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation","cwt"}},
                "Variance-normalized Acycle CWT requires nonconstant data; raw Torrence-Compo power is available for constant data"));
        const auto name = m_mother.toUtf8();
        // Work in samples and unit-amplitude centered values. Restore physical units only at output.
        std::unique_ptr<cwt_set, decltype(&cwt_free)> wave(cwt_init(name.constData(), m_parameter, n, 1, scales), cwt_free);
        if (!wave) return NodeResult::failure(makeError(ErrorCode::AllocationFailure, {{"operation", "cwt"}, {"samples", n}, {"scales", scales}}));
        setCWTScales(wave.get(), geometry.initialScaleSamples, m_dj, "pow", 2);
        setCWTPadding(wave.get(), 1);
        const int status = cwt_checked(wave.get(), signal.values.data());
        if (status != 0)
            return NodeResult::failure(makeError(status == -1 ? ErrorCode::AllocationFailure : ErrorCode::NumericRange,
                {{"operation", "cwt"}, {"samples", n}, {"scales", scales}, {"native_status", status}}));
        if (context.cancellation.isCancelled()) return NodeResult::cancelled();
        GridData power;
        power.x = source->x; power.y.resize(scales); power.rows = scales; power.columns = n;
        power.values.resize(scales*static_cast<qsizetype>(n));
        power.xAxis = source->xAxis; power.yAxis = {"Period", source->xAxis.unit};
        power.valueAxis = {"Wavelet power", source->yAxis.unit.isEmpty() ? QString() : "("+source->yAxis.unit+")^2"};
        for (int j=0; j<scales; ++j) {
            power.y[j] = wave->period[j]*dt;
            if (!(power.y[j] > 0) || !std::isfinite(power.y[j]) || (j>0 && !(power.y[j]>power.y[j-1])))
                throw std::runtime_error("Wavelet period coordinates cannot be represented in the selected X unit");
        }
        QVector<double> unitPower;
        if ((m_significance || acycle) && signal.sampleVariance > 0) unitPower.resize(power.values.size());
        bool hasPower = false;
        for (qsizetype i=0; i<power.values.size(); ++i) {
            const auto v = wave->output[i];
            if (!unitPower.isEmpty()) unitPower[i] = v.re*v.re+v.im*v.im;
            const double magnitude = std::hypot(v.re,v.im)*signal.amplitude;
            power.values[i] = magnitude*magnitude;
            if (!std::isfinite(power.values[i])) throw std::runtime_error("Wavelet power exceeds the floating-point range; rescale the input amplitude");
            hasPower = hasPower || power.values[i] > 0;
        }
        if (signal.amplitude > 0 && !hasPower)
            throw std::runtime_error("All nonconstant wavelet power underflows to zero; rescale the input amplitude");
        SeriesData coi{source->x, QVector<double>(n), source->xAxis, {"COI period", source->xAxis.unit}};
        for (int i=0; i<n; ++i) {
            coi.y[i] = wave->coi[i]*dt;
            if (!std::isfinite(coi.y[i])) throw std::runtime_error("COI coordinates exceed the floating-point range");
        }
        auto result = NodeResult::success({{"power", makeData(power)}, {"coi", makeData(std::move(coi))}},
            {{"mother", m_mother}, {"parameter", m_parameter}, {"sample_spacing", dt}, {"dj", m_dj}, {"scales", scales},
             {"s0", geometry.initialScaleSamples*dt}, {"s0_samples", geometry.initialScaleSamples},
             {"fourier_factor", geometry.fourierFactor}, {"period_min", power.y.front()}, {"period_max", power.y.back()},
             {"padding_samples", wave->npad}, {"layout", "row-major: period rows, time columns"},
             {"power_normalization", "abs(CWT)^2 in original Y units squared; not PSD or variance-normalized power"},
             {"coi_semantics", "period > COI period is edge-affected; outputs are not masked"},
             {"coi_model", "Torrence-Compo mother-specific e-folding convention; approximate for nondefault orders"}});
        result.report["coi_interpretation"] = "Classical e-folding convention, not a fixed probability or uniform reconstruction-error bound";
        result.report["wavelet_convention"] = m_method;
        result.report["calibration_reference"] = "docs/methods/statistical_limits.md";
        result.report["significance_calibration"] = "Nominal pointwise approximation; finite-record error rates depend on mother, order and background";
        if (geometry.initialScaleSamples > 2)
            result.warnings.push_back("Smallest scale increased so its nominal Fourier period is at least two samples; wavelet band tails may still alias");
        if ((m_mother == "paul" && m_parameter != 4) || (m_mother == "dog" && m_parameter != 2))
            result.warnings.push_back("COI uses the conventional mother-specific factor; nondefault orders need separate edge-effect calibration");
        if (m_significance && signal.sampleVariance > 0) {
            science_detail::initializeGsl();
            const auto lag = acycle ? numeric::estimateAcycleWaveletLag1(signal.values.size(),signal.values.data())
                                    : numeric::estimateWaveletLag1(signal.values.size(),signal.values.data());
            result.report["lag1"] = lag.used; result.report["lag1_raw"] = lag.raw; result.report["lag1_clipped"] = lag.clipped;
            result.report["lag1_estimator"] = acycle ? "Acycle ar1nv: lag-one covariance (N-1) / variance (N); no clipping"
                                                      : "conditional OLS of mean-removed series; bounded to [-0.99, 0.99]";
            if (acycle && (!std::isfinite(lag.used) || std::abs(lag.used)>=1)) {
                result.report["significance_available"] = false;
                result.warnings.push_back("Estimated AR1 coefficient is not stationary (abs(rho)>=1); wavelet power is retained but the significance ratio is unavailable");
            } else {
            const auto ratio = wave_Significance(n, scales, unitPower.data(), wave->scale, signal.sampleVariance,
                                                 1, m_parameter, m_mother, lag.used, .95);
            GridData significance = power; significance.values = QVector<double>(ratio.begin(),ratio.end());
            significance.valueAxis = {"Power / nominal 95% threshold", "dimensionless"};
            result.outputs.insert("significance_ratio", makeData(std::move(significance)));
            result.report["significance_available"] = true;
            result.report["significance_level"] = .95; result.report["significance_dof"] = geometry.degreesOfFreedom;
            result.report["variance_convention"] = "sample variance (N-1)";
            result.report["significance_model"] = "pointwise AR1 chi-square approximation; fitted-background uncertainty and multiple comparisons not corrected";
            result.report["significance_ratio_semantics"] = ">1 exceeds the local threshold; not a probability or global discovery guarantee; check COI";
            if (lag.clipped) result.warnings.push_back("Estimated lag-1 coefficient was clipped to [-0.99, 0.99]; inspect stationarity and background suitability");
            result.warnings.push_back("Nominal 95% wavelet significance is pointwise and approximate; it does not control discoveries over the whole time-frequency map. The COI is not a fixed error-probability boundary");
            }
        } else if (m_significance) result.warnings.push_back("Constant signal: significance ratio is undefined");
        if (acycle) {
            // Acycle method 2 stores |W|^2/scale and displays it divided by
            // sample variance. Keep all three meanings as distinct outputs.
            result.outputs.insert("raw_power", result.outputs["power"]);
            GridData corrected = power, displayed = power;
            corrected.valueAxis = {"Scale-corrected wavelet power", source->yAxis.unit.isEmpty() || source->xAxis.unit.isEmpty()
                ? QString() : "("+source->yAxis.unit+")^2 / ("+source->xAxis.unit+")"};
            displayed.valueAxis = {"Variance-normalized scale-corrected power", source->xAxis.unit.isEmpty()
                ? QString() : "1 / ("+source->xAxis.unit+")"};
            for (int j=0; j<scales; ++j) {
                const auto scale = static_cast<long double>(wave->scale[j])*dt;
                for (int i=0; i<n; ++i) {
                    const qsizetype index=static_cast<qsizetype>(j)*n+i;
                    corrected.values[index] = static_cast<double>(static_cast<long double>(power.values[index])/scale);
                    displayed.values[index] = static_cast<double>((static_cast<long double>(unitPower[index])/signal.sampleVariance)/scale);
                    if (!std::isfinite(corrected.values[index]) || !std::isfinite(displayed.values[index]))
                        throw std::runtime_error("Scale-corrected wavelet power is not representable in the selected X unit");
                }
            }
            result.outputs["scale_corrected_power"] = makeData(std::move(corrected));
            result.outputs["power"] = makeData(std::move(displayed));
            result.report["power_normalization"] = "Acycle method 2 display: abs(CWT)^2 / sample_variance / physical_scale";
            result.report["raw_power_normalization"] = "abs(CWT)^2 in original Y units squared";
            result.report["scale_corrected_power_normalization"] = "Acycle method 2 saved power: abs(CWT)^2 / physical_scale";
            result.report["reference_implementation"] = "Acycle 3.1.1 validation snapshot wave_readGUI method 2 / wave_update_wavelet / ar1nv";
            result.report["calibration_reference"] = "docs/methods/acycle_compatibility.md";
            result.report["significance_calibration"] = "Acycle pointwise approximation; not globally calibrated";
        }
        return result;
    } catch (const std::invalid_argument& e) {
        return NodeResult::failure(makeError(ErrorCode::ParameterInvalid, {{"mother", m_mother}, {"parameter", m_parameter}, {"dj", m_dj}}, QString::fromUtf8(e.what())));
    } catch (const std::runtime_error& e) {
        return NodeResult::failure(makeError(ErrorCode::NumericRange, {{"operation", "cwt"}, {"samples", n}, {"spacing", dt}}, QString::fromUtf8(e.what())));
    }
}
}
