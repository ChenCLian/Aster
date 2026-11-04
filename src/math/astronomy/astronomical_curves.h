#ifndef ASTER_MATH_ASTRONOMY_ASTRONOMICAL_CURVES_H
#define ASTER_MATH_ASTRONOMY_ASTRONOMICAL_CURVES_H
#include "math/astronomy/orbital_elements.h"
#include <vector>
namespace aster::astronomy {
// Throws invalid_argument on nonphysical/nonfinite parameters.
double dailyMeanInsolation(const OrbitalElements&, double latitudeRadians,
                          double solarLongitudeRadians, double solarConstant);
double climaticPrecession(const OrbitalElements&);
// Sample standard deviation (N-1), as in MATLAB zscore. Each component is
// standardized over exactly the requested output grid, before weighting.
std::vector<double> etp(const std::vector<OrbitalElements>&,
                        double eccentricityWeight, double obliquityWeight,
                        double precessionWeight);
}
#endif
