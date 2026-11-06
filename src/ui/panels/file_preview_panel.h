// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_FILE_PREVIEW_PANEL_H
#define ASTER_UI_FILE_PREVIEW_PANEL_H
#include <QFrame>
#include <QJsonObject>
#include "core/task.h"
class QCustomPlot; class QCPCurve; class QLabel;
class QShowEvent; class QHideEvent;
namespace aster::ui
{
class FilePreviewLoader;
struct FilePreviewData;
// Embedded raw-file view. The containing preview column owns collapse/resize behavior.
class FilePreviewPanel : public QFrame
{
    Q_OBJECT
  public:
    explicit FilePreviewPanel(QWidget *parent = nullptr);
    void setInput(const QString &path, const QJsonObject &options, bool ready);
    void setWorkflowSource(const NodeResult &source);
  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
  private:
    void updatePreview(); void updateTitle(); void applyColors(); void fit();
    QString m_path;
    QJsonObject m_options;
    bool m_ready = false;
    std::shared_ptr<const FilePreviewData> m_executionSource;
    FilePreviewLoader *m_loader;
    QLabel *m_title, *m_filename, *m_message, *m_summary;
    QWidget *m_body;
    QCustomPlot *m_plot;
    QCPCurve *m_curve;
};
}
#endif
