// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_WORKSPACE_H
#define ASTER_PLOT_WORKSPACE_H
#include "ui/results/plot_view.h"
#include "ui/results/plot_layout_widget.h"
#include "ui/exports/figure_export.h"
#include "io/database/output_reference.h"
#include <QPointer>
#include <QVector>
class QTabWidget;
class QThread;
namespace aster::ui
{
class PlotWorkspace : public QWidget
{
    Q_OBJECT
  public:
    enum class Target
    {
        Current,
        Subplot,
        Page,
        Overlay,
        Replace,
        Separate
    };
    explicit PlotWorkspace(QWidget *parent = nullptr);
    ~PlotWorkspace() override;
    PlotView *currentView() const;
    OutputReference currentReference() const;
    bool currentIsEmpty() const;
    bool canOverlaySelection(const QVector<OutputReference> &references) const;
    void open(const OutputReference &reference, Target target = Target::Current);
    // Navigation reuses an existing primary output; it never replaces a figure.
    void openDefault(const OutputReference &reference);
    void setDefaultPlotNewPage(bool enabled) { m_defaultPlotNewPage = enabled; }
    // Batch commands freeze stable references and reject capacity/type errors
    // before changing pages. Overlay/replacement commit only after successful I/O.
    void openBatch(const QVector<OutputReference> &references, Target target);
    QString destinationLabel() const;
    void closePage(int index);
    void closeSubplot();
    void reset();
    void forgetRecord(const QString &databasePath, qint64 recordId);
    QVector<FigureCell> figureCells(bool wholePage) const;
    int pageCount() const;
    int subplotCount() const;
    bool loading() const
    {
        return m_worker != nullptr || !m_jobs.isEmpty();
    }
  signals:
    void currentViewChanged(PlotView *view);
    void selectionChanged(aster::OutputReference reference);
    void message(QString text);
    void outputReady();

  private:
    struct Cell
    {
        QUuid id = QUuid::createUuid();
        QVector<OutputReference> refs, pendingRefs;
        bool replacing = false;
        PlotStyle style;
        bool saved = false;
        QPointer<PlotView> view;
        quint64 generation = 0;
    };
    struct Page
    {
        QUuid id = QUuid::createUuid();
        QString label;
        QVector<Cell> cells;
        QPointer<QWidget> widget;
        QPointer<PlotLayoutWidget> host;
        PlotArrangement arrangement;
        bool customArrangement = false;
    };
    struct Job
    {
        QUuid page, cell;
        quint64 generation;
        QVector<OutputReference> refs;
        PlotRequest request;
        bool refine = false;
        bool transaction = false, replacing = false;
    };
    void openReferences(const QVector<OutputReference> &references, Target target);
    void activate(int index);
    void build();
    void reflow(Page &page);
    void queue(Cell &cell, bool refine = false);
    void pump();
    void invalidate(Cell &cell);
    void clearCell(QUuid cell);
    void showEmpty(Cell &cell);
    void select(QUuid cell);
    void store();
    QTabWidget *m_tabs;
    QVector<Page> m_pages;
    int m_active = -1;
    bool m_defaultPlotNewPage = true;
    QUuid m_selected;
    QVector<Job> m_jobs;
    QThread *m_worker = nullptr;
    CancellationToken m_cancel;
    QUuid m_workerCell;
    QPointer<PlotView> m_empty;
};
} // namespace aster::ui
#endif
