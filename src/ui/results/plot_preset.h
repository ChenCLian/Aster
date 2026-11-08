// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_PRESET_H
#define ASTER_PLOT_PRESET_H
#include "ui/results/plot_style.h"
#include "core/task.h"
namespace aster::ui
{
// Presentation defaults only. Shared by preview, result views and figure export.
// Resolve from stable output IDs/data kinds/axis semantics; user overrides are
// applied by PlotView afterwards and never persisted into scientific results.
PlotStyle plotPreset(const NodeResult &result, const QString &port,
                     const QString &metric = "coherence");
}
#endif
