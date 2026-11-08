// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_PLOT_STYLE_H
#define ASTER_UI_PLOT_STYLE_H
#include <QColor>
#include <QMap>
#include <QString>
namespace aster::ui
{
enum class LegendPosition
{
    TopRight,
    TopLeft,
    BottomRight,
    BottomLeft
};
enum class MarkerStyle
{
    None,
    Circle,
    Square,
    Triangle,
    Cross,
    Plus,
    Diamond,
    Star,
    TriangleInverted,
    CrossSquare,
    PlusSquare,
    CrossCircle,
    PlusCircle,
    Peace
};
struct AxisStyle
{
    QString label;
    bool logarithmic = false, reversed = false, grid = true, manualRange = false;
    bool axisVisible = true, ticks = true, tickLabels = true, subTicks = false;
    bool secondaryVisible = false, secondaryTicks = true, secondaryLabels = false, subGrid = false;
    int tickCount = 6, precision = 6;
    double logBase = 10, axisWidth = 1, gridWidth = 1, subGridWidth = 1;
    QString numberFormat = "g";
    QColor axisColor{"#b4c1d3"}, gridColor{"#e8edf5"}, subGridColor{"#f1f4f8"};
    Qt::PenStyle axisPen = Qt::SolidLine, gridPen = Qt::DotLine, subGridPen = Qt::DotLine;
    bool independentSecondary = false;
    int secondaryCount = 6, secondaryPrecision = 6;
    double secondaryWidth = 1;
    QColor secondaryColor{"#b4c1d3"};
    Qt::PenStyle secondaryPen = Qt::SolidLine;
    QString secondaryFormat = "g";
    double lower = 0, upper = 1; // Always original coordinates, including CWT period.
};
struct CurveStyle
{
    QString name;
    QColor color;
    QColor themeDefaultColor; // Metadata used to retheme inactive pages without overwriting custom
                              // colors.
    double width = 1.5, markerSize = 5;
    Qt::PenStyle pen = Qt::SolidLine;
    MarkerStyle marker = MarkerStyle::None;
    bool visible = true, line = true, inLegend = true, fill = false, markerFilled = false;
    int connection = 1; // QCPGraph line/step-left/step-right/step-center/impulse, 1..5.
    Qt::BrushStyle fillPattern = Qt::SolidPattern;
    bool customMarkerBorder = false;
    QColor markerBorderColor{Qt::black};
    QColor fillColor{50, 110, 190, 45}, markerColor{Qt::white};
};
struct PlotStyle
{
    QString title;
    QString crossMetric = "coherence";
    double axisFontSize = 9.5, legendFontSize = 8.5, titleFontSize = 12;
    bool followTheme = true;
    QString backgroundImage;
    bool backgroundKeepAspect = true;
    int colorGradient = 4; // blue-green-yellow / grayscale / thermal / polar / rainbow
    bool colorLog = false, colorManual = false, colorInterpolate = false;
    double colorLower = 0, colorUpper = 1;
    bool colorReverse = false, colorScaleVisible = true;
    int colorLevels = 256;
    bool significanceContour = true; // Ratio = power / local threshold; contour at 1.
    QColor significanceColor{Qt::black};
    double significanceWidth = 1.25;
    QColor background{Qt::white};
    bool legend = true, swapXY = false;
    LegendPosition legendPosition = LegendPosition::TopRight;
    AxisStyle x, y;
    QMap<QString, CurveStyle> curves; // Stable output keys, never QCP object pointers.
};
} // namespace aster::ui
#endif
