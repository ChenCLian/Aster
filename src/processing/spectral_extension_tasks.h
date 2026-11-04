#ifndef ASTER_PROCESSING_SPECTRAL_EXTENSION_TASKS_H
#define ASTER_PROCESSING_SPECTRAL_EXTENSION_TASKS_H
#include "core/task_registry.h"
namespace aster
{
#define ASTER_SPECTRAL_EXTENSION_TASK(Type)                                                        \
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
ASTER_SPECTRAL_EXTENSION_TASK(LombScargleTask)
ASTER_SPECTRAL_EXTENSION_TASK(ButterworthLowpassTask)
ASTER_SPECTRAL_EXTENSION_TASK(ButterworthHighpassTask)
#undef ASTER_SPECTRAL_EXTENSION_TASK
} // namespace aster
#endif
