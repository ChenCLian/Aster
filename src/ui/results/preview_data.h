#ifndef ASTER_UI_PREVIEW_DATA_H
#define ASTER_UI_PREVIEW_DATA_H
#include "core/task.h"
namespace aster::ui
{
struct PreviewLimits
{
    static constexpr qsizetype seriesPoints = 4096, gridSide = 384, tableRows = 2000;
    static constexpr qsizetype tableColumns = 256, tableCells = 200000;
};
// Worker-only display copies. Never used as input to algorithms or database persistence.
NodeResult reducePreviewData(const NodeResult &result, const CancellationToken &cancel);
}
#endif
