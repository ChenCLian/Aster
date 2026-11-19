// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/smoothing/lowess_fit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace aster::numeric {
namespace {
constexpr int degree = 9;
template<size_t N> struct Sums {
    std::array<double,N> value{}, correction{};
    void add(size_t k, double term) {
        const double adjusted = term - correction[k];
        const double next = value[k] + adjusted;
        correction[k] = (next - value[k]) - adjusted;
        value[k] = next;
    }
    double since(const Sums& prior, size_t k) const {
        return (value[k] - prior.value[k]) - (correction[k] - prior.correction[k]);
    }
};

// Moments measured from a nearby origin, in units of one local bandwidth.
// Four cursors bound the left wing, flat centre and right wing of the kernel.
struct Prefix {
    size_t pos = 0;
    Sums<degree+3> x;
    Sums<degree+2> y;
    void move(size_t target, double origin, double scale,
              const std::vector<double>& coordinates, const std::vector<double>& signal) {
        const double direction = target >= pos ? 1.0 : -1.0;
        for (size_t j = std::min(pos, target); j < std::max(pos, target); ++j) {
            const double u = (coordinates[j] - origin) / scale;
            double power = direction;
            for (int k = 0; k <= degree+1; ++k) {
                x.add(k, power);
                y.add(k, power * signal[j]);
                power *= u;
            }
            x.add(degree+2, power);
        }
        pos = target;
    }
};

constexpr auto binomial() {
    std::array<std::array<int,degree+1>,degree+1> c{};
    for (int n = 0; n <= degree; ++n) {
        c[n][0] = c[n][n] = 1;
        for (int k = 1; k < n; ++k) c[n][k] = c[n-1][k-1] + c[n-1][k];
    }
    return c;
}
constexpr auto choose = binomial();

double fittedValue(const std::array<Prefix,4>& p, double t, double scale, double h, double range)
{
    std::array<double,degree+1> shift{};
    shift[0] = 1;
    for (int k = 1; k <= degree; ++k) shift[k] = -t * shift[k-1];
    const double a = scale / h, a3 = a*a*a, a6 = a3*a3, a9 = a6*a3;
    double mass = p[2].x.since(p[1].x, 0);
    double sx = p[2].x.since(p[1].x, 1), sxx = p[2].x.since(p[1].x, 2);
    double sy = p[2].y.since(p[1].y, 0), sxy = p[2].y.since(p[1].y, 1);
    // Expand (1 - |a*(u-t)|^3)^3 separately on each side of the target.
    for (int k = 0; k <= degree; ++k) {
        double even = k == 0 ? 1.0 : 0.0;
        if (k <= 6) even += 3*a6*choose[6][k]*shift[6-k];
        double odd = a9*choose[9][k]*shift[9-k];
        if (k <= 3) odd += 3*a3*choose[3][k]*shift[3-k];
        const double left = even+odd, right = even-odd;
        const auto sum = [&](const auto Prefix::*member, int j) {
            return left * (p[1].*member).since(p[0].*member, j)
                 + right * (p[3].*member).since(p[2].*member, j);
        };
        mass += sum(&Prefix::x,k);
        sx += sum(&Prefix::x,k+1);
        sxx += sum(&Prefix::x,k+2);
        sy += sum(&Prefix::y,k);
        sxy += sum(&Prefix::y,k+1);
    }
    const double meanX = sx/mass, meanY = sy/mass;
    const double variance = sxx/mass - meanX*meanX;
    if (variance > 0 && std::sqrt(variance)*scale > .001*range)
        return meanY + (t-meanX)*(sxy/mass - meanX*meanY)/variance;
    return meanY;
}
}

bool fitOrdinaryLowess(const std::vector<double>& x, const std::vector<double>& y,
                       size_t neighbours, std::vector<double>& fitted,
                       const std::function<bool(size_t)>& keepGoing)
{
    const size_t n = x.size();
    if (n < 2 || y.size() != n || neighbours < 2 || neighbours > n)
        throw std::invalid_argument("Invalid local-linear LOWESS input");
    fitted.resize(n);
    if (keepGoing && !keepGoing(0)) return false;
    const auto limits = std::minmax_element(y.begin(), y.end());
    if (*limits.first == *limits.second) {
        std::fill(fitted.begin(), fitted.end(), *limits.first);
        return true;
    }
    const double magnitude = std::max(std::abs(*limits.first), std::abs(*limits.second));
    // Centre narrow, offset signals. A global offset for a signal crossing
    // zero would erase small values far away from an isolated large value.
    const double offset = *limits.second/magnitude - *limits.first/magnitude < .5
        ? y.front()/magnitude : 0.;
    std::vector<double> signal(n);
    for (size_t j = 0; j < n; ++j) signal[j] = y[j]/magnitude - offset;
    const double range = x.back() - x.front();
    size_t start = 0, left = 0, right = neighbours-1;
    double origin = 0, scale = 0;
    std::array<Prefix,4> prefix{};
    for (size_t i = 0; i < n; ++i) {
        if (i % 128 == 0 && keepGoing && !keepGoing(i)) return false;
        const double target = x[i];
        while (right+1 < n && target-x[left] > x[right+1]-target) { ++left; ++right; }
        const double h = std::max(target-x[left], x[right]-target);
        const double edge = .999*h, flat = .001*h;
        auto cut = [&](size_t lo, size_t hi, auto before) {
            return size_t(std::partition_point(x.begin()+lo, x.begin()+hi, before) - x.begin());
        };
        const std::array<size_t,4> stops{
            cut(left, i, [&](double v) { return target-v > edge; }),
            cut(left, i, [&](double v) { return target-v > flat; }),
            cut(i, right+1, [&](double v) { return v-target <= flat; }),
            cut(i, right+1, [&](double v) { return v-target <= edge; })};
        // Rebase before coordinates or powers become ill-conditioned. On a
        // regular grid each input is accumulated a bounded number of times.
        const bool rebase = i == 0 || i-start >= neighbours/4 || target-origin > .4*scale
                            || h < .7*scale || h > 1.5*scale;
        if (rebase) {
            start = i; origin = target; scale = h;
            prefix[0] = {}; prefix[0].pos = stops[0];
        }
        for (size_t j = 0; j < prefix.size(); ++j) {
            if (rebase && j) prefix[j] = prefix[j-1];
            prefix[j].move(stops[j], origin, scale, x, signal);
        }
        fitted[i] = (offset + fittedValue(prefix, (target-origin)/scale, scale, h, range)) * magnitude;
    }
    return true;
}
}
