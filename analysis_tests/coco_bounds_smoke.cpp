// Standalone sanitizer harness for new cached COCO/FFT indexing and cancellation.
// Build instructions are in docs/methods/coco_analysis.md. Not a second algorithm.
#include "math/chronology/coco_analysis.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace aster::chronology;
int main()
{
    std::mt19937_64 rng(4711);
    for(std::size_t n:{63,64,127,128,129,255})for(int padding:{1,2,3,4}){
        CocoOptions o;o.periodsKa={40,17,11};o.relativePower={1,.7,.2};
        o.minimumRate=3;o.maximumRate=6;o.rateStep=.5;o.maximumFrequency=.1;o.paddingFactor=padding;
        CocoMonteCarloOptions mc;mc.simulations=19;mc.rho=.8;
        auto y=cocoAr1(n,.8,rng);auto r=cocoMonteCarlo(y,.1,o,mc);
        assert(r.maximum.nullMaxima.size()==19);
        for(std::size_t i=0;i<r.rates.size();++i){assert(std::isfinite(r.pointwiseP[i]));assert(r.maximum.adjustedP[i]>=r.pointwiseP[i]);}
        mc.nullModel=CocoMonteCarloOptions::Null::RandomPeaks;mc.peakCount=3;
        auto p=cocoMonteCarlo(y,.1,o,mc);assert(p.maximum.nullMaxima.size()==19);
        auto yy=cocoAr1(3*n+5,.6,rng);CocoEvolutionOptions e;e.windowSamples=n;e.stepSamples=n/2;
        auto v=cocoEvolution(yy,.1,o,e);assert(v.correlation.size()==v.centers.size()*v.rates.size());
        for(auto x:v.correlation)assert(std::isfinite(x)&&x>=-1&&x<=1);
        bool stop=false;
        try{cocoEvolution(yy,.1,o,e,[&]{return stop;},[&](int progress){if(progress>20)stop=true;});assert(false);}
        catch(const CocoCancelled &){}
    }
    std::cout<<"24 odd/even/padding cases; both nulls, sliding map and cancellation passed\n";
}
