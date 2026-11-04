#ifndef ASTER_PROCESSING_FILE_SOURCE_TASK_H
#define ASTER_PROCESSING_FILE_SOURCE_TASK_H
#include "core/task_registry.h"
#include "io/files/series_file_reader.h"
namespace aster {
class FileSourceTask final : public Task {
public:
    explicit FileSourceTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private: SeriesFileOptions m_options;
};
}
#endif
