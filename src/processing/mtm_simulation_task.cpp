#include "processing/task_categories.h"
#include "processing/mtm_simulation_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/spectrum/spectrum_peaks.h"
#include <QJsonArray>
#include <stdexcept>

namespace aster {
TaskDescriptor MtmSimulationTask::descriptor()
{
    TaskDescriptor d;
    d.catalogVisible = false; // Retain archived method identity; not part of the Acycle-aligned catalog.
    d.label = "MTM AR1 simulation test (experimental)";
    d.category = taskCategory(TaskCategory::Significance); d.inputKind = DataKind::Series; d.defaultOutput = "spectrum";
    d.outputs = {{"spectrum", "Power spectrum", DataKind::Spectrum, true},
        {"mc_background", "Time-domain AR1 fit", DataKind::Spectrum, true},
        {"mc_family_threshold", "Experimental family threshold", DataKind::Spectrum, true},
        {"peaks", "Candidate peaks and family-adjusted p", DataKind::Table, true},
        {"mc_test", "Experimental global test", DataKind::Table, true},
        {"mc_null_maxima", "Simulated maximum ratios", DataKind::Table, true}};
    d.parameters = {task_detail::number("nw", "Time-bandwidth NW", 3, 1, 32, ParameterKind::Integer),
        task_detail::number("fmax_ratio", "Maximum frequency / Nyquist", 1, .001, 1),
        task_detail::number("mc_simulations", "Simulation count", 999, 199, 9999, ParameterKind::Integer),
        task_detail::number("mc_alpha", "Family significance level", .05, .0001, .2),
        task_detail::number("mc_seed", "Random seed", 20261101, 0, 2147483647, ParameterKind::Integer),
        science_detail::boolean("mc_estimate_rho", "Estimate rho from input (approximate)", true),
        task_detail::number("mc_fixed_rho", "Independently specified rho (if not estimated)", 0, -.999999, .999999)};
    return d;
}
MtmSimulationTask::MtmSimulationTask(const QJsonObject& p)
{
    m_options.nw = p["nw"].toInt(); m_options.fmaxRatio = p["fmax_ratio"].toDouble();
    m_options.simulations = p["mc_simulations"].toInt(); m_options.alpha = p["mc_alpha"].toDouble();
    m_options.seed = static_cast<std::uint64_t>(p["mc_seed"].toInt());
    m_options.estimateRho = p["mc_estimate_rho"].toBool(); m_options.fixedRho = p["mc_fixed_rho"].toDouble();
}
NodeResult MtmSimulationTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty()) return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    double dt = 0; const auto error = science_detail::uniformSpacing(*source, dt);
    if (!error.isEmpty()) return NodeResult::failure(error);
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    try {
        const auto test = numeric::simulateMtmAr1(std::vector<double>(source->y.begin(), source->y.end()),
            1/dt, m_options, [&] { return context.cancellation.isCancelled(); });
        auto result = NodeResult::success({}, {
            {"experimental", true}, {"statistical_method", "AR1 stationary Gaussian / profile likelihood / MTM maximum-ratio Monte Carlo"},
            {"rho_mode", m_options.estimateRho ? "estimated_parametric_bootstrap" : "independently_specified_conditional_test"},
            {"rho", test.backgroundFit.rho}, {"fitted_mean", test.backgroundFit.mean},
            {"innovation_variance", test.backgroundFit.innovationVariance}, {"sample_spacing", dt},
            {"nw", m_options.nw}, {"tapers", 2*m_options.nw-1}, {"simulations", m_options.simulations},
            {"seed", double(m_options.seed)}, {"random_generator", "SplitMix64 / Box-Muller v1"},
            {"alpha", m_options.alpha}, {"global_p", test.globalP}, {"reject_global_null", test.globalP <= m_options.alpha},
            {"observed_maximum_ratio", test.maximumRatio}, {"critical_ratio", test.criticalRatio},
            {"tested_bin_count", double(test.testedBins.size())}, {"minimum_p", 1.0/(m_options.simulations+1)},
            {"tested_frequency_min", test.spectrum.frequency[test.testedBins.front()]},
            {"tested_frequency_max", test.spectrum.frequency[test.testedBins.back()]},
            {"family_scope", "One input record, selected positive frequency grid; DC/Nyquist excluded. Not across records or repeated analyses"},
            {"parameter_handling", "Every unknown parameter refitted on every simulated series; failed simulations abort the test"},
            {"preprocessing", "Only MTM mean removal. Upstream LOWESS, interpolation, missing-data handling and parameter selection are NOT replayed"},
            {"inference_scope", "Global AR1-null test. Peak adjusted p uses simulated maxima; not FDR or probability that a physical cycle exists"},
            {"validation_reference", "docs/methods/statistical_limits.md"}});
        result.report["numerical_backend"] = science_detail::numericBackendReport();
        auto addSpectrum = [&](const QString& key, const std::vector<double>& f, const std::vector<double>& values) {
            auto output = science_detail::spectrum(QVector<double>(f.begin(), f.end()),
                QVector<double>(values.begin(), values.end()), source->xAxis.unit);
            if (!source->yAxis.unit.isEmpty() && !source->xAxis.unit.isEmpty())
                output.powerAxis.unit = "("+source->yAxis.unit+")^2 * ("+source->xAxis.unit+")";
            auto data = makeData(std::move(output)); const auto problem = validateData(data);
            if (!problem.isEmpty()) throw std::runtime_error(problem.toStdString());
            result.outputs.insert(key, data);
        };
        addSpectrum("spectrum", test.spectrum.frequency, test.spectrum.power);
        addSpectrum("mc_background", test.spectrum.frequency, test.background);
        std::vector<double> frequencies, thresholds;
        for (auto i : test.testedBins) {
            frequencies.push_back(test.spectrum.frequency[i]);
            thresholds.push_back(test.background[i]*test.criticalRatio);
        }
        addSpectrum("mc_family_threshold", frequencies, thresholds);
        spectrum::PeakOptions options;
        const auto peaks = spectrum::findSpectrumPeaks(static_cast<int>(test.spectrum.power.size()), test.spectrum.power.data(), options);
        TableData table{{"frequency", "period", "power", "family_adjusted_p", "passes_family_test"}, {}};
        for (const auto& peak : peaks) {
            const auto i = static_cast<size_t>(peak.index);
            if (!std::binary_search(test.testedBins.begin(), test.testedBins.end(), i)) continue;
            const double f = test.spectrum.frequency[i];
            table.rows.push_back({f, 1/f, peak.height, test.adjustedP[i], test.adjustedP[i] <= m_options.alpha ? 1.0 : 0.0});
        }
        result.outputs.insert("peaks", makeData(std::move(table)));
        result.outputs.insert("mc_test", makeData(TableData{{"global_p", "alpha", "reject_global_null", "rho", "rho_estimated", "simulations", "seed"},
            {{test.globalP, m_options.alpha, test.globalP <= m_options.alpha ? 1.0 : 0.0, test.backgroundFit.rho,
              m_options.estimateRho ? 1.0 : 0.0, double(m_options.simulations), double(m_options.seed)}}}));
        TableData maxima{{"replicate", "maximum_ratio"}, {}};
        for (size_t i = 0; i < test.nullMaxima.size(); ++i) maxima.rows.push_back({double(i+1), test.nullMaxima[i]});
        result.outputs.insert("mc_null_maxima", makeData(std::move(maxima)));
        result.warnings.push_back("Experimental AR1 simulation test: a rejected noise model is not proof of an astronomical cycle. The test covers one record and one preselected frequency family only");
        if (m_options.estimateRho)
            result.warnings.push_back("Estimated-rho bootstrap is approximate; short strongly correlated records remain miscalibrated in validation. Do not interpret alpha as guaranteed error control");
        else
            result.warnings.push_back("Conditional test requires rho specified independently of this record. Copying an estimate from this same record does not make rho known");
        result.warnings.push_back("This task does not replay upstream processing. LOWESS, interpolation, gaps, nonstationarity and non-Gaussian noise can invalidate the AR1 null; no preprocessing is inserted automatically");
        return result;
    } catch (const std::exception& e) {
        return context.cancellation.isCancelled() ? NodeResult::cancelled()
            : NodeResult::failure(makeError(ErrorCode::MtmSimulationFailure,
                {{"samples", double(source->y.size())}, {"simulations", m_options.simulations}, {"alpha", m_options.alpha}}, QString::fromUtf8(e.what())));
    }
}
}
