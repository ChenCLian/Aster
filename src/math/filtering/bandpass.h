#ifndef ASTER_MATH_FILTERING_BANDPASS_H
#define ASTER_MATH_FILTERING_BANDPASS_H
#include <functional>
#include <stdexcept>
#include <vector>
#include <cstddef>
namespace aster::filtering {
enum class Shape { Gaussian, Taner };
inline constexpr std::size_t maximumSamples = 1000000;
inline constexpr std::size_t maximumFftLength = 4194304;
struct Cancelled : std::runtime_error { Cancelled() : std::runtime_error("Filtering cancelled") {} };
using Cancel = std::function<bool()>;
struct FilterResult {
    std::vector<double> filtered, frequency, gain;
    std::size_t fftLength = 0;
    double removedMean = 0;
};
struct AnalyticResult { std::vector<double> quadrature, envelope; double removedMean = 0; };
// Half-power band edges. Taner exponent uses c=10^q (q is NOT dB/octave).
// Gaussian: 10*nextpow2(N) trailing zero padding; Taner: N-point periodic DFT.
double responseGain(Shape, double frequency, double lower, double upper, double q = 12);
std::size_t fftLength(Shape, std::size_t samples);
FilterResult bandpass(const std::vector<double>& values, double spacing, Shape,
                      double lower, double upper, double q = 12, const Cancel& cancel = {});
// Imaginary part of the finite-block analytic signal, using the same N-point
// DFT as MATLAB hilbert. DC/even Nyquist are retained in the real part only.
AnalyticResult hilbert(const std::vector<double>& values, bool removeMean = false,
                       const Cancel& cancel = {});
}
#endif
