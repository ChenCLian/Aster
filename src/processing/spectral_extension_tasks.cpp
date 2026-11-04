#include "processing/spectral_extension_tasks.h"
#include "processing/task_helpers.h"
#include "processing/task_categories.h"
#include "processing/science_helpers.h"
#include "math/spectrum/lomb_scargle.h"
#include "math/filtering/butterworth.h"
#include <new>
namespace aster
{
namespace
{
template <class Operation>
NodeResult checked(const TaskInput &input, const ExecutionContext &c, const QJsonObject &parameters,
                   bool uniform, Operation operation)
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
    const auto ordered =
        uniform ? science_detail::uniformSpacing(*s, dt) : science_detail::orderedSeries(*s);
    if (!ordered.isEmpty())
        return NodeResult::failure(ordered);
    if (std::size_t(s->y.size()) > numeric::LombLimits::maxSamples)
        return NodeResult::failure(ErrorInfo{"SPECTRAL_EXTENSION_BUDGET",
                                             {{"samples", double(s->y.size())}},
                                             "Input exceeds one million samples"});
    try
    {
        return operation(*s, dt);
    }
    catch (const numeric::LombCancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const filtering::ButterworthCancelled &)
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
            ErrorInfo{"SPECTRAL_EXTENSION_BUDGET", parameters, QString::fromUtf8(e.what())});
    }
    catch (const std::domain_error &e)
    {
        return NodeResult::failure(ErrorInfo{"LOMB_NO_VARIANCE", {}, QString::fromUtf8(e.what())});
    }
    catch (const std::invalid_argument &e)
    {
        return NodeResult::failure(
            ErrorInfo{"SPECTRAL_EXTENSION_PARAMETERS", parameters, QString::fromUtf8(e.what())});
    }
    catch (const std::overflow_error &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::NumericRange, {}, QString::fromUtf8(e.what())));
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(
            ErrorInfo{"SPECTRAL_EXTENSION_FAILED", parameters, QString::fromUtf8(e.what())});
    }
}
TaskDescriptor filterDescriptor(bool high)
{
    auto d = task_detail::seriesTransform(high ? "Butterworth high-pass" : "Butterworth low-pass",
                                          taskCategory(TaskCategory::Filtering));
    d.outputs = {{"filtered", "Filtered series", DataKind::Series, true},
                 {"filter_response", "Amplitude response", DataKind::Series, true}};
    d.defaultOutput = "filtered";
    d.parameters = {
        task_detail::number("butter_cutoff", "Single-pass cutoff frequency", .05, 0, 1e12),
        task_detail::number("butter_order", "Single-pass order", 4, 1,
                            filtering::ButterworthLimits::maxOrder, ParameterKind::Integer)};
    for (auto &p : d.parameters)
        p.group = "butterworth";
    return d;
}
NodeResult filter(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p,
                  bool high)
{
    return checked(
        input, c, p, true,
        [&](const SeriesData &s, double dt)
        {
            const int order = p["butter_order"].toInt();
            const int minimum = 3 * (order + 1) + 1;
            if (s.y.size() < minimum)
                return NodeResult::failure(
                    makeError(ErrorCode::TooFewSamples,
                              {{"actual", double(s.y.size())}, {"minimum", minimum}}));
            const auto cutoff = p["butter_cutoff"].toDouble();
            const auto ratio = 2.L * cutoff * dt;
            if (!std::isfinite(cutoff) || cutoff <= 0 || !std::isfinite(ratio) ||
                ratio < filtering::ButterworthLimits::minimumNyquistRatio ||
                ratio > 1 - filtering::ButterworthLimits::minimumNyquistRatio)
                return NodeResult::failure(ErrorInfo{"BUTTERWORTH_CUTOFF",
                                                     {{"cutoff", cutoff}, {"nyquist", .5 / dt}},
                                                     "Require 0.0001 <= cutoff/Nyquist <= 0.9999"});
            if (c.progress)
                c.progress(5, "progress.butterworth");
            const auto r = filtering::butterworth(
                std::vector<double>(s.y.begin(), s.y.end()), dt, cutoff, order,
                high ? filtering::PassType::High : filtering::PassType::Low,
                [&] { return c.cancellation.isCancelled(); });
            auto filtered = s;
            filtered.y = QVector<double>(r.filtered.begin(), r.filtered.end());
            SeriesData response{
                QVector<double>(r.frequency.begin(), r.frequency.end()),
                QVector<double>(r.gain.begin(), r.gain.end()),
                {"Frequency", s.xAxis.unit.isEmpty() ? QString() : "1/" + s.xAxis.unit},
                {"Forward-backward amplitude gain", "1"}};
            auto result = NodeResult::success({{"filtered", makeData(std::move(filtered))},
                                               {"filter_response", makeData(std::move(response))}},
                                              {{"method", "Butterworth_bilinear_SOS"},
                                               {"type", high ? "highpass" : "lowpass"},
                                               {"sample_interval", dt},
                                               {"single_pass_cutoff", cutoff},
                                               {"single_pass_order", order},
                                               {"combined_order", 2 * order},
                                               {"cutoff_combined_amplitude_gain", .5},
                                               {"boundary", "odd_reflection"},
                                               {"padding_samples_per_end", double(r.padding)},
                                               {"initial_state", "step_steady_state"},
                                               {"phase", "forward_backward_zero_phase"},
                                               {"demeaned", false},
                                               {"resampled", false}});
            result.warnings << "warning.butterworth_boundary" << "warning.butterworth_cutoff";
            if (c.progress)
                c.progress(100, "progress.butterworth");
            return result;
        });
}
} // namespace
TaskDescriptor LombScargleTask::descriptor()
{
    auto d = task_detail::seriesTransform("Lomb-Scargle (floating mean)",
                                          taskCategory(TaskCategory::PowerSpectrum));
    d.outputs = {{"periodogram", "Normalized Lomb-Scargle power", DataKind::Spectrum, true}};
    d.defaultOutput = "periodogram";
    d.parameters = {task_detail::number("ls_min_frequency", "Minimum frequency", 0, 0, 1e12),
                    task_detail::number("ls_max_frequency", "Maximum frequency", 0, 0, 1e12),
                    task_detail::number("ls_samples_per_peak", "Samples per peak", 5, 1, 20,
                                        ParameterKind::Integer)};
    for (auto &p : d.parameters)
        p.group = "lomb_grid";
    return d;
}
NodeResult LombScargleTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return checked(
        input, c, m_parameters, false,
        [&](const SeriesData &s, double)
        {
            if (s.y.size() < 5)
                return NodeResult::failure(makeError(
                    ErrorCode::TooFewSamples, {{"minimum", 5}, {"actual", double(s.y.size())}}));
            numeric::LombOptions o;
            o.minimumFrequency = m_parameters["ls_min_frequency"].toDouble();
            o.maximumFrequency = m_parameters["ls_max_frequency"].toDouble();
            o.samplesPerPeak = m_parameters["ls_samples_per_peak"].toInt();
            auto r = numeric::lombScargle(
                std::vector<double>(s.x.begin(), s.x.end()),
                std::vector<double>(s.y.begin(), s.y.end()), o,
                [&] { return c.cancellation.isCancelled(); },
                [&](int p)
                {
                    if (c.progress)
                        c.progress(p, "progress.lomb_scargle");
                });
            SpectrumData output{
                QVector<double>(r.frequency.begin(), r.frequency.end()),
                QVector<double>(r.power.begin(), r.power.end()),
                {"Frequency", s.xAxis.unit.isEmpty() ? QString() : "1/" + s.xAxis.unit},
                {"Normalized Lomb-Scargle power", "1"}};
            auto result =
                NodeResult::success({{"periodogram", makeData(std::move(output))}},
                                    {{"method", "generalized_Lomb_Scargle"},
                                     {"fit", "cos_sin_floating_intercept"},
                                     {"weights", "equal"},
                                     {"normalization", "(chi0-chi_f)/chi0"},
                                     {"power_unit", "dimensionless"},
                                     {"is_psd", false},
                                     {"confidence_scope", "descriptive_no_FAP"},
                                     {"duration", r.duration},
                                     {"median_spacing", r.medianSpacing},
                                     {"frequency_step", r.frequencyStep},
                                     {"first_frequency", r.frequency.front()},
                                     {"last_frequency", r.frequency.back()},
                                     {"requested_maximum_frequency", r.requestedMaximum},
                                     {"automatic_maximum", o.maximumFrequency == 0},
                                     {"automatic_minimum", o.minimumFrequency == 0},
                                     {"grid_points", double(r.frequency.size())},
                                     {"rank_one_bins", double(r.rankOneBins)},
                                     {"rank_zero_bins", double(r.rankZeroBins)},
                                     {"rank_tolerance", numeric::LombLimits::rankTolerance},
                                     {"resampled", false}});
            result.warnings << "warning.lomb_normalization";
            if (o.maximumFrequency == 0)
                result.warnings << "warning.lomb_auto_frequency";
            if (r.rankOneBins || r.rankZeroBins)
                result.warnings << "warning.lomb_rank";
            return result;
        });
}
TaskDescriptor ButterworthLowpassTask::descriptor()
{
    return filterDescriptor(false);
}
TaskDescriptor ButterworthHighpassTask::descriptor()
{
    return filterDescriptor(true);
}
NodeResult ButterworthLowpassTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return filter(i, c, m_parameters, false);
}
NodeResult ButterworthHighpassTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return filter(i, c, m_parameters, true);
}
} // namespace aster
