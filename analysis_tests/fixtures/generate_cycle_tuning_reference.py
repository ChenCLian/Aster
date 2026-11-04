"""Independent numeric fixtures: mpmath 80-digit equations + SciPy peak locations.
Waltham 2015, doi:10.2110/jsr.2015.66; calculator conventions checked against
user-supplied Acycle MilankovitchCal.m. No MATLAB runtime is used here.
This file is a validation tool, not a production dependency.
"""
from pathlib import Path
import json
import mpmath as mp
import numpy as np
import scipy
from scipy.signal import find_peaks
mp.mp.dps = 80
D=mp.mpf

def theory(age):
    t=D(str(age))
    def smooth(tau,f0,whole):
        tau,f0,whole=map(D,(tau,f0,whole))
        a=tau/4500*(1-mp.exp(-4500/tau))
        mean=(whole-a*f0)/(1-a)
        return f0 if t==0 else mean+tau/t*(f0-mean)*(1-mp.exp(-t/tau))
    m=((1+D(45))*D('6.93')-22)/(45*mp.exp(D('1.48')/mp.sqrt(45)))
    n=1+t/100
    lo=min(smooth('100','20','6.77'),D('5.69')+D('1.225')*(1-mp.exp(-t/2103)))
    hi=max(smooth('1000','22','6.93'),(22+n*m*mp.exp(D('1.48')/mp.sqrt(n)))/(1+n))
    distances=[(D(384399000)**D('6.5')-D('6.5')*D('3.15576e50')*t*f)**(D(1)/D('6.5'))/1000000 for f in (hi,lo)]
    tilts=[D('84.23171')-D('.8430214')*a+D('.003838065')*a**2-D('7.656218e-6')*a**3+D('5.991817e-9')*a**4 for a in distances]
    momentum=[D('3.397618e9')-D('7.26061057334')*mp.sqrt(D('4.0334319e14')*a) for a in distances]
    # Algebraic cancellation of cos(tilt) gives an independent frequency evaluation.
    ks=[D('3.989917e8')*j/D('8.04e12')*(D(73500)/a**3+D('.000594103125029')) for a,j in zip(distances,momentum)]
    days=lambda pi_angle:[2*mp.pi*(1+1/D('365.24'))*D('8.04e12')*mp.cos(o*pi_angle/180)/(j*3600) for o,j in zip(tilts,momentum)]
    kmax,kmin=ks
    periods=[]
    for shift,spread in [('-18.848','.066'),('4.257482','.00003'),('7.453','.019'),('17.916','.2'),('17.368','.2')]:
        periods.append([1296/(kmax+D(shift)+D(spread)),1296/(kmin+D(shift)-D(spread))])
    for f,u in [('3.196','.019'),('13.66','.2'),('10.46','.2'),('13.11','.2'),('9.92','.2')]:periods.append([1296/(D(f)+D(u)),1296/(D(f)-D(u))])
    cast=lambda arr:[float(x) for x in arr]
    return dict(age_ma=age,periods=[cast(p) for p in periods],distance_km=cast([1000*a for a in distances]),day_hours=cast(days(mp.pi)),legacy_day_hours=cast(days(D('3.14159'))),mean_tilt_deg=cast(tilts),axial_ka=cast([1296/kmax,1296/kmin]))

def cycle(name,x,y,period=41,first=41,troughs=False,relief=0,distance=0,extend=False):
    x,y=np.array(x,dtype=float),np.array(y,dtype=float)
    z=-y if troughs else y
    peaks,properties=find_peaks(z,plateau_size=(1,None))
    valleys=[min(z[a:b+1]) for a,b in zip([0]+peaks.tolist(),peaks.tolist()+[len(z)-1])]
    rel=np.array([z[p]-max(valleys[k:k+2]) for k,p in enumerate(peaks)])
    candidates=[i for i in range(len(peaks)) if rel[i]>=relief]
    if distance:
        chosen=[]
        for i in sorted(candidates,key=lambda i:(-rel[i],peaks[i])):
            if all(abs(x[peaks[i]]-x[peaks[j]])>=distance for j in chosen):chosen.append(i)
        candidates=sorted(chosen)
    markers=peaks[candidates];assert len(markers)>=2,name
    ages=first+np.arange(len(markers))*period
    depths=x[markers];model_depth,model_age=depths.copy(),ages.copy()
    if extend:
        t0=ages[0]+(x[0]-depths[0])*(ages[1]-ages[0])/(depths[1]-depths[0])
        t1=ages[-1]+(x[-1]-depths[-1])*(ages[-1]-ages[-2])/(depths[-1]-depths[-2])
        model_depth=np.r_[x[0],depths,x[-1]];model_age=np.r_[t0,ages,t1]
    return dict(name=name,x=x.tolist(),y=y.tolist(),period=period,first=first,troughs=troughs,minimum_relief=relief,minimum_distance=distance,extend=extend,indices=markers.tolist(),relief=rel[candidates].tolist(),anchor_age=ages.tolist(),model_depth=model_depth.tolist(),model_age=model_age.tolist(),interval_rates=(np.diff(model_depth)/np.diff(model_age)).tolist(),candidates=len(peaks),plateaus=int(np.sum(properties['plateau_sizes']>1)))

if __name__=='__main__':
    cases=[]
    y=[0,1,0,-1,0,1,0,-1,0,1,0];x=np.arange(len(y))*.7
    cases.append(cycle('peaks',x,y))
    cases.append(cycle('troughs',x,y,troughs=True,period=23,first=-5))
    cases.append(cycle('correct_endpoint_secants',x,y,extend=True))
    cases.append(cycle('irregular_depth',[0,.5,1,2,4,5,5.5,8,9,12,15],y,period=405,first=1200,extend=True))
    cases.append(cycle('flat_top_and_bottom',range(16),[0,2,2,0,-1,-1,0,4,4,4,0,-2,-2,0,3,0]))
    cases.append(cycle('flat_troughs',range(16),[0,2,2,0,-1,-1,0,4,4,4,0,-2,-2,0,3,0],troughs=True))
    cases.append(cycle('exclude_flat_ends',range(13),[3,3,0,2,0,3,3,3,0,4,0,5,5]))
    varied=[0,2,0,.1,0,3,0,.2,0,2,0]
    cases.append(cycle('local_relief',range(11),varied,relief=1))
    cases.append(cycle('strongest_first_separation',range(11),varied,distance=3))
    cases.append(cycle('equal_distance_allowed',range(11),varied,distance=4))
    cases.append(cycle('exact_threshold',range(11),varied,relief=2))
    rng=np.random.default_rng(300)
    for n in [31,127,1001]:
        data=np.sin(np.arange(n)*.8)+.2*rng.normal(size=n)
        cases.append(cycle('random_'+str(n),np.cumsum(rng.uniform(.5,1.5,n)),data,relief=.1,distance=3,extend=True))
    payload={'sources':['https://doi.org/10.2110/jsr.2015.66','https://davidwaltham.com/wp-content/uploads/2014/01/Milankovitch.html'], 'versions':{'mpmath':mp.__version__,'numpy':np.__version__,'scipy':scipy.__version__}, 'precision_digits':80,'theory':[theory(a) for a in [0,1e-9,1,50,100,300,600,1000,2000,4000]],'cycles':cases}
    Path(__file__).with_name('cycle_tuning_reference.json').write_text(json.dumps(payload,indent=2)+'\n')
    print('Generated',len(payload['theory']),'theory cases and',len(cases),'cycle cases')
