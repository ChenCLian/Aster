// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_THEME_MANAGER_H
#define ASTER_THEME_MANAGER_H
#include <QObject>
#include <QColor>
#include <QJsonObject>
class QApplication;
namespace aster::ui
{
class ThemeManager : public QObject
{
    Q_OBJECT
  public:
    static ThemeManager &instance();
    QString apply(QApplication &app, const QString &theme);
    QColor color(const QString &role) const;
    QString name() const
    {
        return m_name;
    }
  signals:
    void changed();

  private:
    ThemeManager();
    QString m_name = "light";
    QJsonObject m_colors;
};
QColor themeColor(const char *role);
} // namespace aster::ui
#endif
