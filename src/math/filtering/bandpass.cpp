// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/filtering/bandpass.h"
#include "math/backend/numeric_backend.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
namespace aster::filtering {
namespace {
void check(const Cancel& cancel) { if (cancel && cancel()) throw Cancelled(); }
void validateBand(Shape shape, double low, double high, double q) {
    if (!std::isfinite(low) || !std::isfinite(high) || !(low > 0 && high > low) ||
        !((high-low)/2 > 0) ||
        (shape == Shape::Taner && (!std::isfinite(q) || q <= 0 || q > 20)))
        throw std::invalid_argument("Require 0 < lower < upper and Taner exponent in (0,20]");
}
double gainAt(Shape shape, double f, double low, double high, double q) {
    const double a = std::log(2.) / 2;
    const double exponent = shape == Shape::Gaussian ? 2 : std::log2(1 + q*std::log(10.)/a);
    const double offset = std::abs((f-(low+(high-low)/2))/((high-low)/2));
    return std::exp(-a*std::pow(offset, exponent)); // overflow of power => gain zero
}
struct Working { std::vector<double> y; double scale=0, mean=0; };
Working normalized(const std::vector<double>& values, bool demean, const Cancel& cancel) {
    if (values.size() < 2) throw std::invalid_argument("At least two samples required");
    if (values.size() > maximumSamples) throw std::length_error("Filter input exceeds sample budget");
    Working w;
    for (std::size_t i=0;i<values.size();++i) {
        if ((i&4095)==0) check(cancel);
        if (!std::isfinite(values[i])) throw std::invalid_argument("Input must be finite");
        w.scale=std::max(w.scale,std::abs(values[i]));
    }
    w.y.resize(values.size());
    if (w.scale==0) return w;
    long double mean=0;
    if (demean) for (double v:values) mean += (v/w.scale)/static_cast<long double>(values.size());
    w.mean=static_cast<double>(mean)*w.scale;
    for (std::size_t i=0;i<values.size();++i) w.y[i]=values[i]/w.scale-static_cast<double>(mean);
    return w;
}
double restored(double value, double scale) {
    const double v=value*scale;
    if (!std::isfinite(v)) throw std::overflow_error("Filter output exceeds double precision range");
    return v;
}
}
double responseGain(Shape shape, double f, double low, double high, double q) {
    validateBand(shape,low,high,q);
    if (!std::isfinite(f) || f<0) throw std::invalid_argument("Frequency must be finite and nonnegative");
    return gainAt(shape,f,low,high,q);
}
std::size_t fftLength(Shape shape, std::size_t n) {
    if (n<2) throw std::invalid_argument("At least two samples required");
    if(n>maximumSamples) throw std::length_error("Filter input exceeds sample budget");
    std::size_t length=n;
    if(shape==Shape::Gaussian) { length=1; while(length<n)length*=2; length*=10; }
    if(length>maximumFftLength) throw std::length_error("Filter FFT exceeds workspace budget");
    return length;
}
FilterResult bandpass(const std::vector<double>& values, double spacing, Shape shape,
                      double low, double high, double q, const Cancel& cancel) {
    check(cancel); validateBand(shape,low,high,q);
    if(!std::isfinite(spacing) || spacing<=0 || !std::isfinite(.5/spacing) || high>=.5/spacing)
        throw std::invalid_argument("Upper frequency must be below Nyquist (0.5 / sample interval)");
    FilterResult r; r.fftLength=fftLength(shape,values.size());
    const double df=(1./spacing)/double(r.fftLength);
    if(!std::isfinite(df) || df<=0) throw std::overflow_error("Unrepresentable frequency grid");
    auto w=normalized(values,true,cancel);r.removedMean=w.mean;
    w.y.resize(r.fftLength,0);
    check(cancel);
    auto coefficients=numeric::backend::realFft(std::move(w.y),r.fftLength);
    check(cancel);
    r.frequency.resize(coefficients.size());r.gain.resize(coefficients.size());
    for(std::size_t k=0;k<coefficients.size();++k) {
        if((k&4095)==0)check(cancel);
        r.frequency[k]=double(k)*df;
        r.gain[k]=gainAt(shape,r.frequency[k],low,high,q);
        coefficients[k]*=r.gain[k];
    }
    auto filtered=numeric::backend::inverseRealFft(std::move(coefficients),r.fftLength);
    filtered.resize(values.size());
    for(std::size_t i=0;i<filtered.size();++i) {
        if((i&4095)==0)check(cancel);
        filtered[i]=restored(filtered[i],w.scale);
    }
    r.filtered=std::move(filtered);return r;
}
AnalyticResult hilbert(const std::vector<double>& values, bool removeMean, const Cancel& cancel) {
    check(cancel);
    auto w=normalized(values,removeMean,cancel);
    auto spectrum=numeric::backend::realFft(w.y,values.size());
    check(cancel);
    // H(f)=-i for positive f, +i for negative f. irfft supplies the conjugate
    // half; do NOT double positive bins here (that applies to the complex DFT).
    spectrum.front()=0;
    for(std::size_t k=1;k<spectrum.size();++k) spectrum[k]*=std::complex<double>(0,-1);
    if(values.size()%2==0) spectrum.back()=0;
    auto q=numeric::backend::inverseRealFft(std::move(spectrum),values.size());
    AnalyticResult r; r.removedMean=w.mean;r.envelope.resize(values.size());
    for(std::size_t i=0;i<values.size();++i) {
        if((i&4095)==0)check(cancel);
        r.envelope[i]=restored(std::hypot(w.y[i],q[i]),w.scale);
        q[i]=restored(q[i],w.scale);
    }
    r.quadrature=std::move(q);return r;
}
}
