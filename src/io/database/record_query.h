// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_QUERY_H
#define ASTER_RECORD_QUERY_H
#include "io/database/record_filter.h"
#include <QVariant>
namespace aster::database_detail
{
inline QString peakConditions(const RecordFilter &f, QVariantList &values)
{
    QString sql = " AND p.unit=?";
    values << (f.frequencyUnit.isNull() ? QString("") : f.frequencyUnit);
    if (f.minFrequency >= 0)
    {
        sql += " AND p.frequency>=?";
        values << f.minFrequency;
    }
    if (f.maxFrequency >= 0)
    {
        sql += " AND p.frequency<=?";
        values << f.maxFrequency;
    }
    return sql;
}
inline QString recordConditions(const RecordFilter &filter, QVariantList &values)
{
    QString sql;
    if (!filter.text.isEmpty())
    {
        sql +=
            " AND (instr(r.source_label,?)>0 OR instr(r.display_name,?)>0 OR instr(r.notes,?)>0)";
        values << filter.text << filter.text << filter.text;
    }
    if (filter.since.isValid())
    {
        sql += " AND r.started_utc>=?";
        values << filter.since.toUTC().toString(Qt::ISODateWithMs);
    }
    if (filter.until.isValid())
    {
        sql += " AND r.started_utc<=?";
        values << filter.until.toUTC().toString(Qt::ISODateWithMs);
    }
    if (filter.hasFrequency())
        sql += " AND EXISTS(SELECT 1 FROM aster_peaks p WHERE p.run_id=r.id" +
               peakConditions(filter, values) + ")";
    return sql;
}
} // namespace aster::database_detail
#endif
