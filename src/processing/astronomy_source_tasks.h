#ifndef ASTER_PROCESSING_ASTRONOMY_SOURCE_TASKS_H
#define ASTER_PROCESSING_ASTRONOMY_SOURCE_TASKS_H
#include "core/task_registry.h"
namespace aster {
// Independently registered source tasks; no file binding or parent required.
#define ASTER_DECLARE_ASTRONOMY_SOURCE(Type) \
class Type final : public Task { \
public: \
    explicit Type(const QJsonObject& p) : m_parameters(p) {} \
    static TaskDescriptor descriptor(); \
    NodeResult execute(const TaskInput&, const ExecutionContext&) override; \
private: QJsonObject m_parameters; \
};
ASTER_DECLARE_ASTRONOMY_SOURCE(OrbitalSourceTask)
ASTER_DECLARE_ASTRONOMY_SOURCE(EtpSourceTask)
ASTER_DECLARE_ASTRONOMY_SOURCE(InsolationSourceTask)
ASTER_DECLARE_ASTRONOMY_SOURCE(SeasonalInsolationTask)
#undef ASTER_DECLARE_ASTRONOMY_SOURCE
}
#endif
