// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/plot_preset.h"
#include "ui/settings/theme_manager.h"
#include "ui/common/presentation.h"
#include "io/database/plot_request.h"
#include "io/database/grid_sampling.h"
#include <QJsonArray>
#include <QPen>
#include <algorithm>
#include <cmath>
namespace aster::ui
{
namespace
{
QString axisLabel(const AxisInfo &a)
{
    return a.unit.isEmpty() ? a.name : a.name + " (" + a.unit + ")";
}
QPen penFor(const QString &port, double width, const QColor &main)
{
    if (port == "spectrum" || port == "series" || port == "detrended")
        return QPen(main, width);
    if (port == "trend" || port == "ar1_smoothed")
        return QPen(themeColor("purpleCurve"), 1.5, Qt::DashDotLine);
    if (port == "mc_family_threshold")
        return QPen(themeColor("purpleCurve"), 1.8, Qt::DashLine);
    bool ar1 = port.startsWith("ar1");
    QColor c = themeColor(ar1 ? "secondaryCurve" : "tertiaryCurve");
    if (port.contains("background"))
        return QPen(c, 1.9, ar1 ? Qt::SolidLine : Qt::DashLine);
    if (port.startsWith("fdr"))
        return QPen(themeColor("purpleCurve"), 1.1, Qt::DashDotDotLine);
    if (port.endsWith("999"))
        c = c.darker(130);
    else if (port.endsWith("90"))
        c = c.lighter(135);
    return QPen(c, 1.25, ar1 ? Qt::DashLine : Qt::DotLine);
}
bool geometricAxis(const QVector<double> &values)
{
    return grid_display::geometric(values.size(), [&](qsizetype i) { return values[i]; });
}
}
PlotStyle plotPreset(const NodeResult &result, const QString &port, const QString &metric)
{
    const auto data = result.outputs.value(port);
    PlotStyle style;
    style.background = themeColor("plot");
    for (auto *a : {&style.x, &style.y})
    {
        a->axisColor = themeColor("axis");
        a->gridColor = themeColor("grid");
        a->subGridColor = themeColor("subGrid");
    }
    if (const auto *d = dataAs<SeriesData>(data))
    {
        style.x.label = axisLabel(d->xAxis);
        style.y.label = axisLabel(d->yAxis);
        if (port == "coi" && d->yAxis.name == "COI period")
        { style.y.logarithmic = true; style.y.reversed = true; style.y.logBase = 2; }
    }
    else if (const auto *d = dataAs<SpectrumData>(data))
    {
        style.x.label = axisLabel(d->frequencyAxis);
        style.y.label = axisLabel(d->powerAxis);
    }
    else if (const auto *d = dataAs<GridData>(data))
    {
        style.x.label = axisLabel(d->xAxis);
        style.y.label = axisLabel(d->yAxis);
        const auto samples = result.report["plot_samples"].toObject().value(port).toObject();
        style.y.logarithmic = samples.contains("y_geometric") ? samples["y_geometric"].toBool() : geometricAxis(d->y);
        // CWT periods increase downward; frequency and sedimentation-rate grids
        // retain their own coordinate direction. This changes presentation only.
        // The axis/COI fallback also supports results without report metadata.
        // Axis names here are stored identifiers,
        // not translated labels or user-edited display titles.
        const auto *coi = dataAs<SeriesData>(result.outputs.value("coi"));
        const bool periodFamily = d->yAxis.name == "Period" && coi &&
                                  coi->yAxis.name == "COI period" &&
                                  coi->yAxis.unit == d->yAxis.unit;
        const bool waveletPeriod = result.report.contains("wavelet_convention") || periodFamily;
        style.y.reversed = waveletPeriod;
        if (waveletPeriod) { style.y.logarithmic = true; style.y.logBase = 2; }
    }
    for (auto it = result.outputs.begin(); it != result.outputs.end(); ++it)
    {
        bool include =
            it.key() == port && !dataAs<TableData>(data) && !dataAs<GridData>(data);
        if (dataAs<SpectrumData>(data))
            include = compatibleCurveAxes(data, it.value()) || it.key() == "peaks";
        if (dataAs<SeriesData>(data))
            include = compatibleCurveAxes(data, it.value());
        if (dataAs<GridData>(data))
            include = it.key() == "coi" && dataAs<SeriesData>(it.value());
        if (!include)
            continue;
        CurveStyle c;
        c.name = result.report.value(it.key()).toString(portLabel(it.key()));
        auto pen = penFor(it.key(), 2., themeColor("curve"));
        c.color = pen.color();
        c.width = pen.widthF();
        c.pen = pen.style();
        c.visible = result.report["plot_explicit_ports"].toArray().contains(it.key()) || it.key().startsWith("overlay_") || it.key() == port || it.key() == "coi" ||
                    (port == "spectrum" &&
                     QStringList{"ar1_background", "ar1_95", "swa_background", "swa_95", "mc_background", "mc_family_threshold"}.contains(
                         it.key()));
        if (it.key() == port)
        {
            c.color = themeColor("curve");
            c.width = 2.;
            c.pen = Qt::SolidLine;
        }
        if (it.key().startsWith("overlay_"))
        {
            const QColor colors[] = {themeColor("secondaryCurve"), themeColor("tertiaryCurve"),
                                     themeColor("purpleCurve"), themeColor("pinkCurve")};
            c.color = colors[it.key().section('_', 1).toInt() % 4];
            c.width = 1.8;
        }
        if (it.key() == "coi")
        {
            c.name = text("ui.coi");
            c.color = themeColor("axis");
            c.width = 1.4;
            c.pen = Qt::DashLine;
        }
        if (it.key() == "peaks")
        {
            c.visible = port == "spectrum" || result.report["plot_explicit_ports"].toArray().contains(it.key());
            c.color = themeColor("secondaryCurve");
            c.line = false;
            c.marker = MarkerStyle::Circle;
        }
        c.themeDefaultColor = c.color;
        style.curves.insert(it.key(), c);
    }
    if ((port == "cross_spectrum" && result.report["cross_spectrum_schema"].toInt() == 1 && dataAs<TableData>(data)))
    {
        style.crossMetric = metric;
        const auto *table = dataAs<TableData>(data);
        const auto units = result.report["column_units"].toArray();
        const auto c = table->columns.indexOf(style.crossMetric);
        style.x.label = axisLabel({"Frequency", units[0].toString()});
        style.y.label = axisLabel({text((QString("cross.") + metric).toUtf8().constData()), c>=0 ? units[c].toString() : QString()});
        CurveStyle curve; curve.name = text((QString("cross.") + metric).toUtf8().constData()); curve.color = themeColor("curve");
        curve.themeDefaultColor = curve.color; curve.visible = true;
        style.curves[port] = curve;
    }
    return style;
}
}
