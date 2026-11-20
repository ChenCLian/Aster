// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/exports/export_path.h"
#include "io/database/sqlite_snapshot.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
namespace aster
{
namespace
{
QString recordStem(OutputReference ref)
{
    QSet<QString> visited;
    QString fallback;
    // Saved-output sources may themselves refer to a saved output. Follow only
    // Aster's generated label, leaving user-supplied names intact.
    while (ref.valid() && visited.size() < 16 && !visited.contains(ref.key()))
    {
        visited.insert(ref.key());
        try
        {
            // A suggested filename must not wait for a database writer.
            database_detail::SqliteFile file(ref.databasePath, true, 0);
            QSqlQuery query(file.db);
            query.prepare("SELECT r.display_name,r.source_label,r.provenance,n.method "
                          "FROM aster_runs r LEFT JOIN aster_nodes n "
                          "ON n.run_id=r.id AND n.node_uuid=r.root_uuid "
                          "WHERE r.id=? AND r.run_uuid=?");
            query.addBindValue(ref.recordId);
            query.addBindValue(ref.runId.toString(QUuid::WithoutBraces));
            if (!query.exec() || !query.next()) break;
            const auto display = query.value(0).toString().trimmed();
            const auto source = query.value(1).toString().trimmed();
            const auto provenance = QJsonDocument::fromJson(query.value(2).toByteArray()).object();
            auto path = provenance.value("path").toString();
            if (path.isEmpty() && query.value(3).toString() == "file_source") path = source;
            if (!provenance.contains("batch_pair") && !path.isEmpty())
            {
                const auto leaf = path.replace('\\', '/').section('/', -1);
                if (!display.isEmpty() && display != leaf) return display;
                const auto dot = leaf.lastIndexOf('.');
                if (!leaf.isEmpty()) return dot > 0 ? leaf.left(dot) : leaf;
            }
            if (!display.isEmpty()) return display;
            const auto parent = OutputReference::fromJson(provenance.value("database_source").toObject());
            if (!parent.valid() || source != QString("ID %1 · %2").arg(parent.recordId).arg(parent.port))
                return source;
            fallback = parent.port;
            ref = parent;
        }
        catch (const std::exception &)
        {
            break; // An unavailable source may still have a usable port name.
        }
    }
    return fallback;
}

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
        const auto part = fileNamePart(recordStem(records[i]), records.size() > 1 ? 84 : 176);
        parts << (part.isEmpty() ? QStringLiteral("Aster") : part);
    }
    auto stem = parts.isEmpty() ? QStringLiteral("Aster") : parts.join(" + ");
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
