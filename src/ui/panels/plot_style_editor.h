// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_PLOT_STYLE_EDITOR_H
#define ASTER_UI_PLOT_STYLE_EDITOR_H
#include "ui/results/plot_style.h"
#include <QWidget>
#include <QPointer>
#include <QTimer>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTabBar;
class QPushButton;
namespace aster::ui
{
class PlotView;
class AxisDetailsEditor;
class ColorButton;
class CanvasDetailsEditor;
class HeatmapStyleEditor;
class PlotStyleEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit PlotStyleEditor(PlotView *view, QWidget *parent = nullptr);
    void reload();
    void setView(PlotView *view);
    bool apply();

  private:
    void syncOutputAvailability();
    void refreshDisplayInfo();
    void syncViewportRanges();
    void scheduleApply(int delayMs = 0);
    void connectLiveControls();
    void storeAxis();
    void loadAxis();
    void storeCurve();
    void loadCurve();
    QPointer<PlotView> m_view;
    PlotStyle m_draft;
    bool m_loading = false;
    bool m_applying = false;
    bool m_rangeEdited[2] = {false, false};
    QTimer m_liveTimer;
    int m_axisIndex = 0;
    QString m_curveKey;
    QLineEdit *m_axisLabel, *m_lower, *m_upper, *m_curveName;
    QCheckBox *m_log, *m_reverse, *m_grid, *m_manual, *m_visible, *m_line;
    QTabBar *m_axis;
    QComboBox *m_curve, *m_pen, *m_marker;
    QDoubleSpinBox *m_width, *m_markerSize;
    ColorButton *m_color;
    QLabel *m_error;
    QWidget *m_curveFields;
    AxisDetailsEditor *m_axisDetails;
    QCheckBox *m_inLegend, *m_fill, *m_markerFilled;
    QComboBox *m_connection;
    ColorButton *m_fillColor, *m_markerColor, *m_markerBorder;
    QCheckBox *m_customBorder;
    QComboBox *m_brush;
    CanvasDetailsEditor *m_canvasDetails;
    HeatmapStyleEditor *m_heatmap;
    QTabWidget *m_tabs;
};
} // namespace aster::ui
#endif
