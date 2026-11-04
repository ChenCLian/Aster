#ifndef ASTER_PROCESSING_EXAMPLE_SOURCE_TASK_H
#define ASTER_PROCESSING_EXAMPLE_SOURCE_TASK_H
#include "core/task_registry.h"

#define ODP_EXAMPLE_METHODS(X)                                                                     \
    X(odp846_d18o, Odp846OxygenTask)                                                               \
    X(odp846_d13c, Odp846CarbonTask)                                                               \
    X(odp1143_d18o, Odp1143OxygenTask)                                                             \
    X(odp926_d13c, Odp926CarbonTask)                                                               \
    X(odp926_lightness, Odp926LightnessTask)

namespace aster
{
class ExampleSourceTask : public Task
{
  public:
    ExampleSourceTask(QString id, const QJsonObject &parameters);
    static TaskDescriptor describe(const QString &id);
    NodeResult execute(const TaskInput &, const ExecutionContext &) override;

  private:
    QString m_id, m_axis;
};
#define DECLARE_ODP_TASK(id, type)                                                                 \
    class type final : public ExampleSourceTask                                                    \
    {                                                                                              \
      public:                                                                                      \
        explicit type(const QJsonObject &parameters) : ExampleSourceTask(#id, parameters) {}       \
        static TaskDescriptor descriptor()                                                         \
        {                                                                                          \
            return describe(#id);                                                                  \
        }                                                                                          \
    };
ODP_EXAMPLE_METHODS(DECLARE_ODP_TASK)
#undef DECLARE_ODP_TASK
} // namespace aster
#endif
