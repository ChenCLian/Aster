// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_EVOLUTIONARY_SPECTRUM_H
#define ASTER_MATH_EVOLUTIONARY_SPECTRUM_H
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>
#include <exception>

namespace aster::numeric
{
namespace EvolutionLimits
{
inline constexpr std::size_t maxInputSamples = 1000000;
inline constexpr std::size_t maxFftSamples = 262144;
inline constexpr std::size_t maxWindows = 20000;
inline constexpr std::size_t maxCells = 2000000;
inline constexpr long double maxWorkTerms = 100000000.L;
} // namespace EvolutionLimits
enum class EvolutionMethod
{
    Fft,
    Mtm
};
enum class FftWindow
{
    Hann,
    Rectangular
};
enum class SegmentTrend
{
    Mean,
    Linear
};
struct EvolutionOptions
{
    EvolutionMethod method = EvolutionMethod::Fft;
    FftWindow window = FftWindow::Hann;
    SegmentTrend trend = SegmentTrend::Mean;
    std::size_t windowSamples = 128, stepSamples = 16, fftLength = 0;
    double nw = 2, maximumFrequencyRatio = 1;
    // Absolute output limit takes precedence; zero retains automatic/legacy ratio.
    double maximumFrequency = 0;
    bool normalizeWindows = false;
};
struct EvolutionResult
{
    std::vector<double> centers, frequency, power, meanPower;
    // power[frequencyRow * centers.size() + windowColumn].
    std::size_t unusedTail = 0, fftLength = 0;
    int unconvergedWindows = 0;
    std::size_t zeroPowerWindows = 0;
};
struct EvolutionCancelled final : std::exception
{
    const char *what() const noexcept override
    {
        return "Evolutionary spectrum cancelled";
    }
};
// Full windows only, centered on the first/last observed sample. No implicit
// resampling, edge padding or significance testing. Optional normalization divides
// each PSD by its full one-sided maximum BEFORE output cropping. All-zero windows
// remain zero. meanPower averages the same (raw or normalized) window spectra.
// retainMap=false returns only window centers and meanPower, without allocating
// a time-frequency map (used for streaming Welch averaging).
// PSD is one-sided with units Y^2 * X. FFT uses a periodic Hann or boxcar.
EvolutionResult evolutionarySpectrum(const std::vector<double> &y, double spacing,
                                     const EvolutionOptions &options,
                                     const std::function<bool()> &cancelled = {},
                                     const std::function<void(int)> &progress = {},
                                     bool retainMap = true);
} // namespace aster::numeric
#endif
