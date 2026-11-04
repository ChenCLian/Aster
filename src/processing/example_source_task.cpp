#include "processing/example_source_task.h"
#include "processing/task_categories.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "io/examples/example_catalog.h"
#include <stdexcept>

namespace aster
{
ExampleSourceTask::ExampleSourceTask(QString id, const QJsonObject &parameters)
    : m_id(std::move(id)), m_axis(parameters["example_axis"].toString("age"))
{
}

TaskDescriptor ExampleSourceTask::describe(const QString &id)
{
    auto d = task_detail::seriesTransform(examples::description(id)["label"].toString(),
                                          taskCategory(TaskCategory::OdpExamples));
    d.inputKind.reset();
    auto axis = science_detail::text("example_axis", "Horizontal axis", "age", {"age", "depth"});
    axis.help = "Use the published age model or composite depth; values are not resampled.";
    d.parameters = {axis};
    return d;
}
NodeResult ExampleSourceTask::execute(const TaskInput &, const ExecutionContext &context)
{
    try
    {
        auto loaded =
            examples::load(m_id, m_axis, [&] { return context.cancellation.isCancelled(); });
        if (loaded.cancelled || context.cancellation.isCancelled())
            return NodeResult::cancelled();
        auto result = NodeResult::success({{"series", makeData(std::move(loaded.series))}},
                                          {{"example_source", loaded.provenance}});
        if (!loaded.provenance["uniform_spacing"].toBool())
            result.warnings << "warning.example_spacing";
        if (loaded.provenance["omitted_missing_pairs"].toInt())
            result.warnings << "warning.example_missing";
        if (m_axis == "age")
            result.warnings << "warning.example_chronology";
        return result;
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(
            ErrorInfo{"EXAMPLE_DATA", {{"example", m_id}}, QString::fromUtf8(e.what())});
    }
}
} // namespace aster
