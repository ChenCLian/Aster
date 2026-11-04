#ifndef ASTER_PROCESSING_BASIC_SIGNAL_TASKS_H
#define ASTER_PROCESSING_BASIC_SIGNAL_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_BASIC_SIGNAL_TASK(Type)                                                              \
    class Type final : public Task                                                                 \
    {                                                                                              \
      public:                                                                                      \
        explicit Type(const QJsonObject &parameters) : m_parameters(parameters) {}                 \
        static TaskDescriptor descriptor();                                                        \
        NodeResult execute(const TaskInput &, const ExecutionContext &) override;                  \
                                                                                                   \
      private:                                                                                     \
        QJsonObject m_parameters;                                                                  \
    };
ASTER_BASIC_SIGNAL_TASK(MovingAverageTask)
ASTER_BASIC_SIGNAL_TASK(SavitzkyGolayTask)
ASTER_BASIC_SIGNAL_TASK(FftSpectrumTask)
ASTER_BASIC_SIGNAL_TASK(WelchSpectrumTask)
#undef ASTER_BASIC_SIGNAL_TASK
} // namespace aster
#endif
