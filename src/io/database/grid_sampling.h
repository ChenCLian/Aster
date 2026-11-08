// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_GRID_SAMPLING_H
#define ASTER_GRID_SAMPLING_H
#include "io/database/plot_request.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace aster::grid_display
{
// Presentation geometry is measured on the original coordinates, never on a
// reduced grid. The same sampler serves in-memory previews and streamed BLOBs.
template<class At> bool geometric(qsizetype count, At at)
{
    if (count < 2 || at(0) <= 0 || at(1) <= at(0)) return false;
    const double step = std::log2(at(1)) - std::log2(at(0));
    double previous = std::log2(at(1));
    for (qsizetype i = 2; i < count; ++i)
    {
        const double next = at(i);
        if (!(next > 0)) return false;
        const double value = std::log2(next);
        if (std::abs(value - previous - step) > std::abs(step) * 1e-6) return false;
        previous = value;
    }
    return true;
}
inline qsizetype sampleIndex(qsizetype first, qsizetype count, qsizetype samples, qsizetype i)
{
    return first + (samples <= 1 ? 0 : i * (count - 1) / (samples - 1));
}
struct Sample
{
    GridData grid;
    QJsonObject statistics;
};
template<class X, class Y, class Value>
Sample sample(qsizetype columns, qsizetype rows, X x, Y y, Value value,
              QPair<qsizetype, qsizetype> cx, QPair<qsizetype, qsizetype> ry,
              int width, int height, const CancellationToken &cancel, bool fullRange)
{
    Sample out;
    auto &g = out.grid;
    if (columns <= 0 || rows <= 0 || cx.first < 0 || ry.first < 0 ||
        cx.second > columns || ry.second > rows || cx.first >= cx.second || ry.first >= ry.second)
        throw std::invalid_argument("Invalid display grid extent");
    const auto nc = cx.second - cx.first, nr = ry.second - ry.first;
    g.columns = std::min<qsizetype>(std::clamp(width, 2, PlotLimits::maxDimension), nc);
    g.rows = std::min<qsizetype>(std::clamp(height, 2, PlotLimits::maxDimension), nr);
    while (g.columns * g.rows > PlotLimits::maxHeatmapCells)
    { g.columns = (g.columns + 1) / 2; g.rows = (g.rows + 1) / 2; }
    g.x.resize(g.columns); g.y.resize(g.rows); g.values.resize(g.columns * g.rows);
    for (qsizetype c = 0; c < g.columns; ++c) g.x[c] = x(sampleIndex(cx.first, nc, g.columns, c));
    for (qsizetype r = 0; r < g.rows; ++r)
    {
        if (cancel.isCancelled()) throw std::runtime_error("Plot preparation cancelled");
        const auto sr = sampleIndex(ry.first, nr, g.rows, r);
        g.y[r] = y(sr);
        if (!fullRange)
            for (qsizetype c = 0; c < g.columns; ++c)
                g.values[r * g.columns + c] = value(sr * columns + sampleIndex(cx.first, nc, g.columns, c));
    }
    out.statistics = {{"source_columns", double(columns)}, {"source_rows", double(rows)},
        {"view_columns", double(nc)}, {"view_rows", double(nr)},
        {"display_columns", double(g.columns)}, {"display_rows", double(g.rows)},
        {"reduced", g.columns < nc || g.rows < nr}, {"y_geometric", geometric(rows, y)}};
    // Stable automatic colour limits come from the original output. Refinement
    // retains these limits; it must not change colours when the sampling changes.
    if (fullRange)
    {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo, positive = lo;
        qsizetype destination = 0;
        auto sourceIndex = [&] {
            return sampleIndex(ry.first, nr, g.rows, destination / g.columns) * columns +
                   sampleIndex(cx.first, nc, g.columns, destination % g.columns);
        };
        auto next = sourceIndex();
        // Fill the reduced grid during the colour-range scan. A second pass
        // would repeat BLOB I/O over the same large output.
        for (qsizetype i = 0; i < rows * columns; ++i)
        {
            if ((i & 4095) == 0 && cancel.isCancelled()) throw std::runtime_error("Plot preparation cancelled");
            const auto v = value(i);
            lo = std::min(lo, v); hi = std::max(hi, v);
            if (v > 0) positive = std::min(positive, v);
            if (i == next)
            {
                g.values[destination++] = v;
                next = destination < g.values.size() ? sourceIndex() : rows * columns;
            }
        }
        out.statistics["color_bounds"] = QJsonArray{lo, hi, std::isfinite(positive) ? positive : 0};
    }
    return out;
}
} // namespace aster::grid_display
#endif
