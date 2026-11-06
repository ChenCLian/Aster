// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/clean_series_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "processing/services/series_cleanup.h"
namespace aster {
TaskDescriptor CleanSeriesTask::descriptor()
{
    auto d=task_detail::seriesTransform("Sort and deduplicate",taskCategory(TaskCategory::Cleanup));
    d.parameters={science_detail::text("duplicate_policy","Duplicate X","average",{"average","first","last","error"})};
    return d;
}
NodeResult CleanSeriesTask::execute(const TaskInput& input,const ExecutionContext& context)
{
    const auto* source=dataAs<SeriesData>(input.data);
    if(!source)return NodeResult::failure(makeError(ErrorCode::InputType,{{"expected","Series"}}));
    const auto cleaned=cleanSeries(*source,m_policy,context.cancellation);
    if(cleaned.cancelled)return NodeResult::cancelled();
    if(!cleaned.error.isEmpty())return NodeResult::failure(cleaned.error);
    return NodeResult::success({{"series",makeData(cleaned.data)}},
        {{"input_rows",double(source->x.size())},{"output_rows",double(cleaned.data.x.size())},
         {"merged_rows",double(cleaned.mergedRows)},{"reordered",cleaned.reordered},
         {"duplicate_policy",m_policy},{"resampled",false}});
}
}
