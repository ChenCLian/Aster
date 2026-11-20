// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_APP_SETTINGS_H
#define ASTER_UI_APP_SETTINGS_H
#include <QString>
#include "application/execution_defaults.h"
namespace aster::ui
{
struct AppSettings
{
    QString databasePath;
    QString language = "zh";
    QString theme = "light";
    int concurrency = defaultConcurrency();
    bool defaultPlotNewPage = true;
    static QString configPath();
    static AppSettings load(const QString &path = {});
    static AppSettings loadProfile(const QString &settingsFile, const QString &defaultDatabase,
                                   const QString &previousSettings, const QString &previousDatabase);
    QString save(const QString &path = {}) const;
};
} // namespace aster::ui
#endif
