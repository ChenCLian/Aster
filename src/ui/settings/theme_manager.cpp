// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/settings/theme_manager.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QPalette>
#include <QFontDatabase>

// Explicit initialization is necessary because desktop_ui is a static library.
static void initializeResources()
{
    Q_INIT_RESOURCE(aster);
    Q_INIT_RESOURCE(help);
}
namespace aster::ui
{
namespace
{
QJsonObject palette(const QString &name)
{
    QFile file(":/themes/" + name + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace
ThemeManager::ThemeManager()
{
    initializeResources();
    m_colors = palette("light");
}
ThemeManager &ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}
QColor ThemeManager::color(const QString &role) const
{
    return QColor(m_colors.value(role).toString(m_colors.value("text").toString()));
}
QColor themeColor(const char *role)
{
    return ThemeManager::instance().color(QString::fromLatin1(role));
}
QString ThemeManager::apply(QApplication &app, const QString &name)
{
    if (name != "light" && name != "dark")
        return "Unknown theme";
    const auto colors = palette(name);
    QFile file(":/themes/base.qss");
    if (colors.isEmpty() || !file.open(QIODevice::ReadOnly))
        return "Missing theme resources";
    auto qss = QString::fromUtf8(file.readAll());
    for (auto i = colors.begin(); i != colors.end(); ++i)
        qss.replace("@" + i.key() + "@", i.value().toString());
    qss.replace("@theme@", name);
    if (qss.contains('@'))
        return "Unresolved theme token";
    m_colors = colors;
    m_name = name;
    app.setStyle("Fusion");
    QPalette p;
    // Scroll viewports and unstyled children inherit the same panel surface.
    // The top-level workspace background is styled separately in base.qss.
    p.setColor(QPalette::Window, color("surface"));
    p.setColor(QPalette::Base, color("surface"));
    p.setColor(QPalette::AlternateBase, color("canvas"));
    p.setColor(QPalette::Button, color("surface"));
    p.setColor(QPalette::ToolTipBase, color("surface"));
    for (auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
    {
        p.setColor(role, color("text"));
        p.setColor(QPalette::Disabled, role, color("disabled"));
    }
    p.setColor(QPalette::Highlight, color("selection"));
    p.setColor(QPalette::HighlightedText, color("selectionText"));
    p.setColor(QPalette::Link, color("accent"));
    app.setPalette(p);
    QFont font = app.font();
    for (const auto &family :
         QStringList{"Microsoft YaHei UI", "Noto Sans CJK SC", "Noto Sans SC", "Segoe UI"})
        if (QFontDatabase::families().contains(family))
        {
            font.setFamily(family);
            break;
        }
    font.setPointSizeF(10.0);
    app.setFont(font);
    app.setStyleSheet(qss);
    emit changed();
    return {};
}
} // namespace aster::ui
