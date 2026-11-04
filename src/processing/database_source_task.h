#ifndef ASTER_DATABASE_SOURCE_TASK_H
#define ASTER_DATABASE_SOURCE_TASK_H
#include "processing/task_categories.h"
#include "core/task_registry.h"
#include "io/database/result_repository.h"
namespace aster
{
template <DataKind Kind> class DatabaseSourceTask final : public Task
{
  public:
    explicit DatabaseSourceTask(const QJsonObject &) {}
    static TaskDescriptor descriptor()
    {
        TaskDescriptor d;
        d.label = "Database " + dataKindName(Kind);
        d.category = taskCategory(TaskCategory::StoredData);
        d.requiresSourceBinding = true;
        d.catalogVisible = false;
        d.outputs = {{"data", "Database output", Kind}};
        d.defaultOutput = "data";
        return d;
    }
    NodeResult execute(const TaskInput &, const ExecutionContext &c) override
    {
        if (c.cancellation.isCancelled())
            return NodeResult::cancelled();
        auto ref = OutputReference::fromJson(c.sourceMetadata["database_source"].toObject());
        if (!ref.valid())
            return NodeResult::failure(makeError(ErrorCode::DatabaseReference));
        ResultRepository repo;
        auto error = repo.open(ref.databasePath, false);
        if (!error.isEmpty())
            return NodeResult::failure(makeError(ErrorCode::DatabaseOpen, {{"path", ref.databasePath}}, error));
        auto out = repo.loadOutput(ref);
        if (!out.ok())
            return NodeResult::failure(makeError(ErrorCode::DatabaseRead, {{"path", ref.databasePath}, {"record_id", double(ref.recordId)}, {"source_node_id", ref.nodeId.toString(QUuid::WithoutBraces)}, {"output_port", ref.port}}, out.error));
        if (c.cancellation.isCancelled())
            return NodeResult::cancelled();
        if (dataKind(*out.data) != Kind)
            return NodeResult::failure(makeError(ErrorCode::DatabaseType, {{"expected", dataKindName(Kind)}, {"actual_type", dataKindName(dataKind(*out.data))}}));
        return NodeResult::success({{"data", out.data}}, {{"database_source", ref.toJson()}});
    }
};
using DatabaseSeriesTask = DatabaseSourceTask<DataKind::Series>;
using DatabaseSpectrumTask = DatabaseSourceTask<DataKind::Spectrum>;
using DatabaseGridTask = DatabaseSourceTask<DataKind::Grid>;
using DatabaseTableTask = DatabaseSourceTask<DataKind::Table>;
} // namespace aster
#endif
