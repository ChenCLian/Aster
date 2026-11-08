// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_RESULTS_PAGE_H
#define ASTER_UI_RESULTS_PAGE_H
#include "ui/services/result_loader.h"
#include "ui/results/plot_view.h"
#include "ui/results/plot_workspace.h"
#include "ui/services/export_controller.h"
#include "ui/exports/figure_export_controller.h"
#include "core/task_registry.h"
#include <QWidget>
class QTableWidget;
class QTableView;
class QStandardItemModel;
class QAction;
class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QSplitter;
class QToolButton;
namespace aster::ui
{
class RecordDirectoryPanel;
class ResultExportPanel;
class PlotStyleEditor;
class ResultsPage : public QWidget
{
    Q_OBJECT
  public:
    explicit ResultsPage(TaskRegistry registry, QWidget *parent = nullptr);
    void setDatabase(const QString &path);
    void refresh(qint64 selectId = 0);
    void setDatabaseMutationsEnabled(bool enabled);
    void openRecord(qint64 id);
    qint64 recordId() const
    {
        return m_recordId;
    }
    PlotView *plotView() const
    {
        return m_plot;
    }
    int recordCount() const;
    PlotWorkspace *workspace() const
    {
        return m_workspace;
    }
    bool databaseMutationBusy() const { return m_directoryMutating; }
    bool databaseBusy() const
    {
        return m_directoryBusy;
    }
    ExportController *exporter() const
    {
        return m_exporter;
    }
    bool exportBusy() const
    {
        return m_exporter->busy() || m_figureExporter->busy();
    }
    void cancelExports()
    {
        m_exporter->cancel();
        m_figureExporter->cancel();
    }
    FigureExportController *figureExporter() const
    {
        return m_figureExporter;
    }
    bool startCsvExport(const QString &path);
    void selectStep(int row, bool display = false);
    QVector<OutputReference> checkedOutputs() const;
    void plotChecked(PlotWorkspace::Target target);
    QSplitter *splitter() const
    {
        return m_splitter;
    }
  signals:
    void message(QString text);
    void exportBusyChanged(bool busy);
    void databaseBusyChanged(bool busy);
    void databaseMutationBusyChanged(bool busy);
    void recordLoaded(qint64 id);
    void recordChanging();
    void sourceRequested(aster::OutputReference reference);

  private:
    QVector<OutputInfo> orderedOutputs(NodeId node) const;
    QHash<NodeId, QString> m_primaryPorts;
    QHash<NodeId, QString> m_nodeMethods;
    void rebuildOutputs();
    void inspectChecked();
    QTableView *m_outputs;
    QStandardItemModel *m_outputModel;
    QMap<QString, OutputReference> m_checked;
    QToolButton *m_checkedButton;
    QPushButton *m_plotChecked, *m_overlayChecked;
    QAction *m_replaceAction, *m_pageAction;
    QLabel *m_destination;
    RecordDirectoryPanel *m_directory;
    bool m_directoryBusy = false, m_directoryMutating = false;
    bool m_defaultPlotOnLoad = false;
    void showRecord(qint64 id, std::shared_ptr<const LoadResult> result);
    void showPort();
    void showErrorDetails();
    QString selectedErrorDetails() const;
    void updateOutputActions();
    OutputReference selectedOutput() const;
    void syncPlotSelection(const OutputReference &reference);
    OutputReference reference(NodeId node, const QString &port) const;
    OutputReference reference(NodeId node, const OutputInfo &output) const;
    PlotWorkspace *m_workspace;
    PlotStyleEditor *m_styleEditor;
    QString m_path;
    TaskRegistry m_registry;
    ResultLoader *m_loader;
    ExportController *m_exporter;
    FigureExportController *m_figureExporter;
    std::shared_ptr<const LoadResult> m_record;
    qint64 m_recordId = 0;
    qint64 m_requestedRecordId = 0;
    QTableWidget *m_steps;
    PlotView *m_plot;
    QLabel *m_title;
    QLabel *m_subtitle;
    QLabel *m_stepNotice;
    QPushButton *m_errorDetailsButton;
    QLabel *m_plotTarget;
    ResultExportPanel *m_exportPanel;
    QToolButton *m_addPlot;
    QLabel *m_details;
    QComboBox *m_ports;

    QSplitter *m_splitter;
    QVector<NodeId> m_nodeIds;
    NodeId m_selectedNode;
};
} // namespace aster::ui
#endif
