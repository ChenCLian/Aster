// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_PLOT_VIEW_H
#define ASTER_UI_PLOT_VIEW_H
#include "core/task.h"
#include "io/database/output_reference.h"
#include "ui/results/plot_style.h"
#include "ui/results/plot_probe.h"
#include <QWidget>
#include <QCache>
#include <QTimer>
#include <QJsonArray>
#include "io/database/plot_request.h"
class QCustomPlot;
class QCPAxis;
class QCPColorScale;
class QCPTextElement;
class QStackedWidget;
class QTableView;
class QComboBox;
class QLabel;
class QMenu;
namespace aster::ui
{
class NumericalTableModel;
class ContourPlottable;
class PlotView : public QWidget
{
    Q_OBJECT
  public:
    explicit PlotView(QWidget *parent = nullptr);
    void showOutput(const NodeResult &result, const QString &port, const QString &viewKey = {});
    void clear(const QString &message = {});
    void clearByUser();
    void setOutputReference(const OutputReference &reference)
    {
        m_reference = reference;
    }
    void scheduleRefinement() { m_viewportTimer.start(); }
    QString resolutionDescription() const;
    void fit();
    void fitAutomaticAxes();
    void refreshOutput(const NodeResult &result);
    PlotRequest plotRequest(int width = 0, int height = 0) const;
    QCustomPlot *plotWidget() const
    {
        return m_plot;
    }
    void setLegend(bool visible);
    void setLogY(bool enabled);
    void setLineWidth(double width);
    void setMainColor(QColor color);
    const PlotStyle &style() const
    {
        return m_style;
    }
    QString applyStyle(const PlotStyle &style);
    void resetStyle();
    PlotStyle styleWithViewport() const;
    // Preserve automatic axes when reloading data; exports freeze both ranges.
    PlotStyle styleForReload() const;
    bool canLogX() const;
    bool canLogY() const;
    bool hasPlot() const;
    QMenu *curveMenu(QWidget *owner);
    QString exportCsv(const QString &path) const; // Synchronous API for non-interactive callers.
    bool exportPng(const QString &path) const;
    bool hasOutput() const
    {
        return bool(m_data);
    }
    int plottedCount() const;
    bool hasSignificanceGrid() const;
    QString heatmapStatus() const;
    QString contourError() const;
    int significanceSegmentCount() const;
    QString currentPort() const
    {
        return m_port;
    }
    DataRef currentData() const
    {
        return m_data;
    }
  signals:
    void activated();
    void viewportChanged();
    void styleChanged();
    void outputChanged();
    void clearedByUser(); // Explicit clear command, never a load or error state.
    void displayDataChanged(); // Zoom refinement; preserve unfinished style edits.

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    struct SavedView
    {
        PlotStyle style;
        double xLow, xHigh, yLow, yHigh;
    };
    PlotStyle defaults() const;
    bool isCrossSpectrum() const;
    bool crossPositive(bool x, const QString &metric = {}) const;
    QComboBox *m_crossMetric;
    QCPAxis *keyAxis() const;
    QCPAxis *valueAxis() const;
    void rememberView();
    void draw();
    std::optional<ProbeSample> sampleAt(QPointF point) const;
    void applyAppearance();
    void fitRanges();
    double displayedY(double original) const;
    double originalY(double displayed) const;
    QCustomPlot *m_plot;
    QCPColorScale *m_scale;
    QCPTextElement *m_title;
    QStackedWidget *m_stack;
    QTableView *m_table;
    NumericalTableModel *m_tableModel;
    QLabel *m_empty;
    OutputReference m_reference;
    NodeResult m_result;
    QString m_port, m_viewKey;
    DataRef m_data;
    PlotStyle m_style, m_defaults;
    QCache<QString, SavedView> m_views{128};
    quint64 m_generation = 0;
    bool m_gridLog = false;
    ContourPlottable *m_contour = nullptr; // Owned by QCustomPlot; reset before clearPlottables.
    int m_contourState = 1; // ContourState::NoBoundary
    double m_colorMin = 0, m_colorMax = 1, m_positiveMin = 0, m_positiveMax = 0;
    qsizetype m_nonPositiveCells = 0;
    bool m_rendering = false;
    QTimer m_viewportTimer;
    QTimer m_presentTimer;
    PlotProbe *m_probe;
    QJsonArray m_fullBounds;
    QString m_backgroundPath;
    QPixmap m_backgroundImage;
};
} // namespace aster::ui
#endif
