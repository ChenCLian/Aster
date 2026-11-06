// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/database/result_repository.h"
#include "io/database/record_query.h"
#include <QSqlQuery>
#include <QSqlError>

namespace aster
{
namespace
{
// Connection-local selection table avoids SQLite variable limits and never loads BLOBs.
QString fillSelection(QSqlDatabase &db, const RecordIdentities &ids)
{
    QSqlQuery q(db);
    if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS aster_selection(id INTEGER PRIMARY KEY,run_uuid "
                "TEXT NOT NULL)") ||
        !q.exec("DELETE FROM aster_selection"))
        return q.lastError().text();
    q.prepare("INSERT INTO aster_selection VALUES(?,?)");
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
    {
        if (it.key() <= 0 || it.value().isNull())
            return "Invalid selected record identity";
        q.bindValue(0, it.key());
        q.bindValue(1, it.value().toString(QUuid::WithoutBraces));
        if (!q.exec())
            return q.lastError().text();
    }
    return {};
}
} // namespace
RecordSelectionState ResultRepository::identities(const RecordFilter &filter) const
{
    RecordSelectionState result;
    result.error = checkThread();
    if (result.error.isEmpty())
        result.error = filter.validate();
    if (!result.error.isEmpty())
        return result;
    QVariantList values;
    QSqlQuery q(m_db);
    q.prepare("SELECT r.id,r.run_uuid FROM aster_runs r WHERE 1=1" +
              database_detail::recordConditions(filter, values));
    for (const auto &v : values)
        q.addBindValue(v);
    if (!q.exec())
    {
        result.error = q.lastError().text();
        return result;
    }
    while (q.next())
        result.existing.insert(q.value(0).toLongLong(), QUuid(q.value(1).toString()));
    result.matching = result.existing.size();
    return result;
}
RecordSelectionState ResultRepository::selectionState(const RecordIdentities &selected,
                                                      const RecordFilter &filter)
{
    RecordSelectionState result;
    result.error = checkThread();
    if (result.error.isEmpty())
        result.error = filter.validate();
    if (!result.error.isEmpty() || selected.isEmpty())
        return result;
    if (!m_db.transaction())
    {
        result.error = m_db.lastError().text();
        return result;
    }
    result.error = fillSelection(m_db, selected);
    QSqlQuery q(m_db);
    const QString join =
        " FROM aster_runs r JOIN aster_selection s ON s.id=r.id AND s.run_uuid=r.run_uuid";
    if (result.error.isEmpty())
    {
        if (!q.exec("SELECT r.id,r.run_uuid" + join))
            result.error = q.lastError().text();
        else
            while (q.next())
                result.existing.insert(q.value(0).toLongLong(), QUuid(q.value(1).toString()));
    }
    if (result.error.isEmpty())
    {
        QVariantList values;
        q.prepare("SELECT count(*)" + join + " WHERE 1=1" +
                  database_detail::recordConditions(filter, values));
        for (const auto &v : values)
            q.addBindValue(v);
        if (!q.exec() || !q.next())
            result.error = q.lastError().text();
        else
            result.matching = q.value(0).toLongLong();
    }
    q.finish();
    // Only a temporary table was changed; rollback also clears the transient snapshot.
    m_db.rollback();
    return result;
}
RecordRemoval ResultRepository::remove(const RecordIdentities &selected)
{
    RecordRemoval result;
    result.error = checkThread();
    if (!result.error.isEmpty() || selected.isEmpty())
        return result;
    QSqlQuery q(m_db);
    if (!q.exec("BEGIN IMMEDIATE"))
    {
        result.error = q.lastError().text();
        return result;
    }
    const auto fail = [&](QString error)
    {
        m_db.rollback();
        result.error = error;
        return result;
    };
    const auto fillError = fillSelection(m_db, selected);
    if (!fillError.isEmpty())
        return fail(fillError);
    if (!q.exec("SELECT s.id FROM aster_selection s LEFT JOIN aster_runs r "
                "ON r.id=s.id AND r.run_uuid=s.run_uuid WHERE r.id IS NULL LIMIT 1"))
        return fail(q.lastError().text());
    if (q.next())
        return fail(
            QString("Selected record ID %1 changed or no longer exists; refresh and select again.")
                .arg(q.value(0).toLongLong()));
    if (!q.exec("SELECT d.source_id,d.owner_id FROM aster_dependencies d "
                "JOIN aster_selection s ON s.id=d.source_id "
                "WHERE NOT EXISTS(SELECT 1 FROM aster_selection o WHERE o.id=d.owner_id) LIMIT 1"))
        return fail(q.lastError().text());
    if (q.next())
    {
        result.blockedSource = q.value(0).toLongLong();
        result.blockingOwner = q.value(1).toLongLong();
        m_db.rollback();
        return result;
    }
    // Owners in the set are going away too: remove their dependency edges first.
    if (!q.exec(
            "DELETE FROM aster_dependencies WHERE owner_id IN(SELECT id FROM aster_selection)") ||
        !q.exec("DELETE FROM aster_runs WHERE id IN(SELECT id FROM aster_selection)"))
        return fail(q.lastError().text());
    q.finish();
    if (!m_db.commit())
        return fail(m_db.lastError().text());
    result.removed = selected;
    return result;
}
} // namespace aster
