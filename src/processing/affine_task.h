// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_AFFINE_TASK_H
#define ASTER_PROCESSING_AFFINE_TASK_H

#include "core/task_registry.h"

namespace aster {
class AffineTask final : public Task {
public:
    explicit AffineTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    double m_factor, m_offset;
};
} // namespace aster
#endif
