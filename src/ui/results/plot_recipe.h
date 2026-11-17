// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_RECIPE_H
#define ASTER_PLOT_RECIPE_H
#include "io/database/output_reference.h"
#include <QSet>
#include <algorithm>
namespace aster::ui
{
// Recipes describe presentation relationships, not numerical computation.
// Unknown task/output types remain independent instead of guessing from units.
inline bool spectrumRecipe(const QString &method)
{
    static const QSet<QString> methods{"mtm", "mtm_ar1_simulation", "ar1_global",
                                      "fft_spectrum", "welch_spectrum", "lomb_scargle"};
    return methods.contains(method);
}
inline bool samePlotNode(const OutputReference &a, const OutputReference &b)
{
    return a.databasePath == b.databasePath && a.recordId == b.recordId &&
           a.runId == b.runId && a.nodeId == b.nodeId;
}
inline bool waveletGrid(const OutputReference &ref)
{
    return ref.method == "cwt" && ref.kind == "Grid" &&
        QStringList{"power", "raw_power", "scale_corrected_power", "significance_ratio"}.contains(ref.port);
}
inline bool auxiliaryLayer(const OutputReference &target, const OutputReference &input)
{
    if (!samePlotNode(target, input)) return false;
    return (waveletGrid(target) && input.port == "coi" && input.kind == "Series") ||
        (spectrumRecipe(target.method) && target.kind == "Spectrum" && input.port == "peaks" && input.kind == "Table");
}
inline bool canLayerReferences(const OutputReference &target, const OutputReference &input)
{
    return target.key() == input.key() || auxiliaryLayer(target, input) ||
        ((target.kind == "Series" || target.kind == "Spectrum") && target.kind == input.kind);
}
inline bool compatiblePlotLayer(const OutputReference &target, const DataRef &a,
                               const OutputReference &input, const DataRef &b)
{
    if (auxiliaryLayer(target, input))
    {
        if (target.kind == "Spectrum") return dataAs<SpectrumData>(a) && dataAs<TableData>(b);
        const auto *grid = dataAs<GridData>(a); const auto *coi = dataAs<SeriesData>(b);
        const auto same = [](const AxisInfo &x, const AxisInfo &y) {
            return x.unit == y.unit && (!x.unit.isEmpty() || x.name == y.name);
        };
        return grid && coi && same(grid->xAxis, coi->xAxis) && grid->yAxis.name == "Period" &&
            coi->yAxis.name == "COI period" && grid->yAxis.unit == coi->yAxis.unit;
    }
    if (compatibleCurveAxes(a, b)) return true;
    // This path is an explicit overlay requested by the user. Legacy filters
    // stored output labels ("Amplitude envelope") as Y-axis names. Names alone
    // cannot establish different dimensions when both Y units are unspecified.
    // Keep X-axis compatibility and reject differing declared Y units.
    const auto *x = dataAs<SeriesData>(a), *y = dataAs<SeriesData>(b);
    return x && y && x->yAxis.unit.isEmpty() && y->yAxis.unit.isEmpty() &&
        x->xAxis.unit == y->xAxis.unit &&
        (!x->xAxis.unit.isEmpty() || x->xAxis.name == y->xAxis.name);
}
inline QVector<QVector<OutputReference>> plotGroups(const QVector<OutputReference> &refs, bool separate = false)
{
    QVector<QVector<OutputReference>> groups;
    QSet<QString> used;
    for (const auto &ref : refs)
    {
        if (used.contains(ref.key())) continue;
        if (!separate && ref.method == "cwt" && ref.port == "coi" &&
            std::any_of(refs.cbegin(), refs.cend(), [&](const OutputReference &v) {
                return samePlotNode(ref, v) && waveletGrid(v);
            })) continue;
        QVector<OutputReference> group{ref}; used.insert(ref.key());
        if (!separate && spectrumRecipe(ref.method) && (ref.kind == "Spectrum" || ref.port == "peaks"))
        {
            for (const auto &other : refs)
                if (!used.contains(other.key()) && samePlotNode(ref, other) &&
                    (other.kind == "Spectrum" || (other.port == "peaks" && other.kind == "Table")))
                { group.push_back(other); used.insert(other.key()); }
            // A spectrum is always the axis owner; the recommended primary wins.
            std::stable_sort(group.begin(), group.end(), [](const OutputReference &a, const OutputReference &b) {
                auto rank = [](const OutputReference &v) { return v.port == "spectrum" ? 0 : v.kind == "Spectrum" ? 1 : 2; };
                return rank(a) < rank(b);
            });
        }
        if (!separate && ref.method == "outlier_handling" && ref.kind == "Series") {
            for (const auto &other : refs)
                if (!used.contains(other.key()) && samePlotNode(ref,other) && other.kind=="Series")
                { group.push_back(other); used.insert(other.key()); }
            std::stable_sort(group.begin(),group.end(),[](const OutputReference &a,const OutputReference &b) {
                auto rank=[](const OutputReference &v) { return v.port=="series" ? 0 : v.port=="original" ? 1 : 2; };
                return rank(a)<rank(b);
            });
        }
        if (!separate && QStringList{"hilbert_envelope", "gaussian_filter", "taner_filter"}.contains(ref.method) &&
            QStringList{"envelope", "signal", "filtered"}.contains(ref.port))
            for (const auto &other : refs)
                if (!used.contains(other.key()) && samePlotNode(ref, other) && other.kind == "Series" &&
                    QStringList{"envelope", "signal", "filtered"}.contains(other.port))
                { group.push_back(other); used.insert(other.key()); }
        if (!separate && waveletGrid(ref))
            for (const auto &other : refs)
                if (samePlotNode(ref, other) && other.port == "coi" && other.kind == "Series")
                { group.push_back(other); used.insert(other.key()); }
        groups.push_back(group);
    }
    return groups;
}
} // namespace aster::ui
#endif
