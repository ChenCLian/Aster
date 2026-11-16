// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_HELP_CATALOG_H
#define ASTER_UI_HELP_CATALOG_H
#include <QStringList>
#include <QVector>
namespace aster::ui {
struct HelpSection {
    QString title, markdown;
    QStringList anchors;
    int level = 1;
};
struct HelpPageLink {
    QString title, path;
    QStringList group;
};
QVector<HelpSection> helpSections(const QString &path);
QVector<HelpPageLink> helpNavigation(const QString &language);
} // namespace aster::ui
#endif
