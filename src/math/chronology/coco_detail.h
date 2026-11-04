#ifndef ASTER_MATH_COCO_DETAIL_H
#define ASTER_MATH_COCO_DETAIL_H
#include "math/chronology/coco_scan.h"
// Internal shared numerical primitives. The descriptive scan keeps streaming its
// targets; MC/sliding-window analysis builds a separately bounded immutable cache.
namespace aster::chronology::coco_detail
{
void check(const std::function<bool()> &);
void normalize(std::vector<double> &);
CocoResult grid(std::size_t, double, const CocoOptions &, const std::function<bool()> &);
std::vector<double> observedPower(const std::vector<double> &, std::size_t, CocoOptions::Detrend);
std::vector<double> targetPower(std::size_t, double, double, std::size_t, const CocoOptions &,
                                const std::function<bool()> &);
double correlation(const std::vector<double> &, const std::vector<double> &, std::size_t);
} // namespace aster::chronology::coco_detail
#endif
