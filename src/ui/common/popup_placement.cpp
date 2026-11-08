// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/popup_placement.h"
#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QWidget>
#include <algorithm>
namespace aster::ui
{
QRect popupBounds(QWidget *owner, const QRect &anchor)
{
    QWidget *window = owner ? owner->window() : nullptr;
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen && window) screen = window->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    QRect bounds = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    if (window && window->isVisible())
    {
        const QRect visible = bounds.intersected(QRect(window->mapToGlobal(QPoint()), window->size()));
        if (!visible.isEmpty()) bounds = visible;
    }
    if (bounds.width() > 32 && bounds.height() > 32) bounds.adjust(8, 8, -8, -8);
    return bounds;
}
QRect popupGeometry(QSize requested, const QRect &anchor, const QRect &bounds)
{
    if (bounds.isEmpty()) return {};
    const QSize size(std::clamp(requested.width(), 1, bounds.width()),
                     std::clamp(requested.height(), 1, bounds.height()));
    const int x = std::clamp(anchor.left(), bounds.left(), bounds.right() - size.width() + 1);
    const int y = std::clamp(anchor.top(), bounds.top(), bounds.bottom() - size.height() + 1);
    const QRect candidates[] = {
        QRect(QPoint(x, anchor.bottom() + 7), size),
        QRect(QPoint(x, anchor.top() - size.height() - 6), size),
        QRect(QPoint(anchor.right() + 7, y), size),
        QRect(QPoint(anchor.left() - size.width() - 6, y), size)};
    for (const auto &candidate : candidates)
        if (bounds.contains(candidate)) return candidate;
    return QRect(QPoint(bounds.left() + (bounds.width() - size.width()) / 2,
                        bounds.top() + (bounds.height() - size.height()) / 2), size);
}
void configureMenu(QMenu *menu, int minimumWidth)
{
    auto font = QApplication::font();
    font.setPointSizeF(std::max(11.0, font.pointSizeF()));
    menu->setFont(font);
    menu->setToolTipsVisible(true);
    if (minimumWidth) menu->setMinimumWidth(minimumWidth);
}
QPoint menuPopupPosition(QMenu *menu, QWidget *owner, const QRect &anchor)
{
    configureMenu(menu);
    menu->ensurePolished();
    const auto bounds = popupBounds(owner, anchor);
    // QMenu handles scrolling and submenu direction itself. Root menus are
    // anchored inside the visible owner instead of inheriting the last cursor position.
    return popupGeometry(menu->sizeHint(), anchor, bounds).topLeft();
}
void showMenuWithinWindow(QMenu *menu, QWidget *owner, const QRect &anchor)
{
    menu->popup(menuPopupPosition(menu, owner, anchor));
}
} // namespace aster::ui
