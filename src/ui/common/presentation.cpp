// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/presentation.h"
#include "ui/common/error_presenter.h"
#include "ui/settings/language_catalog.h"
#include "ui/settings/theme_manager.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QRegularExpression>
namespace aster::ui
{
void setLanguage(const QString &language)
{
    LanguageCatalog::instance().load(language);
}
QString text(const char *key)
{
    return LanguageCatalog::instance().label(QString::fromUtf8(key));
}
QString shortcutLabel(const QString &label, const QKeySequence &shortcut)
{
    return text("ui.shortcut_label").arg(label, shortcut.toString(QKeySequence::NativeText));
}
QString translatedLabel(const QString &key, const QString &fallback)
{
    return LanguageCatalog::instance().label(key, fallback);
}
QString methodLabel(const TaskDescriptor &d)
{
    return translatedLabel("method." + d.key, d.label);
}
QString categoryLabel(const QString &category)
{
    return translatedLabel("category." + category, category);
}
QString parameterLabel(const ParameterSpec &p)
{
    return LanguageCatalog::instance().optionalLabel("parameter." + p.key,
                                                     p.label.isEmpty() ? p.key : p.label);
}
QString parameterLabel(const ParameterSpec &p, const QString &method)
{
    return LanguageCatalog::instance().optionalLabel("parameter." + method + "." + p.key,
                                                     parameterLabel(p));
}
QString parameterHelp(const ParameterSpec &p, const QString &method)
{
    auto &catalog = LanguageCatalog::instance();
    return catalog.optionalLabel("parameterhelp." + method + "." + p.key,
                                 catalog.optionalLabel("parameterhelp." + p.key, p.help));
}
QString methodHelp(const QString &method)
{
    return LanguageCatalog::instance().optionalLabel("methodhelp." + method);
}
QString portLabel(const QString &p)
{
    return translatedLabel("port." + p, p);
}
QString portLabel(const TaskDescriptor &descriptor, const QString &port)
{
    QString fallback = port;
    for (const auto &output : descriptor.outputs)
        if (output.key == port) fallback = output.label;
    fallback = translatedLabel("port." + port, fallback);
    return translatedLabel("output." + descriptor.key + "." + port, fallback);
}
QString resultError(const NodeResult &result)
{
    return presentError(result).summary;
}
QString choiceLabel(const QString &parameter, const QString &value)
{
    return translatedLabel("choice." + parameter + "." + value, value);
}
QString stateLabel(NodeState s)
{
    switch (s)
    {
    case NodeState::Pending:
        return text("ui.pending");
    case NodeState::Running:
        return text("ui.running");
    case NodeState::Succeeded:
        return text("ui.completed");
    case NodeState::Failed:
        return text("ui.failed");
    case NodeState::Blocked:
        return text("ui.blocked");
    case NodeState::Cancelled:
        return text("ui.cancelled");
    }
    return {};
}
QColor stateColor(NodeState s)
{
    if (s == NodeState::Succeeded)
        return themeColor("success");
    if (s == NodeState::Failed)
        return themeColor("error");
    if (s == NodeState::Running)
        return themeColor("accent");
    return themeColor("muted");
}
QIcon methodIcon(bool source)
{
    QPixmap pix(24, 24);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(themeColor("hover"));
    p.drawRoundedRect(1, 1, 22, 22, 6, 6);
    p.setPen(QPen(themeColor(source ? "success" : "accent"), 1.8));
    if (source)
    {
        p.drawEllipse(QRectF(7, 7, 10, 10));
        p.drawLine(12, 5, 12, 19);
        p.drawLine(5, 12, 19, 12);
    }
    else
    {
        QPainterPath path;
        path.moveTo(5, 15);
        path.lineTo(9, 9);
        path.lineTo(13, 16);
        path.lineTo(18, 7);
        p.drawPath(path);
    }
    return QIcon(pix);
}
QIcon runIcon()
{
    QPixmap pix(40, 40);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(themeColor("accentText"));
    painter.drawPolygon(QPolygonF{QPointF(5, 3), QPointF(17, 10), QPointF(5, 17)});
    return QIcon(pix);
}
void applyTheme(QApplication &app)
{
    ThemeManager::instance().apply(app, "light");
}
} // namespace aster::ui
