// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/database/record_name.h"
#include "io/database/sqlite_snapshot.h"
#include <QJsonDocument>
#include <QSet>

namespace aster
{
namespace
{
enum class NameUse { Display, ExportStem };

QString sourcePart(const QString &display, const QString &source, const QJsonObject &provenance,
                   const QString &rootMethod, NameUse use, OutputReference &parent)
{
    auto path = provenance.value("path").toString();
    if (path.isEmpty() && rootMethod == "file_source") path = source;
    if (!provenance.contains("batch_pair") && !path.isEmpty())
    {
        const auto leaf = path.replace('\\', '/').section('/', -1);
        if (!display.isEmpty() && display != leaf) return display;
        if (!leaf.isEmpty())
        {
            const auto dot = leaf.lastIndexOf('.');
            return use == NameUse::ExportStem && dot > 0 ? leaf.left(dot) : leaf;
        }
    }
    if (!display.isEmpty()) return display;
    auto ref = OutputReference::fromJson(provenance.value("database_source").toObject());
    if (ref.valid() && source == QString("ID %1 · %2").arg(ref.recordId).arg(ref.port))
    {
        parent = ref;
        return {};
    }
    return source;
}

// Follow only application-generated references. Keep aliases and scientific names verbatim.
QString storedName(OutputReference ref, NameUse use)
{
    QSet<QString> visited;
    QString fallback;
    while (ref.valid() && visited.size() < 16 && !visited.contains(ref.key()))
    {
        visited.insert(ref.key());
        try
        {
            database_detail::SqliteFile file(ref.databasePath, true, 0);
            QSqlQuery query(file.db);
            query.prepare("SELECT r.display_name,r.source_label,r.provenance,n.method "
                          "FROM aster_runs r LEFT JOIN aster_nodes n "
                          "ON n.run_id=r.id AND n.node_uuid=r.root_uuid "
                          "WHERE r.id=? AND r.run_uuid=?");
            query.addBindValue(ref.recordId);
            query.addBindValue(ref.runId.toString(QUuid::WithoutBraces));
            if (!query.exec() || !query.next()) break;
            OutputReference parent;
            const auto name = sourcePart(query.value(0).toString().trimmed(), query.value(1).toString().trimmed(),
                                         QJsonDocument::fromJson(query.value(2).toByteArray()).object(),
                                         query.value(3).toString(), use, parent);
            if (!parent.valid()) return name;
            fallback = parent.port;
            ref = parent;
        }
        catch (const std::exception &)
        {
            break;
        }
    }
    return use == NameUse::ExportStem ? fallback : QString();
}
} // namespace
QString recordSourceName(const QString &displayName, const QString &sourceLabel,
                         const QJsonObject &provenance, const QString &rootMethod)
{
    OutputReference parent;
    const auto name = sourcePart(displayName.trimmed(), sourceLabel.trimmed(), provenance, rootMethod,
                                 NameUse::Display, parent);
    const auto resolved = parent.valid() ? storedName(parent, NameUse::Display) : name;
    // A known empty name is distinct from a reference whose name has not been read.
    return resolved.isNull() ? QStringLiteral("") : resolved;
}
QString recordExportStem(const OutputReference &reference)
{
    return storedName(reference, NameUse::ExportStem);
}
} // namespace aster
