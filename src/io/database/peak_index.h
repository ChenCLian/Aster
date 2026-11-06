// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PEAK_INDEX_H
#define ASTER_PEAK_INDEX_H
#include "core/task.h"
#include <QSqlDatabase>
namespace aster::database_detail
{
QString ensureSchema(QSqlDatabase &db);
QString indexPeaks(QSqlDatabase &db, qint64 run, NodeId node,
                   const QMap<QString, DataRef> &outputs);
} // namespace aster::database_detail
#endif
