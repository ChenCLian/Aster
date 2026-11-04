"""Independent SciPy implementation of COCO Monte Carlo and sliding COCO.
No Aster code imported. MT19937-64 is implemented from its published recurrence;
fixed Box-Muller ordering is the portable Aster generator contract, not a fit.
Run: python3 generate_coco_analysis_reference.py
"""
from pathlib import Path
import json, math
import numpy as np
import scipy
from scipy.fft import rfft
from scipy.signal import detrend
MASK=(1<<64)-1
class MT64:
    def __init__(self,seed):
        self.state=[seed&MASK]
        for i in range(1,312): self.state.append((6364136223846793005*(self.state[-1]^(self.state[-1]>>62))+i)&MASK)
        self.index=312
    def uint(self):
        if self.index==312:
            for i in range(312):
                x=(self.state[i]&0xffffffff80000000)|(self.state[(i+1)%312]&0x7fffffff)
                self.state[i]=self.state[(i+156)%312]^(x>>1)^(0xb5026f5aa96619e9 if x&1 else 0)
            self.index=0
        y=self.state[self.index];self.index+=1
        y^=(y>>29)&0x5555555555555555;y^=(y<<17)&0x71d67fffeda60000
        y^=(y<<37)&0xfff7eee000000000;y^=y>>43
        return y&MASK
    def uniform(self): return (self.uint()>>11)*2.**-53
assert MT64(5489).uint()==14514284786278117030

def ar1(n,rho,rng):
    z=[]
    while len(z)<n:
        radius=math.sqrt(-2*math.log(1-rng.uniform()));angle=2*math.pi*rng.uniform()
        z.extend([radius*math.cos(angle),radius*math.sin(angle)])
    y=[z[0]]
    for a in z[1:n]:y.append(rho*y[-1]+math.sqrt((1-rho)*(1+rho))*a)
    return np.array(y)

def random_peaks(n,fft,count,rng):
    k=np.arange(fft//2+1);p=np.zeros_like(k,dtype=float);width=2*fft/n
    for i in range(count):
        start=rng.uniform()*(fft/2-width);height=rng.uniform()
        mask=(k>=start)&(k<=start+width)
        p[mask]+=height*np.maximum(0,np.sin(np.pi*(k[mask]-start)/width))
    return p

n=128;dx=.5;rates=np.arange(2,6.01,.5);periods=np.array([405.,125.,95.]);weights=np.array([1.,.6,.3]);fmax=.02;padding=2;fft=256

def power(v,mode):
    v=np.asarray(v)
    if mode!='none':v=detrend(v,type='constant' if mode=='mean' else 'linear')
    p=np.abs(rfft(v,fft))**2;p[1:-1]*=2;return p

def plan(mode):
    targets=[];bins=[]
    for rate in rates:
        dt=dx*100/rate;t=np.arange(n)*dt
        target=sum(w/2*(power(np.sin(2*np.pi*t/p),mode)+power(np.cos(2*np.pi*t/p),mode)) for p,w in zip(periods,weights))
        end=int(np.floor(fmax*fft*dt+1e-12));targets.append(target[1:end+1]);bins.append(end)
    return targets,bins

def correlations(p,targets,bins):
    out=[]
    for target,end in zip(targets,bins):
        a=p[1:end+1];aa=a-a.mean();bb=target-target.mean()
        norm=np.linalg.norm(aa)*np.linalg.norm(bb)
        out.append(float(np.clip(np.dot(aa,bb)/norm,-1,1)) if norm>0 else 0.)
    return np.array(out)

def upper(null,observed):
    return (1+np.count_nonzero(np.array(null)>=observed-100*np.finfo(float).eps*abs(observed)))/(len(null)+1)

def mc_case(model,rho,mode,seed=1729,count=7):
    y=ar1(n,.45,MT64(961))+ .7*np.sin(2*np.pi*np.arange(n)*dx*100/4/95.)
    target,bins=plan(mode);obs=correlations(power(y,mode),target,bins);evidence=np.maximum(0,obs)
    rng=MT64(seed);B=79;draws=[]
    for i in range(B):
        p=power(ar1(n,rho,rng),mode) if model=='fixed_ar1' else random_peaks(n,fft,count,rng)
        draws.append(np.maximum(0,correlations(p,target,bins)))
    draws=np.array(draws);maxima=draws.max(axis=1)
    return dict(model=model,rho=rho,mode=mode,seed=seed,B=B,peaks=count,y=y.tolist(),
        correlations=obs.tolist(),pointwise=[upper(draws[:,i],v) for i,v in enumerate(evidence)],
        adjusted=[upper(maxima,v) for v in evidence],maxima=maxima.tolist(),global_p=upper(maxima,max(evidence)),
        critical=float(np.sort(maxima)[B-int(.05*(B+1))]))

def evolution():
    depth=np.arange(485)*dx;rate=3+2*np.arange(len(depth))/(len(depth)-1)
    time=np.cumsum(dx*100/rate);y=sum(np.sqrt(w)*np.sin(2*np.pi*time/p+ph) for p,w,ph in zip(periods,weights,[.7,2.,-1.2]))
    y+=.03*np.cos(.321*np.arange(len(y)))
    targets,bins=plan('mean');scores=[];centers=[]
    for first in range(0,len(y)-n+1,64):
        scores.append(correlations(power(y[first:first+n],'mean'),targets,bins));centers.append(first+(n-1)/2)
    matrix=np.array(scores);idx=matrix.argmax(axis=1)
    return dict(y=y.tolist(),window=n,step=64,centers=centers,tail=37,
        matrix=matrix.T.ravel().tolist(),best_rate=rates[idx].tolist(),best_correlation=matrix.max(axis=1).tolist())

out=dict(reference='Independent SciPy periodograms/Pearson + explicit MT19937-64/Box-Muller',numpy=np.__version__,scipy=scipy.__version__,
    dx=dx,rates=rates.tolist(),periods=periods.tolist(),weights=weights.tolist(),fmax=fmax,padding=padding,
    ar1=ar1(25,.75,MT64(1729)).tolist(),random_peaks=random_peaks(n,fft,7,MT64(1729)).tolist(),
    mc=[mc_case('fixed_ar1',0,'mean'),mc_case('fixed_ar1',.75,'linear'),mc_case('fixed_ar1',-.5,'none'),mc_case('random_peaks',0,'mean')],
    evolution=evolution())
Path(__file__).with_name('coco_analysis_reference.json').write_text(json.dumps(out,indent=2)+'\n')
print('4 Monte Carlo references, 6-window eCOCO reference, 2 generator references')
