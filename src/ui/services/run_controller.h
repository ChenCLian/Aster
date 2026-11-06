// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_RUN_CONTROLLER_H
#define ASTER_UI_RUN_CONTROLLER_H
#include "application/batch_runner.h"
#include <QObject>
class QThread;
Q_DECLARE_METATYPE(aster::RunEvent)
Q_DECLARE_METATYPE(aster::BatchItem)
Q_DECLARE_METATYPE(std::shared_ptr<const aster::BatchResult>)
namespace aster::ui
{
class RunController : public QObject
{
    Q_OBJECT
  public:
    explicit RunController(QObject *parent = nullptr);
    ~RunController() override;
    bool busy() const
    {
        return m_worker != nullptr;
    }
    bool start(const RunPlan &plan, const QString &path, int concurrency);
    bool retry(const QVector<std::shared_ptr<const RunResult>> &results, const QString &path);
    void cancel()
    {
        m_cancel.cancel();
    }
  signals:
    void busyChanged(bool busy);
    void eventReady(aster::RunEvent event);
    void itemReady(aster::BatchItem item);
    void batchReady(std::shared_ptr<const aster::BatchResult> result);

  private:
    bool launch(std::function<BatchResult()> work);
    QThread *m_worker = nullptr;
    CancellationToken m_cancel;
};
} // namespace aster::ui
#endif
