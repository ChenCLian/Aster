// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/smoothing/smoothing.h"
#include "math/sample_scaling.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aster::numeric
{
namespace
{
void stop(const std::function<bool()> &c)
{
    if (c && c())
        throw SmoothingCancelled{};
}
} // namespace
std::vector<double> smoothSeries(const std::vector<double> &y, std::size_t w, int degree,
                                 SmoothingMethod method, const std::function<bool()> &cancel)
{
    stop(cancel);
    if (method != SmoothingMethod::MovingMean && method != SmoothingMethod::SavitzkyGolay)
        throw std::invalid_argument("Unknown smoothing method");
    if (y.empty() || w == 0 || w % 2 == 0 || w > y.size() || degree < 0 ||
        degree > SmoothingLimits::maxDegree ||
        (method == SmoothingMethod::SavitzkyGolay && std::size_t(degree) >= w))
        throw std::invalid_argument("Use an odd window <= sample count and SG degree < window");
    if (y.size() > SmoothingLimits::maxSamples || w > SmoothingLimits::maxWindow ||
        y.size() > SmoothingLimits::maxWorkTerms / w)
        throw std::length_error("Smoothing sample/window/work budget exceeded");
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 1023) == 0)
            stop(cancel);
        if (!std::isfinite(y[i]))
            throw std::invalid_argument("Non-finite smoothing input");
    }
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (*bounds.first == *bounds.second || w == 1)
        return y;
    const auto half = w / 2;
    // Orthonormal projection in scaled coordinates, with two MGS passes.
    // P = Q Q^T avoids normal equations and large unscaled polynomial powers.
    std::vector<std::vector<long double>> weights;
    if (method == SmoothingMethod::SavitzkyGolay)
    {
        std::vector<std::vector<long double>> q;
        for (int k = 0; k <= degree; ++k)
        {
            stop(cancel);
            std::vector<long double> v(w);
            for (std::size_t j = 0; j < w; ++j)
                v[j] = std::pow(2.L * j / (w - 1) - 1, k);
            for (int pass = 0; pass < 2; ++pass)
                for (const auto &basis : q)
                {
                    long double dot = 0;
                    for (std::size_t j = 0; j < w; ++j)
                        dot += basis[j] * v[j];
                    for (std::size_t j = 0; j < w; ++j)
                        v[j] -= dot * basis[j];
                }
            long double norm = 0;
            for (auto a : v)
                norm += a * a;
            norm = std::sqrt(norm);
            if (!std::isfinite(norm) || norm <= 1e-14L)
                throw std::runtime_error("Degenerate SG polynomial basis");
            for (auto &a : v)
                a /= norm;
            q.push_back(std::move(v));
        }
        weights.assign(w, std::vector<long double>(w, 0));
        for (std::size_t pos = 0; pos < w; ++pos)
        {
            stop(cancel);
            for (std::size_t j = 0; j < w; ++j)
                for (const auto &basis : q)
                    weights[pos][j] += basis[pos] * basis[j];
        }
    }
    std::vector<double> result(y.size());
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 255) == 0)
            stop(cancel);
        const bool mean = method == SmoothingMethod::MovingMean;
        const auto left = i > half ? i - half : 0;
        const auto first = mean ? left : std::min(left, y.size() - w);
        const auto last = mean ? std::min(y.size(), i + half + 1) : first + w;
        const auto window = std::minmax_element(y.begin() + first, y.begin() + last);
        if (*window.first == *window.second) {
            result[i] = *window.first;
            continue;
        }
        // A distant spike must never change this window's normalization.
        const double scale = binarySampleScale(std::max(std::abs(*window.first), std::abs(*window.second)));
        long double sum = 0;
        for (std::size_t j = first; j < last; ++j) {
            const long double unit = static_cast<long double>(y[j]) / scale;
            sum += mean ? unit : weights[i-first][j-first] * unit;
        }
        if (mean)
            sum /= static_cast<long double>(last - first);
        result[i] = static_cast<double>(sum * scale);
        if (!std::isfinite(result[i]))
            throw std::overflow_error("Smoothing output exceeds finite double range");
    }
    stop(cancel);
    return result;
}
} // namespace aster::numeric
