// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_STATISTICS_AR1_GLOBAL_H
#define ASTER_MATH_STATISTICS_AR1_GLOBAL_H
#include "math/statistics/ar1_nuisance.h"
#include "math/statistics/mtm_simulation.h"
#include <functional>
#include <memory>

namespace aster::numeric {
// Versioned, deliberately small model: stationary Gaussian AR1, unknown mean
// and scale, NW=2/K=3/NFFT=N, all positive interior bins. No automatic sorting.
struct Ar1GlobalOptions {
    int analysisReplicates = 199, screeningReplicates = 999;
    std::uint64_t seed = 202611021281ULL;
    bool lowess = false;
    double lowessFraction = .35;
    double rhoLower = -.999, rhoUpper = .999, screeningBudget = .005, variationBudget = .01;
};
using Ar1Progress = std::function<void(int)>;
struct Ar1ReferenceRow { std::vector<double> screening, analysis; };
struct Ar1ReferenceBank {
    std::size_t samples = 0;
    Ar1GlobalOptions options;
    std::vector<Ar1ReferenceRow> rows;
};
struct Ar1GlobalResult {
    MtmSpectrum spectrum;
    Ar1LikelihoodFit fit;
    std::vector<double> background, analysedSeries;
    CompositeNullResult test;
    double maximumRatio = 0, lag = 0, variationBound = 0;
};
// Memory/time budgets are checked before allocations and simulation.
std::vector<Ar1GridCell> ar1GlobalGrid(std::size_t samples, const Ar1GlobalOptions&);
Ar1GlobalResult ar1GlobalStatistic(const std::vector<double>& y, double sampleRate,
    const Ar1GlobalOptions&, std::shared_ptr<const MtmTaperPlan> plan = {},
    const std::function<bool()>& cancelled = {});
std::shared_ptr<const Ar1ReferenceBank> buildAr1ReferenceBank(std::size_t samples,
    const Ar1GlobalOptions&, const std::function<bool()>& cancelled = {}, const Ar1Progress& progress = {});
Ar1GlobalResult evaluateAr1Global(const std::vector<double>& y, double sampleRate,
    const Ar1ReferenceBank&, const std::function<bool()>& cancelled = {});
}
#endif
