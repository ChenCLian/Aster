// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_STATISTICS_MTM_SIMULATION_H
#define ASTER_MATH_STATISTICS_MTM_SIMULATION_H
#include "math/spectrum/mtm_spectrum.h"
#include <cstdint>

namespace aster::numeric {
// Stationary Gaussian AR(1), with a fitted constant mean. The variance is that
// of the innovations, not the marginal variance. No preprocessing is implicit.
struct Ar1LikelihoodFit {
    double rho = 0, mean = 0, innovationVariance = 0;
};
Ar1LikelihoodFit fitStationaryAr1(const std::vector<double>& y,
                                bool estimateRho = true, double fixedRho = 0);
// One-sided AR1 density on an existing MTM frequency grid. The fit uses
// innovation variance; endpoint scaling follows spectrum.densityScale.
std::vector<double> stationaryAr1Background(const MtmSpectrum& spectrum,
    const Ar1LikelihoodFit& fit, double sampleRate);
struct MtmSimulationOptions {
    int nw = 3, simulations = 999;
    double fmaxRatio = 1, alpha = .05;
    bool estimateRho = true;
    double fixedRho = 0;
    std::uint64_t seed = 20261101;
};
struct MtmSimulationResult {
    MtmSpectrum spectrum;
    Ar1LikelihoodFit backgroundFit;
    std::vector<double> background, adjustedP, nullMaxima;
    // Only these bins belong to the family; DC and Nyquist are excluded.
    std::vector<size_t> testedBins;
    double maximumRatio = 0, globalP = 1, criticalRatio = 0;
};
// Experimental plug-in parametric bootstrap if rho is estimated. With rho
// specified independently, the MC rank test is conditional on that AR(1) model.
// Mean and scale are profiled anew for every simulation in BOTH modes.
MtmSimulationResult simulateMtmAr1(const std::vector<double>& y, double fs,
    const MtmSimulationOptions& options, const std::function<bool()>& cancelled = {});
}
#endif
