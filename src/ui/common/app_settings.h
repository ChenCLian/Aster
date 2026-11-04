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
    static QString configPath();
    static AppSettings load(const QString &path = {});
    QString save(const QString &path = {}) const;
};
} // namespace aster::ui
#endif
