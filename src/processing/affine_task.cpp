#include "processing/task_categories.h"
#include "processing/affine_task.h"
#include "processing/task_helpers.h"
#include <algorithm>
#include <cmath>

namespace aster {
using namespace task_detail;
TaskDescriptor AffineTask::descriptor()
{
    auto d = seriesTransform("Scale and offset", taskCategory(TaskCategory::Amplitude));
    d.parameters = {number("factor", "Scale", 1.0, -1e100, 1e100),
                    number("offset", "Offset", 0.0, -1e100, 1e100)};
    return d;
}
AffineTask::AffineTask(const QJsonObject& p)
    : m_factor(p.value("factor").toDouble()), m_offset(p.value("offset").toDouble()) {}
NodeResult AffineTask::execute(const TaskInput& input, const ExecutionContext& context)
{
    const auto* source = dataAs<SeriesData>(input.data);
    if (!source) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    SeriesData result = *source;
    for (qsizetype i = 0; i < result.y.size(); ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        result.y[i] = source->y[i] * m_factor + m_offset;
    }
    return NodeResult::success({{"series", makeData(std::move(result))}}, {{"factor", m_factor}, {"offset", m_offset}});
}

} // namespace aster
