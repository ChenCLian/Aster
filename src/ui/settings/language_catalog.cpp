// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/settings/language_catalog.h"
#include "ui/settings/theme_manager.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
namespace aster::ui
{
namespace
{
QJsonObject readCatalog(const QString &language)
{
    QFile file(":/i18n/" + language + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace
LanguageCatalog::LanguageCatalog()
{
    ThemeManager::instance(); // Initializes the static-library resource bundle.
    m_english = readCatalog("en");
    m_current = readCatalog("zh");
}
LanguageCatalog &LanguageCatalog::instance()
{
    static LanguageCatalog catalog;
    return catalog;
}
bool LanguageCatalog::load(const QString &language)
{
    if (language != "zh" && language != "en")
        return false;
    const auto catalog = readCatalog(language);
    if (catalog.isEmpty())
        return false;
    m_language = language;
    m_current = catalog;
    if (QCoreApplication::instance())
    {
        QCoreApplication::removeTranslator(this);
        QCoreApplication::installTranslator(this);
    }
    return true;
}
QString LanguageCatalog::label(const QString &key, const QString &fallback) const
{
    if (m_current.contains(key))
        return m_current[key].toString();
    return m_english.value(key).toString(fallback.isEmpty() ? key : fallback);
}
QString LanguageCatalog::optionalLabel(const QString &key, const QString &fallback) const
{
    const auto current = m_current.value(key).toString();
    if (!current.isEmpty()) return current;
    const auto english = m_english.value(key).toString();
    return english.isEmpty() ? fallback : english;
}
QString LanguageCatalog::translate(const char *context, const char *source, const char *, int) const
{
    const auto key = QString::fromUtf8(source);
    if (qstrcmp(context, "Aster") == 0)
        return label(key);
    // Qt's common standard buttons. Native OS file dialogs remain OS-localized.
    if (qstrcmp(context, "QPlatformTheme") == 0 || qstrcmp(context, "QDialogButtonBox") == 0)
        return m_current.value("qt." + key).toString();
    return {};
}
} // namespace aster::ui
