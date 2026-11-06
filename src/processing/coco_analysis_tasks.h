// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_COCO_ANALYSIS_TASKS_H
#define ASTER_PROCESSING_COCO_ANALYSIS_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define DECLARE_COCO_ANALYSIS_TASK(Name)                                                           \
    class Name final : public Task                                                                 \
    {                                                                                              \
      public:                                                                                      \
        explicit Name(const QJsonObject &p) : m_parameters(p) {}                                   \
        static TaskDescriptor descriptor();                                                        \
        NodeResult execute(const TaskInput &, const ExecutionContext &) override;                  \
                                                                                                   \
      private:                                                                                     \
        QJsonObject m_parameters;                                                                  \
    };
DECLARE_COCO_ANALYSIS_TASK(CocoMonteCarloTask)
DECLARE_COCO_ANALYSIS_TASK(CocoEvolutionTask)
#undef DECLARE_COCO_ANALYSIS_TASK
} // namespace aster
#endif
