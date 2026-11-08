// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/cross_spectrum.h"
#include "math/backend/numeric_backend.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace aster::numeric
{
namespace
{
constexpr double pi = 3.14159265358979323846;
void stop(const std::function<bool()> &cancel)
{
    if (cancel && cancel())
        throw CrossSpectrumCancelled{};
}
struct Sum
{
    double value = 0, correction = 0;
    void add(double v)
    {
        const double y = v - correction, t = value + y;
        correction = (t - value) - y;
        value = t;
    }
};
// Avoid overflow/underflow of intermediate products, including on MSVC where
// long double has the same range as double. A nonzero result rounding to zero
// is diagnosed; callers may explicitly rescale their observations.
double product(double v, double a, double b, double c, double d)
{
    if (v == 0 || a == 0 || b == 0)
        return 0;
    double mantissa = 1;
    int exponent = 0;
    for (double x : {v, a, b, c, d})
    {
        int e = 0;
        mantissa *= std::frexp(x, &e);
        exponent += e;
    }
    const double result = std::ldexp(mantissa, exponent);
    if (!std::isfinite(result) || result == 0)
        throw std::overflow_error(
            "Cross-spectral density is not representable; rescale input units");
    return result;
}
std::vector<double> segment(const std::vector<double> &input, std::size_t start,
                            const std::vector<double> &window, std::size_t nfft, double scale,
                            CrossDetrend trend)
{
    const auto n = window.size();
    std::vector<double> out(nfft, 0);
    if (!scale)
        return out;
    bool constant = true;
    Sum mean, cross, square;
    for (std::size_t i = 0; i < n; ++i)
    {
        out[i] = input[start + i] / scale;
        if (input[start + i] != 0 && out[i] == 0)
            throw std::overflow_error("Input dynamic range exceeds cross-spectrum precision");
        constant = constant && input[start + i] == input[start];
        const double t = 2.0 * i / (n - 1) - 1;
        mean.add(out[i] / n);
        cross.add(t * out[i]);
        square.add(t * t);
    }
    if (constant && trend != CrossDetrend::None)
    {
        std::fill(out.begin(), out.end(), 0);
        return out;
    }
    const double slope = trend == CrossDetrend::Linear ? cross.value / square.value : 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double offset =
            trend == CrossDetrend::None ? 0 : mean.value + slope * (2.0 * i / (n - 1) - 1);
        out[i] = (out[i] - offset) * window[i];
    }
    return out;
}
} // namespace
CrossSpectrumResult crossSpectrum(const std::vector<double> &a, const std::vector<double> &b,
                                  double spacing, const CrossSpectrumOptions &o,
                                  const std::function<bool()> &cancel,
                                  const std::function<void(int)> &progress)
{
    stop(cancel);
    const auto n = a.size(), length = o.segment, nfft = o.nfft ? o.nfft : length;
    if (n != b.size() || n < 8 || length < 8 || length > n || o.overlap >= length ||
        nfft < length || !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(1 / spacing))
        throw std::invalid_argument(
            "Equal input lengths, valid spacing and segment/overlap/FFT sizes required");
    if ((o.window != CrossWindow::Hann && o.window != CrossWindow::Rectangular) ||
        (o.detrend != CrossDetrend::None && o.detrend != CrossDetrend::Mean &&
         o.detrend != CrossDetrend::Linear))
        throw std::invalid_argument("Unknown cross-spectrum window or detrending method");
    if (n > CrossSpectrumLimits::maxSamples || nfft > CrossSpectrumLimits::maxFft)
        throw std::length_error("Cross-spectrum input/FFT budget exceeded");
    const auto step = length - o.overlap, count = 1 + (n - length) / step, bins = nfft / 2 + 1;
    if (count < 2)
        throw std::invalid_argument("Coherence requires at least two complete segments");
    const double work =
        double(count) * (2.0 * nfft * std::ceil(std::log2(double(nfft))) + 4.0 * length);
    if (count > CrossSpectrumLimits::maxSegments || work > CrossSpectrumLimits::maxWork)
        throw std::length_error(
            "Cross-spectrum work budget exceeded; reduce overlap or FFT length");
    const double df = (1 / spacing) / nfft;
    if (!std::isfinite(df) || df <= 0)
        throw std::overflow_error("Frequency grid is not representable");
    // Validate all observations, but scale from the samples actually used by
    // complete windows. A discarded tail must never change the estimate.
    const auto used = (count - 1) * step + length;
    double scaleA = 0, scaleB = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!(i & 4095))
            stop(cancel);
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
            throw std::invalid_argument("Non-finite paired input");
        if (i < used)
        {
            scaleA = std::max(scaleA, std::abs(a[i]));
            scaleB = std::max(scaleB, std::abs(b[i]));
        }
    }
    std::vector<double> window(length, 1);
    Sum energy;
    for (std::size_t i = 0; i < length; ++i)
    {
        if (o.window == CrossWindow::Hann)
            window[i] = .5 - .5 * std::cos(2 * pi * i / length);
        energy.add(window[i] * window[i]);
    }
    std::vector<Sum> aa(bins), bb(bins), re(bins), im(bins);
    for (std::size_t s = 0; s < count; ++s)
    {
        stop(cancel);
        const auto fa =
            backend::realFft(segment(a, s * step, window, nfft, scaleA, o.detrend), nfft);
        stop(cancel);
        const auto fb =
            backend::realFft(segment(b, s * step, window, nfft, scaleB, o.detrend), nfft);
        // An unweighted segment with its fitted intercept removed has exactly
        // zero DC in real arithmetic. Enforce that identity instead of turning
        // floating-point subtraction residuals into an arbitrary coherence.
        // A Hann window changes the weighted mean, so its DC is not suppressed.
        const std::size_t firstBin =
            o.window == CrossWindow::Rectangular && o.detrend != CrossDetrend::None ? 1 : 0;
        for (std::size_t k = firstBin; k < bins; ++k)
        {
            if (!(k & 4095))
                stop(cancel);
            const double powerA = std::norm(fa[k]), powerB = std::norm(fb[k]);
            if ((powerA == 0 && fa[k] != std::complex<double>{}) ||
                (powerB == 0 && fb[k] != std::complex<double>{}))
                throw std::overflow_error(
                    "Normalized spectral power underflow; reduce input dynamic range");
            aa[k].add(powerA / count);
            bb[k].add(powerB / count);
            const auto ab = std::conj(fa[k]) * fb[k];
            re[k].add(ab.real() / count);
            im[k].add(ab.imag() / count);
        }
        if (progress)
            progress(int(95 * (s + 1) / count));
    }
    CrossSpectrumResult out;
    out.nfft = nfft;
    out.segments = count;
    out.unusedTail = n - ((count - 1) * step + length);
    for (auto *v : {&out.frequency, &out.autoA, &out.autoB, &out.real, &out.imaginary,
                    &out.magnitude, &out.coherence, &out.phaseDegrees})
        v->resize(bins);
    out.coherenceDefined.resize(bins);
    out.phaseDefined.resize(bins);
    const double tolerance = 128 * std::numeric_limits<double>::epsilon() *
                             (1 + std::log2(double(nfft)) + std::log2(double(count)));
    for (std::size_t k = 0; k < bins; ++k)
    {
        if (!(k & 4095))
            stop(cancel);
        out.frequency[k] = k * df;
        const double factor = (k == 0 || (nfft % 2 == 0 && k == nfft / 2)) ? 1 : 2;
        const double norm = factor / energy.value;
        out.autoA[k] = product(aa[k].value, scaleA, scaleA, spacing, norm);
        out.autoB[k] = product(bb[k].value, scaleB, scaleB, spacing, norm);
        out.real[k] = product(re[k].value, scaleA, scaleB, spacing, norm);
        out.imaginary[k] = product(im[k].value, scaleA, scaleB, spacing, norm);
        out.magnitude[k] = std::hypot(out.real[k], out.imaginary[k]);
        if (!std::isfinite(out.magnitude[k]))
            throw std::overflow_error("Cross-spectrum magnitude overflow");
        if (aa[k].value > 0 && bb[k].value > 0)
        {
            const double q = std::hypot(re[k].value, im[k].value) / std::sqrt(aa[k].value) /
                             std::sqrt(bb[k].value);
            const double c = q * q;
            if (!std::isfinite(c) || c > 1 + tolerance)
                throw std::overflow_error("Coherence violates Cauchy-Schwarz bound");
            out.coherence[k] = std::min(1.0, c);
            out.coherenceDefined[k] = 1;
        }
        if (re[k].value != 0 || im[k].value != 0)
        {
            out.phaseDegrees[k] = std::atan2(im[k].value, re[k].value) * (180 / pi);
            out.phaseDefined[k] = 1;
        }
    }
    stop(cancel);
    if (progress)
        progress(100);
    stop(cancel);
    return out;
}
} // namespace aster::numeric
