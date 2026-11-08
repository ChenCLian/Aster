// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/preview_data.h"
#include "io/database/grid_sampling.h"
#include <algorithm>
#include <array>
namespace aster::ui
{
namespace
{
constexpr auto PointLimit = PreviewLimits::seriesPoints, GridSide = PreviewLimits::gridSide, TableRows = PreviewLimits::tableRows;
void reducePair(const QVector<double> &x, const QVector<double> &y,
                QVector<double> &ox, QVector<double> &oy, const CancellationToken &cancel)
{
    if (x.size() <= PointLimit) { ox = x; oy = y; return; }
    const qsizetype buckets = PointLimit / 6;
    ox.reserve(PointLimit); oy.reserve(PointLimit);
    for (qsizetype b = 0; b < buckets; ++b)
    {
        if (cancel.isCancelled()) return;
        const auto a = b * x.size() / buckets, end = (b + 1) * x.size() / buckets;
        std::array<qsizetype, 6> indices{a, end - 1, a, a, a, a};
        for (auto i = a + 1; i < end; ++i)
        {
            if (x[i] < x[indices[2]]) indices[2] = i;
            if (x[i] > x[indices[3]]) indices[3] = i;
            if (y[i] < y[indices[4]]) indices[4] = i;
            if (y[i] > y[indices[5]]) indices[5] = i;
        }
        std::sort(indices.begin(), indices.end());
        const auto last = std::unique(indices.begin(), indices.end());
        for (auto i = indices.begin(); i != last; ++i) { ox << x[*i]; oy << y[*i]; }
    }
}
}
NodeResult reducePreviewData(const NodeResult &result, const CancellationToken &cancel)
{
    // Cross-spectrum masks and phase wraps require the complete, bounded table.
    // Rendering applies adaptive sampling after splitting undefined intervals.
    if (result.report["cross_spectrum_schema"].toInt() == 1) return result;
    auto display = result;
    for (auto it = display.outputs.begin(); it != display.outputs.end(); ++it)
    {
        if (cancel.isCancelled()) return NodeResult::cancelled();
        const auto source = it.value();
        if (const auto *s = dataAs<SeriesData>(source); s && s->x.size() > PointLimit)
        {
            SeriesData d; d.xAxis = s->xAxis; d.yAxis = s->yAxis;
            reducePair(s->x, s->y, d.x, d.y, cancel); it.value() = makeData(std::move(d));
        }
        else if (const auto *s = dataAs<SpectrumData>(source); s && s->frequency.size() > PointLimit)
        {
            SpectrumData d; d.frequencyAxis = s->frequencyAxis; d.powerAxis = s->powerAxis;
            reducePair(s->frequency, s->power, d.frequency, d.power, cancel);
            it.value() = makeData(std::move(d));
        }
        else if (const auto *s = dataAs<GridData>(source))
        {
            try
            {
                auto sample = grid_display::sample(s->columns, s->rows,
                    [&](qsizetype i) { return s->x[i]; }, [&](qsizetype i) { return s->y[i]; },
                    [&](qsizetype i) { return s->values[i]; }, {0, s->columns}, {0, s->rows},
                    GridSide, GridSide, cancel, true);
                sample.grid.xAxis = s->xAxis; sample.grid.yAxis = s->yAxis; sample.grid.valueAxis = s->valueAxis;
                auto samples = display.report["plot_samples"].toObject();
                samples[it.key()] = sample.statistics;
                display.report["plot_samples"] = samples;
                if (sample.statistics["reduced"].toBool()) it.value() = makeData(std::move(sample.grid));
            }
            catch (const std::exception &e)
            { return cancel.isCancelled() ? NodeResult::cancelled() : NodeResult::failure(QString::fromUtf8(e.what())); }
        }
        else if (const auto *s = dataAs<TableData>(source))
        {
            const auto columns = std::min(PreviewLimits::tableColumns, s->columns.size());
            const auto rows = std::min(TableRows, PreviewLimits::tableCells / std::max(qsizetype(1), columns));
            if (columns < s->columns.size() || rows < s->rows.size())
            {
                TableData d; d.columns = s->columns.mid(0, columns);
                for (qsizetype row = 0; row < std::min(rows, s->rows.size()); ++row)
                {
                    if (cancel.isCancelled()) return NodeResult::cancelled();
                    d.rows << s->rows[row].mid(0, columns);
                    if (!s->defined.isEmpty()) d.defined << s->defined[row].mid(0, columns);
                }
                it.value() = makeData(std::move(d));
            }
        }
        if (it.value() != source) display.report["display_reduced"] = true;
    }
    return display;
}
}
