// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_AR1_GLOBAL_TASK_H
#define ASTER_PROCESSING_AR1_GLOBAL_TASK_H
#include "core/task_registry.h"
#include "math/statistics/ar1_global.h"
namespace aster {
class Ar1GlobalTask final : public Task {
public:
    explicit Ar1GlobalTask(const QJsonObject&);
    Ar1GlobalTask(const QJsonObject&, QString cacheDirectory);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&,const ExecutionContext&) override;
private:
    numeric::Ar1GlobalOptions m_options;
    bool m_cache = true;
    QString m_cacheDirectory;
};
}
#endif
