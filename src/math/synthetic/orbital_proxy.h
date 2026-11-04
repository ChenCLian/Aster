// SPDX-License-Identifier: GPL-3.0-or-later
// AstroGeoFit: Copyright (C) 2025 CNRS, lead author J. Laskar.
#ifndef ASTER_MATH_SYNTHETIC_ORBITAL_PROXY_H
#define ASTER_MATH_SYNTHETIC_ORBITAL_PROXY_H
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
namespace aster::synthetic {
struct Model {
    std::array<double,5> frequencyMean, frequencySd; // arcsec/year
    double g5, interceptSd, slopeSd, residualSd, lengthScale;
};
struct Options { double rateMin, rateMax, snr, rho; };
struct Latent {
    std::array<double,5> frequency; // arcsec/year: p0, g1..g4
    std::array<double,8> unitRates;
    std::array<double,20> beta; // all 10 sine terms, then all 10 cosine terms
    std::vector<double> whiteNoise; // 256 burn-in samples followed by N innovations
};
struct Result {
    std::vector<double> depth, ageKa, rate, clean, noise, proxy;
    std::array<double,10> angularFrequency; // radians/Myr
    std::array<double,8> inverseKnots; // Myr/m
    double rawMean=0, rawSd=0, clippedFraction=0;
    bool cancelled=false;
};
using Cancel = std::function<bool()>;
Latent draw(const Model&, std::size_t samples, std::uint64_t seed, std::uint64_t realization);
Result evaluate(const Model&, const Options&, const std::vector<double>& depth,
                const Latent&, const Cancel& cancelled = {});
} // namespace aster::synthetic
#endif
