#include "processing/task_categories.h"
#include "processing/mtm_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/spectrum/mtm_spectrum.h"
#include "math/spectrum/spectrum_peaks.h"
#include "math/statistics/significance.h"
#include <vector>
#include <exception>
#include <stdexcept>

namespace aster {
#define MTM_OPTIONAL_PORTS(X) \
    X(ar1_smoothed) X(ar1_background) X(ar1_90) X(ar1_95) X(ar1_99) X(ar1_999) \
    X(swa_background) X(swa_90) X(swa_95) X(swa_99) X(swa_999) \
    X(fdr_05) X(fdr_01) X(fdr_001) X(fdr_0001)

TaskDescriptor MtmTask::descriptor()
{
    TaskDescriptor d; d.version = 7; d.label = "Multitaper spectrum (MTM)"; d.category = taskCategory(TaskCategory::PowerSpectrum);
    d.inputKind = DataKind::Series; d.defaultOutput = "spectrum";
    d.outputs = {{"spectrum", "Power spectrum", DataKind::Spectrum, true}, {"peaks", "Detected peaks", DataKind::Table, true}};
#define OUTPUT_PORT(port) d.outputs.push_back({#port, #port, DataKind::Spectrum, false});
    MTM_OPTIONAL_PORTS(OUTPUT_PORT)
#undef OUTPUT_PORT
    d.parameters = {task_detail::number("nw", "Time-bandwidth NW", 2, 1, 32),
        task_detail::number("nfft", "FFT length (0 = input length)", 0, 0, 4000000, ParameterKind::Integer),
        task_detail::number("fmax_ratio", "Output frequency / Nyquist (fit uses full spectrum)", 1, 0.001, 1),
        science_detail::boolean("ar1", "Fit AR1 background", true), science_detail::boolean("swa", "SWA background and FDR", true),
        science_detail::boolean("logfit", "Logarithmic AR1 fit", false),
        task_detail::number("smooth_window", "Median window fraction", 0.2, 0.05, 0.25),
        task_detail::number("peak_relative_prominence", "Relative prominence (%)", 0.1, 0, 100),
        task_detail::number("peak_prominence", "Minimum peak prominence", 0, 0, 1e100),
        task_detail::number("peak_distance", "Minimum peak distance (bins)", 3, 1, 1000000, ParameterKind::Integer),
        task_detail::number("peak_width", "Minimum peak width (bins)", 0, 0, 1000000),
        task_detail::number("peak_height", "Minimum peak power", 0, 0, 1e100),
        task_detail::number("peak_radius", "Prominence radius (0 = full)", 0, 0, 1000000, ParameterKind::Integer)};
    for(auto& p:d.parameters) {
        p.group=p.key.startsWith("peak_") ? "peak_detection" :
            QStringList{"ar1","swa","logfit","smooth_window"}.contains(p.key) ? "background" : "spectrum";
    }
    return d;
}
MtmTask::MtmTask(const QJsonObject& p) : m_peakDistance(p["peak_distance"].toInt()), m_nfft(p["nfft"].toInt()), m_peakRadius(p["peak_radius"].toInt()), m_nw(p["nw"].toDouble()),
    m_fmaxRatio(p["fmax_ratio"].toDouble()), m_smoothWindow(p["smooth_window"].toDouble()),
    m_prominence(p["peak_prominence"].toDouble()), m_peakWidth(p["peak_width"].toDouble()), m_peakHeight(p["peak_height"].toDouble()), m_relativeProminence(p["peak_relative_prominence"].toDouble()/100.0), m_ar1(p["ar1"].toBool()), m_swa(p["swa"].toBool()), m_logfit(p["logfit"].toBool()) {}
NodeResult MtmTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    double dt = 0; const auto error = science_detail::uniformSpacing(*source, dt);
    if (!error.isEmpty()) return NodeResult::failure(error);
    if (m_nw >= source->y.size() / 2.0) return NodeResult::failure(makeError(ErrorCode::MtmNW, {{"nw", m_nw}, {"samples", double(source->y.size())}, {"upper_exclusive", source->y.size() / 2.0}}));
    if (m_nw != 1 && m_nw < 1.25)
        return NodeResult::failure(makeError(ErrorCode::ParameterInvalid, {{"nw", m_nw}}, "Acycle MTM requires NW=1 or NW>=1.25"));
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    const int tapers = m_nw == 1 ? 2 : int(std::lround(2*m_nw))-1;
    const auto fftLength = m_nfft ? size_t(m_nfft) : size_t(source->y.size());
    numeric::MtmSpectrum spectrum;
    try {
        spectrum = numeric::mtmSpectrum(std::vector<double>(source->y.begin(), source->y.end()),
            1.0 / dt, m_nw, tapers, 1.0 / (2 * dt), [&] { return context.cancellation.isCancelled(); }, fftLength,
            numeric::MtmConvergenceDomain::FullSpectrum);
    } catch (const std::exception& e) {
        return context.cancellation.isCancelled() ? NodeResult::cancelled() : NodeResult::failure(makeError(ErrorCode::MtmFailure, {{"nw", m_nw}, {"samples", double(source->y.size())}, {"spacing", dt}}, QString::fromUtf8(e.what())));
    }
    const double outputMaximum = m_fmaxRatio/(2*dt);
    const size_t outputBins = std::upper_bound(spectrum.frequency.begin(), spectrum.frequency.end(),
        outputMaximum + (1.0/dt/fftLength)*1e-10) - spectrum.frequency.begin();
    const QVector<double> frequency(spectrum.frequency.begin(), spectrum.frequency.begin()+outputBins);
    auto result = NodeResult::success({}, {{"sample_spacing", dt}, {"nw", m_nw}, {"tapers", tapers}, {"nfft", double(fftLength)},
        {"adaptive_iterations", spectrum.iterations}, {"adaptive_converged", spectrum.converged},
        {"estimator", "symmetric DPSS / adaptive Thomson / one-sided PSD"}});
    result.report["numerical_backend"] = science_detail::numericBackendReport();
    SpectrumInference inference;
    inference.acycle = true;
    inference.degreesOfFreedom.assign(spectrum.power.size(),2*tapers);
    result.report["reference_implementation"] = "Uploaded Acycle 3.1.1 spectrum.m / redconfML.m / spectralswafdr.m";
    result.report["adaptive_convergence"] = "sum(abs(delta)) over the full two-sided spectrum < 0.0005*variance; paired real-FFT bins counted twice";
    result.report["significance_calibration"] = "Nominal approximations; no empirical false-positive-rate guarantee";
    result.report["calibration_reference"] = "docs/methods/statistical_limits.md";
    result.report["unknown_parameter_calibration_reference"] = "docs/methods/ar1_global.md";
    result.report["confidence_scope"] = "Pointwise nominal thresholds, not a global test of the spectrum";
    result.report["fdr_control_status"] = "Not established for fitted SWA backgrounds; current-default Gaussian AR1 calibration shows inflated false discoveries";
    result.report["fdr_scope"] = "Full one-sided spectrum including endpoints; Hopkins/Weedon bandwidth dependence convention";
    result.report["significance_model"] = "Acycle fixed 2K chi-square approximation; fitted-background uncertainty excluded";
    result.report["fdr_method"] = "Acycle bandwidth H(floor(NW)); last rejected observed p-value (not generic BY)";
    result.report["fdr_curve_semantics"] = "Inclusive observed rejection boundary; retained curves may coincide at different q levels";
    result.report["significance_dof"] = 2*tapers;
    result.report["acycle_power_conversion"] = "Aster physical-frequency PSD = Acycle normalized-angular-frequency power * 2*pi*dt";
    result.report["analysis_frequency_max"] = spectrum.frequency.back();
    result.report["nyquist_frequency"] = 1.0/(2*dt);
    result.report["rayleigh_frequency"] = 1.0/(source->y.size()*dt);
    result.report["half_bandwidth"] = m_nw/(source->y.size()*dt);
    result.report["background_fit_domain"] = "complete one-sided spectrum; fmax_ratio crops published outputs only";
    result.report["output_frequency_max"] = frequency.back();
    result.report["peak_semantics"] = "local maxima, not automatically significant periodicities";
    auto add = [&](const QString& key, const std::vector<double>& values) {
        if (values.empty()) return;
        auto output = science_detail::spectrum(frequency, QVector<double>(values.begin(), values.begin()+outputBins), source->xAxis.unit);
        if (!source->yAxis.unit.isEmpty() && !source->xAxis.unit.isEmpty())
            output.powerAxis.unit = "(" + source->yAxis.unit + ")^2 * (" + source->xAxis.unit + ")";
        auto data = makeData(std::move(output));
        const auto invalid = validateData(data);
        if (!invalid.isEmpty()) throw std::runtime_error((key + ": " + invalid).toStdString());
        result.outputs.insert(key, data);
    };
    add("spectrum", spectrum.power);
    if (!spectrum.converged) result.warnings.push_back("Adaptive MTM reached the iteration limit");
    science_detail::initializeGsl();
    RedNoiseConf ar1;
    const bool hasPower = std::any_of(spectrum.power.begin(), spectrum.power.end(), [](double p) { return p > 0; });
    if (m_ar1 && hasPower) {
        try {
            ar1 = computeRedNoiseConfidence(spectrum.power.data(), spectrum.frequency.data(), m_smoothWindow,
                                           1.0 / (2 * dt), spectrum.power.size(), m_nw, m_logfit, inference);
            result.report["ar1_rho"] = ar1.rho; result.report["ar1_scale"] = ar1.scale;
            add("ar1_smoothed", ar1.pxxsmooth); add("ar1_background", ar1.theored);
            add("ar1_90", ar1.chiConf.chi90); add("ar1_95", ar1.chiConf.chi95);
            add("ar1_99", ar1.chiConf.chi99); add("ar1_999", ar1.chiConf.chi999);
        } catch (const std::exception& e) {
            for (const auto& key : QStringList{"ar1_smoothed", "ar1_background", "ar1_90", "ar1_95", "ar1_99", "ar1_999"}) result.outputs.remove(key);
            ar1 = {}; result.warnings.push_back("AR1 unavailable: " + QString::fromUtf8(e.what()));
        }
    }
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    if (m_swa && hasPower) {
        try {
            const auto swa = FDR_TEST(spectrum.power, spectrum.frequency, m_nw, inference);
            result.report["fdr_test_count"] = double(swa.testCount);
            result.report["fdr_dependence_correction"] = swa.dependenceCorrection;
            result.report["swa_window_bins"] = swa.diagnostics.window;
            result.report["swa_smoothing_passes"] = swa.diagnostics.smoothingPasses;
            result.report["swa_short_fallback"] = swa.diagnostics.shortFallback;
            add("swa_background", swa.swa); add("swa_90", swa.chiConf.chi90); add("swa_95", swa.chiConf.chi95);
            add("swa_99", swa.chiConf.chi99); add("swa_999", swa.chiConf.chi999);
            add("fdr_05", swa.fdrConf.clfdr5); add("fdr_01", swa.fdrConf.clfdr1);
            add("fdr_001", swa.fdrConf.clfdr01); add("fdr_0001", swa.fdrConf.clfdr001);
            if (swa.diagnostics.shortFallback) result.warnings.push_back("Short spectrum: SWA uses a constant log-mean background");
        } catch (const std::exception& e) {
            for (const auto& key : QStringList{"swa_background", "swa_90", "swa_95", "swa_99", "swa_999", "fdr_05", "fdr_01", "fdr_001", "fdr_0001"}) result.outputs.remove(key);
            result.warnings.push_back("SWA/FDR unavailable: " + QString::fromUtf8(e.what()));
        }
    }
    if (!hasPower && (m_swa || m_ar1)) result.warnings.push_back("Constant signal: background significance is not evaluated");
    spectrum::PeakOptions options; options.minProminence = m_prominence; options.minDistance = m_peakDistance;
    options.relativeProminence=m_relativeProminence;
    options.minWidth=m_peakWidth;options.minHeight=m_peakHeight;options.prominenceRadius=m_peakRadius;
    const auto peaks = spectrum::findSpectrumPeaks(static_cast<int>(spectrum.power.size()), spectrum.power.data(), options);
    TableData table{{"frequency", "period", "power", "prominence", "width_bins", "ar1_95_available", "passes_ar1_95"}, {}};
    const bool hasAr1 = ar1.chiConf.chi95.size() == spectrum.power.size() && !ar1.chiConf.chi95.empty();
    for (const auto& peak : peaks) {
        const double frequencyValue = spectrum.frequency[peak.index];
        if (frequencyValue > 0 && size_t(peak.index) < outputBins) table.rows.push_back({frequencyValue, 1.0 / frequencyValue, peak.height, peak.prominence,
            peak.width, hasAr1 ? 1.0 : 0.0, hasAr1 && peak.height > ar1.chiConf.chi95[peak.index] ? 1.0 : 0.0});
    }
    result.report["peak_count"]=table.rows.size();
    result.report["peak_detection"]=QJsonObject{{"algorithm","prominence_width_distance"},{"minimum_prominence",m_prominence},
        {"relative_prominence_fraction",m_relativeProminence},{"effective_minimum_prominence",spectrum::spectrumProminenceThreshold(int(spectrum.power.size()),spectrum.power.data(),options)},
        {"minimum_distance_bins",m_peakDistance},{"minimum_width_bins",m_peakWidth},{"minimum_power",m_peakHeight},{"prominence_radius_bins",m_peakRadius}};
    result.outputs.insert("peaks", makeData(std::move(table)));
    if (hasAr1 || result.outputs.contains("swa_background"))
        result.warnings.push_back("Acycle-compatible spectral thresholds are nominal approximations, not a guarantee of actual false-positive rates. Local maxima alone do not establish cycles");
    result.report["ar1_available"] = hasAr1;
    result.report["swa_available"] = result.outputs.contains("swa_background");
    return result;
}
#undef MTM_OPTIONAL_PORTS
}
