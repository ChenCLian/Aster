#include "math/wavelet/cwt_support.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace aster::numeric {
double waveletFourierFactor(const QString& mother, double parameter)
{
    if (!std::isfinite(parameter) || parameter < 1 || parameter > 20)
        throw std::invalid_argument("Wavelet parameter must be within [1, 20]");
    const double pi = std::acos(-1.0);
    if (mother == "morlet") return 4*pi/(parameter + std::sqrt(2 + parameter*parameter));
    if (mother == "paul") return 4*pi/(2*parameter + 1);
    if (mother == "dog") return 2*pi*std::sqrt(2/(2*parameter + 1));
    throw std::invalid_argument("Unknown mother wavelet");
}
WaveletGeometry waveletGeometry(const QString& mother, double parameter, size_t samples, double dj)
{
    if (samples < 2 || !std::isfinite(dj) || dj < .02 || dj > 1)
        throw std::invalid_argument("Invalid sample count or log2 scale step");
    WaveletGeometry g;
    g.fourierFactor = waveletFourierFactor(mother, parameter);
    g.degreesOfFreedom = mother == "dog" ? 1 : 2;
    // Retain the conventional s0=2*dt unless its nominal period is sub-Nyquist.
    // This limits the nominal period; it does not eliminate wavelet band-tail aliasing.
    g.initialScaleSamples = std::max(2.0, 2/g.fourierFactor);
    const double last = std::log2(samples/g.initialScaleSamples)/dj;
    if (last < 0) return g;
    const double rounded = std::round(last);
    const double index = std::abs(last-rounded) <= 16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(last)) ? rounded : last;
    g.count = 1 + static_cast<int>(std::floor(index)); // j=0,...,J, inclusive.
    return g;
}
CenteredSignal centeredUnitSignal(size_t count, const double* values)
{
    if (!values || count < 2) throw std::invalid_argument("At least two samples are required");
    CenteredSignal result;
    result.values.resize(count);
    double mean = 0, correction = 0;
    for (size_t i=0; i<count; ++i) {
        const double difference = values[i]-values[0];
        if (!std::isfinite(difference)) throw std::runtime_error("Centered signal exceeds the floating-point range");
        result.values[i] = difference;
        const double term = difference/double(count)-correction;
        const double next = mean+term;
        correction = (next-mean)-term;
        mean = next;
    }
    for (auto& value : result.values) {
        value -= mean;
        if (!std::isfinite(value)) throw std::runtime_error("Centered signal exceeds the floating-point range");
        result.amplitude = std::max(result.amplitude,std::abs(value));
    }
    if (result.amplitude == 0) return result;
    double squareSum = 0;
    for (auto& value : result.values) {
        value /= result.amplitude;
        squareSum += value*value;
    }
    result.sampleVariance = squareSum/double(count-1);
    return result;
}
Lag1Estimate estimateWaveletLag1(size_t count, const double* values)
{
    if (count < 3) return {};
    const auto centered = centeredUnitSignal(count,values);
    double numerator = 0, denominator = 0;
    for (size_t i=1; i<count; ++i) {
        numerator += centered.values[i]*centered.values[i-1];
        denominator += centered.values[i-1]*centered.values[i-1];
    }
    Lag1Estimate result;
    if (denominator > 0) result.raw = numerator/denominator;
    result.used = std::clamp(result.raw,-.99,.99);
    result.clipped = result.raw != result.used;
    return result;
}
Lag1Estimate estimateAcycleWaveletLag1(size_t count, const double* values)
{
    if (count < 3) return {};
    const auto centered = centeredUnitSignal(count,values);
    long double lag = 0, sum = 0;
    for (size_t i=0; i<count; ++i) {
        sum += static_cast<long double>(centered.values[i])*centered.values[i];
        if (i) lag += static_cast<long double>(centered.values[i])*centered.values[i-1];
    }
    Lag1Estimate result;
    if (sum > 0) result.raw = static_cast<double>((lag/(count-1))/(sum/count));
    // Do not clip a valid Acycle estimate such as 0.995 to an arbitrary 0.99.
    // The task omits the optional significance output if stationarity fails.
    result.used = result.raw;
    return result;
}
WaveletGeometry acycleWaveletGeometry(const QString& mother, double parameter, size_t samples, double dj)
{
    auto result = waveletGeometry(mother,parameter,samples,dj);
    // Acycle method 2 uses MaxScale=.34*N*dt and round(log2(MaxScale/s0)/dj).
    // Retain the nominal-Nyquist guard for unusual mother parameters.
    const double last = std::log2(.34*samples/result.initialScaleSamples)/dj;
    result.count = std::max(0,1+static_cast<int>(std::round(last)));
    return result;
}
}
