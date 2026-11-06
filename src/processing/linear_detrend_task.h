// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_LINEAR_DETREND_TASK_H
#define ASTER_PROCESSING_LINEAR_DETREND_TASK_H

#include "core/task_registry.h"

namespace aster {
class LinearDetrendTask final : public Task {
public:
    explicit LinearDetrendTask(const QJsonObject&) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
};
} // namespace aster
#endif
