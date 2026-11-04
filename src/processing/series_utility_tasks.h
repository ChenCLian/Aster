#ifndef ASTER_PROCESSING_SERIES_UTILITY_TASKS_H
#define ASTER_PROCESSING_SERIES_UTILITY_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_SERIES_UTILITY(Type)                                                                 \
    class Type final : public Task                                                                 \
    {                                                                                              \
      public:                                                                                      \
        explicit Type(const QJsonObject &p) : m_parameters(p) {}                                   \
        static TaskDescriptor descriptor();                                                        \
        NodeResult execute(const TaskInput &, const ExecutionContext &) override;                  \
                                                                                                   \
      private:                                                                                     \
        QJsonObject m_parameters;                                                                  \
    };
ASTER_SERIES_UTILITY(SelectIntervalTask)
ASTER_SERIES_UTILITY(DemeanTask)
ASTER_SERIES_UTILITY(StandardizeTask)
ASTER_SERIES_UTILITY(SamplingCheckTask)
#undef ASTER_SERIES_UTILITY
} // namespace aster
#endif
