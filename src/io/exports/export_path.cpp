// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/exports/export_path.h"
#include "io/database/record_name.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
namespace aster
{
namespace
{
QString fileNamePart(QString value, int byteLimit)
{
    static const QRegularExpression invalid(R"([\\/:*?"<>|\x00-\x1f\x7f]+)");
    value = value.trimmed().replace(invalid, "_");
    const auto utf8 = value.toUtf8();
    if (utf8.size() > byteLimit)
    {
        while ((static_cast<unsigned char>(utf8.at(byteLimit)) & 0xc0) == 0x80) --byteLimit;
        value = QString::fromUtf8(utf8.constData(), byteLimit);
    }
    while (value.endsWith('.') || value.endsWith(' ')) value.chop(1);
    return value;
}

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
QString suggestedExportFileName(const QVector<OutputReference> &references, const QString &detail,
                                const QString &extension)
{
    QVector<OutputReference> records;
    QSet<QPair<QString, QUuid>> seen;
    for (const auto &ref : references)
    {
        const auto key = qMakePair(ref.databasePath, ref.runId);
        if (ref.valid() && !seen.contains(key))
        {
            seen.insert(key);
            records.push_back(ref);
        }
    }
    QStringList parts;
    for (qsizetype i = 0; i < records.size() && i < 2; ++i)
    {
        const auto part = fileNamePart(recordExportStem(records[i]), records.size() > 1 ? 84 : 176);
        parts << (part.isEmpty() ? QStringLiteral("AstroBatch") : part);
    }
    auto stem = parts.isEmpty() ? QStringLiteral("AstroBatch") : parts.join(" + ");
    if (records.size() > 2) stem += QString(" (+%1)").arg(records.size() - 2);
    const auto output = fileNamePart(detail, 64);
    if (!output.isEmpty()) stem += '_' + output;
    static const QRegularExpression reserved(
        R"(^(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|(?:COM|LPT)[1-9¹²³])(?:\.|$))",
        QRegularExpression::CaseInsensitiveOption);
    if (stem.startsWith('.') || reserved.match(stem).hasMatch()) stem.prepend('_');
    return stem + '.' + extension;
}

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
