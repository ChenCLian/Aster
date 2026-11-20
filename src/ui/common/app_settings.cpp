// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/app_settings.h"
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
namespace aster::ui
{
namespace
{
AppSettings readSettings(const QString &path, const QString &defaultDatabase)
{
    QSettings s(path, QSettings::IniFormat);
    AppSettings out;
    out.databasePath = s.value("storage/database", defaultDatabase).toString();
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
    out.defaultPlotNewPage = s.value("plotting/defaultTarget", "new_page").toString() != "current_page";
    return out;
}
} // namespace
QString AppSettings::configPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("settings.ini");
}
AppSettings AppSettings::load(const QString &path)
{
    const QString database = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                 .filePath("results.sqlite");
    if (!path.isEmpty())
        return readSettings(path, database);
    const QDir configRoot(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    const QDir dataRoot(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    return loadProfile(configPath(), database, configRoot.filePath("Aster/Aster_Workspace/settings.ini"),
                       dataRoot.filePath("Aster/Aster_Workspace/results.sqlite"));
}
AppSettings AppSettings::loadProfile(const QString &settingsFile, const QString &defaultDatabase,
                                    const QString &previousSettings, const QString &previousDatabase)
{
    if (QFileInfo::exists(settingsFile))
        return readSettings(settingsFile, defaultDatabase);

    const bool hasSettings = QFileInfo(previousSettings).isFile();
    const bool usePreviousDatabase = !QFileInfo::exists(defaultDatabase) && QFileInfo(previousDatabase).isFile();
    if (!hasSettings && !usePreviousDatabase)
        return readSettings(settingsFile, defaultDatabase);

    const QString database = usePreviousDatabase ? previousDatabase : defaultDatabase;
    const auto previous = readSettings(previousSettings, database);
    const QDir dir = QFileInfo(settingsFile).absoluteDir();
    if (!dir.mkpath("."))
        return previous;
    QTemporaryDir staging(dir.filePath(".profile-XXXXXX"));
    if (!staging.isValid())
        return previous;
    const QString staged = staging.filePath("settings.ini");
    {
        QSettings original(previousSettings, QSettings::IniFormat);
        QSettings next(staged, QSettings::IniFormat);
        for (const auto &key : original.allKeys())
            next.setValue(key, original.value(key));
        next.setValue("storage/database", previous.databasePath);
        next.sync();
        if (original.status() != QSettings::NoError || next.status() != QSettings::NoError)
            return previous;
    }
    // Publish in the same directory tree. A concurrent first launch may already have installed its file.
    if (!QFile::rename(staged, settingsFile) && !QFileInfo::exists(settingsFile))
        return previous;
    return readSettings(settingsFile, defaultDatabase);
}
QString AppSettings::save(const QString &path) const
{
    QSettings s(path.isEmpty() ? configPath() : path, QSettings::IniFormat);
    s.setValue("storage/database", databasePath);
    s.setValue("appearance/language", language);
    s.setValue("appearance/theme", theme);
    s.setValue("execution/concurrency", concurrency);
    s.setValue("plotting/defaultTarget", defaultPlotNewPage ? "new_page" : "current_page");
    s.sync();
    return s.status() == QSettings::NoError
               ? QString()
               : QString("Cannot write settings: %1").arg(s.fileName());
}
} // namespace aster::ui
