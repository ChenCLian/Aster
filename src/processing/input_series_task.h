#ifndef ASTER_PROCESSING_INPUT_SERIES_TASK_H
#define ASTER_PROCESSING_INPUT_SERIES_TASK_H

#include "core/task_registry.h"

namespace aster {
class InputSeriesTask final : public Task {
public:
    explicit InputSeriesTask(const QJsonObject&) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
};
} // namespace aster
#endif
