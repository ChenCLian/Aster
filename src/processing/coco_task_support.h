#ifndef ASTER_PROCESSING_COCO_TASK_SUPPORT_H
#define ASTER_PROCESSING_COCO_TASK_SUPPORT_H
#include "core/task.h"
#include "math/chronology/coco_scan.h"
namespace aster::coco_task_detail
{
using Operation =
    std::function<NodeResult(const SeriesData &, double, const chronology::CocoOptions &)>;
chronology::CocoOptions options(const QJsonObject &);
NodeResult outputs(const SeriesData &, const QJsonObject &, const chronology::CocoOptions &,
                   const chronology::CocoResult &, double dxMetres);
NodeResult run(const TaskInput &, const ExecutionContext &, const QJsonObject &,
               std::size_t maxSamples, const Operation &);
} // namespace aster::coco_task_detail
#endif
