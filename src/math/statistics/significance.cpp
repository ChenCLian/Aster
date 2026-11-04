#include "math/statistics/significance.h"
#include "math/wavelet/cwt_support.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <gsl/gsl_cdf.h>

std::vector<double> wave_Significance(size_t nTime, size_t nPeriod,
    const double* power, const double* scale, double variance, double dt,
    double parameter, const QString& mother, double lag1, double level)
{
    if (!power || !scale || nTime == 0 || nPeriod == 0 || nPeriod > std::numeric_limits<size_t>::max()/nTime ||
        !(variance > 0) || !std::isfinite(variance) || !(dt > 0) || !std::isfinite(dt) ||
        !std::isfinite(lag1) || std::abs(lag1) >= 1 || !(level > 0 && level < 1))
        throw std::invalid_argument("Invalid local wavelet significance input");
    const double factor = aster::numeric::waveletFourierFactor(mother,parameter);
    const double dof = mother == "dog" ? 1 : 2;
    const double quantile = gsl_cdf_chisq_Pinv(level,dof)/dof;
    const double pi = std::acos(-1.0);
    std::vector<double> ratio(nPeriod*nTime);
    for (size_t row=0; row<nPeriod; ++row) {
        if (!(scale[row] > 0) || !std::isfinite(scale[row]))
            throw std::invalid_argument("Wavelet scale must be positive and finite");
        const double angle = 2*pi*(dt/scale[row])/factor;
        // Equivalent to 1-2*rho*cos(angle)+rho^2, stable for either sign of rho.
        const double trig = lag1 >= 0 ? std::sin(angle/2) : std::cos(angle/2);
        const double absolute = std::abs(lag1);
        const double denominator = (1-absolute)*(1-absolute)+4*absolute*trig*trig;
        const double threshold = ((1-lag1)*(1+lag1)/denominator)*quantile;
        if (!(threshold > 0) || !std::isfinite(threshold))
            throw std::runtime_error("Wavelet significance threshold exceeds the numeric range");
        for (size_t col=0; col<nTime; ++col) {
            const size_t index = row*nTime+col;
            if (!(power[index] >= 0) || !std::isfinite(power[index]))
                throw std::invalid_argument("Wavelet power must be finite and nonnegative");
            // Do not form 1/(variance*threshold): the reciprocal can overflow.
            ratio[index] = (power[index]/variance)/threshold;
            if (!std::isfinite(ratio[index]))
                throw std::runtime_error("Wavelet significance ratio exceeds the numeric range");
        }
    }
    return ratio;
}
