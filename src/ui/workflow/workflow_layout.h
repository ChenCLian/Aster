// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_WORKFLOW_LAYOUT_H
#define ASTER_UI_WORKFLOW_LAYOUT_H
#include "model/workflow_types.h"
#include <QRectF>

namespace aster::ui
{
inline constexpr double WorkflowCardWidth = 198;
inline constexpr double WorkflowCardHeight = 88;
// Presentation only: never changes the persisted grid, bindings or execution order.
QMap<NodeId, QRectF> verticalWorkflowLayout(const WorkflowDefinition &definition);
} // namespace aster::ui
#endif
