#ifndef ASTER_GRID_CONTOURS_H
#define ASTER_GRID_CONTOURS_H
#include "model/data_types.h"
#include <QLineF>
namespace aster::ui
{
enum class ContourState { Ready, NoBoundary, Invalid, TooDense };
struct GridContours
{
    QVector<QLineF> segments;
    ContourState state = ContourState::NoBoundary;
};
// Marching squares, with an asymptotic saddle decider. Coordinates remain physical.
// An exceeded budget returns NO partial outline, never a misleading truncated boundary.
GridContours gridContours(const GridData &grid, double level = 1., int maxSegments = 100000);
} // namespace aster::ui
#endif
