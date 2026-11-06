// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_FIGURE_EXPORT_CONTROLLER_H
#define ASTER_FIGURE_EXPORT_CONTROLLER_H
#include "ui/exports/figure_export.h"
#include <QObject>
class QThread;
namespace aster::ui
{
class FigureExportController : public QObject
{
    Q_OBJECT
  public:
    explicit FigureExportController(QObject *parent = nullptr) : QObject(parent) {}
    ~FigureExportController() override;
    bool start(QVector<FigureCell> cells, FigureOptions options);
    bool busy() const
    {
        return m_busy;
    }
    void cancel()
    {
        m_cancel.cancel();
    }
  signals:
    void busyChanged(bool busy);
    void finished(QString path, QString error, bool cancelled);

  private:
    bool m_busy = false;
    QThread *m_worker = nullptr;
    CancellationToken m_cancel;
};
} // namespace aster::ui
#endif
