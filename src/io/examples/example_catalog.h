#ifndef ASTER_IO_EXAMPLE_CATALOG_H
#define ASTER_IO_EXAMPLE_CATALOG_H
#include "model/data_types.h"
#include <functional>

namespace aster::examples
{
struct ExampleSeries
{
    SeriesData series;
    QJsonObject provenance;
    bool cancelled = false;
};
QJsonObject description(const QString &id);
ExampleSeries load(const QString &id, const QString &axis,
                   const std::function<bool()> &cancelled = {});
} // namespace aster::examples
#endif
