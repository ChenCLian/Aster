// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CALIBRATION_PORTABLE_RANDOM_H
#define ASTER_CALIBRATION_PORTABLE_RANDOM_H
#include <cstdint>
#include <cmath>
#include <vector>
// Fixed SplitMix64 + Box-Muller, avoiding implementation-dependent normal_distribution.
class CalibrationRandom {
    std::uint64_t state;
public:
    explicit CalibrationRandom(std::uint64_t seed):state(seed){}
    double uniform() {
        std::uint64_t z=(state+=UINT64_C(0x9e3779b97f4a7c15));
        z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
        return ((z^(z>>31))>>11)*0x1.0p-53 + 0x1.0p-54;
    }
    double normal() {
        // Explicit evaluation order keeps the stream identical across compilers.
        const double u=uniform(),v=uniform();
        return std::sqrt(-2*std::log(u))*std::cos(2*std::acos(-1.)*v);
    }
    std::vector<double> ar1(int n,double rho) {
        std::vector<double> y(n); y[0]=normal(); // Exact stationary N(0,1) initial value; no burn-in.
        const double innovation=std::sqrt((1-rho)*(1+rho));
        for(int i=1;i<n;++i)y[i]=rho*y[i-1]+innovation*normal();
        return y;
    }
};
#endif
