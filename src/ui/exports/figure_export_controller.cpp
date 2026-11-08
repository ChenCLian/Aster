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
    const auto rectangles = figureCellRects(cells, options.pixels);
    if (rectangles.size() != cells.size() || rectangles.isEmpty())
        error = text(cells.isEmpty() ? "error.no_figure_selected" : "error.invalid_figure_layout");
    if (error.isEmpty()) for (const auto &rect : rectangles)
        if (qRound(rect.width() * 96. / options.dpi) < 220 || qRound(rect.height() * 96. / options.dpi) < 160)
            error = text("error.increase_export_size_or_reduce_dpi_for_this_subplot_layout");
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
    m_worker = QThread::create(
        [cells, rectangles, cancel, data, failure]
        {
            try
            {
                for (int i = 0; i < cells.size(); ++i)
                {
                    const auto &cell = cells[i];
                    if (cancel.isCancelled())
                        return;
                    auto request = cell.request;
                    request.columns = rectangles[i].width();
                    request.rows = rectangles[i].height();
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
