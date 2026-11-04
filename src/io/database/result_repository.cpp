#include "io/database/result_repository.h"
#include "io/database/plot_reader.h"
#include "io/database/data_codec.h"
#include "io/database/peak_index.h"
#include "io/database/record_query.h"
#include "model/workflow_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <exception>
#include <new>
#include <algorithm>

namespace aster
{
namespace
{
bool plotFamily(const OutputReference &ref, const QString &port, const QString &kind)
{
    return port == ref.port ||
           ((ref.kind == "Series" || ref.kind == "Spectrum") && kind == ref.kind) ||
           (ref.kind == "Spectrum" && port == "peaks" && kind == "Table") ||
           (ref.kind == "Grid" && ((port == "coi" && kind == "Series") ||
                                  (port == "significance_ratio" && kind == "Grid")));
}
QString uuid(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}
QString json(const QJsonObject &value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
QString json(const QStringList &value)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(value)).toJson(QJsonDocument::Compact));
}
QStringList strings(const QString &value, QString &error)
{
    QJsonParseError parse;
    const auto d = QJsonDocument::fromJson(value.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !d.isArray())
    {
        error = "Invalid stored string list";
        return {};
    }
    QStringList out;
    for (const auto &item : d.array())
    {
        if (!item.isString())
        {
            error = "Invalid stored warning";
            return {};
        }
        out.push_back(item.toString());
    }
    return out;
}
QJsonObject object(const QString &value, QString &error)
{
    QJsonParseError parse;
    const auto d = QJsonDocument::fromJson(value.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !d.isObject())
        error = "Invalid stored JSON object";
    return d.object();
}
std::optional<NodeState> nodeState(const QString &name)
{
#define STATE_VALUE(value)                                                                         \
    if (name == #value)                                                                            \
        return NodeState::value;
    ASTER_NODE_STATES(STATE_VALUE)
#undef STATE_VALUE
    return {};
}
std::optional<RunState> runState(const QString &name)
{
    for (auto state : {RunState::Succeeded, RunState::PartialFailed, RunState::Failed,
                       RunState::Cancelled, RunState::Invalid})
        if (runStateName(state) == name)
            return state;
    return {};
}
} // namespace
ResultRepository::ResultRepository()
    : m_connection("aster_" + uuid(QUuid::createUuid())), m_thread(QThread::currentThreadId())
{
}
ResultRepository::~ResultRepository()
{
    if (m_db.isValid())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}
QString ResultRepository::checkThread() const
{
    if (QThread::currentThreadId() != m_thread)
        return "Repository used from the wrong thread";
    if (!m_db.isOpen())
        return "Database is not open";
    return {};
}
QString ResultRepository::open(const QString &path, bool createIfMissing)
{
    if (QThread::currentThreadId() != m_thread)
        return "Repository used from the wrong thread";
    if (m_db.isValid())
        return "Repository connection already initialized";
    if (path.isEmpty())
        return "Database path is empty";
    if (!createIfMissing && !QFileInfo(path).isFile())
        return "Source database file no longer exists";
    if (path != ":memory:")
    {
        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath()))
            return "Cannot create database directory";
    }
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open())
        return m_db.lastError().text();
    QSqlQuery q(m_db);
    if (!q.exec("PRAGMA foreign_keys=ON") || !q.exec("PRAGMA busy_timeout=5000"))
        return q.lastError().text();
    const auto error = database_detail::ensureSchema(m_db);
    if (!error.isEmpty())
        m_db.close();
    return error;
}

SaveReceipt ResultRepository::save(const RunResult &run)
{
    // Keep the receipt contract even if encoding/SQL wrappers throw. Otherwise
    // BatchRunner unwinds and loses its in-memory collection of unsaved results.
    // Check ownership before touching the connection, including rollback.
    const auto error = checkThread();
    if (!error.isEmpty())
        return {run.runId, 0, error};
    try
    {
        return saveImpl(run);
    }
    catch (const std::bad_alloc &)
    {
        m_db.rollback();
        return {run.runId, 0, QStringLiteral("Insufficient memory while saving result")};
    }
    catch (const std::exception &e)
    {
        m_db.rollback();
        return {run.runId, 0, QStringLiteral("Exception while saving result: ") + QString::fromUtf8(e.what())};
    }
    catch (...)
    {
        m_db.rollback();
        return {run.runId, 0, QStringLiteral("Unknown exception while saving result")};
    }
}

