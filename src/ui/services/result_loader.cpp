// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/result_loader.h"
#include <QThread>
namespace aster::ui
{
ResultLoader::~ResultLoader()
{
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
void ResultLoader::invalidate()
{
    ++m_generation;
    m_pending = false;
}
void ResultLoader::request(const QString &p, qint64 id, RecordFilter filter)
{
    m_path = p;
    m_filter = filter;
    m_id = id;
    ++m_generation;
    m_pending = true;
    if (!m_worker)
        start();
}
void ResultLoader::start()
{
    m_pending = false;
    const auto generation = m_generation;
    const auto path = m_path;
    const auto id = m_id;
    const auto filter = m_filter;
    m_worker = QThread::create(
        [this, generation, path, id, filter]
        {
            auto result = std::make_shared<LoadResult>();
            try
            {
                ResultRepository repo;
                // Browsing an existing record must not recreate a database that
                // has been moved/deleted since the directory was populated.
                result->error = repo.open(path, false);
                if (result->error.isEmpty())
                    *result = repo.load(id, true);
                if (result->ok() && filter.hasFrequency())
                {
                    const auto matches = repo.matchingPeaks(id, filter);
                    result->matchingPeaks = matches.peaks;
                    result->error = matches.error;
                }
            }
            catch (const std::exception &e)
            {
                result->error = QString::fromUtf8(e.what());
            }
            QMetaObject::invokeMethod(
                this,
                [this, generation, id, result]
                {
                    if (generation == m_generation)
                        emit ready(id, result);
                },
                Qt::QueuedConnection);
        });
    connect(m_worker, &QThread::finished, this,
            [this]
            {
                auto *w = m_worker;
                m_worker = nullptr;
                w->deleteLater();
                if (m_pending)
                    start();
            });
    m_worker->start();
}
} // namespace aster::ui
