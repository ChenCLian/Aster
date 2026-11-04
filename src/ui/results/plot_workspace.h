#ifndef ASTER_PLOT_WORKSPACE_H
#define ASTER_PLOT_WORKSPACE_H
#include "ui/results/plot_view.h"
#include "ui/exports/figure_export.h"
#include "io/database/output_reference.h"
#include <QPointer>
#include <QVector>
class QTabWidget;
class QGridLayout;
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
        Overlay
    };
    explicit PlotWorkspace(QWidget *parent = nullptr);
    ~PlotWorkspace() override;
    PlotView *currentView() const;
    OutputReference currentReference() const;
    void open(const OutputReference &reference, Target target = Target::Current);
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
        QVector<OutputReference> refs;
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
    };
    struct Job
    {
        QUuid page, cell;
        quint64 generation;
        QVector<OutputReference> refs;
        PlotRequest request;
        bool refine = false;
    };
    void activate(int index);
    void build();
    void queue(Cell &cell, bool refine = false);
    void pump();
    void select(QUuid cell);
    void store();
    QTabWidget *m_tabs;
    QVector<Page> m_pages;
    int m_active = -1;
    QUuid m_selected;
    QVector<Job> m_jobs;
    QThread *m_worker = nullptr;
    CancellationToken m_cancel;
    QUuid m_workerCell;
    QPointer<PlotView> m_empty;
};
} // namespace aster::ui
#endif
