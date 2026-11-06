// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/file_preview_loader.h"
#include <QThread>
#include <algorithm>
namespace aster::ui
{
namespace
{
FilePreviewData readPreview(const QString &path, const QJsonObject &value,
                            const CancellationToken &cancel)
{
    FilePreviewData result;
    SeriesFileOptions options;
    const auto error = applyFileOptions(value, options);
    if (!error.isEmpty())
    {
        result.failure = NodeResult::failure(makeError(ErrorCode::FileOptions, {}, error));
        return result;
    }
    auto read = readSeriesFile(path, options, cancel);
    if (!read.ok())
    {
        result.failure = NodeResult::failure(read.error);
        result.failure.report = std::move(read.report);
        return result;
    }
    const auto &series = *dataAs<SeriesData>(read.data);
    result.rows = series.x.size();
    result.skippedRows = qsizetype(read.report["skipped_rows"].toDouble());
    const auto display = reducePreviewData(NodeResult::success({{"series", read.data}}), cancel);
    if (cancel.isCancelled()) return {};
    if (const auto *samples = dataAs<SeriesData>(display.outputs.value("series")))
        result.samples = *samples;
    return result;
}
} // namespace
FilePreviewLoader::FilePreviewLoader(QObject *parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(140);
    connect(&m_debounce, &QTimer::timeout, this, [this]
    {
        m_due = true;
        if (!m_worker) start();
    });
}
FilePreviewLoader::~FilePreviewLoader()
{
    m_cancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
void FilePreviewLoader::setInput(const QString &path, const QJsonObject &options, bool ready)
{
    if (path == m_path && options == m_options && ready == m_ready)
        return;
    auto parsingOptions = options, previousParsing = m_options;
    for (const auto &key : {"x_unit", "y_unit"})
    {
        parsingOptions.remove(key);
        previousParsing.remove(key);
    }
    if (path == m_path && ready && m_state == State::Ready && parsingOptions == previousParsing)
    {
        // Units change labels, not numerical samples. Reuse the current preview only.
        SeriesFileOptions checked;
        if (applyFileOptions(options, checked).isEmpty())
        {
            auto result = std::make_shared<FilePreviewData>(*m_result);
            result->samples.xAxis.unit = checked.xUnit;
            result->samples.yAxis.unit = checked.yUnit;
            m_result = result;
            m_options = options;
            emit changed();
            return;
        }
    }
    m_path = path;
    m_options = options;
    m_ready = ready;
    resetRequest();
}
void FilePreviewLoader::setActive(bool active)
{
    if (active == m_active) return;
    m_active = active;
    resetRequest();
}
void FilePreviewLoader::resetRequest()
{
    ++m_generation;
    m_cancel.cancel();
    m_debounce.stop();
    m_due = false;
    m_result.reset();
    m_state = !m_active ? State::Inactive : m_path.isEmpty() ? State::Empty
                           : !m_ready ? State::Waiting : State::Loading;
    emit changed();
    if (m_state == State::Loading) m_debounce.start();
}
void FilePreviewLoader::start()
{
    m_due = false;
    const auto generation = m_generation;
    const auto path = m_path;
    const auto options = m_options;
    m_cancel = CancellationToken();
    const auto cancel = m_cancel;
    emit readStarted(path);
    m_worker = QThread::create([this, generation, path, options, cancel]
    {
        auto result = std::make_shared<FilePreviewData>();
        try
        {
            *result = readPreview(path, options, cancel);
        }
        catch (const std::exception &e)
        {
            result->failure = NodeResult::failure(QString::fromUtf8(e.what()));
        }
        catch (...)
        {
            result->failure = NodeResult::failure(makeError(ErrorCode::Unknown));
        }
        if (cancel.isCancelled()) return;
        QMetaObject::invokeMethod(this, [this, generation, result]
        {
            if (generation != m_generation) return;
            m_result = result;
            m_state = result->ok() ? State::Ready : State::Failed;
            emit changed();
        }, Qt::QueuedConnection);
    });
    connect(m_worker, &QThread::finished, this, [this]
    {
        auto *worker = m_worker;
        m_worker = nullptr;
        worker->deleteLater();
        if (m_due) start();
    });
    m_worker->start();
}
} // namespace aster::ui
