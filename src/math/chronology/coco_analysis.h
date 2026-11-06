// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_COCO_ANALYSIS_H
#define ASTER_MATH_COCO_ANALYSIS_H
#include "math/chronology/coco_scan.h"
#include "math/statistics/monte_carlo_test.h"
#include <cstdint>
#include <random>
namespace aster::chronology
{
namespace CocoAnalysisLimits
{
inline constexpr std::size_t maxCacheCells = 8000000, maxInputSamples = 1000000, maxWindows = 512,
                             maxMapCells = 2000000, maxPeaks = 8192, maxSimulations = 9999;
inline constexpr long double maxEvaluationWork = 500000000.L;
} // namespace CocoAnalysisLimits
struct CocoMonteCarloOptions
{
    enum class Null
    {
        FixedAr1,
        RandomPeaks
    } nullModel = Null::FixedAr1;
    std::size_t simulations = 199;
    std::uint64_t seed = 1729;
    double rho = 0, alpha = .05; // rho fixed independently of tested data, |rho|<1
    std::size_t peakCount = 0;   // 0: count strict non-DC/non-Nyquist periodogram peaks
};
struct CocoMonteCarloResult
{
    CocoResult observed;
    std::vector<double> rates, evidence, pointwiseP;
    numeric::MonteCarloMaximumResult maximum;
    std::size_t peaks = 0, cacheCells = 0;
};
CocoMonteCarloResult cocoMonteCarlo(const std::vector<double> &, double, const CocoOptions &,
                                    const CocoMonteCarloOptions & = {},
                                    const std::function<bool()> & = {},
                                    const std::function<void(int)> & = {});
struct CocoEvolutionOptions
{
    std::size_t windowSamples = 512, stepSamples = 128;
};
struct CocoEvolutionResult
{
    std::vector<CocoRow> rateGrid;
    std::vector<double> centers, rates, correlation, bestRate, bestCorrelation;
    // correlation[rateRow*centers.size()+windowColumn]; centers in sample indices.
    std::size_t unusedTail = 0, fftSize = 0, cacheCells = 0;
};
CocoEvolutionResult cocoEvolution(const std::vector<double> &, double, const CocoOptions &,
                                  const CocoEvolutionOptions & = {},
                                  const std::function<bool()> & = {},
                                  const std::function<void(int)> & = {});
// Explicit deterministic generators used by the analysis and independent tests.
// MT19937-64 high 53 bits -> U[0,1); normals use fixed Box-Muller ordering.
std::vector<double> cocoAr1(std::size_t, double, std::mt19937_64 &);
std::vector<double> cocoRandomPeaks(std::size_t, std::size_t, std::size_t, std::mt19937_64 &);
} // namespace aster::chronology
#endif
