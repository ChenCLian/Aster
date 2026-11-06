// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/presentation.h"
#include "ui/services/export_controller.h"
#include <QThread>
#include <QPointer>
#include "io/database/result_repository.h"
#include "io/exports/export_path.h"
namespace aster::ui
{
ExportController::~ExportController()
{
    m_cancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
bool ExportController::start(DataRef data, const QString &path)
{
    return launch(data, {}, path);
}
bool ExportController::startOutput(OutputReference reference, const QString &path)
{
    return launch({}, reference, path);
}
bool ExportController::launch(DataRef data, OutputReference reference, const QString &path)
{
    if (busy() || (!data && !reference.valid()) || path.isEmpty())
        return false;
    if (conflictsWithDatabase(path, {reference.databasePath}))
    {
        emit finished(path, text("error.cannot_overwrite_a_source_database"), false);
        return false;
    }
    m_cancel = CancellationToken();
    auto result = std::make_shared<CsvExportResult>();
    m_worker = QThread::create(
        [this, data = std::move(data), reference, path, result, cancel = m_cancel]() mutable
        {
            try
            {
                if (!data)
                {
                    ResultRepository repo;
                    result->error = repo.open(reference.databasePath, false);
                    if (!result->error.isEmpty())
                        return;
                    auto loaded = repo.loadOutput(reference);
                    result->error = loaded.error;
                    if (!loaded.ok())
                        return;
                    data = loaded.data;
                }
                *result =
                    exportDataCsv(data, path, cancel, [this](int n) { emit progressChanged(n); });
            }
            catch (const std::exception &e)
            {
                result->error = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                result->error = text("error.unexpected_export_error");
            }
        });
    connect(m_worker, &QThread::finished, this,
            [this, path, result]
            {
                auto *worker = m_worker;
                // Complete the old job before allowing another start from an
                // idle notification. Receivers may also delete the controller.
                const QPointer<ExportController> guard(this);
                emit finished(path, result->error, result->cancelled);
                if (!guard)
                    return;
                m_worker = nullptr;
                worker->deleteLater();
                emit busyChanged(false);
            });
    emit busyChanged(true);
    m_worker->start();
    return true;
}
} // namespace aster::ui
