"""Independent NumPy/SciPy reference; direct DFT cross-check for a small case.
Run from anywhere. Not needed to build or run Aster.
Fixed harmonic target: E[periodogram(sum sqrt(w_j)*sin(w_j*t+phi_j))]
for independent uniform phases equals sum w_j/2*(P(sin)+P(cos)).
"""
import json
import itertools
from pathlib import Path
import numpy as np
import scipy
from scipy.signal import detrend
from scipy.fft import rfft

def reference(n, mode, padding, offset=0):
    dx=.5; rates=np.arange(2.,6.01,.5); periods=np.array([405.,125.,95.]); weights=np.array([1.,.6,.3]); fmax=.02
    t=np.arange(n)*dx*100/4
    y=sum(np.sqrt(w)*np.sin(2*np.pi*t/p+ph) for p,w,ph in zip(periods,weights,[.7,2.,-1.2]))
    y=y+.09*np.cos(.321*np.arange(n))+offset
    if mode=='linear': y=y+.003*np.arange(n)
    nfft=2**int(np.ceil(np.log2(n*padding)))
    def prepare(v):
        return v if mode=='none' else detrend(v,type='constant' if mode=='mean' else 'linear')
    def psd(v):
        z=rfft(prepare(v),nfft); p=np.abs(z)**2;p[1:-1]*=2;return p
    obs=psd(y); scores=[]; spectra=[]; diagnostics=[]
    for r in rates:
        dt=dx*100/r; f=np.fft.rfftfreq(nfft,dt); ray=1/(n*dt); nyq=1/(2*dt)
        valid=int(np.count_nonzero((1/periods>=ray)&(1/periods<nyq)))
        status=1 if nyq<fmax else 2 if valid!=len(periods) else 0
        mask=(f>0)&(f<=fmax+1e-16)
        target=sum(w/2*(psd(np.sin(2*np.pi*np.arange(n)*dt/p))+psd(np.cos(2*np.pi*np.arange(n)*dt/p))) for p,w in zip(periods,weights))
        cc=float(np.corrcoef(obs[mask],target[mask])[0,1]) if status==0 else 0.
        diagnostics.append([float(r),cc,ray,nyq,valid,int(mask.sum()) if status==0 else 0,status])
        if status==0:scores.append((cc,len(diagnostics)-1));spectra.append((f[mask],obs[mask]/obs[mask].max(),target[mask]/target[mask].max()))
    k=max(range(len(scores)),key=lambda k:scores[k][0]); f,p,q=spectra[k]
    # A direct complex exponential matrix checks FFT normalization independently.
    if n==128 and mode=='mean':
        v=prepare(y); direct=np.abs(np.exp(-2j*np.pi*np.arange(nfft//2+1)[:,None]*np.arange(n)/nfft)@v)**2;direct[1:-1]*=2
        assert np.allclose(direct,obs,atol=2e-9,rtol=2e-10)
        dt=dx*100/4; angle=2*np.pi*np.arange(n)*dt
        averaged=sum(psd(sum(np.sqrt(w)*np.sin(angle/p+phase) for p,w,phase in zip(periods,weights,phases))) for phases in itertools.product([0,np.pi/2,np.pi,3*np.pi/2],repeat=3))/64
        expected=sum(w/2*(psd(np.sin(angle/p))+psd(np.cos(angle/p))) for p,w in zip(periods,weights))
        assert np.allclose(averaged,expected,atol=2e-9,rtol=2e-10)
    return dict(n=n,dx=dx,mode=mode,padding=padding,y=y.tolist(),periods=periods.tolist(),weights=weights.tolist(),minimum=2,maximum=6,step=.5,fmax=fmax,rows=diagnostics,best_row=scores[k][1],frequency=f.tolist(),observed=p.tolist(),target=q.tolist())
out={'reference':'NumPy/SciPy + direct DFT, independent phase-average formula','numpy':np.__version__,'scipy':scipy.__version__,
     'cases':[reference(128,'mean',2),reference(129,'linear',1),reference(129,'none',3,.2)]}
Path(__file__).with_name('coco_reference.json').write_text(json.dumps(out,indent=2)+'\n')
print('COCO reference cases:',len(out['cases']))
