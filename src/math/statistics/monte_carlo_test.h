// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_STATISTICS_MONTE_CARLO_TEST_H
#define ASTER_MATH_STATISTICS_MONTE_CARLO_TEST_H
#include <cstddef>
#include <functional>
#include <vector>

namespace aster::numeric {
struct MonteCarloMaximumResult {
    double observedMaximum = 0;
    double globalP = 1;
    double criticalValue = 0; // Plotting aid only: use p<=alpha (ties are conservative).
    std::vector<double> adjustedP;
    std::vector<double> nullMaxima; // Generation order, for reproducibility.
};
// Upper-tail rank p=(1+#null>=observed)/(B+1); near ties are conservative.
// The reference must be nonempty, finite and sorted in ascending order.
double empiricalUpperTailP(const std::vector<double>& sortedReference, double observed);

// One predeclared family of finite statistics; large values reject the null.
// Callback must generate a NEW null input and replay the SAME processing,
// parameter estimation and statistic calculation used for the observed input.
// Every replicate must return the same family, in the same order. Failures abort;
// no replicate is silently discarded/retried. Only one maximum per replicate is kept.
//
// A valid rank test requires exchangeability of observed and null statistics.
// Plug-in nuisance estimates do NOT automatically provide this. This helper
// performs no model fitting, guesses no noise model and asserts no FDR control.
// It tests the complete null for one fixed family; repeated analyses, mixed
// null/alternative strong FWER and arbitrary upstream workflows are not certified.
MonteCarloMaximumResult monteCarloMaximumTest(
    const std::vector<double>& observed,
    size_t simulations, double alpha,
    const std::function<std::vector<double>(size_t)>& nullStatistics,
    const std::function<bool()>& cancelled = {});
}
#endif