SaveReceipt ResultRepository::saveImpl(const RunResult &run)
{
    SaveReceipt receipt{run.runId, 0, checkThread()};
    if (!receipt.error.isEmpty())
        return receipt;
    const auto definition = workflowFromJson(run.workflowSnapshot);
    if (run.runId.isNull() || run.root.isNull() || run.state == RunState::Invalid ||
        !run.startedAt.isValid() || !run.finishedAt.isValid() || run.finishedAt < run.startedAt ||
        !definition.validation.ok() || definition.definition.nodes.size() != run.nodes.size())
    {
        receipt.error = "Incomplete or invalid run result";
        return receipt;
    }
    QMap<NodeId, NodeDefinition> definitions;
    for (const auto &node : definition.definition.nodes)
        definitions.insert(node.id, node);
    if (definitions.size() != run.nodes.size() || !definitions.contains(run.root) ||
        definitions[run.root].input)
    {
        receipt.error = "Result/snapshot node mismatch";
        return receipt;
    }
    QSet<NodeId> executed;
    for (const auto &id : run.executionOrder)
    {
        if (!run.nodes.contains(id) || executed.contains(id))
        {
            receipt.error = "Invalid execution order";
            return receipt;
        }
        executed.insert(id);
    }
    if (!m_db.transaction())
    {
        receipt.error = m_db.lastError().text();
        return receipt;
    }
    auto fail = [&](const QString &error)
    {
        m_db.rollback();
        receipt.error = error;
        receipt.recordId = 0;
        return receipt;
    };
    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO "
                   "aster_runs(run_uuid,root_uuid,state,source_label,provenance,snapshot,started_"
                   "utc,finished_utc,observer_warnings) VALUES(?,?,?,?,?,?,?,?,?)");
    for (const auto &v : QVariantList{
             uuid(run.runId), uuid(run.root), runStateName(run.state),
             run.sourceLabel.isNull() ? QString("") : run.sourceLabel, json(run.provenance),
             json(run.workflowSnapshot), run.startedAt.toUTC().toString(Qt::ISODateWithMs),
             run.finishedAt.toUTC().toString(Qt::ISODateWithMs), json(run.observerWarnings)})
        insert.addBindValue(v);
    if (!insert.exec())
        return fail(insert.lastError().text());
    const qint64 id = insert.lastInsertId().toLongLong();
    for (auto it = run.nodes.begin(); it != run.nodes.end(); ++it)
    {
        const auto &result = it.value();
        if (!definitions.contains(it.key()) || result.state == NodeState::Pending ||
            result.state == NodeState::Running ||
            (result.state != NodeState::Succeeded && !result.outputs.isEmpty()))
            return fail("Invalid node result");
        const auto &node = definitions[it.key()];
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO aster_nodes VALUES(?,?,?,?,?,?,?,?,?,?)");
        for (const auto &v :
             QVariantList{id, uuid(it.key()), node.method, node.methodVersion,
                          json(node.parameters), nodeStateName(result.state), json(result.report),
                          result.error.isNull() ? QString("") : result.error, json(result.warnings),
                          run.executionOrder.indexOf(it.key())})
            q.addBindValue(v);
        if (!q.exec())
            return fail(q.lastError().text());
        for (auto out = result.outputs.begin(); out != result.outputs.end(); ++out)
        {
            const auto encoded = encodeData(out.value());
            if (!encoded.ok())
                return fail(encoded.error);
            QSqlQuery payload(m_db);
            payload.prepare("INSERT INTO aster_outputs VALUES(?,?,?,?,?)");
            payload.addBindValue(id);
            payload.addBindValue(uuid(it.key()));
            payload.addBindValue(out.key());
            payload.addBindValue(dataKindName(dataKind(*out.value())));
            payload.addBindValue(encoded.bytes);
            if (!payload.exec())
                return fail(payload.lastError().text());
        }
        const auto indexError = database_detail::indexPeaks(m_db, id, it.key(), result.outputs);
        if (!indexError.isEmpty())
            return fail(indexError);
    }
    const auto source = OutputReference::fromJson(run.provenance["database_source"].toObject());
    if (source.valid() && run.nodes.value(run.root).state == NodeState::Succeeded)
    {
        QSqlQuery dependency(m_db);
        // References inside this database are protected. External sources are captured
        // as immutable root output snapshots; no cross-file foreign key is possible.
        dependency.prepare("INSERT INTO aster_dependencies(owner_id,source_id) SELECT ?,r.id FROM "
                           "aster_runs r JOIN aster_outputs o ON o.run_id=r.id WHERE "
                           "r.run_uuid=? AND o.node_uuid=? AND o.port=?");
        dependency.addBindValue(id);
        dependency.addBindValue(uuid(source.runId));
        dependency.addBindValue(uuid(source.nodeId));
        dependency.addBindValue(source.port);
        if (!dependency.exec())
            return fail(dependency.lastError().text());
    }
    if (!m_db.commit())
        return fail(m_db.lastError().text());
    receipt.recordId = id;
    return receipt; // Only publish IDs after COMMIT succeeds.
}

