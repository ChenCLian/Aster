#include "math/statistics/significance.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
double rmse(const std::vector<double>& y, const std::vector<double>& fit)
{
    long double sum = 0;
    for (size_t i = 0; i < y.size(); ++i) { const auto d = y[i]-fit[i]; sum += static_cast<long double>(d)*d; }
    return std::sqrt(static_cast<double>(sum / std::max(size_t(1),y.size()-1)));
}
// A scaled, orthogonal polynomial basis avoids normal equations in raw frequency
// units. On the uniform frequency grid, index and frequency yield the same fit.
std::vector<double> quadraticTail(const std::vector<double>& y, size_t start)
{
    const auto n = y.size()-start;
    std::vector<double> x(n), z(n), result(n);
    long double x2sum = 0;
    for (size_t i = 0; i < n; ++i) { x[i] = -1+2.0*i/(n-1); x2sum += x[i]*x[i]; }
    const auto x2mean = static_cast<double>(x2sum/n);
    long double ysum=0, xy=0, zy=0, zz=0;
    for (size_t i = 0; i < n; ++i) {
        z[i] = x[i]*x[i]-x2mean;
        ysum += y[start+i]; xy += x[i]*y[start+i]; zy += z[i]*y[start+i]; zz += z[i]*z[i];
    }
    const auto a=static_cast<double>(ysum/n), b=static_cast<double>(xy/x2sum), c=static_cast<double>(zy/zz);
    for (size_t i = 0; i < n; ++i) result[i] = a+b*x[i]+c*z[i];
    return result;
}
}

std::vector<double> specswa(const std::vector<double>& frequency, const std::vector<double>& logPower,
                           SwaDiagnostics* diagnostics)
{
    if (diagnostics) *diagnostics = {};
    const auto n = logPower.size();
    if (frequency.size() != n) throw std::invalid_argument("SWA axis length mismatch");
    if (!n) return {};
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(frequency[i]) || !std::isfinite(logPower[i]) || frequency[i] < 0 ||
            (i && !(frequency[i] > frequency[i-1])))
            throw std::invalid_argument("SWA requires finite log power and increasing nonnegative frequencies");
    if (n > 2) {
        const auto dx = (static_cast<long double>(frequency.back())-frequency.front())/(n-1);
        for (size_t i = 1; i < n; ++i)
            if (std::abs((static_cast<long double>(frequency[i])-frequency[i-1])-dx) >
                1e-6L*dx+16*std::numeric_limits<double>::epsilon()*std::abs(frequency[i]))
                throw std::invalid_argument("SWA requires uniformly spaced frequency bins");
    }
    const auto offset = *std::max_element(logPower.begin(), logPower.end());
    std::vector<double> y(n), bestFit(n);
    for (size_t i = 0; i < n; ++i) y[i] = logPower[i]-offset;
    if (n < 33) {
        // Two extrapolated slopes need three complete windows (minimum width 11).
        // Retain an explicitly reported short-spectrum fallback for compatibility.
        const auto mean = std::accumulate(y.begin(),y.end(),0.0)/n;
        std::fill(bestFit.begin(),bestFit.end(),mean);
        if (diagnostics) diagnostics->shortFallback = true;
    } else {
        size_t bestReversals = n, finish = 0;
        int bestWidth = 0; double bestError = std::numeric_limits<double>::infinity(), lastMean = 0;
        for (int width = 11; width <= 99 && 3*size_t(width) <= n; width += 2) {
            const size_t blocks = n/width, half = width/2;
            std::vector<double> means(blocks);
            for (size_t b = 0; b < blocks; ++b)
                means[b] = std::accumulate(y.begin()+b*width,y.begin()+(b+1)*width,0.0)/width;
            const size_t last = half+(blocks-1)*width;
            for (int variant = 0; variant < 2; ++variant) {
                std::vector<double> fit(n);
                const auto d0=(means[1]-means[0])/width, d1=(means[2]-means[1])/width;
                const auto de=(means[blocks-1]-means[blocks-2])/width, dp=(means[blocks-2]-means[blocks-3])/width;
                const auto lowSlope=variant ? 2*d0-d1 : d0, highSlope=variant ? 2*de-dp : de;
                for (size_t i = 0; i <= half; ++i) fit[i] = means.front()+lowSlope*(static_cast<double>(i)-half);
                for (size_t b = 0; b+1 < blocks; ++b)
                    for (int j = 1; j <= width; ++j)
                        fit[half+b*width+j] = means[b]+(means[b+1]-means[b])*(double(j)/width);
                for (size_t i = last+1; i < n; ++i) fit[i] = means.back()+highSlope*(i-last);
                size_t reversals = 0;
                for (size_t i = 1; i < n; ++i) if (fit[i] >= fit[i-1]) ++reversals;
                const auto error = rmse(y,fit);
                if (reversals < bestReversals || (reversals == bestReversals && error < bestError)) {
                    bestFit=std::move(fit); bestReversals=reversals; bestError=error;
                    bestWidth=width; finish=last; lastMean=means.back();
                }
            }
        }
        auto flatTail=bestFit;
        std::fill(flatTail.begin()+finish,flatTail.end(),lastMean);
        auto error=rmse(y,flatTail);
        if (error < bestError) { bestFit=std::move(flatTail); bestError=error; }
        const size_t start = finish-bestWidth/2;
        auto polynomial=quadraticTail(y,start), quadratic=bestFit;
        std::copy(polynomial.begin(),polynomial.end(),quadratic.begin()+start);
        error=rmse(y,quadratic);
        if (error < bestError) { bestFit=std::move(quadratic); bestError=error; }
        // Compare all passes to the same pre-smoothing RMSE, with the same divisor.
        // Keep the last accepted fit; do not accumulate a 1% allowance each pass.
        const auto allowed = bestError*1.01;
        int accepted = 0;
        std::vector<double> smooth(n);
        for (int iteration = 0; iteration < 500; ++iteration) {
            smooth.front()=.5*bestFit.front()+.5*bestFit[1];
            smooth.back()=.5*bestFit[n-2]+.5*bestFit.back();
            for (size_t i=1;i+1<n;++i) smooth[i]=.25*bestFit[i-1]+.5*bestFit[i]+.25*bestFit[i+1];
            if (!(rmse(y,smooth) < allowed)) break;
            ++accepted;
            if (smooth == bestFit) break;
            bestFit.swap(smooth);
        }
        if (diagnostics) { diagnostics->window=bestWidth; diagnostics->smoothingPasses=accepted; }
    }
    for (auto& v : bestFit) {
        v=std::pow(10.0,v+offset);
        if (!std::isfinite(v) || v <= 0) throw std::runtime_error("SWA background is not representable");
    }
    return bestFit;
}
