// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/database/sqlite_snapshot.h"
#include "io/database/peak_index.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QUuid>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace aster::database_detail
{
void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}
void queryExec(QSqlQuery &q, const QString &sql)
{
    const bool ok = sql.isNull() ? q.exec() : q.exec(sql);
    require(ok, q.lastError().text());
}
void queryPrepare(QSqlQuery &q, const QString &sql)
{
    const bool ok = q.prepare(sql);
    require(ok, q.lastError().text());
}
void execute(QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    queryExec(q, sql);
}
SqliteFile::SqliteFile(const QString &path, bool readOnly)
    : m_name("aster_transfer_" + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    require(path == ":memory:" || QFileInfo(path).isFile(),
            "Database file does not exist: " + path);
    db = QSqlDatabase::addDatabase("QSQLITE", m_name);
    db.setDatabaseName(path);
    db.setConnectOptions(readOnly ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"
                                  : "QSQLITE_BUSY_TIMEOUT=5000");
    // A throwing constructor must release its named connection explicitly.
    try
    {
        const bool opened = db.open();
        require(opened, db.lastError().text());
        execute(db, "PRAGMA foreign_keys=ON");
        execute(db, "PRAGMA trusted_schema=OFF");
        execute(db, "PRAGMA synchronous=FULL");
    }
    catch (...)
    {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
        throw;
    }
}
SqliteFile::~SqliteFile()
{
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_name);
}
namespace
{
QList<QByteArray> rows(QSqlDatabase &db, const QString &sql, bool sorted = true)
{
    QSqlQuery q(db);
    queryExec(q, sql);
    QList<QByteArray> result;
    while (q.next())
    {
        QJsonArray values;
        for (int i = 0; i < q.record().count(); ++i)
            values.append(QJsonValue::fromVariant(q.value(i)));
        result.append(QJsonDocument(values).toJson(QJsonDocument::Compact));
    }
    require(!q.lastError().isValid(), q.lastError().text());
    if (sorted)
        std::sort(result.begin(), result.end());
    return result;
}
QList<QByteArray> indexes(QSqlDatabase &db, const QString &table)
{
    QSqlQuery q(db);
    queryExec(q, "PRAGMA index_list(" + table + ")");
    QList<QByteArray> result;
    while (q.next())
    {
        const auto name = q.value(1).toString();
        QString quoted = name;
        quoted.replace("'", "''");
        QByteArray signature = q.value(2).toByteArray() + '|' + q.value(3).toByteArray() + '|' +
                               q.value(4).toByteArray();
        for (const auto &row : rows(db, "PRAGMA index_xinfo('" + quoted + "')", false))
            signature += '|' + row;
        result.append(signature);
    }
    std::sort(result.begin(), result.end());
    return result;
}
} // namespace
void validateDatabase(QSqlDatabase &db, bool integrity)
{
    QSqlQuery q(db);
    require(q.exec("SELECT schema_version FROM aster_meta"),
            "Not an Aster database: missing metadata");
    require(q.next(), "Missing Aster schema version");
    require(q.value(0).metaType().id() == QMetaType::LongLong ||
                q.value(0).metaType().id() == QMetaType::Int,
            "Invalid Aster schema version type");
    require(q.value(0).toInt() == 3,
            "Unsupported database schema; merge requires Aster schema 3. Source is not upgraded.");
    require(!q.next(), "Ambiguous Aster schema metadata");
    q.finish();
    // Build the expected schema from the actual migration implementation, rather than a second DDL
    // copy.
    SqliteFile reference(":memory:", false);
    const auto schemaError = ensureSchema(reference.db);
    require(schemaError.isEmpty(), schemaError);
    const auto objects = [](QSqlDatabase &db)
    {
        return rows(
            db,
            "SELECT name,type FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' AND type!='index'");
    };
    require(objects(db) == objects(reference.db), "Incompatible Aster tables, views or triggers");
    for (const auto &table : {"aster_meta", "aster_runs", "aster_nodes", "aster_outputs",
                              "aster_peaks", "aster_dependencies"})
    {
        const auto t = QString::fromLatin1(table);
        require(rows(db, "PRAGMA table_xinfo(" + t + ")") ==
                    rows(reference.db, "PRAGMA table_xinfo(" + t + ")"),
                "Incompatible columns: " + t);
        require(rows(db, "PRAGMA foreign_key_list(" + t + ")") ==
                    rows(reference.db, "PRAGMA foreign_key_list(" + t + ")"),
                "Incompatible foreign keys: " + t);
        require(indexes(db, t) == indexes(reference.db, t), "Incompatible indexes: " + t);
    }
    if (integrity)
    {
        queryExec(q, "PRAGMA integrity_check");
        require(q.next() && q.value(0).toString() == "ok" && !q.next(),
                "Database integrity check failed");
        q.finish();
        queryExec(q, "PRAGMA foreign_key_check");
        require(!q.next(), "Database contains orphaned references");
    }
}
void snapshotDatabase(QSqlDatabase &db, const QString &newFile)
{
    const QFileInfo file(newFile);
    require(!QFileInfo::exists(newFile), "Backup already exists; choose a new filename");
    require(QDir().mkpath(file.absolutePath()), "Cannot create backup directory");
    QTemporaryDir staging(file.absolutePath() + "/.aster-snapshot-XXXXXX");
    require(staging.isValid(), "Cannot create snapshot staging directory");
    const auto staged = staging.filePath("snapshot.sqlite");
    {
        QSqlQuery q(db);
        queryPrepare(q, "VACUUM main INTO ?");
        q.addBindValue(staged);
        queryExec(q);
    }
    {
        SqliteFile copy(staged);
        validateDatabase(copy.db, true);
    }
    // Same-directory rename publishes only a complete, verified snapshot; Qt refuses an existing
    // target.
    require(QFile::rename(staged, file.absoluteFilePath()),
            "Cannot publish database backup (target may already exist)");
}
} // namespace aster::database_detail
