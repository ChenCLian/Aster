// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/grid_contours.h"
#include "io/database/plot_request.h"
#include <algorithm>
#include <cmath>
namespace aster::ui
{
GridContours gridContours(const GridData &g, double level, int maxSegments)
{
    GridContours result;
    auto invalid = [&] { result.segments.clear(); result.state = ContourState::Invalid; return result; };
    if (g.rows < 2 || g.columns < 2 || g.rows != g.y.size() || g.columns != g.x.size() ||
        g.rows > g.values.size() / g.columns || g.rows * g.columns != g.values.size() ||
        !std::isfinite(level) || maxSegments < 1) return invalid();
    if (g.values.size() > PlotLimits::maxHeatmapCells)
    { result.state = ContourState::TooDense; return result; }
    for (const auto *axis : {&g.x, &g.y})
        for (qsizetype i = 0; i < axis->size(); ++i)
            if (!std::isfinite((*axis)[i]) || (i && (*axis)[i] <= (*axis)[i-1])) return invalid();
    for (double v : g.values) if (!std::isfinite(v)) return invalid();
    for (qsizetype r = 0; r + 1 < g.rows; ++r)
        for (qsizetype c = 0; c + 1 < g.columns; ++c)
        {
            const double v[] = {g.values[r*g.columns+c], g.values[r*g.columns+c+1],
                                g.values[(r+1)*g.columns+c+1], g.values[(r+1)*g.columns+c]};
            const QPointF p[] = {{g.x[c],g.y[r]}, {g.x[c+1],g.y[r]},
                                 {g.x[c+1],g.y[r+1]}, {g.x[c],g.y[r+1]}};
            // Scale before subtraction/multiplication to avoid overflow at extreme values.
            double scale = std::max(1., std::abs(level));
            for (double value : v) scale = std::max(scale, std::abs(value));
            double a[4]; for (int i=0;i<4;++i) a[i] = v[i]/scale - level/scale;
            QPointF cross[4]; int edges[4], count=0;
            for (int i=0;i<4;++i)
            {
                const int j=(i+1)%4;
                if ((v[i]>level)==(v[j]>level)) continue;
                // Interpolate each edge on its own scale: a huge third corner
                // must not underflow two near-threshold endpoints to zero.
                const double edgeScale = std::max({1.,std::abs(v[i]),std::abs(v[j]),std::abs(level)});
                const double from = v[i]/edgeScale-level/edgeScale;
                const double to = v[j]/edgeScale-level/edgeScale;
                const double t = from/(from-to);
                if (!std::isfinite(t)) return invalid();
                cross[i] = (1-t)*p[i] + t*p[j];
                if (!std::isfinite(cross[i].x()) || !std::isfinite(cross[i].y())) return invalid();
                edges[count++] = i;
            }
            auto add = [&](int i, int j)
            { if (cross[i] != cross[j]) result.segments.push_back(QLineF(cross[i],cross[j])); };
            if (count==2) add(edges[0],edges[1]);
            else if (count==4)
            {
                if (a[0]*a[2]-a[1]*a[3] >= 0) { add(0,1); add(2,3); }
                else { add(0,3); add(1,2); }
            }
            if (result.segments.size()>maxSegments)
            { result.segments.clear(); result.state=ContourState::TooDense; return result; }
        }
    result.state = result.segments.isEmpty() ? ContourState::NoBoundary : ContourState::Ready;
    return result;
}
} // namespace aster::ui
