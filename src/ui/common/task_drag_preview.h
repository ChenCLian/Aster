// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_TASK_DRAG_PREVIEW_H
#define ASTER_UI_TASK_DRAG_PREVIEW_H
#include "core/task_registry.h"
#include <QFont>
#include <QPixmap>
#include <QPoint>
namespace aster::ui
{
struct TaskDragPreview
{
    QPixmap pixmap;
    QPoint hotSpot;
};
TaskDragPreview taskDragPreview(const TaskDescriptor &, const QFont &, qreal devicePixelRatio);
} // namespace aster::ui
#endif
