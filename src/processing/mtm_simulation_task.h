#ifndef ASTER_PROCESSING_MTM_SIMULATION_TASK_H
#define ASTER_PROCESSING_MTM_SIMULATION_TASK_H
#include "core/task_registry.h"
#include "math/statistics/mtm_simulation.h"
namespace aster {
class MtmSimulationTask final : public Task {
public:
    explicit MtmSimulationTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    numeric::MtmSimulationOptions m_options;
};
}
#endif