RecordList ResultRepository::list(int limit, int offset) const
{
    return list(RecordFilter{}, limit, offset);
}
RecordList ResultRepository::list(const RecordFilter &filter, int limit, int offset) const
{
    return queryList(filter, limit, offset, 0);
}
RecordList ResultRepository::listBefore(const RecordFilter &filter, int limit, qint64 beforeId) const
{
    return queryList(filter, limit, 0, beforeId);
}
RecordList ResultRepository::queryList(const RecordFilter &filter, int limit, int offset, qint64 beforeId) const
{
    RecordList result;
    result.error = checkThread();
    if (!result.error.isEmpty())
        return result;
    result.error = filter.validate();
    if (!result.error.isEmpty())
        return result;
    if (limit < 1 || limit > 10000 || offset < 0 || beforeId < 0)
    {
        result.error = "Invalid pagination";
        return result;
    }
    QString statement = "SELECT "
                        "r.id,r.run_uuid,r.source_label,r.state,r.started_utc,r.finished_utc,r."
                        "display_name,r.notes FROM aster_runs r WHERE 1=1";
    QVariantList values;
    statement += database_detail::recordConditions(filter, values);
    if (beforeId > 0) { statement += " AND r.id<?"; values << beforeId; }
    statement += " ORDER BY r.id DESC LIMIT ? OFFSET ?";
    values << limit + 1 << offset;
    QSqlQuery q(m_db);
    q.prepare(statement);
    for (const auto &v : values)
        q.addBindValue(v);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    while (q.next())
        result.records.push_back({q.value(0).toLongLong(), QUuid(q.value(1).toString()),
                                  q.value(2).toString(), q.value(3).toString(),
                                  QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs),
                                  QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs),
                                  q.value(6).toString(), q.value(7).toString()});
    result.hasMore = result.records.size() > limit;
    if (result.hasMore) result.records.removeLast();
    return result;
}
QStringList ResultRepository::frequencyUnits(QString *error) const
{
    QStringList result;
    auto e = checkThread();
    if (e.isEmpty())
    {
        QSqlQuery q(m_db);
        if (!q.exec("SELECT DISTINCT unit FROM aster_peaks ORDER BY unit"))
            e = q.lastError().text();
        else
            while (q.next())
                result.push_back(q.value(0).toString());
    }
    if (error)
        *error = e;
    return result;
}
PeakMatches ResultRepository::matchingPeaks(qint64 id, const RecordFilter &filter) const
{
    PeakMatches result;
    result.error = checkThread();
    if (!result.error.isEmpty())
        return result;
    result.error = filter.validate();
    if (!result.error.isEmpty())
        return result;
    QVariantList values{id};
    QString statement =
        "SELECT p.node_uuid,p.port,p.unit,p.frequency FROM aster_peaks p WHERE p.run_id=?";
    if (filter.hasFrequency())
        statement += database_detail::peakConditions(filter, values);
    statement += " ORDER BY p.node_uuid,p.frequency";
    QSqlQuery q(m_db);
    q.prepare(statement);
    for (const auto &v : values)
        q.addBindValue(v);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    while (q.next())
        result.peaks.push_back({QUuid(q.value(0).toString()), q.value(1).toString(),
                                q.value(2).toString(), q.value(3).toDouble()});
    return result;
}
QString ResultRepository::updateMetadata(qint64 id, const QString &name, const QString &notes)
{
    auto error = checkThread();
    if (!error.isEmpty())
        return error;
    QSqlQuery q(m_db);
    q.prepare("UPDATE aster_runs SET display_name=?,notes=? WHERE id=?");
    q.addBindValue(name.isNull() ? QString("") : name.trimmed());
    q.addBindValue(notes.isNull() ? QString("") : notes);
    q.addBindValue(id);
    if (!q.exec())
        return q.lastError().text();
    return q.numRowsAffected() == 1 ? QString() : QString("Record not found");
}
QString ResultRepository::remove(qint64 id)
{
    auto error = checkThread();
    if (!error.isEmpty())
        return error;
    QSqlQuery q(m_db);
    q.prepare("SELECT owner_id FROM aster_dependencies WHERE source_id=? LIMIT 1");
    q.addBindValue(id);
    if (!q.exec())
        return q.lastError().text();
    if (q.next())
        return QString("ID %1 is used by ID %2. Delete the derived record first.")
            .arg(id)
            .arg(q.value(0).toLongLong());
    q.prepare("DELETE FROM aster_runs WHERE id=?");
    q.addBindValue(id);
    if (!q.exec())
        return q.lastError().text();
    return q.numRowsAffected() == 1 ? QString() : QString("Record not found");
}
LoadResult ResultRepository::load(qint64 recordId, bool metadataOnly) const
{
    LoadResult result;
    result.error = checkThread();
    if (!result.error.isEmpty())
        return result;
    QSqlQuery q(m_db);
    q.prepare("SELECT "
              "run_uuid,root_uuid,state,source_label,provenance,snapshot,started_utc,finished_utc,"
              "observer_warnings,display_name,notes FROM aster_runs WHERE id=?");
    q.addBindValue(recordId);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    if (!q.next())
    {
        result.error = "Record not found";
        return result;
    }
    result.displayName = q.value(9).toString();
    result.notes = q.value(10).toString();
    auto &run = result.run;
    run.runId = QUuid(q.value(0).toString());
    run.root = QUuid(q.value(1).toString());
    const auto state = runState(q.value(2).toString());
    if (!state)
    {
        result.error = "Invalid stored run state";
        return result;
    }
    run.state = *state;
    run.sourceLabel = q.value(3).toString();
    run.provenance = object(q.value(4).toString(), result.error);
    run.workflowSnapshot = object(q.value(5).toString(), result.error);
    run.startedAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    run.finishedAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    run.observerWarnings = strings(q.value(8).toString(), result.error);
    if (!result.error.isEmpty())
        return result;
    const auto definition = workflowFromJson(run.workflowSnapshot);
    if (!definition.validation.ok())
    {
        result.error = definition.validation.message();
        return result;
    }
    QMap<NodeId, NodeDefinition> definitions;
    for (const auto &n : definition.definition.nodes)
        definitions.insert(n.id, n);
    q.prepare("SELECT "
              "node_uuid,state,report,error,warnings,execution_order,method,method_version,"
              "parameters FROM aster_nodes WHERE run_id=?");
    q.addBindValue(recordId);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    QMap<int, NodeId> order;
    while (q.next())
    {
        const NodeId id(q.value(0).toString());
        const auto stateValue = nodeState(q.value(1).toString());
        if (!stateValue || !definitions.contains(id) ||
            definitions[id].method != q.value(6).toString() ||
            definitions[id].methodVersion != q.value(7).toInt() ||
            definitions[id].parameters != object(q.value(8).toString(), result.error))
        {
            result.error = "Stored node/snapshot mismatch";
            return result;
        }
        NodeResult node;
        node.state = *stateValue;
        node.report = object(q.value(2).toString(), result.error);
        node.error = q.value(3).toString();
        node.warnings = strings(q.value(4).toString(), result.error);
        run.nodes.insert(id, node);
        const int index = q.value(5).toInt();
        if (index >= 0)
        {
            if (order.contains(index))
            {
                result.error = "Duplicate stored execution order";
                return result;
            }
            order.insert(index, id);
        }
    }
    if (!result.error.isEmpty())
        return result;
    if (run.nodes.size() != definitions.size())
    {
        result.error = "Missing stored node results";
        return result;
    }
    for (auto it = order.begin(); it != order.end(); ++it)
    {
        if (it.key() != run.executionOrder.size())
        {
            result.error = "Invalid stored execution order";
            return result;
        }
        run.executionOrder.push_back(it.value());
    }
    q.prepare(metadataOnly
                  ? "SELECT node_uuid,port,kind,length(data) FROM aster_outputs WHERE run_id=?"
                  : "SELECT node_uuid,port,kind,data FROM aster_outputs WHERE run_id=?");
    q.addBindValue(recordId);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    while (q.next())
    {
        const NodeId id(q.value(0).toString());
        const auto bytes = metadataOnly ? q.value(3).toLongLong() : q.value(3).toByteArray().size();
        result.outputs[id].push_back({q.value(1).toString(), q.value(2).toString(), bytes});
        if (metadataOnly)
        {
            if (!run.nodes.contains(id) || run.nodes[id].state != NodeState::Succeeded)
            {
                result.error = "Invalid stored output directory";
                return result;
            }
            continue;
        }
        const auto decoded = decodeData(q.value(3).toByteArray());
        if (!decoded.ok() || !run.nodes.contains(id) ||
            run.nodes[id].state != NodeState::Succeeded ||
            (decoded.data && dataKindName(dataKind(*decoded.data)) != q.value(2).toString()))
        {
            result.error = decoded.error.isEmpty() ? "Invalid stored output" : decoded.error;
            return result;
        }
        run.nodes[id].outputs.insert(q.value(1).toString(), decoded.data);
    }
    return result;
}
OutputLoad ResultRepository::loadOutput(const OutputReference &ref) const
{
    auto error = checkThread();
    if (!error.isEmpty())
        return {{}, error};
    if (!ref.valid())
        return {{}, "Invalid output reference"};
    QSqlQuery q(m_db);
    q.prepare("SELECT o.kind,o.data FROM aster_outputs o JOIN aster_runs r ON r.id=o.run_id "
              "WHERE r.id=? AND r.run_uuid=? AND o.node_uuid=? AND o.port=?");
    q.addBindValue(ref.recordId);
    q.addBindValue(uuid(ref.runId));
    q.addBindValue(uuid(ref.nodeId));
    q.addBindValue(ref.port);
    if (!q.exec())
        return {{}, q.lastError().text()};
    if (!q.next())
        return {{}, "Source output no longer exists, or database identity has changed"};
    if (!ref.kind.isEmpty() && q.value(0).toString() != ref.kind)
        return {{}, "Output kind mismatch"};
    const auto d = decodeData(q.value(1).toByteArray());
    if (!d.ok())
        return {{}, d.error};
    if (dataKindName(dataKind(*d.data)) != q.value(0).toString())
        return {{}, "Stored output kind mismatch"};
    return {d.data, {}};
}
NodeResult ResultRepository::loadPlot(const OutputReference &ref, qint64 budget) const
{
    const auto error = checkThread();
    if (!error.isEmpty())
        return NodeResult::failure(error);
    if (!ref.valid() || budget <= 0)
        return NodeResult::failure("Invalid plot reference or budget");
    QSqlQuery q(m_db);
    q.prepare("SELECT port,kind,length(data) FROM aster_outputs WHERE run_id=? AND node_uuid=? "
              "ORDER BY port");
    q.addBindValue(ref.recordId);
    q.addBindValue(uuid(ref.nodeId));
    if (!q.exec())
        return NodeResult::failure(q.lastError().text());
    QVector<OutputReference> selected;
    qint64 bytes = 0;
    while (q.next())
    {
        const auto port = q.value(0).toString(), kind = q.value(1).toString();
        if (port == ref.port && !ref.kind.isEmpty() && ref.kind != kind)
            return NodeResult::failure("Output kind mismatch");
        if (plotFamily(ref, port, kind))
        {
            bytes += q.value(2).toLongLong();
            auto r = ref;
            r.port = port;
            r.kind = kind;
            selected.push_back(r);
        }
    }
    if (bytes > budget)
        return NodeResult::failure("Plot exceeds the 128 MiB per-subplot input budget; export CSV "
                                   "or use a smaller output");
    QMap<QString, DataRef> outputs;
    for (const auto &r : selected)
    {
        const auto d = loadOutput(r);
        if (!d.ok())
            return NodeResult::failure(d.error);
        outputs.insert(r.port, d.data);
    }
    if (!outputs.contains(ref.port))
        return NodeResult::failure("Output not found");
    return NodeResult::success(outputs);
}
NodeResult ResultRepository::loadPlotPreview(const OutputReference &ref, const PlotRequest &request,
                                             const CancellationToken &cancel) const
{
    const auto threadError = checkThread();
    if (!threadError.isEmpty())
        return NodeResult::failure(threadError);
    if (!ref.valid())
        return NodeResult::failure("Invalid plot reference");
    auto db = m_db;
    if (!db.transaction())
        return NodeResult::failure(db.lastError().text());
    struct Rollback
    {
        QSqlDatabase db;
        ~Rollback()
        {
            db.rollback();
        }
    } guard{db};
    QSqlQuery q(db);
    q.prepare(
        "SELECT o.rowid,o.port,o.kind FROM aster_outputs o JOIN aster_runs r ON r.id=o.run_id "
        "WHERE r.id=? AND r.run_uuid=? AND o.node_uuid=? ORDER BY o.port");
    q.addBindValue(ref.recordId);
    q.addBindValue(uuid(ref.runId));
    q.addBindValue(uuid(ref.nodeId));
    if (!q.exec())
        return NodeResult::failure(q.lastError().text());
    struct Item
    {
        qint64 id;
        QString port, kind;
    };
    QVector<Item> items;
    while (q.next())
        items.push_back({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString()});
    q.finish();
    NodeResult result = NodeResult::success({});
    if (items.size() > PlotLimits::maxFamilyOutputs)
        return NodeResult::failure("Too many outputs in one plot family");
    auto boundedRequest = request;
    boundedRequest.columns =
        std::min(boundedRequest.columns,
                 PlotLimits::maxCurveFamilyPoints / std::max(1, int(items.size())) / 4);
    for (const auto &i : items)
    {
        if (cancel.isCancelled())
            return NodeResult::cancelled();
        if (i.port == ref.port && !ref.kind.isEmpty() && ref.kind != i.kind)
            return NodeResult::failure("Output kind mismatch");
        if (i.port != ref.port && !request.includeFamily)
            continue;
        if (!plotFamily(ref, i.port, i.kind))
            continue;
        auto outputRequest = boundedRequest;
        // Threshold boundaries need samples at their stated coordinates. Taking a
        // block maximum would expand significant regions into non-significant cells.
        outputRequest.gridSampleCoordinates = i.port == "significance_ratio";
        auto part = readPlotBlob(db, i.id, i.kind, outputRequest, cancel);
        if (part.state != NodeState::Succeeded)
            return part;
        result.outputs[i.port] = part.outputs.value("data");
        if (part.report["display_reduced"].toBool())
            result.report["display_reduced"] = true;
        result.report["bytes_read"] =
            result.report["bytes_read"].toDouble() + part.report["bytes_read"].toDouble();
        const auto b = part.report["bounds"].toArray();
        auto outputBounds = result.report["output_bounds"].toObject();
        outputBounds[i.port] = b;
        result.report["output_bounds"] = outputBounds;
        if (ref.kind == "Series" || ref.kind == "Spectrum")
        {
            auto bounds = result.report["bounds"].toArray();
            if (b.size() == 4)
            {
                if (bounds.size() != 4)
                    bounds = b;
                else
                {
                    bounds[0] = std::min(bounds[0].toDouble(), b[0].toDouble());
                    bounds[1] = std::max(bounds[1].toDouble(), b[1].toDouble());
                    bounds[2] = std::min(bounds[2].toDouble(), b[2].toDouble());
                    bounds[3] = std::max(bounds[3].toDouble(), b[3].toDouble());
                }
                result.report["bounds"] = bounds;
            }
        }
        else if (i.port == ref.port)
            result.report["bounds"] = b;
    }
    if (!result.outputs.contains(ref.port))
        return NodeResult::failure(
            "Source output no longer exists, or database identity has changed");
    return result;
}
} // namespace aster
