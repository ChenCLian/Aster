// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/evolutionary_spectrum.h"
#include "math/spectrum/mtm_spectrum.h"
#include "math/backend/numeric_backend.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aster::numeric
{
namespace
{
constexpr double pi = 3.14159265358979323846;
void stop(const std::function<bool()> &cancel)
{
    if (cancel && cancel())
        throw EvolutionCancelled{};
}
void removeTrend(std::vector<double> &y, SegmentTrend trend)
{
    // Normalized coordinates/values also work where long double == double.
    double magnitude = 0;
    for (double v : y)
        magnitude = std::max(magnitude, std::abs(v));
    if (magnitude == 0)
        return;
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (*bounds.first == *bounds.second)
    {
        std::fill(y.begin(), y.end(), 0);
        return;
    }
    long double mean = 0, cross = 0, square = 0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        const long double t = 2.L * i / (y.size() - 1) - 1;
        mean += (y[i] / magnitude) / static_cast<long double>(y.size());
        cross += t * (y[i] / magnitude);
        square += t * t;
    }
    const long double slope = trend == SegmentTrend::Linear ? cross / square : 0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        y[i] = static_cast<double>(
            (y[i] / magnitude - mean - slope * (2.L * i / (y.size() - 1) - 1)) * magnitude);
        if (!std::isfinite(y[i]))
            throw std::overflow_error("Segment detrending overflow");
    }
}
} // namespace
EvolutionResult evolutionarySpectrum(const std::vector<double> &y, double dt,
                                     const EvolutionOptions &o, const std::function<bool()> &cancel,
                                     const std::function<void(int)> &progress, bool retainMap)
{
    stop(cancel);
    const auto length = o.windowSamples;
    const auto nfft = o.fftLength ? o.fftLength : length;
    if (y.size() < 8 || length < 8 || length > y.size() || !o.stepSamples ||
        o.stepSamples > length || nfft < length || !std::isfinite(dt) || dt <= 0 ||
        !std::isfinite(1 / dt) || !std::isfinite(o.maximumFrequencyRatio) ||
        o.maximumFrequencyRatio <= 0 || o.maximumFrequencyRatio > 1 ||
        !std::isfinite(o.maximumFrequency) || o.maximumFrequency < 0)
        throw std::invalid_argument("Invalid window, step, FFT length or sampling interval");
    if (o.method != EvolutionMethod::Fft && o.method != EvolutionMethod::Mtm)
        throw std::invalid_argument("Unknown spectral estimator");
    if (o.trend != SegmentTrend::Mean && o.trend != SegmentTrend::Linear)
        throw std::invalid_argument("Unknown segment detrending method");
    if (o.window != FftWindow::Hann && o.window != FftWindow::Rectangular)
        throw std::invalid_argument("Unknown FFT taper");
    if (y.size() > EvolutionLimits::maxInputSamples || nfft > EvolutionLimits::maxFftSamples)
        throw std::length_error("Input or FFT length exceeds evolutionary spectrum budget");
    int k = 1;
    if (o.method == EvolutionMethod::Mtm)
    {
        if (!std::isfinite(o.nw) || o.nw < 1 || o.nw > 32 || o.nw >= length / 2. ||
            (o.nw != 1 && o.nw < 1.25))
            throw std::invalid_argument("MTM requires NW=1 or 1.25<=NW<=32, and NW<window/2");
        k = o.nw == 1 ? 2 : int(std::lround(2 * o.nw)) - 1;
    }
    const std::size_t windows = 1 + (y.size() - length) / o.stepSamples;
    const double fs = 1 / dt, df = fs / nfft;
    if (!std::isfinite(df) || df <= 0)
        throw std::overflow_error("Frequency grid cannot be represented");
    std::size_t bins = 0;
    const double maximum = o.maximumFrequency > 0 ? std::min(o.maximumFrequency, .5 * fs)
                                                  : .5 * fs * o.maximumFrequencyRatio;
    while (bins < nfft / 2 + 1 && bins * df <= maximum + df * 1e-10)
        ++bins;
    if (bins < 2)
        throw std::invalid_argument("Requested frequency band contains fewer than two bins");
    if (windows > EvolutionLimits::maxWindows || (retainMap && windows > EvolutionLimits::maxCells / bins) ||
        static_cast<long double>(windows) * nfft * k > EvolutionLimits::maxWorkTerms)
        throw std::length_error(
            "Too many windows/cells; enlarge step or reduce input/window/FFT length");
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        if (!std::isfinite(y[i]))
            throw std::invalid_argument("Non-finite input value");
    }
    EvolutionResult out;
    out.fftLength = nfft;
    out.unusedTail = y.size() - ((windows - 1) * o.stepSamples + length);
    out.centers.resize(windows);
    out.frequency.resize(bins);
    if (retainMap) out.power.resize(windows * bins);
    out.meanPower.assign(bins, 0);
    for (std::size_t f = 0; f < bins; ++f)
        out.frequency[f] = f * df;
    std::shared_ptr<const MtmTaperPlan> plan;
    if (o.method == EvolutionMethod::Mtm)
        plan = makeMtmTaperPlan(length, o.nw, k, cancel);
    std::vector<double> taper(length, 1);
    long double windowEnergy = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        if (o.window == FftWindow::Hann)
            taper[i] = .5 - .5 * std::cos(2 * pi * i / length);
        windowEnergy += taper[i] * taper[i];
    }
    for (std::size_t w = 0; w < windows; ++w)
    {
        stop(cancel);
        const auto first = w * o.stepSamples;
        out.centers[w] = (first + (length - 1) / 2.) * dt;
        if (!std::isfinite(out.centers[w]))
            throw std::overflow_error("Window position overflow");
        std::vector<double> segment(y.begin() + first, y.begin() + first + length);
        // Existing MTM always removes the mean. Preserve its exact mean-only
        // path; optional local linear detrending is performed here explicitly.
        if (o.method == EvolutionMethod::Fft || o.trend == SegmentTrend::Linear)
            removeTrend(segment, o.trend);
        std::vector<double> psd;
        if (o.method == EvolutionMethod::Mtm)
        {
            auto result = mtmSpectrum(segment, fs, o.nw, k, fs / 2, cancel, nfft,
                                      MtmConvergenceDomain::FullSpectrum, plan);
            psd = std::move(result.power);
            if (!result.converged)
                ++out.unconvergedWindows;
        }
        else
        {
            std::vector<double> buffer(nfft, 0);
            for (std::size_t i = 0; i < length; ++i)
                buffer[i] = segment[i] * taper[i];
            const auto transform = backend::realFft(std::move(buffer), nfft, 1);
            psd.resize(o.normalizeWindows ? nfft / 2 + 1 : bins);
            for (std::size_t f = 0; f < psd.size(); ++f)
            {
                const auto z = transform[f];
                const long double r = z.real(), im = z.imag();
                const int factor = (f == 0 || (nfft % 2 == 0 && f == nfft / 2)) ? 1 : 2;
                psd[f] = static_cast<double>(((r * r + im * im) / windowEnergy) * dt * factor);
                if (psd[f] == 0 && (r != 0 || im != 0))
                    throw std::overflow_error("Spectral power underflow; rescale the input");
            }
        }
        if (o.normalizeWindows)
        {
            double peak = 0;
            for (const double p : psd)
            {
                if (!std::isfinite(p) || p < 0)
                    throw std::overflow_error("Non-finite spectral power; rescale the input");
                peak = std::max(peak, p);
            }
            if (peak > 0)
                for (std::size_t f = 0; f < bins; ++f) psd[f] /= peak;
            else
                ++out.zeroPowerWindows;
        }
        for (std::size_t f = 0; f < bins; ++f)
        {
            if (!std::isfinite(psd[f]) || psd[f] < 0)
                throw std::overflow_error("Non-finite spectral power; rescale the input");
            if (retainMap) out.power[f * windows + w] = psd[f];
            out.meanPower[f] += psd[f] / windows;
            if (!std::isfinite(out.meanPower[f]))
                throw std::overflow_error("Mean power overflow");
        }
        if (progress)
            progress(int(100 * (w + 1) / windows));
    }
    stop(cancel);
    return out;
}
} // namespace aster::numeric
