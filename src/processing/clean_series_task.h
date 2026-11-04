#ifndef ASTER_PROCESSING_CLEAN_SERIES_TASK_H
#define ASTER_PROCESSING_CLEAN_SERIES_TASK_H
#include "core/task_registry.h"
namespace aster {
class CleanSeriesTask final:public Task {
public:
    explicit CleanSeriesTask(const QJsonObject& p):m_policy(p["duplicate_policy"].toString()){}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&,const ExecutionContext&) override;
private:QString m_policy;
};
}
#endif
