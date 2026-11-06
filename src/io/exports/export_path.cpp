// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/exports/export_path.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
namespace aster
{
namespace
{
QSet<QString> aliases(const QString &path)
{
    const QFileInfo info(path);
    QSet<QString> names{info.absoluteFilePath()};
    if (!info.canonicalFilePath().isEmpty())
        names.insert(info.canonicalFilePath());
    // Also resolves a symlinked parent for sidecars which do not yet exist.
    const auto parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (!parent.isEmpty())
        names.insert(QDir(parent).filePath(info.fileName()));
#ifdef Q_OS_WIN
    QSet<QString> folded;
    for (const auto &name : names)
        folded.insert(name.toCaseFolded());
    return folded;
#else
    return names;
#endif
}
} // namespace
bool conflictsWithDatabase(const QString &target, const QStringList &databases)
{
    const auto targets = aliases(target);
    for (const auto &database : databases)
    {
        if (database.isEmpty() || database == ":memory:")
            continue;
        for (const auto &base : aliases(database))
            for (const auto &suffix : {"", "-wal", "-shm", "-journal"})
                if (targets.intersects(aliases(base + suffix)))
                    return true;
    }
    return false;
}
} // namespace aster
