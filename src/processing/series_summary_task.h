// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_SERIES_SUMMARY_TASK_H
#define ASTER_PROCESSING_SERIES_SUMMARY_TASK_H

#include "core/task_registry.h"

namespace aster {
class SeriesSummaryTask final : public Task {
public:
    explicit SeriesSummaryTask(const QJsonObject&) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
};
} // namespace aster
#endif
