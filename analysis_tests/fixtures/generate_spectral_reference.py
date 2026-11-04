"""Batch 2 mathematical references (NumPy/SciPy required only for regeneration).
DPSS: dense sinc concentration kernel (independent of production tridiagonal).
MTM: direct DFT, two-sided Thomson weights, then one-sided PSD conversion.
SWA: vector interpolation and least-squares polynomial tail, not a C++ port.
Peak prominence/width: scipy.signal. FDR: SciPy BY adjusted p-values.
These are mathematical checks, not a MATLAB/Acycle execution comparison.
"""
from pathlib import Path
import json
import numpy as np
from scipy.fft import next_fast_len
from scipy.linalg import eigh
from scipy.signal import find_peaks
from scipy.stats import false_discovery_control, chi2
from scipy.optimize import minimize_scalar


def swa(f, logp):
    logp=np.asarray(logp); offset=logp.max(); y=logp-offset; n=len(y)
    if n<33:return 10**(np.full(n,y.mean())+offset)
    ix=np.arange(n); best=None
    def rmse(a):return np.sqrt(np.sum((y-a)**2)/(n-1))
    for w in range(11,min(99,n//3)+1,2):
        m=y[:n//w*w].reshape(-1,w).mean(1); centers=np.arange(len(m))*w+w//2
        for variant in [0,1]:
            lo=(m[1]-m[0])/w; hi=(m[-1]-m[-2])/w
            if variant:lo=2*lo-(m[2]-m[1])/w;hi=2*hi-(m[-2]-m[-3])/w
            fit=np.interp(ix,centers,m)
            fit[:centers[0]]=m[0]+lo*(ix[:centers[0]]-centers[0])
            fit[centers[-1]+1:]=m[-1]+hi*(ix[centers[-1]+1:]-centers[-1])
            score=(int(np.sum(np.diff(fit)>=0)),rmse(fit))
            if best is None or score<best[0]:best=(score,fit,w,int(centers[-1]),m[-1])
    (_,error),fit,w,end,last=best
    flat=fit.copy();flat[end:]=last
    if rmse(flat)<error:fit,error=flat,rmse(flat)
    start=end-w//2;z=np.linspace(-1,1,n-start)
    design=np.stack([np.ones(len(z)),z,z*z],axis=1)
    quad=fit.copy();quad[start:]=design@np.linalg.lstsq(design,y[start:],rcond=None)[0]
    if rmse(quad)<error:fit,error=quad,rmse(quad)
    for _ in range(500):
        smooth=np.convolve(fit,[.25,.5,.25],mode='same');smooth[0]=.5*(fit[0]+fit[1]);smooth[-1]=.5*(fit[-2]+fit[-1])
        if not rmse(smooth)<error*1.01:break
        if np.array_equal(fit,smooth):break
        fit=smooth
    return 10**(fit+offset)

cases=[]; rng=np.random.default_rng(20261101)
for n,nw,k,fs in [(32,1,1,1),(64,2,3,1),(127,3,5,.2),(151,3,5,4),(65,4,7,1)]:
    t=np.arange(n); y=np.sin(2*np.pi*t/16)+.35*np.cos(2*np.pi*t/37)+rng.normal(0,.2,n)
    yy=y-y.mean();v=np.mean(yy**2);w=nw/n
    kernel=2*w*np.sinc(2*w*(t[:,None]-t[None,:]));lam,evec=eigh(kernel,subset_by_index=[n-k,n-1]);lam=lam[::-1];tap=evec[:,::-1].T
    nfft=next_fast_len(max(4*n,512),real=True);f=np.arange(nfft//2+1)/nfft
    e=np.abs((tap*yy)@np.exp(-2j*np.pi*t[:,None]*f))**2
    p=e[:min(k,2)].mean(0)
    for it in range(1000 if k>1 else 0):
        weights=lam[:,None]*(p/(lam[:,None]*p+v*(1-lam[:,None])))**2
        q=(weights*e).sum(0)/weights.sum(0);d=np.sum(np.abs(q-p));p=q
        if d<.0005*v:break
    weights=lam[:,None]*(p/(lam[:,None]*p+v*(1-lam[:,None])))**2
    dof=2*weights.sum(0)**2/(weights**2).sum(0)
    factors=np.full(len(f),2.);factors[0]=1;dof[0]/=2
    if nfft%2==0:factors[-1]=1;dof[-1]/=2
    cases.append(dict(name=f'mtm_dense_{n}_{nw}_{fs}',method='mtm',x=(f*fs).tolist(),y=y.tolist(),fs=fs,nw=nw,k=k,power=(p*factors/fs).tolist(),dof=dof.tolist()))
for rho in [0,.7,-.6,.98]:
    for amp in [1,1e-20,1e20]:
        for logfit in [False,True]:
            f=np.linspace(0,.5,257);p=amp*(1-rho*rho)/(1-2*rho*np.cos(2*np.pi*f)+rho*rho)
            cases.append(dict(name=f'ar1_{rho}_{amp}_{logfit}',method='ar1',x=f.tolist(),y=p.tolist(),rho=rho,amplitude=amp,logfit=logfit))
for logfit in [False,True]:
    f=np.linspace(0,.5,257);rho=.73
    p=(1-rho*rho)/(1-2*rho*np.cos(2*np.pi*f)+rho*rho)*np.exp(.18*np.sin(81*f)+.13*np.cos(127*f))
    width=39; smooth=np.array([np.median(p[max(0,i-width//2):min(len(p),i+width//2+1)]) for i in range(len(p))])
    target=np.log(smooth) if logfit else smooth
    def objective(r):
        g=(1-r*r)/(1-2*r*np.cos(2*np.pi*f)+r*r)
        a=np.exp(np.mean(target-np.log(g))) if logfit else np.dot(smooth,g)/np.dot(g,g)
        residual=target-np.log(a*g) if logfit else target-a*g
        return np.sum(residual*residual)
    r=minimize_scalar(objective,bounds=(-.99,.99),method='bounded',options={'xatol':1e-13}).x
    g=(1-r*r)/(1-2*r*np.cos(2*np.pi*f)+r*r)
    a=np.exp(np.mean(target-np.log(g))) if logfit else np.dot(smooth,g)/np.dot(g,g)
    cases.append(dict(name=f'ar1_noisy_median_{logfit}',method='ar1',x=f.tolist(),y=p.tolist(),background=(a*g).tolist(),smooth=smooth.tolist(),rho=r,amplitude=a,logfit=logfit,window=.15))
for strong in [100,1e20]:
    f=np.linspace(0,1,257);p=np.exp(-3*f)+.02*(1+np.sin(np.arange(257)*.53));p[78]*=strong;p[137]*=2*strong
    bg=swa(f,np.log10(p));prob=chi2.sf(10*p/bg,10);adjusted=false_discovery_control(prob,method='by');curves=[]
    for q in [.05,.01,.001,.0001]:
        count=int(np.sum(adjusted<=q))
        alpha=count*q/(len(p)*np.sum(1/np.arange(1,len(p)+1)))
        curves.append((bg*chi2.isf(alpha,10)/10).tolist() if count else [])
    cases.append(dict(name=f'fdr_full_{strong}',method='fdr_full',x=f.tolist(),y=p.tolist(),background=bg.tolist(),curves=curves))
for n in [7,32,33,57,128,257,513]:
    t=np.linspace(0,1,n);p=2-3*t+.15*np.sin(37*t)+2*np.exp(-((t-.29)/.02)**2)+.8*np.maximum(0,t-.7)**2
    expected=swa(t,p)
    for unit in [1,1e-7,1e7]:
        cases.append(dict(name=f'swa_{n}_unit_{unit}',method='swa',x=(t*unit).tolist(),y=p.tolist(),background=expected.tolist()))
for case,y in [('plateau',[0,1,3,3,1,0,2,0]),('nested',[0,5,1,3,2,4,0]),('equal',[0,3,0,3,0,3,0]),('endpoints',[4,2,0,1,0,5]),('negative',[-8,-3,-4,-1,-7]),('noise',rng.normal(size=200).tolist())]:
    for radius in [0,3,8]:
        indices,props=find_peaks(y,prominence=0,width=0,wlen=2*radius+1 if radius else None)
        cases.append(dict(name=f'peaks_{case}_{radius}',method='peaks',y=y,radius=radius,indices=indices.tolist(),prominence=props['prominences'].tolist(),width=props['widths'].tolist()))
for case,p in [('empty',[]),('none',[.5,.9,.99]),('tiny',[1e-300,1e-30,1e-10,.01,.8]),('ties',[.0001,.0001,.04,.04,.5]),('random',rng.uniform(0,1,200).tolist())]:
    adjusted=false_discovery_control(p,method='by') if p else np.array([])
    cases.append(dict(name='fdr_'+case,method='fdr',y=p,rejections=[(adjusted<=q).tolist() for q in [.05,.01,.001,.0001]]))
Path(__file__).with_name('spectral_reference.json').write_text(json.dumps({'description':__doc__,'cases':cases},indent=2)+'\n')
