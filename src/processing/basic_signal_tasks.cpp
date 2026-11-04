#include "processing/basic_signal_tasks.h"
#include "processing/task_helpers.h"
#include "processing/task_categories.h"
#include "processing/science_helpers.h"
#include "math/smoothing/smoothing.h"
#include "math/spectrum/evolutionary_spectrum.h"
#include <new>
namespace aster
{
namespace
{
template <class Operation>
NodeResult checked(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p,
                   bool uniform, Operation operation)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    if (!dataAs<SeriesData>(input.data))
        return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    const auto &s = *dataAs<SeriesData>(input.data);
    double dt = 0;
    const auto error =
        uniform ? science_detail::uniformSpacing(s, dt) : science_detail::orderedSeries(s);
    if (!error.isEmpty())
        return NodeResult::failure(error);
    if (std::size_t(s.y.size()) > numeric::SmoothingLimits::maxSamples)
        return NodeResult::failure(ErrorInfo{"SIGNAL_BUDGET",
                                             {{"samples", double(s.y.size())}},
                                             "Input exceeds one million samples"});
    try
    {
        return operation(s, dt);
    }
    catch (const numeric::SmoothingCancelled &)
    {
        return NodeResult::cancelled();
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
        return NodeResult::failure(ErrorInfo{"SIGNAL_BUDGET", p, QString::fromUtf8(e.what())});
    }
    catch (const std::invalid_argument &e)
    {
        return NodeResult::failure(ErrorInfo{"SIGNAL_PARAMETERS", p, QString::fromUtf8(e.what())});
    }
    catch (const std::overflow_error &e)
    {
        return NodeResult::failure(
            makeError(ErrorCode::NumericRange, {}, QString::fromUtf8(e.what())));
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(ErrorInfo{"SIGNAL_FAILED", p, QString::fromUtf8(e.what())});
    }
}
TaskDescriptor smoothDescriptor(bool sg)
{
    auto d = task_detail::seriesTransform(sg ? "Savitzky-Golay smoothing" : "Moving average",
                                          taskCategory(TaskCategory::Smoothing));
    d.parameters = {task_detail::number("smooth_window", "Window points", 11, 1,
                                        numeric::SmoothingLimits::maxWindow,
                                        ParameterKind::Integer)};
    if (sg)
        d.parameters.push_back(task_detail::number("smooth_degree", "Polynomial degree", 2, 0,
                                                   numeric::SmoothingLimits::maxDegree,
                                                   ParameterKind::Integer));
    for (auto &p : d.parameters)
        p.group = "smoothing";
    return d;
}
NodeResult smooth(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p, bool sg)
{
    return checked(input, c, p, sg,
                   [&](const SeriesData &s, double)
                   {
                       auto y = numeric::smoothSeries(std::vector<double>(s.y.begin(), s.y.end()),
                                                      p["smooth_window"].toInt(),
                                                      sg ? p["smooth_degree"].toInt() : 0,
                                                      sg ? numeric::SmoothingMethod::SavitzkyGolay
                                                         : numeric::SmoothingMethod::MovingMean,
                                                      [&] { return c.cancellation.isCancelled(); });
                       auto output = s;
                       output.y = QVector<double>(y.begin(), y.end());
                       auto result = NodeResult::success(
                           {{"series", makeData(std::move(output))}},
                           {{"window_points", p["smooth_window"]},
                            {"polynomial_degree", sg ? p["smooth_degree"] : QJsonValue(0)},
                            {"boundary", sg ? "polynomial_fit_first_last_full_window"
                                            : "clipped_centered_window"},
                            {"window_domain", "sample_index"},
                            {"resampled", false},
                            {"samples", double(s.y.size())}});
                       result.warnings << "warning.smoothing_changes_signal";
                       if (!sg)
                       {
                           double dt = 0;
                           if (!science_detail::uniformSpacing(s, dt).isEmpty())
                               result.warnings << "warning.smoothing_point_window";
                       }
                       return result;
                   });
}
TaskDescriptor psdDescriptor(bool welch)
{
    auto d = task_detail::seriesTransform(welch ? "Welch power spectrum" : "FFT power spectrum",
                                          taskCategory(TaskCategory::PowerSpectrum));
    d.outputs = {{"spectrum", "Power spectral density", DataKind::Spectrum, true}};
    d.defaultOutput = "spectrum";
    if (welch)
        d.parameters = {task_detail::number("psd_window", "Segment points", 256, 8,
                                            numeric::EvolutionLimits::maxFftSamples,
                                            ParameterKind::Integer),
                        task_detail::number("psd_overlap", "Overlap points", 128, 0,
                                            numeric::EvolutionLimits::maxFftSamples - 1,
                                            ParameterKind::Integer)};
    d.parameters += QVector<ParameterSpec>{
        task_detail::number("psd_nfft", "FFT points", 0, 0, numeric::EvolutionLimits::maxFftSamples,
                            ParameterKind::Integer),
        science_detail::text("psd_taper", "Window", "hann", {"hann", "rectangular"}),
        science_detail::text("psd_trend", "Detrending", "mean", {"mean", "linear"})};
    for (auto &p : d.parameters)
        p.group = p.key == "psd_window" || p.key == "psd_overlap" ? "welch_segments"
                                                                  : "spectral_estimator";
    return d;
}
NodeResult psd(const TaskInput &input, const ExecutionContext &c, const QJsonObject &p, bool welch)
{
    return checked(
        input, c, p, true,
        [&](const SeriesData &s, double dt)
        {
            const auto length =
                welch ? std::size_t(p["psd_window"].toInt()) : std::size_t(s.y.size());
            const auto overlap = welch ? std::size_t(p["psd_overlap"].toInt()) : 0;
            if (overlap >= length)
                throw std::invalid_argument("Welch overlap must be smaller than segment length");
            numeric::EvolutionOptions o;
            o.method = numeric::EvolutionMethod::Fft;
            o.windowSamples = length;
            o.stepSamples = length - overlap;
            o.fftLength = p["psd_nfft"].toInt();
            o.window = p["psd_taper"].toString() == "rectangular" ? numeric::FftWindow::Rectangular
                                                                  : numeric::FftWindow::Hann;
            o.trend = p["psd_trend"].toString() == "linear" ? numeric::SegmentTrend::Linear
                                                            : numeric::SegmentTrend::Mean;
            const auto r = numeric::evolutionarySpectrum(
                std::vector<double>(s.y.begin(), s.y.end()), dt, o,
                [&] { return c.cancellation.isCancelled(); },
                [&](int n)
                {
                    if (c.progress)
                        c.progress(n, "progress.power_spectrum");
                },
                false);
            SpectrumData spectrum{
                QVector<double>(r.frequency.begin(), r.frequency.end()),
                QVector<double>(r.meanPower.begin(), r.meanPower.end()),
                {"Frequency", s.xAxis.unit.isEmpty() ? QString() : "1/" + s.xAxis.unit},
                {"Power spectral density",
                 s.xAxis.unit.isEmpty() || s.yAxis.unit.isEmpty()
                     ? QString()
                     : "(" + s.yAxis.unit + ")^2 * (" + s.xAxis.unit + ")"}};
            auto result =
                NodeResult::success({{"spectrum", makeData(std::move(spectrum))}},
                                    {{"estimator", welch ? "Welch_mean" : "FFT_periodogram"},
                                     {"spacing", dt},
                                     {"segment_points", double(length)},
                                     {"overlap_points", double(overlap)},
                                     {"segments", double(r.centers.size())},
                                     {"nfft", double(r.fftLength)},
                                     {"unused_tail_samples", double(r.unusedTail)},
                                     {"taper", p["psd_taper"]},
                                     {"detrending", p["psd_trend"]},
                                     {"normalization", "one_sided_physical_PSD"},
                                     {"average", "arithmetic_mean"},
                                     {"confidence_scope", "descriptive_no_significance_test"},
                                     {"resampled", false},
                                     {"stored_time_frequency_map", false}});
            result.warnings << "warning.psd_descriptive";
            return result;
        });
}
} // namespace
TaskDescriptor MovingAverageTask::descriptor()
{
    return smoothDescriptor(false);
}
TaskDescriptor SavitzkyGolayTask::descriptor()
{
    return smoothDescriptor(true);
}
TaskDescriptor FftSpectrumTask::descriptor()
{
    return psdDescriptor(false);
}
TaskDescriptor WelchSpectrumTask::descriptor()
{
    return psdDescriptor(true);
}
NodeResult MovingAverageTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return smooth(i, c, m_parameters, false);
}
NodeResult SavitzkyGolayTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return smooth(i, c, m_parameters, true);
}
NodeResult FftSpectrumTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return psd(i, c, m_parameters, false);
}
NodeResult WelchSpectrumTask::execute(const TaskInput &i, const ExecutionContext &c)
{
    return psd(i, c, m_parameters, true);
}
} // namespace aster
