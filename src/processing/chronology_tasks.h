// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_CHRONOLOGY_TASKS_H
#define ASTER_PROCESSING_CHRONOLOGY_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_CHRONOLOGY_TASK(Type)                                                                \
    class Type final : public Task                                                                 \
    {                                                                                              \
      public:                                                                                      \
        explicit Type(const QJsonObject &parameters) : m_parameters(parameters) {}                 \
        static TaskDescriptor descriptor();                                                        \
        NodeResult execute(const TaskInput &, const ExecutionContext &) override;                  \
                                                                                                   \
      private:                                                                                     \
        QJsonObject m_parameters;                                                                  \
    };
ASTER_CHRONOLOGY_TASK(SedrateToAgeTask)
ASTER_CHRONOLOGY_TASK(ApplyAgeModelTask)
#undef ASTER_CHRONOLOGY_TASK
} // namespace aster
#endif
