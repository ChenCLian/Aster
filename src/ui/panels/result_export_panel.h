// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RESULT_EXPORT_PANEL_H
#define ASTER_RESULT_EXPORT_PANEL_H
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
namespace aster::ui
{
// Presentation only; ResultsPage owns output identity and export controllers.
class ResultExportPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit ResultExportPanel(QWidget *parent = nullptr);
    QComboBox *ports() const { return m_ports; }
    void setTargets(const QString &dataTarget, bool hasData,
                    const QString &figureTarget, bool hasFigure);
    void setBusy(bool busy, bool figureExport = false);
    void setProgress(int value);
    void setStatus(const QString &message);
  signals:
    void exportDataRequested();
    void exportFigureRequested();
    void reuseRequested();
    void cancelRequested();
    void outputActivated();
  private:
    void updateActions();
    QComboBox *m_ports;
    QLabel *m_dataTarget, *m_figureTarget, *m_status;
    QPushButton *m_csv, *m_figure, *m_reuse, *m_cancel;
    QProgressBar *m_progress;
    bool m_busy = false, m_hasData = false, m_hasFigure = false;
};
} // namespace aster::ui
#endif
