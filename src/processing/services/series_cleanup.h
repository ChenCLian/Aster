#ifndef ASTER_PROCESSING_SERVICES_SERIES_CLEANUP_H
#define ASTER_PROCESSING_SERVICES_SERIES_CLEANUP_H
#include "core/task.h"
namespace aster {
struct SeriesCleanupResult {
    SeriesData data;
    qsizetype mergedRows=0;
    bool reordered=false;
    bool cancelled=false;
    ErrorInfo error;
};
// Operates on finite X/Y pairs. Missing or invalid file values must be handled
// by the reader policy before publishing immutable, validated Series data.
SeriesCleanupResult cleanSeries(const SeriesData&,const QString& duplicatePolicy,
                                const CancellationToken& cancellation={});
}
#endif
