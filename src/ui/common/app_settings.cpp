// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/app_settings.h"
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <algorithm>
namespace aster::ui
{
QString AppSettings::configPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("settings.ini");
}
AppSettings AppSettings::load(const QString &path)
{
    QSettings s(path.isEmpty() ? configPath() : path, QSettings::IniFormat);
    AppSettings out;
    out.databasePath =
        s.value("storage/database",
                QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                    .filePath("results.sqlite"))
            .toString();
    out.language = s.value("appearance/language", "zh").toString();
    if (out.language != "zh" && out.language != "en")
        out.language = "zh";
    out.theme = s.value("appearance/theme", "light").toString();
    if (out.theme != "light" && out.theme != "dark")
        out.theme = "light";
    bool validConcurrency = false;
    const int storedConcurrency = s.value("execution/concurrency", out.concurrency).toInt(&validConcurrency);
    if (validConcurrency)
        out.concurrency = std::clamp(storedConcurrency, minimumConcurrency, maximumConcurrency);
    return out;
}
QString AppSettings::save(const QString &path) const
{
    QSettings s(path.isEmpty() ? configPath() : path, QSettings::IniFormat);
    s.setValue("storage/database", databasePath);
    s.setValue("appearance/language", language);
    s.setValue("appearance/theme", theme);
    s.setValue("execution/concurrency", concurrency);
    s.sync();
    return s.status() == QSettings::NoError
               ? QString()
               : QString("Cannot write settings: %1").arg(s.fileName());
}
} // namespace aster::ui
