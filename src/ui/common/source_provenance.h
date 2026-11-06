// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_SOURCE_PROVENANCE_H
#define ASTER_UI_SOURCE_PROVENANCE_H
#include <QJsonObject>
#include <QString>
namespace aster::ui {
QString sourceProvenanceHtml(const QJsonObject &report);
}
#endif
