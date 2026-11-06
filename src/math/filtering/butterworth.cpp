// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/filtering/butterworth.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
namespace aster::filtering
{
namespace
{
constexpr long double pi = 3.1415926535897932384626433832795L;
struct Section
{
    long double b0, b1, b2, a1, a2;
};
void stop(const std::function<bool()> &c)
{
    if (c && c())
        throw ButterworthCancelled{};
}
std::vector<Section> design(int order, long double k, PassType type)
{
    std::vector<Section> sections;
    if (order % 2)
    {
        const long double b = type == PassType::Low ? k / (1 + k) : 1 / (1 + k);
        sections.push_back({b, type == PassType::Low ? b : -b, 0, (k - 1) / (k + 1), 0});
    }
    // Low-Q sections first; each section has unity gain in its passband.
    for (int j = order / 2 - 1; j >= 0; --j)
    {
        const long double inverseQ = 2 * std::sin((2 * j + 1) * pi / (2 * order));
        const long double d = 1 + k * inverseQ + k * k;
        const long double b = type == PassType::Low ? k * k / d : 1 / d;
        sections.push_back({b, type == PassType::Low ? 2 * b : -2 * b, b, 2 * (k * k - 1) / d,
                            (1 - k * inverseQ + k * k) / d});
    }
    return sections;
}
void forward(std::vector<long double> &y, const std::vector<Section> &sections, PassType type,
             const std::function<bool()> &cancel)
{
    long double initial = y.front();
    const long double dc = type == PassType::Low ? 1 : 0;
    for (const auto &s : sections)
    {
        stop(cancel);
        long double z0 = (dc - s.b0) * initial, z1 = (s.b2 - s.a2 * dc) * initial;
        initial *= dc;
        for (std::size_t i = 0; i < y.size(); ++i)
        {
            if ((i & 1023) == 0)
                stop(cancel);
            const auto value = y[i];
            const auto result = s.b0 * value + z0;
            z0 = s.b1 * value - s.a1 * result + z1;
            z1 = s.b2 * value - s.a2 * result;
            y[i] = result;
            if (!std::isfinite(result) || !std::isfinite(z0) || !std::isfinite(z1))
                throw std::overflow_error("Butterworth internal state overflow");
        }
    }
}
} // namespace
ButterworthResult butterworth(const std::vector<double> &y, double dt, double cutoff, int order,
                              PassType type, const std::function<bool()> &cancel)
{
    stop(cancel);
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(cutoff) || cutoff <= 0 || order < 1 ||
        order > ButterworthLimits::maxOrder || (type != PassType::Low && type != PassType::High))
        throw std::invalid_argument("Invalid Butterworth spacing, cutoff, order or pass type");
    const long double ratio = 2 * static_cast<long double>(cutoff) * dt;
    if (!std::isfinite(ratio) || ratio < ButterworthLimits::minimumNyquistRatio ||
        ratio > 1 - ButterworthLimits::minimumNyquistRatio)
        throw std::invalid_argument("Cutoff/Nyquist must be between 0.0001 and 0.9999");
    if (y.size() > ButterworthLimits::maxSamples)
        throw std::length_error("Butterworth sample budget exceeded");
    ButterworthResult r;
    r.padding = 3 * std::size_t(order + 1);
    if (y.size() <= r.padding)
        throw std::invalid_argument("Record must be longer than 3*(order+1) reflection points");
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        if (!std::isfinite(y[i]))
            throw std::invalid_argument("Non-finite Butterworth input");
    }
    const auto sections = design(order, std::tan(pi * ratio / 2), type);
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (*bounds.first == *bounds.second)
        r.filtered = type == PassType::Low ? y : std::vector<double>(y.size(), 0.);
    else
    {
        // Numerically center before the recursive passes. Restore the DC
        // offset for low-pass; high-pass has exactly zero DC response.
        const long double offset = .5L * *bounds.first + .5L * *bounds.second;
        const long double magnitude =
            std::max(std::abs(static_cast<long double>(*bounds.first) - offset),
                     std::abs(static_cast<long double>(*bounds.second) - offset));
        std::vector<long double> buffer(y.size() + 2 * r.padding);
        for (std::size_t i = 0; i < y.size(); ++i)
        {
            if ((i & 4095) == 0)
                stop(cancel);
            buffer[r.padding + i] = (static_cast<long double>(y[i]) - offset) / magnitude;
        }
        for (std::size_t i = 0; i < r.padding; ++i)
        {
            buffer[i] = 2 * buffer[r.padding] - buffer[2 * r.padding - i];
            buffer[r.padding + y.size() + i] =
                2 * buffer[r.padding + y.size() - 1] - buffer[r.padding + y.size() - 2 - i];
        }
        forward(buffer, sections, type, cancel);
        std::reverse(buffer.begin(), buffer.end());
        forward(buffer, sections, type, cancel);
        std::reverse(buffer.begin(), buffer.end());
        r.filtered.resize(y.size());
        for (std::size_t i = 0; i < y.size(); ++i)
        {
            if ((i & 4095) == 0)
                stop(cancel);
            r.filtered[i] = static_cast<double>(buffer[r.padding + i] * magnitude +
                                                (type == PassType::Low ? offset : 0));
            if (!std::isfinite(r.filtered[i]))
                throw std::overflow_error("Butterworth output exceeds finite double range");
        }
    }
    const double nyquist = .5 / dt;
    if (!std::isfinite(nyquist) || nyquist <= 0)
        throw std::overflow_error("Unrepresentable filter frequency axis");
    for (int i = 0; i <= 512; ++i)
        r.frequency.push_back((double(i) / 512) * nyquist);
    r.frequency.push_back(cutoff);
    std::sort(r.frequency.begin(), r.frequency.end());
    r.frequency.erase(std::unique(r.frequency.begin(), r.frequency.end()), r.frequency.end());
    for (std::size_t i = 0; i < r.frequency.size(); ++i)
    {
        if ((i & 127) == 0)
            stop(cancel);
        const auto f = r.frequency[i];
        long double power = 1;
        if (f == 0)
            power = type == PassType::Low ? 1 : 0;
        else if (f == nyquist)
            power = type == PassType::Low ? 0 : 1;
        else
        {
            const auto z = std::polar(1.L, -2 * pi * f * dt);
            for (const auto &s : sections)
                power *=
                    std::norm((s.b0 + s.b1 * z + s.b2 * z * z) / (1.L + s.a1 * z + s.a2 * z * z));
        }
        if (!std::isfinite(power) || power < 0 || power > 1 + 1e-6L)
            throw std::overflow_error("Unrepresentable Butterworth amplitude response");
        r.gain.push_back(std::clamp(static_cast<double>(power), 0., 1.));
    }
    stop(cancel);
    return r;
}
} // namespace aster::filtering
