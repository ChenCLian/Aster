#ifndef ASTER_PROCESSING_COCO_TASK_H
#define ASTER_PROCESSING_COCO_TASK_H
#include "core/task_registry.h"
namespace aster
{
class CocoTask final : public Task
{
  public:
    explicit CocoTask(const QJsonObject &p) : m_parameters(p) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput &, const ExecutionContext &) override;

  private:
    QJsonObject m_parameters;
};
} // namespace aster
#endif
