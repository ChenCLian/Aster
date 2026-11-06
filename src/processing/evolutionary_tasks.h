// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_EVOLUTIONARY_TASKS_H
#define ASTER_PROCESSING_EVOLUTIONARY_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_EVOLUTION_TASK(Type)                                                                 \
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
ASTER_EVOLUTION_TASK(EvolutionFftTask)
ASTER_EVOLUTION_TASK(EvolutionMtmTask)
#undef ASTER_EVOLUTION_TASK
} // namespace aster
#endif
