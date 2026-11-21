// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_RECORD_NAME_H
#define ASTER_IO_RECORD_NAME_H
#include "io/database/output_reference.h"
namespace aster
{
QString recordSourceName(const QString &displayName, const QString &sourceLabel,
                         const QJsonObject &provenance, const QString &rootMethod);
QString recordExportStem(const OutputReference &reference);
}
#endif
