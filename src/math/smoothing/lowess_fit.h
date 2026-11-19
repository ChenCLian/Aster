// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <functional>
#include <vector>

namespace aster::numeric {
// Ordinary local-linear LOWESS at every input coordinate, using the original
// tricube cutoffs and slope test. X is finite, normalized and strictly increasing.
bool fitOrdinaryLowess(const std::vector<double>& x, const std::vector<double>& y,
                       size_t neighbours, std::vector<double>& fitted,
                       const std::function<bool(size_t)>& keepGoing);
}
