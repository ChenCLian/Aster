// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_DATABASE_TRANSFER_H
#define ASTER_DATABASE_TRANSFER_H
#include "core/task.h"
#include <QMap>
#include <functional>

namespace aster
{
struct DatabaseTransferResult
{
    QString error, backupPath;
    qint64 imported = 0, duplicates = 0, externalReferences = 0;
    QMap<qint64, qint64> recordIds; // Source ID -> destination ID, published only after commit.
    bool cancelled = false;
    bool ok() const
    {
        return error.isEmpty() && !cancelled;
    }
};
// Stage identifiers are translated by the UI. SQL and payload work stays off the GUI thread.
using DatabaseProgress = std::function<void(QString stage, qint64 completed, qint64 total)>;
DatabaseTransferResult mergeDatabase(const QString &destination, const QString &source,
                                     const CancellationToken &cancel = {},
                                     const DatabaseProgress &progress = {});
// Writes a new file only. Never overwrites a database or a pre-existing backup.
DatabaseTransferResult backupDatabase(const QString &source, const QString &newFile,
                                      const CancellationToken &cancel = {},
                                      const DatabaseProgress &progress = {});
} // namespace aster
#endif
