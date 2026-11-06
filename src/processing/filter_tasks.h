// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_FILTER_TASKS_H
#define ASTER_PROCESSING_FILTER_TASKS_H
#include "core/task_registry.h"
namespace aster {
#define ASTER_DECLARE_FILTER(Type) \
class Type final : public Task { \
public: \
    explicit Type(const QJsonObject& p) : m_parameters(p) {} \
    static TaskDescriptor descriptor(); \
    NodeResult execute(const TaskInput&, const ExecutionContext&) override; \
private: QJsonObject m_parameters; \
};
ASTER_DECLARE_FILTER(GaussianFilterTask)
ASTER_DECLARE_FILTER(TanerFilterTask)
ASTER_DECLARE_FILTER(HilbertEnvelopeTask)
#undef ASTER_DECLARE_FILTER
}
#endif
