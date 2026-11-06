// Aster integration and modifications: Copyright (C) 2026 ZiJie Chen.
// C++ port distributed with Aster 1.1.1, 2026-11-06; see resources/synthetic/NOTICE.md.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ASTER_PROCESSING_SYNTHETIC_ORBITAL_TASK_H
#define ASTER_PROCESSING_SYNTHETIC_ORBITAL_TASK_H
#include "core/task_registry.h"
namespace aster {
class SyntheticOrbitalTask final : public Task {
    QJsonObject m_parameters;
public:
    explicit SyntheticOrbitalTask(const QJsonObject& p):m_parameters(p){}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
};
}
#endif
