#ifndef ASTER_PROCESSING_ORBITAL_TABLE_TASKS_H
#define ASTER_PROCESSING_ORBITAL_TABLE_TASKS_H
#include "core/task_registry.h"
namespace aster {
#define ASTER_DECLARE_ORBITAL_TASK(Type) \
class Type final : public Task { \
public: \
    explicit Type(const QJsonObject& p) : m_parameters(p) {} \
    static TaskDescriptor descriptor(); \
    NodeResult execute(const TaskInput&, const ExecutionContext&) override; \
private: QJsonObject m_parameters; \
};
ASTER_DECLARE_ORBITAL_TASK(EccentricitySourceTask)
ASTER_DECLARE_ORBITAL_TASK(PrecessionTiltSourceTask)
#undef ASTER_DECLARE_ORBITAL_TASK
}
#endif
