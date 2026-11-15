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
#include <QTimer>
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
    QString key; // Stable output key; never a pointer into a resampled graph.
    QPointF coordinates; // Original coordinates, before log/swap display transforms.
};
class PlotProbe final : public QObject
{
    Q_OBJECT
  public:
    using Picker = std::function<std::optional<ProbeSample>(QPointF)>;
    using Projector = std::function<std::optional<QPointF>(const ProbeSample &)>;
    PlotProbe(QCustomPlot *plot, Picker picker, Projector projector);
    ~PlotProbe() override;
    void dismiss();
    bool hasBubbles() const { return !m_entries.isEmpty(); }
    void refresh();
  signals:
    void changed();
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    struct Entry;
    void inspect(QPointF point);
    void populate(Entry &entry);
    void relayout(Entry &entry);
    void place(Entry &entry);
    void close(Entry *entry);
    QCustomPlot *m_plot;
    Picker m_picker;
    Projector m_projector;
    QVector<Entry *> m_entries;
    Entry *m_active = nullptr, *m_dragging = nullptr;
    QPointF m_pressed;
    QPoint m_dragOffset;
    bool m_pressValid = false, m_dragged = false;
    QTimer m_refresh;
};
}
#endif
