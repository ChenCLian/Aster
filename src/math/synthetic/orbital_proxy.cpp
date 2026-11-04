// SPDX-License-Identifier: GPL-3.0-or-later
// See orbital_proxy.h and resources/synthetic/NOTICE.md for attribution.
#include "orbital_proxy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace aster::synthetic {
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
constexpr std::size_t maxSamples=100000;
void require(bool condition, const char* message) { if(!condition) throw std::invalid_argument(message); }
bool finite(double x) { return std::isfinite(x); }
void validate(const Model& m) {
    for(int i=0;i<5;++i) require(finite(m.frequencyMean[i]) && finite(m.frequencySd[i]) && m.frequencySd[i]>0,"Invalid frequency distribution");
    for(double v:{m.g5,m.interceptSd,m.slopeSd,m.residualSd,m.lengthScale}) require(finite(v)&&v>0,"Invalid smooth-rate model");
}
// A specified RNG rather than implementation-dependent std::normal_distribution.
// Seed streams intentionally differ from NumPy SeedSequence/PCG64.
class Normal {
    std::uint64_t state; bool cached=false; double spare=0;
public:
    explicit Normal(std::uint64_t seed):state(seed){}
    std::uint64_t bits() { auto z=(state+=UINT64_C(0x9e3779b97f4a7c15)); z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9); z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb); return z^(z>>31); }
    double uniform() { return (double(bits()>>11)+.5)*0x1.0p-53; }
    double next() {
        if(cached) {cached=false;return spare;}
        const double r=std::sqrt(-2*std::log(uniform())), angle=2*pi*uniform();
        spare=r*std::sin(angle);cached=true;return r*std::cos(angle);
    }
};
// SciPy CubicSpline's default not-a-knot boundary, NOT a natural spline.
std::array<double,8> secondDerivatives(const std::array<double,8>& y) {
    constexpr double h=1./7;
    std::array<std::array<double,9>,8> a{};
    a[0][0]=-1;a[0][1]=2;a[0][2]=-1;
    a[7][5]=-1;a[7][6]=2;a[7][7]=-1;
    for(int i=1;i<7;++i) {a[i][i-1]=h;a[i][i]=4*h;a[i][i+1]=h;a[i][8]=6*(y[i+1]-2*y[i]+y[i-1])/h;}
    for(int k=0;k<8;++k) {
        int pivot=k;for(int i=k+1;i<8;++i)if(std::abs(a[i][k])>std::abs(a[pivot][k]))pivot=i;
        require(std::abs(a[pivot][k])>1e-14,"Singular spline system");std::swap(a[pivot],a[k]);
        for(int i=k+1;i<8;++i) {const double q=a[i][k]/a[k][k];for(int j=k;j<9;++j)a[i][j]-=q*a[k][j];}
    }
    std::array<double,8> m{};
    for(int i=7;i>=0;--i) {double rhs=a[i][8];for(int j=i+1;j<8;++j)rhs-=a[i][j]*m[j];m[i]=rhs/a[i][i];}
    return m;
}
double spline(const std::array<double,8>& y,const std::array<double,8>& m,double x) {
    constexpr double h=1./7;const int i=std::clamp(int(x*7),0,6);
    const double b=std::clamp((x-i*h)/h,0.,1.),a=1-b;
    return a*y[i]+b*y[i+1]+((a*a*a-a)*m[i]+(b*b*b-b)*m[i+1])*h*h/6;
}
std::pair<double,double> standardize(std::vector<double>& x) {
    long double sum=0;for(double v:x){require(finite(v),"Nonfinite generated signal");sum+=v;}
    const double mean=double(sum/x.size());long double sq=0;
    for(double v:x){const long double d=v-mean;sq+=d*d;}
    const double sd=std::sqrt(double(sq/x.size())); // population standard deviation (ddof=0)
    require(finite(sd)&&sd>0,"Generated signal has zero or invalid variance");
    for(double& v:x)v=(v-mean)/sd;
    return {mean,sd};
}
}
Latent draw(const Model& m,std::size_t n,std::uint64_t seed,std::uint64_t realization) {
    validate(m);require(n>=32&&n<=maxSamples,"Use 32..100000 samples");
    Normal mixer(seed ^ (realization*UINT64_C(0xd2b74407b1ce6e93)));
    Normal freq(mixer.bits()),coeff(mixer.bits()),noise(mixer.bits()),rate(mixer.bits());
    Latent out;
    for(int i=0;i<5;++i){double z;int attempts=0;do{z=freq.next();require(++attempts<10000,"Frequency rejection sampler exhausted");}while(std::abs(z)>3);out.frequency[i]=m.frequencyMean[i]+z*m.frequencySd[i];}
    std::array<std::array<double,8>,8> l{};
    for(int i=0;i<8;++i)for(int j=0;j<=i;++j){
        const double d=(i-j)/(7*m.lengthScale);
        double v=std::exp(-.5*d*d)+(i==j?1e-8:0);
        for(int k=0;k<j;++k)v-=l[i][k]*l[j][k];
        if(i==j){require(v>0,"Invalid smooth covariance");l[i][j]=std::sqrt(v);}else l[i][j]=v/l[j][j];
    }
    const double intercept=m.interceptSd*rate.next(),slope=m.slopeSd*rate.next();
    std::array<double,8> z{};for(double& v:z)v=rate.next();
    for(int i=0;i<8;++i){double v=intercept+slope*(i/7.-.5);for(int j=0;j<=i;++j)v+=m.residualSd*l[i][j]*z[j];out.unitRates[i]=1/(1+std::exp(-v));}
    for(double& v:out.beta)v=coeff.next();
    out.whiteNoise.resize(n+256);for(double& v:out.whiteNoise)v=noise.next();
    return out;
}
Result evaluate(const Model& m,const Options& o,const std::vector<double>& depth,const Latent& latent,const Cancel& cancel) {
    Result r;
    if(cancel&&cancel()){r.cancelled=true;return r;}
    validate(m);const auto n=depth.size();
    require(n>=32&&n<=maxSamples,"Use 32..100000 samples");
    require(finite(o.rateMin)&&finite(o.rateMax)&&o.rateMin>=.01&&o.rateMin<o.rateMax&&o.rateMax<=1000,"Sedimentation rates must satisfy 0.01 <= min < max <= 1000 cm/kyr");
    require(finite(o.snr)&&o.snr>=.01&&o.snr<=10000,"SNR must be 0.01..10000");
    require(finite(o.rho)&&std::abs(o.rho)<=.95,"AR1 rho must be within [-0.95, 0.95]");
    for(std::size_t i=0;i<n;++i)require(finite(depth[i])&&(!i||depth[i]>depth[i-1]),"Depth must be finite and strictly increasing");
    const double span=depth.back()-depth.front();require(finite(span)&&span>=.001&&span<=10000,"Depth span must be 0.001..10000 m");
    require(latent.whiteNoise.size()==n+256,"Wrong innovation count");
    for(double v:latent.beta)require(finite(v),"Nonfinite harmonic coefficient");
    for(double v:latent.frequency)require(finite(v)&&v>0&&v<1000,"Invalid harmonic frequency parameter");
    const double low=.1/o.rateMax,high=.1/o.rateMin;
    for(int i=0;i<8;++i){const double u=latent.unitRates[i];require(finite(u)&&u>=0&&u<=1,"Invalid unit rate");r.inverseKnots[i]=low+u*(high-low);}
    const auto second=secondDerivatives(r.inverseKnots);
    const auto& f=latent.frequency;
    r.angularFrequency={f[0]+f[1],f[0]+f[2],f[0]+f[3],f[0]+f[4],f[0]+m.g5,f[2]-m.g5,f[4]-m.g5,f[4]-f[2],f[3]-m.g5,f[3]-f[2]};
    for(double& w:r.angularFrequency){w*=pi*1e6/(180*3600);require(finite(w)&&w>0,"Nonpositive harmonic frequency");}
    r.depth=depth;r.ageKa.resize(n);r.rate.resize(n);r.clean.resize(n);r.noise.resize(n);r.proxy.resize(n);
    double previous=0,age=0;std::size_t clipped=0;
    for(std::size_t i=0;i<n;++i){
        if(i%1024==0&&cancel&&cancel()){r.cancelled=true;return r;}
        const double raw=spline(r.inverseKnots,second,(depth[i]-depth.front())/span);
        const double inverse=std::clamp(raw,low,high);
        // Count actual overshoot, not spline endpoint roundoff or knots on a bound.
        const double clipTolerance=32*std::numeric_limits<double>::epsilon()*high;
        if(raw<low-clipTolerance||raw>high+clipTolerance)++clipped;
        if(i)age+=(depth[i]-depth[i-1])*(inverse+previous)*.5;
        previous=inverse;r.ageKa[i]=age*1000;r.rate[i]=.1/inverse;
        require(finite(r.ageKa[i])&&(!i||r.ageKa[i]>r.ageKa[i-1]),"Invalid generated age model");
        double y=0;for(int j=0;j<10;++j){const double phase=r.angularFrequency[j]*age;y+=latent.beta[j]*std::sin(phase)+latent.beta[j+10]*std::cos(phase);}r.clean[i]=y;
    }
    standardize(r.clean);
    double ar=0;
    for(std::size_t i=0;i<n+256;++i){
        if(i%1024==0&&cancel&&cancel()){r.cancelled=true;return r;}
        require(finite(latent.whiteNoise[i]),"Nonfinite noise innovation");ar=latent.whiteNoise[i]+o.rho*ar;
        if(i>=256)r.noise[i-256]=ar;
    }
    standardize(r.noise);
    for(std::size_t i=0;i<n;++i){r.noise[i]/=std::sqrt(o.snr);r.proxy[i]=r.clean[i]+r.noise[i];}
    const auto scale=standardize(r.proxy);r.rawMean=scale.first;r.rawSd=scale.second;r.clippedFraction=double(clipped)/n;
    if(cancel&&cancel())r.cancelled=true;
    return r;
}
} // namespace aster::synthetic
