// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_PREPROCESS_TASK_H
#define ASTER_PROCESSING_PREPROCESS_TASK_H
#include "core/task_registry.h"
namespace aster {
class PreprocessTask final : public Task {
public:
    explicit PreprocessTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    QString m_duplicates, m_mode, m_interpolation;
    double m_spacing;
};
}
#endif
