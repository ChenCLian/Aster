// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_REQUEST_H
#define ASTER_PLOT_REQUEST_H
#include "core/task.h"
#include "io/database/data_format.h"
namespace aster
{
// Logical data coordinates, independent of reversed/swapped display axes.
struct PlotRequest
{
    bool window = false, logX = false, logY = false;
    bool includeFamily = true;
    double xLower = 0, xUpper = 1, yLower = 0, yUpper = 1;
    int columns = 1200, rows = 600;
};
namespace PlotLimits
{
// Bounded BLOB/axis caches. Larger blocks amortize SQLite substring retrieval
// without materializing the complete scientific output in the plot worker.
constexpr int chunkBytes = 1024 * 1024;
constexpr int maxDimension = 8192;
constexpr int maxHeatmapCells = 4 * 1024 * 1024;
constexpr int maxTableRows = 10000;
constexpr int maxTableCells = 200000;
constexpr int debounceMs = 150;
constexpr qint64 maxExportPixels = 32 * 1024 * 1024;
constexpr int maxCurveFamilyPoints = 512 * 1024;
constexpr int maxFamilyOutputs = 64;
constexpr quint64 maxNumbers =
    DataFormat::elementLimit; // Same persisted payload contract as data_codec v1.
} // namespace PlotLimits
} // namespace aster
#endif
