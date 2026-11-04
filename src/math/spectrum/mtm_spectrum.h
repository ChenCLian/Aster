#ifndef ASTER_MATH_SPECTRUM_MTM_SPECTRUM_H
#define ASTER_MATH_SPECTRUM_MTM_SPECTRUM_H
#include <vector>
#include <cstddef>
#include <functional>
#include <memory>

namespace aster::numeric {
enum class MtmConvergenceDomain { LegacyOneSided, FullSpectrum };
struct MtmSpectrum {
    std::vector<double> frequency, power;
    std::vector<double> degreesOfFreedom, densityScale;
    int iterations = 0;
    bool converged = true;
};
// Immutable, input-independent tapers for repeated simulations with identical
// N/NW/K. No global cache and no mutable numerical workspace shared by threads.
class MtmTaperPlan {
public:
    size_t samples() const { return m_n; }
    double bandwidth() const { return m_nw; }
    int count() const { return m_k; }
    const std::vector<double>& tapers() const { return m_tapers; }
    const std::vector<double>& concentration() const { return m_ratios; }
private:
    MtmTaperPlan(size_t n, double nw, int k):m_n(n),m_nw(nw),m_k(k){}
    size_t m_n; double m_nw; int m_k;
    std::vector<double> m_tapers, m_ratios;
    friend std::shared_ptr<const MtmTaperPlan> makeMtmTaperPlan(
        size_t, double, int, const std::function<bool()>&);
};
std::shared_ptr<const MtmTaperPlan> makeMtmTaperPlan(
    size_t n, double nw, int tapers, const std::function<bool()>& cancelled = {});
// Symmetric, unit-energy DPSS; adaptive Thomson weighting; one-sided PSD.
// Weights are computed in the two-sided convention before one-sided conversion.
// Effective degrees of freedom are approximate; real endpoints have half the DOF.
MtmSpectrum mtmSpectrum(const std::vector<double>& y, double sampleRate, double nw,
                        int tapers, double fmax, const std::function<bool()>& cancelled = {},
                        size_t fftLength = 0,
                        MtmConvergenceDomain convergenceDomain = MtmConvergenceDomain::LegacyOneSided,
                        std::shared_ptr<const MtmTaperPlan> prepared = {});
// fftLength==0 preserves the legacy padded numerical API. The workflow task
// passes N explicitly, matching Acycle's default, and supports explicit padding.
// The formal MTM task uses FullSpectrum convergence. LegacyOneSided preserves
// existing direct numerical callers and the hidden experimental simulation task.
} // namespace aster::numeric
#endif
