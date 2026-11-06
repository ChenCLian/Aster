// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_WAVELET_TASK_H
#define ASTER_PROCESSING_WAVELET_TASK_H
#include "core/task_registry.h"
namespace aster {
class WaveletTask final : public Task {
public:
    explicit WaveletTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    QString m_mother, m_method;
    double m_parameter, m_dj;
    bool m_significance;
};
}
#endif
