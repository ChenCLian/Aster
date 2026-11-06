// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/presentation.h"
#include "ui/exports/figure_export_controller.h"
#include "ui/services/plot_data_loader.h"
#include <QThread>
#include <QPointer>
#include <QFileInfo>
#include "io/exports/export_path.h"
namespace aster::ui
{
FigureExportController::~FigureExportController()
{
    m_cancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
bool FigureExportController::start(QVector<FigureCell> cells, FigureOptions options)
{
    if (m_busy)
        return false;
    auto error = validateFigureOptions(options);
    if (cells.isEmpty())
        error = text("error.no_figure_selected");
    for (const auto &cell : cells)
        for (const auto &ref : cell.references)
        {
            if (conflictsWithDatabase(options.path, {ref.databasePath}))
                error = text("error.cannot_overwrite_a_source_database");
        }
    if (!error.isEmpty())
    {
        emit finished(options.path, error, false);
        return false;
    }
    m_busy = true;
    m_cancel = CancellationToken();
    auto cancel = m_cancel;
    auto data = std::make_shared<QVector<NodeResult>>();
    auto failure = std::make_shared<QString>();
    const int columns = cells.size() > 1 ? 2 : 1, rows = (cells.size() + columns - 1) / columns;
    m_worker = QThread::create(
        [cells, options, cancel, data, failure, columns, rows]
        {
            try
            {
                for (const auto &cell : cells)
                {
                    if (cancel.isCancelled())
                        return;
                    auto request = cell.request;
                    request.columns = options.pixels.width() / columns;
                    request.rows = options.pixels.height() / rows;
                    if (cell.style.swapXY)
                        std::swap(request.columns, request.rows);
                    auto result = loadPlotData(cell.references, request, cancel);
                    if (result.state != NodeState::Succeeded)
                    {
                        *failure = result.error;
                        return;
                    }
                    data->push_back(std::move(result));
                }
            }
            catch (const std::exception &e)
            {
                *failure = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                *failure = text("error.unexpected_figure_preparation_error");
            }
        });
    connect(m_worker, &QThread::finished, this,
            [this, cells, options, cancel, data, failure]
            {
                auto *thread = m_worker;
                m_worker = nullptr;
                thread->deleteLater();
                if (!cancel.isCancelled() && failure->isEmpty())
                {
                    try
                    {
                        *failure = renderFigure(cells, *data, options);
                    }
                    catch (const std::exception &e)
                    {
                        *failure = QString::fromUtf8(e.what());
                    }
                    catch (...)
                    {
                        *failure = text("error.unexpected_figure_rendering_error");
                    }
                }
                const QPointer<FigureExportController> guard(this);
                emit finished(options.path, *failure, cancel.isCancelled());
                if (!guard)
                    return;
                m_busy = false;
                emit busyChanged(false);
            });
    emit busyChanged(true);
    m_worker->start();
    return true;
}
} // namespace aster::ui
