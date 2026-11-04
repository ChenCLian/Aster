#ifndef ASTER_MATH_STATISTICS_SIGNIFICANCE_H
#define ASTER_MATH_STATISTICS_SIGNIFICANCE_H

#include <vector>
#include <array>
#include <cstddef>
#include <QString>
// Approximate spectral significance thresholds, not confidence intervals on PSD.
// Empty options retain the conventional fixed 2K degrees of freedom.
struct SpectrumInference {
    std::vector<double> degreesOfFreedom;
    // One-sided density conversion (1 at DC/Nyquist, 2 elsewhere). Empty: all 1.
    std::vector<double> densityScale;
    // Nonempty mask defines the FDR family. The Acycle-compatible task leaves
    // it empty and includes DC/Nyquist; some diagnostic callers exclude them.
    std::vector<bool> tested;
    // Acycle: keep even median windows; reject undersupported SWA fits;
    // bandwidth-H(floor(NW)) FDR with the last rejected observed p as curve.
    // This is the Hopkins/Weedon convention, not general-dependence BY.
    bool acycle = false;
};
struct SwaDiagnostics {
    int window = 0;
    int smoothingPasses = 0;
    bool shortFallback = false;
};

struct ChiConf {
    std::vector<double> chi90;
    std::vector<double> chi95;
    std::vector<double> chi99;
    std::vector<double> chi999;
};

struct FDRConf {
    std::vector<double> clfdr5;
    std::vector<double> clfdr1;
    std::vector<double> clfdr01;
    std::vector<double> clfdr001;
};

struct RedNoiseConf {
    std::vector<double> pxxsmooth;
    std::vector<double> theored;
    ChiConf chiConf;
    double scale = 0;
    double rho = 0;
};

struct FDRSpectralAnalysisResult {
    std::vector<double> swa;
    FDRConf fdrConf;
    ChiConf chiConf;
    size_t testCount = 0;
    double dependenceCorrection = 1;
    SwaDiagnostics diagnostics;
};

RedNoiseConf computeRedNoiseConfidence(const double* pxx,
                                       const double* ft,
                                       double smoothwin,
                                       double fmax,
                                       size_t N, double nw,
                                       bool logfit, const SpectrumInference& inference = {});

// Same fitted background used by FDR_TEST, without unused confidence/FDR curves.
// Useful when a simulation statistic needs only power/background ratios.
std::vector<double> estimateSwaBackground(const std::vector<double>& power,
    const std::vector<double>& frequency, const SpectrumInference& inference = {},
    SwaDiagnostics* diagnostics = nullptr);

FDRSpectralAnalysisResult FDR_TEST(const std::vector<double>& pow,
                                   const std::vector<double>& freq,
                                   double nw, const SpectrumInference& inference = {});

std::vector<double> wave_Significance(size_t N_time, size_t N_peridd,
                                      const double* pow, const double* scale,
                                      double variance, double dt,
                                      double parma, const QString& mother,
                                      double lag1 = 0.0, double siglvl = 0.95);

// BY step-up cutoffs for q={.05,.01,.001,.0001}; NaN means no rejection.
// Uses the full harmonic(m) dependence correction, distinct from Acycle's
// bandwidth H(floor(NW)) convention.
std::array<double, 4> spectralFdrCutoffs(const std::vector<double>& pValues);
std::array<double, 4> acycleFdrCutoffs(const std::vector<double>& pValues, double nw);
std::vector<double> specswa(const std::vector<double>& freq, const std::vector<double>& xlogp,
                           SwaDiagnostics* diagnostics = nullptr);

#endif // ASTER_MATH_STATISTICS_SIGNIFICANCE_H
