#ifndef ASTER_MATH_WAVELET_CWT_SUPPORT_H
#define ASTER_MATH_WAVELET_CWT_SUPPORT_H
#include <QString>
#include <vector>
#include <cstddef>

namespace aster::numeric {
struct WaveletGeometry {
    double fourierFactor = 0;
    double degreesOfFreedom = 0;
    double initialScaleSamples = 0;
    int count = 0;
};
// Geometry is expressed in samples, independent of the user's time unit.
WaveletGeometry waveletGeometry(const QString& mother, double parameter, size_t samples, double dj);
double waveletFourierFactor(const QString& mother, double parameter);
struct CenteredSignal {
    std::vector<double> values;
    double amplitude = 0;
    double sampleVariance = 0; // Variance of values, with N-1 denominator.
};
// Subtract an anchor before averaging to preserve variation about a large offset.
CenteredSignal centeredUnitSignal(size_t count, const double* values);
struct Lag1Estimate {
    double raw = 0;
    double used = 0;
    bool clipped = false;
};
// Conditional least-squares AR(1), after removal of the sample mean.
Lag1Estimate estimateWaveletLag1(size_t count, const double* values);
// Acycle's Grinsted CWT branch: unbiased lag-one covariance / population variance.
Lag1Estimate estimateAcycleWaveletLag1(size_t count, const double* values);
WaveletGeometry acycleWaveletGeometry(const QString& mother, double parameter, size_t samples, double dj);
}
#endif
