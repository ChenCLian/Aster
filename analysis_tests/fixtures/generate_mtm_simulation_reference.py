"""Independent SciPy DPSS/FFT and whitened-likelihood reference. Not production code."""
import json, math
from pathlib import Path
import numpy as np
from scipy.optimize import minimize_scalar
from scipy.signal.windows import dpss
from scipy.fft import rfft

LIMIT=.999999
class Stream:
    def __init__(self, seed): self.s=seed
    def uniform(self):
        mask=(1<<64)-1;self.s=(self.s+0x9e3779b97f4a7c15)&mask;z=self.s
        z=((z^(z>>30))*0xbf58476d1ce4e5b9)&mask
        z=((z^(z>>27))*0x94d049bb133111eb)&mask
        return ((z^(z>>31))>>11)*2.**-53+2.**-54
    def normal(self):
        u,v=self.uniform(),self.uniform()
        return math.sqrt(-2*math.log(u))*math.cos(2*math.pi*v)
    def ar1(self,n,rho):
        y=np.empty(n);y[0]=self.normal();a=math.sqrt((1-rho)*(1+rho))
        for i in range(1,n):y[i]=rho*y[i-1]+a*self.normal()
        return y

def likelihood_fit(y, estimate=True, fixed=0):
    n=len(y)
    def evaluate(r, detail=False):
        # Apply the stationary AR1 whitening operator to both y and the mean
        # design vector. This avoids the production aggregate SSE formula.
        w=np.r_[math.sqrt((1-r)*(1+r))*y[0],y[1:]-r*y[:-1]]
        u=np.r_[math.sqrt((1-r)*(1+r)),np.full(n-1,1-r)]
        mu=np.dot(u,w)/np.dot(u,u);v=np.dot(w-mu*u,w-mu*u)/n
        return (mu,v) if detail else n*math.log(v)-math.log1p(-r*r)
    r=fixed
    if estimate:
        grid=np.tanh(np.linspace(-np.arctanh(LIMIT),np.arctanh(LIMIT),257))
        scores=np.array([evaluate(g) for g in grid]);candidates=[(scores[0],grid[0]),(scores[-1],grid[-1])]
        for i in range(1,len(grid)-1):
            if scores[i]<=scores[i-1] and scores[i]<=scores[i+1]:
                f=minimize_scalar(evaluate,bounds=(grid[i-1],grid[i+1]),method='bounded',options={'xatol':1e-12})
                candidates.append((f.fun,f.x))
        _,r=min(candidates)
        if abs(r)>=LIMIT-1e-9:raise RuntimeError('stationarity boundary')
    mu,v=evaluate(r,True);return r,mu,v

def nfft_for(n):
    # Production convention is 4x padding, minimum 512, factors 2/3/5.
    target=max(512,4*n)
    while True:
        t=target
        for p in (2,3,5):
            while t%p==0:t//=p
        if t==1:return target
        target+=1

def statistic(y,nw=3,fs=1,fmax=1,estimate=True,rho=0):
    fit=likelihood_fit(y,estimate,rho);n=len(y);nfft=nfft_for(n)
    tapers,e=dpss(n,nw,Kmax=2*nw-1,sym=True,return_ratios=True)
    z=y-y.mean();var=np.mean(z*z);eig=abs(rfft(tapers*z,n=nfft,axis=1))**2
    s=eig[:min(2,len(e))].mean(axis=0)
    converged=len(e)==1
    for iteration in range(1000 if len(e)>1 else 0):
        w=e[:,None]*(s/(e[:,None]*s+(1-e[:,None])*var))**2
        nxt=(w*eig).sum(axis=0)/w.sum(axis=0);diff=abs(nxt-s).sum();s=nxt
        if diff<.0005*var:converged=True;break
    if not converged:raise RuntimeError('MTM convergence failure')
    f=np.arange(len(s))*fs/nfft;scale=np.full(len(s),2.);scale[[0,-1]]=1
    keep=f<=fs*fmax/2+(fs/nfft)*1e-10;f=f[keep];scale=scale[keep];power=s[keep]/fs*scale
    bg=fit[2]/fs/((1-fit[0])**2+4*fit[0]*np.sin(np.pi*f/fs)**2)*scale
    tested=scale==2
    return dict(fit=fit,f=f,power=power,bg=bg,ratio=power/bg,tested=tested,maximum=float((power/bg)[tested].max()))

def simulate(y,nw=3,fs=1,fmax=1,estimate=True,rho=0,seed=417,replicates=199,alpha=.05):
    obs=statistic(y,nw,fs,fmax,estimate,rho);rng=Stream(seed);null=[]
    for _ in range(replicates):
        simulated=rng.ar1(len(y),obs['fit'][0]);null.append(statistic(simulated,nw,fs,fmax,estimate,rho)['maximum'])
    null=np.array(null);ratio=obs['ratio'];p=np.ones(len(ratio))
    for j in np.flatnonzero(obs['tested']):p[j]=(1+np.count_nonzero(null>=ratio[j]-100*np.finfo(float).eps*abs(ratio[j])))/(replicates+1)
    return dict(y=y.tolist(),nw=nw,fs=fs,fmax=fmax,estimate=estimate,fixed_rho=rho,seed=seed,replicates=replicates,alpha=alpha,
        rho=obs['fit'][0],mean=obs['fit'][1],innovation_variance=obs['fit'][2],frequency=obs['f'].tolist(),power=obs['power'].tolist(),
        background=obs['bg'].tolist(),adjusted_p=p.tolist(),global_p=float(p[obs['tested']].min()),maximum_ratio=obs['maximum'],
        critical_ratio=float(np.sort(null)[len(null)-math.floor(alpha*(replicates+1))]),null_maxima=null.tolist())

if __name__=='__main__':
    rng=np.random.default_rng(913871);cases=[]
    for n,rho,estimate,nw,fmax,fs in [(64,0,False,3,1,1),(64,.8,True,3,1,2),(128,.98,True,3,1,1),(64,-.6,False,1,1,.5),(96,.5,True,2,.3,1)]:
        z=rng.normal(size=n);y=z.copy()
        for i in range(1,n):y[i]=rho*y[i-1]+math.sqrt(1-rho*rho)*z[i]
        cases.append(simulate(y,nw,fs,fmax,estimate,rho))
    target=Path(__file__).with_name('mtm_simulation_reference.json')
    target.write_text(json.dumps({'generator':'SciPy DPSS/rfft; independent whitened Gaussian likelihood; fixed SplitMix64 stream','cases':cases},indent=2)+'\n')
    print(target)
