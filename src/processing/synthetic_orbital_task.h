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
