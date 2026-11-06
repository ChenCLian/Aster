// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_LOWESS_TASK_H
#define ASTER_PROCESSING_LOWESS_TASK_H
#include "core/task_registry.h"
namespace aster {
class LowessTask final : public Task {
public:
    explicit LowessTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private: double m_fraction; int m_iterations; bool m_retainMean;
};
}
#endif
