#ifndef ASTER_MATH_COCO_SCAN_H
#define ASTER_MATH_COCO_SCAN_H
#include <cstddef>
#include <exception>
#include <functional>
#include <vector>
namespace aster::chronology
{
// Descriptive, fixed-target COCO. No significance test or automatic age model.
struct CocoOptions
{
    std::vector<double> periodsKa{405, 125, 95, 41, 24, 22, 19};
    std::vector<double> relativePower{1, 1, 1, .64, .36, .36, .36};
    double minimumRate = .5, maximumRate = 20, rateStep = .1; // cm/ka
    double maximumFrequency = .06;                            // cycles/ka
    int paddingFactor = 2; // power of two FFT >= factor*N; no extra resolution
    enum class Detrend
    {
        None,
        Mean,
        Linear
    } detrend = Detrend::Mean;
};
namespace CocoLimits
{
inline constexpr std::size_t maxSamples = 131072, maxFft = 262144, maxRates = 2000, maxTargets = 16;
inline constexpr long double maxWork = 2000000000.L;
} // namespace CocoLimits
struct CocoCancelled final : std::exception
{
};
enum class CocoStatus
{
    Eligible = 0,
    Nyquist = 1,
    Resolution = 2,
    NoVariance = 3
};
struct CocoRow
{
    double rate = 0, correlation = 0, rayleigh = 0, nyquist = 0;
    std::size_t resolvedTargets = 0, bins = 0;
    CocoStatus status = CocoStatus::Eligible; // correlation valid ONLY if Eligible
};
struct CocoResult
{
    std::vector<CocoRow> rows;
    std::size_t bestRow = 0, fftSize = 0;
    // Same frequency grid at best eligible rate; independently max-normalized.
    std::vector<double> frequency, observedPower, targetPower;
};
// Input amplitudes and positive uniform depth interval IN METRES. Order/spacing
// of the original depth axis are validated by the task adapter. Target is the
// exact phase-averaged periodogram of independent harmonic components, with the
// same N, FFT and detrending as the data. All targets AND the full requested band
// must be resolvable. Excluded rates remain in rows with an explicit status.
CocoResult cocoScan(const std::vector<double> &y, double depthStepMetres,
                    const CocoOptions &options = {}, const std::function<bool()> &cancelled = {},
                    const std::function<void(int)> &progress = {});
} // namespace aster::chronology
#endif
