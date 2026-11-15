// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_OUTLIER_TASKS_H
#define ASTER_OUTLIER_TASKS_H
#include "core/task_registry.h"
namespace aster {
class OutlierTask final : public Task {
public:
    explicit OutlierTask(const QJsonObject &p) : m_parameters(p) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput &,const ExecutionContext &) override;
private: QJsonObject m_parameters;
};
class ValueTransformTask final : public Task {
public:
    explicit ValueTransformTask(const QJsonObject &p) : m_parameters(p) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput &,const ExecutionContext &) override;
private: QJsonObject m_parameters;
};
}
#endif
