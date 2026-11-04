#include "io/database/peak_index.h"
#include "io/database/data_codec.h"
#include "io/database/record_filter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <cmath>
namespace aster
{
QString RecordFilter::validate() const
{
    for (double v : {minFrequency, maxFrequency})
        if (!std::isfinite(v) || (v < 0 && v != -1))
            return "Invalid frequency limit";
    if (minFrequency >= 0 && maxFrequency >= 0 && minFrequency > maxFrequency)
        return "Minimum frequency exceeds maximum";
    if (since.isValid() && until.isValid() && since > until)
        return "Invalid date range";
    return {};
}
} // namespace aster
namespace aster::database_detail
{
QString indexPeaks(QSqlDatabase &db, qint64 run, NodeId node, const QMap<QString, DataRef> &outputs)
{
    const auto *table = dataAs<TableData>(outputs.value("peaks"));
    if (!table)
        return {};
    const int f = table->columns.indexOf("frequency");
    if (f < 0)
        return {};
    const auto *spectrum = dataAs<SpectrumData>(outputs.value("spectrum"));
    const QString unit = spectrum ? spectrum->frequencyAxis.unit : QString("");
    QSqlQuery insert(db);
    insert.prepare("INSERT INTO aster_peaks(run_id,node_uuid,port,ordinal,unit,frequency) "
                   "VALUES(?,?,'peaks',?,?,?)");
    for (qsizetype i = 0; i < table->rows.size(); ++i)
    {
        const auto &row = table->rows[i];
        if (row.size() <= f || !std::isfinite(row[f]) || row[f] <= 0)
            continue;
        insert.bindValue(0, run);
        insert.bindValue(1, node.toString(QUuid::WithoutBraces));
        insert.bindValue(2, i);
        insert.bindValue(3, unit.isNull() ? QString("") : unit);
        insert.bindValue(4, row[f]);
        if (!insert.exec())
            return insert.lastError().text();
    }
    return {};
}
QString ensureSchema(QSqlDatabase &db)
{
    const auto verify = [&]() -> QString
    {
        const auto tables = db.tables();
        for (const auto &name : {"aster_runs", "aster_nodes", "aster_outputs", "aster_peaks"})
            if (!tables.contains(name))
                return "Incomplete database schema";
        return {};
    };
    QSqlQuery q(db);
    const auto tables = db.tables();
    if (!tables.isEmpty())
    {
        if (!tables.contains("aster_meta"))
            return "This is not an Aster new-core database; choose a new path";
        if (!q.exec("SELECT schema_version FROM aster_meta") || !q.next())
            return "Invalid schema metadata";
        const int version = q.value(0).toInt();
        q.finish();
        if (version == 3)
            return db.tables().contains("aster_dependencies")
                       ? verify()
                       : QString("Incomplete dependency schema");
        if (version != 1 && version != 2)
            return "Unsupported database schema";
    }
    // One transactional upgrade. Recheck under the writer lock: another
    // connection may have completed it while this connection was waiting.
    if (!q.exec("BEGIN IMMEDIATE"))
        return q.lastError().text();
    const auto fail = [&](QString error)
    {
        db.rollback();
        return error;
    };
    if (db.tables().isEmpty())
    {
        const QStringList schema = {
            "CREATE TABLE aster_meta(schema_version INTEGER NOT NULL)",
            "INSERT INTO aster_meta VALUES(1)",
            "CREATE TABLE aster_runs(id INTEGER PRIMARY KEY AUTOINCREMENT,run_uuid TEXT UNIQUE "
            "NOT NULL,root_uuid TEXT NOT NULL,state TEXT NOT NULL,source_label TEXT NOT "
            "NULL,provenance TEXT NOT NULL,snapshot TEXT NOT NULL,started_utc TEXT NOT "
            "NULL,finished_utc TEXT NOT NULL,observer_warnings TEXT NOT NULL)",
            "CREATE TABLE aster_nodes(run_id INTEGER NOT NULL,node_uuid TEXT NOT NULL,method TEXT "
            "NOT NULL,method_version INTEGER NOT NULL,parameters TEXT NOT NULL,state TEXT NOT "
            "NULL,report TEXT NOT NULL,error TEXT NOT NULL,warnings TEXT NOT NULL,execution_order "
            "INTEGER NOT NULL,PRIMARY KEY(run_id,node_uuid),FOREIGN KEY(run_id) REFERENCES "
            "aster_runs(id) ON DELETE CASCADE)",
            "CREATE TABLE aster_outputs(run_id INTEGER NOT NULL,node_uuid TEXT NOT NULL,port TEXT "
            "NOT NULL,kind TEXT NOT NULL,data BLOB NOT NULL,PRIMARY "
            "KEY(run_id,node_uuid,port),FOREIGN KEY(run_id,node_uuid) REFERENCES "
            "aster_nodes(run_id,node_uuid) ON DELETE CASCADE)"};
        for (const auto &statement : schema)
            if (!q.exec(statement))
                return fail(q.lastError().text());
    }
    if (!q.exec("SELECT schema_version FROM aster_meta") || !q.next())
        return fail("Invalid schema metadata");
    const int version = q.value(0).toInt();
    q.finish();
    if (version == 1)
    {
        const QStringList upgrade = {
            "ALTER TABLE aster_runs ADD COLUMN display_name TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE aster_runs ADD COLUMN notes TEXT NOT NULL DEFAULT ''",
            "CREATE TABLE aster_peaks(run_id INTEGER NOT NULL,node_uuid TEXT NOT NULL,port TEXT "
            "NOT NULL,ordinal INTEGER NOT NULL,unit TEXT NOT NULL,frequency REAL NOT NULL,PRIMARY "
            "KEY(run_id,node_uuid,port,ordinal),FOREIGN KEY(run_id,node_uuid,port) REFERENCES "
            "aster_outputs(run_id,node_uuid,port) ON DELETE CASCADE)",
            "CREATE INDEX aster_peak_frequency ON aster_peaks(unit,frequency,run_id)",
            "CREATE INDEX aster_run_date ON aster_runs(started_utc,id)"};
        for (const auto &statement : upgrade)
            if (!q.exec(statement))
                return fail(q.lastError().text());
        QSqlQuery rows(db);
        if (!rows.exec("SELECT p.run_id,p.node_uuid,p.data,s.data FROM aster_outputs p LEFT JOIN "
                       "aster_outputs s ON s.run_id=p.run_id AND s.node_uuid=p.node_uuid AND "
                       "s.port='spectrum' WHERE p.port='peaks'"))
            return fail(rows.lastError().text());
        while (rows.next())
        {
            const auto peaks = decodeData(rows.value(2).toByteArray());
            if (!peaks.ok())
                return fail("Peak index migration: " + peaks.error);
            QMap<QString, DataRef> outputs{{"peaks", peaks.data}};
            if (!rows.value(3).isNull())
            {
                const auto spectrum = decodeData(rows.value(3).toByteArray());
                if (!spectrum.ok())
                    return fail("Peak index migration: " + spectrum.error);
                outputs.insert("spectrum", spectrum.data);
            }
            const auto error = indexPeaks(db, rows.value(0).toLongLong(),
                                          QUuid(rows.value(1).toString()), outputs);
            if (!error.isEmpty())
                return fail(error);
        }
        rows.finish();
        if (!q.exec("UPDATE aster_meta SET schema_version=2"))
            return fail(q.lastError().text());
    }
    else if (version != 2 && version != 3)
        return fail("Unsupported database schema");
    if (version < 3)
    {
        if (!q.exec("CREATE TABLE aster_dependencies(owner_id INTEGER NOT NULL,source_id INTEGER "
                    "NOT NULL,PRIMARY KEY(owner_id,source_id),FOREIGN KEY(owner_id) REFERENCES "
                    "aster_runs(id) ON DELETE CASCADE,FOREIGN KEY(source_id) REFERENCES "
                    "aster_runs(id) ON DELETE RESTRICT)"))
            return fail(q.lastError().text());
        if (!q.exec("CREATE INDEX aster_dependency_source ON aster_dependencies(source_id)"))
            return fail(q.lastError().text());
        if (!q.exec("UPDATE aster_meta SET schema_version=3"))
            return fail(q.lastError().text());
    }
    const auto error = verify();
    if (!error.isEmpty())
        return fail(error);
    if (!db.commit())
        return fail(db.lastError().text());
    return {};
}
} // namespace aster::database_detail
