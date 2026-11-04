#include "processing/task_categories.h"
#include "processing/file_source_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
namespace aster
{
TaskDescriptor FileSourceTask::descriptor()
{
    auto d = task_detail::seriesTransform("Read text/CSV", taskCategory(TaskCategory::Files));
    d.inputKind.reset();
    d.requiresSourceBinding = true;
    d.parameters = {
        science_detail::text("delimiter", "Delimiter", "auto",
                             {"auto", "comma", "tab", "whitespace"}),
        science_detail::boolean("header", "First data line is header", true),
        task_detail::number("x_column", "X column (zero-based)", 0, 0, 10000,
                            ParameterKind::Integer),
        task_detail::number("y_column", "Y column (zero-based)", 1, 0, 10000,
                            ParameterKind::Integer),
        science_detail::boolean("skip_invalid_rows", "Skip invalid numeric rows", false)};
    return d;
}
FileSourceTask::FileSourceTask(const QJsonObject &p)
{
    m_options.delimiter = p["delimiter"].toString();
    m_options.header = p["header"].toBool();
    m_options.xColumn = p["x_column"].toInt();
    m_options.yColumn = p["y_column"].toInt();
    m_options.skipInvalidRows = p["skip_invalid_rows"].toBool();
}
NodeResult FileSourceTask::execute(const TaskInput &, const ExecutionContext &context)
{
    if (context.cancellation.isCancelled())
        return NodeResult::cancelled();
    const auto value = context.sourceMetadata.value("path");
    if (!value.isString() || value.toString().isEmpty())
        return NodeResult::failure(makeError(ErrorCode::SourceMissing));
    auto options = m_options;
    if (context.sourceMetadata.contains("file_options"))
    {
        if (!context.sourceMetadata["file_options"].isObject())
            return NodeResult::failure(makeError(ErrorCode::FileOptions));
        const auto error =
            applyFileOptions(context.sourceMetadata["file_options"].toObject(), options);
        if (!error.isEmpty())
            return NodeResult::failure(makeError(ErrorCode::FileOptions, {}, error));
    }
    const auto read = readSeriesFile(value.toString(), options, context.cancellation);
    if (context.cancellation.isCancelled())
        return NodeResult::cancelled();
    if (!read.ok())
    {
        auto result = NodeResult::failure(read.error);
        result.report = read.report;
        return result;
    }
    auto result = NodeResult::success({{"series", read.data}}, read.report);
    result.warnings = read.warnings;
    return result;
}
} // namespace aster
