// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_SINE_SOURCE_TASK_H
#define ASTER_PROCESSING_SINE_SOURCE_TASK_H

#include "core/task_registry.h"

namespace aster {
class SineSourceTask final : public Task {
public:
    explicit SineSourceTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    int m_count;
    double m_step, m_period, m_amplitude, m_slope;
};
} // namespace aster
#endif
