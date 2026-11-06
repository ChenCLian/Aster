// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_EXPORT_PATH_H
#define ASTER_IO_EXPORT_PATH_H
#include <QStringList>
namespace aster
{
// Shared by CSV and figure exports, including non-interactive callers.
bool conflictsWithDatabase(const QString &target, const QStringList &databases);
} // namespace aster
#endif
