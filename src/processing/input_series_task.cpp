#include "processing/task_categories.h"
#include "processing/input_series_task.h"
#include "processing/task_helpers.h"
#include <algorithm>
#include <cmath>

namespace aster {
using namespace task_detail;
TaskDescriptor InputSeriesTask::descriptor()
{
    auto d = seriesTransform("Input series", taskCategory(TaskCategory::ExistingData));
    d.inputKind.reset();
    d.requiresSourceBinding = true;
    d.boundDataKind = DataKind::Series;
    d.catalogVisible = false;
    return d;
}
NodeResult InputSeriesTask::execute(const TaskInput&, const ExecutionContext& context)
{
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    if (!dataAs<SeriesData>(context.boundSource)) return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}, {"binding", "bound_source"}}));
    return NodeResult::success({{"series", context.boundSource}});
}

} // namespace aster
