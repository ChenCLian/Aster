// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/database/database_transfer.h"
#include "io/database/sqlite_snapshot.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/database/data_format.h"
#include "model/workflow_codec.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QDateTime>
#include <utility>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <cmath>

namespace aster
{
namespace
{
using namespace database_detail;
struct Cancelled
{
};
void checkpoint(const CancellationToken &cancel)
{
    if (cancel.isCancelled())
        throw Cancelled{};
}
void report(const DatabaseProgress &progress, QString stage, qint64 done = 0, qint64 total = 0)
{
    // Progress observers must not turn a successful COMMIT into a reported failure.
    if (progress)
    {
        try
        {
            progress(stage, done, total);
        }
        catch (...)
        {
        }
    }
}
QString uuid(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}
QByteArray json(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
QJsonObject parsedObject(const QString &text)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid record JSON");
    return doc.object();
}
QJsonObject normalized(QJsonObject object)
{
    // Import provenance is not scientific content. Mutable display_name/notes are also excluded
    // below.
    object.remove("database_import");
    if (object.contains("database_source"))
    {
        const auto ref = OutputReference::fromJson(object["database_source"].toObject());
        if (!ref.valid())
            return object; // Failed source tasks may retain invalid input for diagnostics.
        auto source = ref.toJson();
        source.remove("database");
        source.remove("record");
        object["database_source"] =
            source; // Stable run/node/port identity, independent of the storage location.
    }
    return object;
}
void hashField(QCryptographicHash &hash, const QByteArray &value)
{
    hash.addData(QByteArray::number(value.size()));
    hash.addData(":");
    hash.addData(value);
}
struct Identities
{
    QMap<qint64, QString> byId;
    QMap<QString, qint64> byRun;
};
Identities identities(QSqlDatabase &db)
{
    Identities result;
    QSqlQuery q(db);
    queryExec(q, "SELECT id,run_uuid FROM aster_runs ORDER BY id");
    while (q.next())
    {
        const auto id = q.value(0).toLongLong();
        const auto run = q.value(1).toString();
        require(id > 0 && !QUuid(run).isNull() && uuid(QUuid(run)) == run &&
                    !result.byRun.contains(run),
                "Invalid record ID or run UUID");
        result.byId.insert(id, run);
        result.byRun.insert(run, id);
    }
    require(!q.lastError().isValid(), q.lastError().text());
    return result;
}
void validateRecord(ResultRepository &repo, qint64 id)
{
    const auto record = repo.load(id, true);
    require(record.ok(), QString("Record %1: %2").arg(id).arg(record.error));
    const auto &run = record.run;
    require(!run.runId.isNull() && !run.root.isNull() && run.nodes.contains(run.root) &&
                run.state != RunState::Invalid && run.startedAt.isValid() &&
                run.finishedAt.isValid() && run.startedAt <= run.finishedAt,
            QString("Invalid record metadata: %1").arg(id));
    const auto definition = workflowFromJson(run.workflowSnapshot).definition;
    QMap<NodeId, NodeId> parents;
    for (const auto &node : definition.nodes)
    {
        if (node.id == run.root && node.method.startsWith("database_") &&
            run.nodes.value(run.root).state == NodeState::Succeeded)
            require(OutputReference::fromJson(run.provenance["database_source"].toObject()).valid(),
                    "Successful database source has an invalid reference");
        require(!parents.contains(node.id), "Duplicate stored workflow node");
        parents.insert(node.id, node.input ? node.input->parent : NodeId{});
    }
    require(parents.size() == run.nodes.size() && parents.value(run.root).isNull(),
            "Invalid stored workflow root");
    for (auto it = parents.cbegin(); it != parents.cend(); ++it)
    {
        QSet<NodeId> seen;
        auto n = it.key();
        while (n != run.root)
        {
            require(parents.contains(n) && !seen.contains(n),
                    "Stored workflow is disconnected or cyclic");
            seen.insert(n);
            n = parents.value(n);
        }
    }
    for (const auto &node : run.nodes)
        require(node.state != NodeState::Pending && node.state != NodeState::Running,
                "Incomplete stored node");
}
void validatePeaks(QSqlDatabase &db, qint64 id, const QString &node, const DataRef &peaks,
                   const QString &unit)
{
    QSqlQuery q(db);
    queryPrepare(q, "SELECT ordinal,unit,frequency,port FROM aster_peaks WHERE run_id=? AND "
                    "node_uuid=? ORDER BY ordinal");
    q.addBindValue(id);
    q.addBindValue(node);
    queryExec(q);
    const auto *table = dataAs<TableData>(peaks);
    const auto f = table ? table->columns.indexOf("frequency") : -1;
    if (f >= 0)
        for (qsizetype i = 0; i < table->rows.size(); ++i)
        {
            const double value = table->rows[i][f];
            if (!std::isfinite(value) || value <= 0)
                continue;
            require(q.next() && q.value(0).toLongLong() == i && q.value(1).toString() == unit &&
                        q.value(2).toDouble() == value && q.value(3).toString() == "peaks",
                    "Stored peak-frequency index does not match its output");
        }
    require(!q.next(), "Unexpected stored peak-frequency index");
}
QByteArray fingerprint(QSqlDatabase &db, qint64 id, bool validatePayload,
                       const CancellationToken &cancel)
{
    checkpoint(cancel);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QSqlQuery q(db);
    queryPrepare(q, "SELECT "
                    "root_uuid,state,source_label,provenance,snapshot,started_utc,finished_utc,"
                    "observer_warnings FROM aster_runs WHERE id=?");
    q.addBindValue(id);
    queryExec(q);
    require(q.next(), "Record disappeared during merge");
    for (int i = 0; i < 8; ++i)
        hashField(hash, i == 3 ? json(normalized(parsedObject(q.value(i).toString())))
                               : q.value(i).toByteArray());
    q.finish();
    queryPrepare(q, "SELECT "
                    "node_uuid,method,method_version,parameters,state,report,error,warnings,"
                    "execution_order FROM aster_nodes WHERE run_id=? ORDER BY node_uuid");
    q.addBindValue(id);
    queryExec(q);
    while (q.next())
    {
        checkpoint(cancel);
        for (int i = 0; i < 9; ++i)
            hashField(hash, i == 5 ? json(normalized(parsedObject(q.value(i).toString())))
                                   : q.value(i).toByteArray());
    }
    require(!q.lastError().isValid(), q.lastError().text());
    q.finish();
    // Inspect lengths before materializing each payload; never load an entire run/database into
    // memory.
    queryPrepare(q, "SELECT node_uuid,port,kind,length(data) FROM aster_outputs WHERE run_id=? "
                    "ORDER BY node_uuid,port");
    q.addBindValue(id);
    queryExec(q);
    QString lastNode, unit;
    DataRef peaks;
    while (q.next())
    {
        checkpoint(cancel);
        const auto node = q.value(0).toString(), port = q.value(1).toString(),
                   kind = q.value(2).toString();
        require(q.value(3).toLongLong() > 0 && q.value(3).toLongLong() <= DataFormat::byteLimit,
                "Stored payload exceeds supported size");
        if (node != lastNode)
        {
            if (validatePayload && !lastNode.isEmpty())
                validatePeaks(db, id, lastNode, peaks, unit);
            lastNode = node;
            peaks.reset();
            unit.clear();
        }
        QSqlQuery data(db);
        queryPrepare(data,
                     "SELECT data FROM aster_outputs WHERE run_id=? AND node_uuid=? AND port=?");
        data.addBindValue(id);
        data.addBindValue(node);
        data.addBindValue(port);
        queryExec(data);
        require(data.next(), "Missing stored payload");
        const auto bytes = data.value(0).toByteArray();
        if (validatePayload)
        {
            const auto decoded = decodeData(bytes);
            require(decoded.ok(), "Invalid stored payload: " + decoded.error);
            require(dataKindName(dataKind(*decoded.data)) == kind, "Stored output kind mismatch");
            if (port == "peaks")
                peaks = decoded.data;
            if (port == "spectrum")
                if (const auto *spectrum = dataAs<SpectrumData>(decoded.data))
                    unit = spectrum->frequencyAxis.unit;
        }
        hashField(hash, node.toUtf8());
        hashField(hash, port.toUtf8());
        hashField(hash, kind.toUtf8());
        hashField(hash, bytes);
    }
    require(!q.lastError().isValid(), q.lastError().text());
    if (validatePayload && !lastNode.isEmpty())
        validatePeaks(db, id, lastNode, peaks, unit);
    return hash.result();
}
struct Transaction
{
    QSqlDatabase &db;
    bool committed = false;
    explicit Transaction(QSqlDatabase &db) : db(db)
    {
        execute(db, "BEGIN IMMEDIATE");
    }
    ~Transaction()
    {
        if (!committed)
            db.rollback();
    }
    void commit()
    {
        const bool ok = db.commit();
        require(ok, db.lastError().text());
        committed = true;
    }
};
QJsonObject relocate(QJsonObject object, const QMap<QString, qint64> &ids, QSqlDatabase &target,
                     const QString &path, qint64 &external, bool required = false)
{
    if (!object.contains("database_source"))
        return object;
    auto ref = OutputReference::fromJson(object["database_source"].toObject());
    if (!ref.valid())
    {
        require(!required, "Invalid stored database source");
        return object;
    }
    const auto id = ids.value(uuid(ref.runId));
    if (!id)
    {
        ++external;
        return object;
    } // Original source is outside both files; keep its provenance and captured root data.
    QSqlQuery q(target);
    queryPrepare(q, "SELECT kind FROM aster_outputs WHERE run_id=? AND node_uuid=? AND port=?");
    q.addBindValue(id);
    q.addBindValue(uuid(ref.nodeId));
    q.addBindValue(ref.port);
    queryExec(q);
    if (!q.next() || (!ref.kind.isEmpty() && q.value(0).toString() != ref.kind))
    {
        require(!required, "Internal database source points to a missing or incompatible output");
        ++external;
        return object;
    }
    ref.recordId = id;
    ref.databasePath = path;
    object["database_source"] = ref.toJson();
    return object;
}
QString backupName(const QString &target)
{
    const QFileInfo info(target);
    return info.absoluteFilePath() + ".backups/" + info.completeBaseName() + "-before-merge-" +
           QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss") + "-" +
           uuid(QUuid::createUuid()) + ".sqlite";
}
} // namespace
DatabaseTransferResult backupDatabase(const QString &source, const QString &newFile,
                                      const CancellationToken &cancel,
                                      const DatabaseProgress &progress)
{
    DatabaseTransferResult result;
    try
    {
        checkpoint(cancel);
        report(progress, "backup");
        require(QFileInfo(source) != QFileInfo(newFile), "Source and backup are the same file");
        SqliteFile input(source);
        validateDatabase(input.db, true);
        checkpoint(cancel);
        snapshotDatabase(input.db, newFile);
        result.backupPath = QFileInfo(newFile).absoluteFilePath();
        // Once published the backup is complete, even if cancel arrived during VACUUM INTO.
        report(progress, "complete", 1, 1);
    }
    catch (const Cancelled &)
    {
        result.cancelled = true;
    }
    catch (const std::exception &e)
    {
        result.error = QString::fromUtf8(e.what());
    }
    catch (...)
    {
        result.error = "Unexpected database backup error";
    }
    return result;
}
DatabaseTransferResult mergeDatabase(const QString &destination, const QString &source,
                                     const CancellationToken &cancel,
                                     const DatabaseProgress &progress)
{
    DatabaseTransferResult result;
    try
    {
        checkpoint(cancel);
        report(progress, "checking");
        require(QFileInfo(source) != QFileInfo(destination), "Cannot merge a database into itself");
        require(QFileInfo(destination).isFile(), "Destination database does not exist");
        const auto targetPath = QFileInfo(destination).canonicalFilePath();
        QTemporaryDir staging(QFileInfo(targetPath).absolutePath() + "/.aster-merge-XXXXXX");
        require(staging.isValid(), "Cannot create merge staging directory");
        const auto incomingPath = staging.filePath("incoming.sqlite");
        {
            SqliteFile original(source);
            validateDatabase(original.db, false);
            checkpoint(cancel);
            report(progress, "snapshot");
            snapshotDatabase(original.db, incomingPath);
        }
        checkpoint(cancel);
        SqliteFile input(incomingPath);
        const auto incoming = identities(input.db);
        ResultRepository reader;
        const auto readerError = reader.open(incomingPath, false);
        require(readerError.isEmpty(), readerError);
        QMap<qint64, QByteArray> hashes;
        qint64 done = 0;
        for (auto it = incoming.byId.cbegin(); it != incoming.byId.cend(); ++it)
        {
            checkpoint(cancel);
            validateRecord(reader, it.key());
            hashes.insert(it.key(), fingerprint(input.db, it.key(), true, cancel));
            report(progress, "validating", ++done, incoming.byId.size());
        }
        checkpoint(cancel);
        SqliteFile target(targetPath, false);
        validateDatabase(target.db, true);
        // Attach only our verified, private snapshot. The original file stays read-only throughout.
        QSqlQuery attach(target.db);
        queryPrepare(attach, "ATTACH DATABASE ? AS aster_incoming");
        attach.addBindValue(incomingPath);
        queryExec(attach);
        attach.finish();
        const auto existing = identities(target.db);
        qint64 duplicates = 0;
        for (auto it = incoming.byId.cbegin(); it != incoming.byId.cend(); ++it)
            if (existing.byRun.contains(it.value()))
            {
                require(fingerprint(target.db, existing.byRun.value(it.value()), false, cancel) ==
                            hashes.value(it.key()),
                        "Conflicting content for run UUID " + it.value() + "; no records merged");
                ++duplicates;
            }
        if (duplicates == incoming.byId.size())
        {
            for (auto it = incoming.byId.cbegin(); it != incoming.byId.cend(); ++it)
                result.recordIds.insert(it.key(), existing.byRun.value(it.value()));
            result.duplicates = duplicates;
            return result;
        }
        report(progress, "backup");
        checkpoint(cancel);
        result.backupPath = backupName(targetPath);
        snapshotDatabase(target.db, result.backupPath);
        checkpoint(cancel);
        Transaction transaction(target.db);
        validateDatabase(target.db, false);
        // Reject record insertions/deletions between duplicate review and the writer lock.
        const auto locked = identities(target.db);
        require(locked.byId == existing.byId,
                "Destination records changed during preparation; retry merge");
        for (auto it = incoming.byId.cbegin(); it != incoming.byId.cend(); ++it)
            if (existing.byRun.contains(it.value()))
                require(fingerprint(target.db, existing.byRun.value(it.value()), false, cancel) ==
                            hashes.value(it.key()),
                        "Destination duplicate changed during preparation; retry merge");
        QMap<qint64, qint64> mapping;
        QMap<QString, qint64> targetIds = existing.byRun;
        QList<qint64> added;
        const QString runColumns = "run_uuid,root_uuid,state,source_label,provenance,snapshot,"
                                   "started_utc,finished_utc,observer_warnings,display_name,notes";
        for (auto it = incoming.byId.cbegin(); it != incoming.byId.cend(); ++it)
        {
            checkpoint(cancel);
            auto id = existing.byRun.value(it.value());
            if (!id)
            {
                QSqlQuery q(target.db);
                queryPrepare(q, "INSERT INTO aster_runs(" + runColumns + ") SELECT " + runColumns +
                                    " FROM aster_incoming.aster_runs WHERE id=?");
                q.addBindValue(it.key());
                queryExec(q);
                id = q.lastInsertId().toLongLong();
                require(id > 0, "Invalid allocated record ID");
                added.append(it.key());
            }
            mapping.insert(it.key(), id);
            targetIds.insert(it.value(), id);
        }
        done = 0;
        for (const auto oldId : added)
        {
            checkpoint(cancel);
            const auto newId = mapping.value(oldId);
            for (const auto &entry : QList<QPair<QString, QString>>{
                     {"aster_nodes", "node_uuid,method,method_version,parameters,state,report,"
                                     "error,warnings,execution_order"},
                     {"aster_outputs", "node_uuid,port,kind,data"},
                     {"aster_peaks", "node_uuid,port,ordinal,unit,frequency"}})
            {
                QSqlQuery q(target.db);
                queryPrepare(q, "INSERT INTO " + entry.first + "(run_id," + entry.second +
                                    ") SELECT ?," + entry.second + " FROM aster_incoming." +
                                    entry.first + " WHERE run_id=?");
                q.addBindValue(newId);
                q.addBindValue(oldId);
                queryExec(q);
                checkpoint(cancel);
            }
            report(progress, "writing", ++done, added.size());
        }
        qint64 external = 0;
        const QSet<qint64> newRecords(added.cbegin(), added.cend());
        // A previously external source may now be among the newly imported records.
        // Repair location/dependency metadata of matching owners too; never overwrite their
        // name/notes.
        for (const auto oldId : incoming.byId.keys())
        {
            checkpoint(cancel);
            const auto newId = mapping.value(oldId);
            QSqlQuery q(target.db);
            queryPrepare(q, "SELECT r.provenance,n.state FROM aster_runs r JOIN aster_nodes n ON "
                            "n.run_id=r.id AND n.node_uuid=r.root_uuid WHERE r.id=?");
            q.addBindValue(newId);
            queryExec(q);
            require(q.next(), "Missing imported run");
            const bool rootSucceeded = q.value(1).toString() == "Succeeded";
            auto provenance = parsedObject(q.value(0).toString());
            q.finish();
            const auto originalRef = provenance.value("database_source");
            const auto storedRef = OutputReference::fromJson(originalRef.toObject());
            if (rootSucceeded && storedRef.valid() && targetIds.contains(uuid(storedRef.runId)))
            {
                QSqlQuery captured(target.db);
                queryPrepare(captured, "SELECT o.kind=s.kind AND o.data=s.data FROM aster_runs r "
                                       "JOIN aster_outputs o ON o.run_id=r.id AND "
                                       "o.node_uuid=r.root_uuid AND o.port='data' "
                                       "JOIN aster_outputs s ON s.run_id=? AND s.node_uuid=? AND "
                                       "s.port=? WHERE r.id=?");
                captured.addBindValue(targetIds.value(uuid(storedRef.runId)));
                captured.addBindValue(uuid(storedRef.nodeId));
                captured.addBindValue(storedRef.port);
                captured.addBindValue(newId);
                queryExec(captured);
                require(captured.next() && captured.value(0).toBool(),
                        "Database source differs from its captured root output; merge rejected");
            }
            provenance =
                relocate(provenance, targetIds, target.db, targetPath, external, rootSucceeded);
            if (newRecords.contains(oldId) && !provenance.contains("database_import"))
                provenance["database_import"] = QJsonObject{
                    {"database", QFileInfo(source).absoluteFilePath()},
                    {"record", QString::number(oldId)},
                    {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {"original_database_source", originalRef}};
            queryPrepare(q, "UPDATE aster_runs SET provenance=? WHERE id=?");
            q.addBindValue(QString::fromUtf8(json(provenance)));
            q.addBindValue(newId);
            queryExec(q);
            q.finish();
            QSqlQuery nodes(target.db);
            queryPrepare(nodes, "SELECT node_uuid,report FROM aster_nodes WHERE run_id=?");
            nodes.addBindValue(newId);
            queryExec(nodes);
            while (nodes.next())
            {
                qint64 unused = 0;
                const auto old = parsedObject(nodes.value(1).toString());
                const auto updated = relocate(old, targetIds, target.db, targetPath, unused);
                if (updated != old)
                {
                    queryPrepare(q,
                                 "UPDATE aster_nodes SET report=? WHERE run_id=? AND node_uuid=?");
                    q.addBindValue(QString::fromUtf8(json(updated)));
                    q.addBindValue(newId);
                    q.addBindValue(nodes.value(0));
                    queryExec(q);
                    q.finish();
                }
            }
            QSqlQuery edges(input.db);
            queryPrepare(edges, "SELECT source_id FROM aster_dependencies WHERE owner_id=?");
            edges.addBindValue(oldId);
            queryExec(edges);
            QSet<qint64> sources;
            while (edges.next())
            {
                require(mapping.contains(edges.value(0).toLongLong()), "Unresolved dependency");
                sources.insert(mapping.value(edges.value(0).toLongLong()));
            }
            const auto ref = OutputReference::fromJson(provenance["database_source"].toObject());
            if (ref.valid() && targetIds.contains(uuid(ref.runId)))
            {
                queryPrepare(q, "SELECT n.state FROM aster_runs r JOIN aster_nodes n ON "
                                "n.run_id=r.id AND n.node_uuid=r.root_uuid WHERE r.id=?");
                q.addBindValue(newId);
                queryExec(q);
                if (q.next() && q.value(0).toString() == "Succeeded")
                    sources.insert(targetIds.value(uuid(ref.runId)));
                q.finish();
            }
            for (const auto dependency : sources)
            {
                require(dependency != newId, "Self-referencing database record");
                queryPrepare(
                    q, "INSERT OR IGNORE INTO aster_dependencies(owner_id,source_id) VALUES(?,?)");
                q.addBindValue(newId);
                q.addBindValue(dependency);
                queryExec(q);
                q.finish();
            }
        }
        checkpoint(cancel);
        report(progress, "committing");
        checkpoint(cancel);
        {
            QSqlQuery q(target.db);
            queryExec(q, "PRAGMA foreign_key_check");
            require(!q.next(), "Merge produced an invalid reference");
        }
        transaction.commit();
        result.recordIds = std::move(mapping);
        result.imported = added.size();
        result.duplicates = duplicates;
        result.externalReferences = external;
        report(progress, "complete", result.imported, result.imported);
    }
    catch (const Cancelled &)
    {
        result.cancelled = true;
    }
    catch (const std::exception &e)
    {
        result.error = QString::fromUtf8(e.what());
        if (result.error.isEmpty())
            result.error = "Database operation failed";
    }
    catch (...)
    {
        result.error = "Unexpected database merge error";
    }
    if (!result.ok())
    {
        result.imported = 0;
        result.recordIds.clear();
        if (!QFileInfo::exists(result.backupPath))
            result.backupPath.clear();
    }
    return result;
}
} // namespace aster
