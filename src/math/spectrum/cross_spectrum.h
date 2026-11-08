// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CROSS_SPECTRUM_H
#define ASTER_CROSS_SPECTRUM_H
#include <cstddef>
#include <exception>
#include <functional>
#include <vector>
namespace aster::numeric
{
namespace CrossSpectrumLimits
{
inline constexpr std::size_t maxSamples = 1000000, maxFft = 262144, maxSegments = 20000;
// Fixed estimate of two FFTs plus segment preparation, not a time/RAM guarantee.
inline constexpr double maxWork = 1e9;
} // namespace CrossSpectrumLimits
enum class CrossWindow
{
    Hann,
    Rectangular
};
enum class CrossDetrend
{
    None,
    Mean,
    Linear
};
struct CrossSpectrumOptions
{
    std::size_t segment = 256, overlap = 128, nfft = 0;
    CrossWindow window = CrossWindow::Hann;
    CrossDetrend detrend = CrossDetrend::Mean;
};
struct CrossSpectrumResult
{
    std::vector<double> frequency, autoA, autoB, real, imaginary, magnitude, coherence,
        phaseDegrees;
    // An undefined value has canonical storage 0 and MUST be read with its mask.
    std::vector<unsigned char> coherenceDefined, phaseDefined;
    std::size_t nfft = 0, segments = 0, unusedTail = 0;
};
struct CrossSpectrumCancelled final : std::exception
{
    const char *what() const noexcept override
    {
        return "Cross spectrum cancelled";
    }
};
// Real, equal-length, already aligned observations. Caller validates X coordinates
// and units. One-sided density, mean(conj(FFT(A))*FFT(B)), complete windows only.
// No interpolation, significance inference or implicit unequal-length padding.
CrossSpectrumResult crossSpectrum(const std::vector<double> &a, const std::vector<double> &b,
                                  double spacing, const CrossSpectrumOptions &options = {},
                                  const std::function<bool()> &cancelled = {},
                                  const std::function<void(int)> &progress = {});
} // namespace aster::numeric
#endif
