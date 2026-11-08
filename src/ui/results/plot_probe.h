// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_RESULTS_PLOT_PROBE_H
#define ASTER_UI_RESULTS_PLOT_PROBE_H
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QPen>
#include <functional>
#include <optional>
class QCustomPlot;
class QFrame;
class QLabel;
class QScrollArea;
class QWidget;
namespace aster::ui
{
// A value snapshot, never a pointer into a graph that refinement may delete.
struct ProbeValue
{
    QString label;
    double value;
};
struct ProbeSample
{
    QPointF pixel;
    QString title;
    QVector<ProbeValue> values;
    QString note;
    std::optional<QPen> pen;
};
class PlotProbe final : public QObject
{
  public:
    using Picker = std::function<std::optional<ProbeSample>(QPointF)>;
    PlotProbe(QCustomPlot *plot, Picker picker);
    void dismiss();
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    void inspect(QPointF point);
    QCustomPlot *m_plot;
    Picker m_picker;
    QFrame *m_bubble, *m_marker;
    QLabel *m_title, *m_swatch;
    QWidget *m_details;
    QScrollArea *m_scroll;
    QString m_copyText;
    QPointF m_pressed;
    bool m_pressValid = false, m_dragged = false;
};
}
#endif
