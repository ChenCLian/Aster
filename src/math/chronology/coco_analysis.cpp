#include "math/chronology/coco_analysis.h"
#include "math/chronology/coco_detail.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
namespace aster::chronology
{
namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
double uniform(std::mt19937_64 &rng)
{
    return double(rng() >> 11) * 0x1.0p-53;
}
struct Plan
{
    CocoResult geometry;
    CocoOptions options;
    std::size_t samples = 0, cells = 0;
    double dx = 0;
    std::vector<std::size_t> indices;
    std::vector<std::vector<double>> targets;
    Plan(std::size_t n, double spacing, const CocoOptions &o,
         const std::function<bool()> &cancelled)
        : geometry(coco_detail::grid(n, spacing, o, cancelled)), options(o), samples(n), dx(spacing)
    {
        for (std::size_t i = 0; i < geometry.rows.size(); ++i)
            if (geometry.rows[i].status == CocoStatus::Eligible)
            {
                indices.push_back(i);
                cells += geometry.rows[i].bins + 1;
            }
        if (cells > CocoAnalysisLimits::maxCacheCells)
            throw std::length_error(
                "COCO target cache exceeds 8 million cells; reduce grid/padding");
    }
    void build(const std::function<bool()> &cancelled, const std::function<void(int)> &progress)
    {
        targets.reserve(indices.size());
        for (std::size_t j = 0; j < indices.size(); ++j)
        {
            coco_detail::check(cancelled);
            const auto &r = geometry.rows[indices[j]];
            auto t =
                coco_detail::targetPower(samples, dx, r.rate, geometry.fftSize, options, cancelled);
            t.resize(r.bins + 1);
            targets.push_back(std::move(t));
            if (progress)
                progress(int(100 * (j + 1) / indices.size()));
        }
    }
    std::vector<double> correlations(const std::vector<double> &spectrum,
                                     const std::function<bool()> &cancelled) const
    {
        if (spectrum.size() != geometry.fftSize / 2 + 1)
            throw std::invalid_argument("COCO spectrum size mismatch");
        std::vector<double> r;
        r.reserve(indices.size());
        for (std::size_t j = 0; j < indices.size(); ++j)
        {
            coco_detail::check(cancelled);
            r.push_back(
                coco_detail::correlation(spectrum, targets[j], geometry.rows[indices[j]].bins));
        }
        return r;
    }
    CocoResult describe(const std::vector<double> &spectrum,
                        const std::vector<double> &scores) const
    {
        auto r = geometry;
        bool found = false;
        std::size_t best = 0;
        for (std::size_t j = 0; j < indices.size(); ++j)
        {
            auto &row = r.rows[indices[j]];
            row.correlation = scores[j];
            if (!std::isfinite(scores[j]))
            {
                row.status = CocoStatus::NoVariance;
                continue;
            }
            if (!found || scores[j] > scores[best])
            {
                found = true;
                best = j;
                r.bestRow = indices[j];
            }
        }
        if (!found)
            throw std::domain_error("No nonconstant COCO comparison spectrum");
        const auto &row = r.rows[r.bestRow];
        r.frequency.resize(row.bins);
        for (std::size_t k = 0; k < row.bins; ++k)
            r.frequency[k] = (k + 1) * row.rate / (100 * double(r.fftSize) * dx);
        r.observedPower.assign(spectrum.begin() + 1, spectrum.begin() + row.bins + 1);
        r.targetPower.assign(targets[best].begin() + 1, targets[best].end());
        coco_detail::normalize(r.observedPower);
        coco_detail::normalize(r.targetPower);
        return r;
    }
};
std::vector<double> evidence(std::vector<double> correlations)
{
    // Defined on the entire fixed geometric family. Empty/constant comparison
    // bands carry no positive matching evidence, including in null replicates.
    for (auto &v : correlations)
        v = std::isfinite(v) ? std::max(0., v) : 0.;
    return correlations;
}
std::size_t peakCount(const std::vector<double> &p)
{
    std::size_t count = 0;
    for (std::size_t i = 1; i + 1 < p.size();)
    {
        std::size_t end = i;
        while (end + 1 < p.size() && p[end + 1] == p[i])
            ++end;
        if (end + 1 < p.size() && p[i] > 0 && p[i] > p[i - 1] && p[i] > p[end + 1])
            ++count;
        i = end + 1;
    }
    return count;
}
void budget(long double work)
{
    if (!std::isfinite(work) || work > CocoAnalysisLimits::maxEvaluationWork)
        throw std::length_error(
            "COCO simulation/window work limit exceeded; reduce simulations/windows or rate grid");
}
} // namespace
std::vector<double> cocoAr1(std::size_t n, double rho, std::mt19937_64 &rng)
{
    if (n < 1 || n > CocoAnalysisLimits::maxInputSamples || !std::isfinite(rho) ||
        std::abs(rho) >= 1)
        throw std::invalid_argument(
            "AR1 generator requires finite |rho|<1 and bounded positive sample count");
    std::vector<double> y(n);
    double spare = 0;
    bool have = false;
    const auto normal = [&]()
    {
        if (have)
        {
            have = false;
            return spare;
        }
        const double radius = std::sqrt(-2 * std::log(1 - uniform(rng))),
                     angle = 2 * pi * uniform(rng);
        spare = radius * std::sin(angle);
        have = true;
        return radius * std::cos(angle);
    };
    y[0] = normal();
    const double scale = std::sqrt((1 - rho) * (1 + rho));
    for (std::size_t i = 1; i < n; ++i)
        y[i] = rho * y[i - 1] + scale * normal();
    return y;
}
std::vector<double> cocoRandomPeaks(std::size_t n, std::size_t fft, std::size_t peaks,
                                    std::mt19937_64 &rng)
{
    if (n < 16 || n > CocoLimits::maxSamples || fft < n || fft > CocoLimits::maxFft || fft % 2 ||
        fft > 8 * n || peaks < 1 || peaks > CocoAnalysisLimits::maxPeaks)
        throw std::invalid_argument("Invalid random-peak dimensions or count");
    std::vector<double> p(fft / 2 + 1, 0);
    const double width = 2. * fft / n;
    // Frequencies expressed in FFT-bin units; support is two Rayleigh widths.
    // Entire support fits [0, Nyquist]; starts continuous uniform, weights U[0,1).
    for (std::size_t j = 0; j < peaks; ++j)
    {
        const double start = uniform(rng) * (fft / 2. - width), height = uniform(rng);
        const auto first = std::size_t(std::ceil(start)),
                   last = std::min(fft / 2, std::size_t(std::floor(start + width)));
        for (auto k = first; k <= last; ++k)
            p[k] += height * std::max(0., std::sin(pi * (k - start) / width));
    }
    return p;
}
CocoMonteCarloResult cocoMonteCarlo(const std::vector<double> &y, double dx, const CocoOptions &o,
                                    const CocoMonteCarloOptions &mc,
                                    const std::function<bool()> &cancelled,
                                    const std::function<void(int)> &progress)
{
    coco_detail::check(cancelled);
    if (mc.simulations < 19 || mc.simulations > CocoAnalysisLimits::maxSimulations ||
        !std::isfinite(mc.alpha) || mc.alpha <= 0 || mc.alpha >= 1 ||
        1. / (mc.simulations + 1) > mc.alpha || !std::isfinite(mc.rho) || std::abs(mc.rho) >= 1 ||
        mc.peakCount > CocoAnalysisLimits::maxPeaks ||
        (mc.nullModel != CocoMonteCarloOptions::Null::FixedAr1 &&
         mc.nullModel != CocoMonteCarloOptions::Null::RandomPeaks))
        throw std::invalid_argument(
            "Invalid COCO Monte Carlo settings; require 19<=B<=9999, 1/(B+1)<=alpha<1 and |rho|<1");
    Plan plan(y.size(), dx, o, cancelled);
    budget(
        static_cast<long double>(mc.simulations) *
        (plan.cells * 3 + plan.geometry.fftSize * (std::log2(double(plan.geometry.fftSize)) + 1)));
    const auto observed = coco_detail::observedPower(y, plan.geometry.fftSize, o.detrend);
    CocoMonteCarloResult result;
    result.cacheCells = plan.cells;
    if (mc.nullModel == CocoMonteCarloOptions::Null::RandomPeaks)
    {
        result.peaks = mc.peakCount ? mc.peakCount : peakCount(observed);
        if (!result.peaks)
            throw std::domain_error("No positive spectral peaks for the random-peak reference");
        if (result.peaks > CocoAnalysisLimits::maxPeaks)
            throw std::length_error("Random-peak count exceeds 8192");
        budget(static_cast<long double>(mc.simulations) *
               (plan.cells * 3 + result.peaks * (2. * plan.geometry.fftSize / y.size() + 3)));
    }
    plan.build(cancelled,
               [&](int p)
               {
                   if (progress)
                       progress(p / 5);
               });
    const auto raw = plan.correlations(observed, cancelled);
    result.observed = plan.describe(observed, raw);
    result.evidence = evidence(raw);
    for (auto i : plan.indices)
        result.rates.push_back(plan.geometry.rows[i].rate);
    std::vector<std::size_t> exceed(raw.size(), 0);
    std::mt19937_64 rng(mc.seed);
    // The helper uses one fixed family and conservative near ties; pointwise
    // counts use exactly the same upper-tail convention. Never drop a replicate.
    result.maximum = numeric::monteCarloMaximumTest(
        result.evidence, mc.simulations, mc.alpha,
        [&](std::size_t j)
        {
            coco_detail::check(cancelled);
            auto p = mc.nullModel == CocoMonteCarloOptions::Null::FixedAr1
                         ? coco_detail::observedPower(cocoAr1(y.size(), mc.rho, rng),
                                                      plan.geometry.fftSize, o.detrend)
                         : cocoRandomPeaks(y.size(), plan.geometry.fftSize, result.peaks, rng);
            auto nullScores = evidence(plan.correlations(p, cancelled));
            for (std::size_t k = 0; k < raw.size(); ++k)
                if (nullScores[k] >=
                    result.evidence[k] -
                        100 * std::numeric_limits<double>::epsilon() * std::abs(result.evidence[k]))
                    ++exceed[k];
            if (progress)
                progress(20 + int(80 * (j + 1) / mc.simulations));
            return nullScores;
        },
        [&]
        {
            coco_detail::check(cancelled);
            return false;
        });
    for (auto n : exceed)
        result.pointwiseP.push_back((1. + n) / (1. + mc.simulations));
    coco_detail::check(cancelled);
    return result;
}
CocoEvolutionResult cocoEvolution(const std::vector<double> &y, double dx, const CocoOptions &o,
                                  const CocoEvolutionOptions &e,
                                  const std::function<bool()> &cancelled,
                                  const std::function<void(int)> &progress)
{
    coco_detail::check(cancelled);
    if (y.size() > CocoAnalysisLimits::maxInputSamples)
        throw std::length_error("eCOCO input exceeds one million samples");
    if (e.windowSamples < 16 || e.windowSamples > y.size() ||
        e.windowSamples > CocoLimits::maxSamples || e.stepSamples < 1 ||
        e.stepSamples > e.windowSamples)
        throw std::invalid_argument("eCOCO requires 16<=window<=input and 1<=step<=window");
    const auto windows = 1 + (y.size() - e.windowSamples) / e.stepSamples;
    if (windows > CocoAnalysisLimits::maxWindows)
        throw std::length_error("eCOCO exceeds 512 full windows; increase step");
    Plan plan(e.windowSamples, dx, o, cancelled);
    if (plan.indices.size() < 2 || windows < 2)
        throw std::invalid_argument("eCOCO needs at least two eligible rates and two windows; use "
                                    "COCO scan for one window");
    if (windows * plan.indices.size() > CocoAnalysisLimits::maxMapCells)
        throw std::length_error("eCOCO map exceeds cell budget");
    budget(
        static_cast<long double>(windows) *
        (plan.cells * 3 + plan.geometry.fftSize * (std::log2(double(plan.geometry.fftSize)) + 1)));
    if (std::any_of(y.begin(), y.end(), [](double v) { return !std::isfinite(v); }))
        throw std::invalid_argument("Non-finite eCOCO input");
    plan.build(cancelled,
               [&](int p)
               {
                   if (progress)
                       progress(p / 5);
               });
    CocoEvolutionResult result;
    result.fftSize = plan.geometry.fftSize;
    result.cacheCells = plan.cells;
    result.rateGrid = plan.geometry.rows;
    for (auto i : plan.indices)
        result.rates.push_back(plan.geometry.rows[i].rate);
    result.correlation.resize(windows * result.rates.size());
    result.unusedTail = y.size() - ((windows - 1) * e.stepSamples + e.windowSamples);
    for (std::size_t w = 0; w < windows; ++w)
    {
        coco_detail::check(cancelled);
        const auto first = w * e.stepSamples;
        std::vector<double> window(y.begin() + first, y.begin() + first + e.windowSamples);
        std::vector<double> scores;
        try
        {
            const auto p = coco_detail::observedPower(window, plan.geometry.fftSize, o.detrend);
            scores = plan.correlations(p, cancelled);
            if (std::any_of(scores.begin(), scores.end(),
                            [](double x) { return !std::isfinite(x); }))
                throw std::domain_error("constant comparison band");
        }
        catch (const std::domain_error &err)
        {
            throw std::domain_error("eCOCO window " + std::to_string(w + 1) + " (first sample " +
                                    std::to_string(first + 1) + "): " + err.what());
        }
        result.centers.push_back(first + (e.windowSamples - 1) / 2.);
        const auto best =
            std::size_t(std::max_element(scores.begin(), scores.end()) - scores.begin());
        result.bestRate.push_back(result.rates[best]);
        result.bestCorrelation.push_back(scores[best]);
        for (std::size_t k = 0; k < scores.size(); ++k)
            result.correlation[k * windows + w] = scores[k];
        if (progress)
            progress(20 + int(80 * (w + 1) / windows));
    }
    coco_detail::check(cancelled);
    return result;
}
} // namespace aster::chronology
