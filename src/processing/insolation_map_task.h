#ifndef ASTER_PROCESSING_INSOLATION_MAP_TASK_H
#define ASTER_PROCESSING_INSOLATION_MAP_TASK_H
#include "core/task_registry.h"
namespace aster {
class InsolationMapTask final : public Task {
public:
    explicit InsolationMapTask(const QJsonObject& p) : m_parameters(p) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&,const ExecutionContext&) override;
private: QJsonObject m_parameters;
};
}
#endif
