// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_CHRONOLOGY_CYCLE_AGE_H
#define ASTER_MATH_CHRONOLOGY_CYCLE_AGE_H
#include "math/chronology/age_model.h"
namespace aster::chronology
{
struct CycleOptions
{
    double period = 41, firstAge = 41, minimumRelief = 0, minimumDistance = 0;
    bool troughs = false, extendEnds = false;
};
struct CycleResult
{
    std::vector<std::size_t> indices;
    std::vector<double> relief, anchorAge, modelDepth, modelAge, intervalRate;
    std::size_t candidateCount = 0, plateauCount = 0;
};
// Each selected same-phase extremum is ONE cycle; missed/double cycles are not inferred.
// Plateau: one central sample (left central when even), strictly bounded on both sides.
// Relief: height above the higher of the nearest inter-peak valleys, NOT global prominence.
CycleResult cycleAge(const std::vector<double> &depth, const std::vector<double> &value,
                     const CycleOptions &, const std::function<bool()> &cancel = {});
} // namespace aster::chronology
#endif
