#ifndef ASTER_MATH_STATISTICS_AR1_NUISANCE_H
#define ASTER_MATH_STATISTICS_AR1_NUISANCE_H
#include <cstddef>
#include <vector>

namespace aster::numeric {
// Stationary Gaussian AR1, common mean and marginal variance. This bounds
// total variation between the complete n-dimensional input distributions.
double ar1TotalVariationBound(std::size_t n, double actualRho, double referenceRho);
struct Ar1GridCell {
    double lower = 0, center = 0, upper = 0, variationBound = 0;
};
// Predetermined continuous parameter interval. Does NOT cover rho outside it.
// Cells partition [lower,upper]; endpoint KL bounds cover every interior rho.
std::vector<Ar1GridCell> ar1NuisanceGrid(std::size_t n, double lower, double upper,
                                      double maximumVariation, std::size_t maximumCells = 100000);
// Location/scale-invariant screening statistic on the RAW input, before any
// deterministic preprocessing that the analysis callback may replay.
double centeredLagOne(const std::vector<double>& y);
double empiricalTwoSidedP(const std::vector<double>& sortedReference, double observed);
// Validate once for repeated observations against an immutable reference bank.
class EmpiricalTailReference {
public:
    explicit EmpiricalTailReference(std::vector<double> sortedReference);
    double upperTail(double observed) const;
    double twoSided(double observed) const;
private:
    std::vector<double> m_sorted;
};
struct NuisancePValue {
    double screeningP = 1;
    double analysisP = 1;
};
struct CompositeNullResult {
    double p = 1;
    double largestAnalysisP = 1;
    std::vector<std::size_t> includedModels;
    bool emptyConfidenceSet = false;
};
// min(1, max_{screeningP>beta} analysisP + beta + variationBound).
// Empty confidence set conservatively returns p=1 (not a discovery).
//
// Validity requires: (a) the fixed grid covers the true nuisance parameter;
// (b) each component p is valid under its grid model, with fresh simulations
// independent of the observation; (c) SAME statistic/pipeline for observed
// and simulated data; (d) variationBound bounds the law of the OBSERVATION,
// uniformly over each cell. Reference simulations may be shared across models.
// Independence between screening and analysis p-values is not required.
// Fixed/reused reference banks have an unconditional (bank + observation)
// guarantee, NOT a guarantee conditional on the realised bank or after tuning.
// This is a global composite-null test, not FDR or peak attribution.
CompositeNullResult combineNuisancePValues(const std::vector<NuisancePValue>& models,
                                          double beta, double variationBound);
}
#endif
