#ifndef ASTER_MATH_SPECTRUM_SPECTRUM_PEAKS_H
#define ASTER_MATH_SPECTRUM_SPECTRUM_PEAKS_H

#include <limits>
#include <vector>

namespace spectrum {

struct PeakOptions {
    double minProminence = 0.0; // Same units as y; 0 disables this threshold.
    int minDistance = 1;       // Minimum distance between returned indices, in bins.
    double minWidth = 0.0;     // Width at half prominence, in bins; 0 disables filter.
    double minHeight = -std::numeric_limits<double>::infinity();
    int prominenceRadius = 0; // 0: full array; >0: search up to this many bins each side.
    double relativeProminence = 0.0; // Fraction of full y range [0,1]; 0 disables.
};

struct Peak {
    int index;                // Plateau midpoint, rounded toward the left.
    double height;
    double prominence;
    double width;             // At height - prominence / 2; not generally raw FWHM.
};

// Returns peaks ordered by index. Input must be finite, in frequency order.
// No smoothing, normalization, log conversion, or endpoint detection is implicit.
// Flat tops use exact equality. NaN/Inf or invalid options throw invalid_argument.
// For dB input, replace -Inf from log(0) with a meaningful finite floor first.
// Distance suppression is LAST and favors prominence, then height, then left index.
// This ordering intentionally differs from scipy.signal.find_peaks.
// Defaults return interior local maxima above floating-point roundoff. Relative
// prominence is an amplitude filter, NOT a significance or noise-model test.
// Cost: O(N + P*W + P*log(P)), W <= N. Full-array worst case: O(N^2).
// Memory: O(P). A short radius speeds scans but changes prominence and width.
std::vector<Peak> findSpectrumPeaks(
    int n, const double* y, const PeakOptions& opt = PeakOptions());

std::vector<int> findSpectrumPeakIndices(
    int n, const double* y, const PeakOptions& opt = PeakOptions());

// Includes an amplitude-scaled roundoff floor. Also validates input/options.
double spectrumProminenceThreshold(int n, const double* y, const PeakOptions& opt);

} // namespace spectrum

#endif // ASTER_MATH_SPECTRUM_SPECTRUM_PEAKS_H
