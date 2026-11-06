// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_CYCLE_TUNING_TASKS_H
#define ASTER_PROCESSING_CYCLE_TUNING_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_CYCLE_TASK(Type)                                                                     \
    class Type final : public Task                                                                 \
    {                                                                                              \
      public:                                                                                      \
        explicit Type(const QJsonObject &p) : m_parameters(p) {}                                   \
        static TaskDescriptor descriptor();                                                        \
        NodeResult execute(const TaskInput &, const ExecutionContext &) override;                  \
                                                                                                   \
      private:                                                                                     \
        QJsonObject m_parameters;                                                                  \
    };
ASTER_CYCLE_TASK(MilankovitchPeriodTask)
ASTER_CYCLE_TASK(CycleAgeModelTask)
#undef ASTER_CYCLE_TASK
} // namespace aster
#endif
