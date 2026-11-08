// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_POPUP_PLACEMENT_H
#define ASTER_UI_POPUP_PLACEMENT_H
#include <QRect>
#include <QSize>
class QWidget;
class QMenu;
namespace aster::ui
{
// Coordinates are Qt logical desktop coordinates, including negative monitor origins.
QRect popupBounds(QWidget *owner, const QRect &anchor);
QRect popupGeometry(QSize requested, const QRect &anchor, const QRect &bounds);
void configureMenu(QMenu *menu, int minimumWidth = 0);
QPoint menuPopupPosition(QMenu *menu, QWidget *owner, const QRect &anchor);
void showMenuWithinWindow(QMenu *menu, QWidget *owner, const QRect &anchor);
} // namespace aster::ui
#endif
