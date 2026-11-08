// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_FIGURE_EXPORT_H
#define ASTER_FIGURE_EXPORT_H
#include "ui/results/plot_style.h"
#include "io/database/plot_request.h"
#include "io/database/output_reference.h"
#include <QSize>
#include <QRectF>
namespace aster::ui
{
struct FigureCell
{
    QVector<OutputReference> references;
    PlotStyle style;
    PlotRequest request;
    // Normalized page rectangle; null requests the default arrangement.
    QRectF area{};
};
struct FigureOptions
{
    enum class Format
    {
        Png,
        Pdf
    };
    QString path;
    Format format = Format::Png;
    QSize pixels{2400, 1600};
    int dpi = 200;
    bool whitePaper = true;
};
QVector<QRect> figureCellRects(const QVector<FigureCell> &cells, QSize pixels);
QString validateFigureOptions(const FigureOptions &options);
QString renderFigure(const QVector<FigureCell> &cells, const QVector<NodeResult> &data,
                     const FigureOptions &options);
} // namespace aster::ui
#endif
