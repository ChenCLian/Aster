#!/usr/bin/env python3
"""Independent SciPy CSD/Welch/coherence fixtures; not a runtime dependency.

Regenerate with NumPy 2.3.5 and SciPy 1.17.0. Explicit periodic windows,
mean averaging, detrending, segment/overlap/FFT sizes, and density scaling.
The small direct DFT cross-check guards conjugation and one-sided scaling.
"""
# Copyright (C) 2026 ZiJie Chen
# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import numpy as np
import scipy
from scipy import signal

def direct(a,b,dt,l,o,nfft,win,trend):
    freq=np.arange(nfft//2+1)
    transform=np.exp(-2j*np.pi*np.outer(freq,np.arange(l))/nfft)
    rows=[]
    for s in range(0,len(a)-l+1,l-o):
        x,y=a[s:s+l].copy(),b[s:s+l].copy()
        if trend:
            x=signal.detrend(x,type=trend);y=signal.detrend(y,type=trend)
        rows.append((transform@(x*win)).conj()*(transform@(y*win)))
    out=np.mean(rows,axis=0)*dt/(win@win)
    out[1:-1 if nfft%2==0 else None]*=2
    return out

cases=[]
for i,(l,o,nfft,window,trend,relation) in enumerate([
    (32,16,32,'hann','constant','shared'),(33,16,65,'hann','constant','shared'),
    (32,0,128,'boxcar',False,'independent'),(33,32,33,'boxcar','linear','shared'),
    (48,24,96,'hann','linear','independent'),(32,20,64,'boxcar','constant','shared'),
    (16,8,17,'hann',False,'shared'),(32,16,32,'hann','constant','same'),
    (32,16,64,'hann','constant','negative'),(32,16,64,'hann','constant','zero'),
    (32,16,64,'hann','constant','constant'),(32,16,64,'boxcar',False,'constant'),
    (32,16,64,'hann','constant','opposite_scale'),(33,11,99,'hann',False,'shared'),
    (64,31,127,'boxcar','linear','shared'),(16,0,16,'boxcar','constant','independent')]):
    rng=np.random.default_rng(1729+i)
    n=337;dt=.25;t=np.arange(n)*dt
    a=np.sin(.7*t)+.7*rng.standard_normal(n)+.002*t
    b=.3*a+np.cos(.7*t+.4)+.4*rng.standard_normal(n)
    if relation=='independent':b=rng.standard_normal(n)
    if relation=='same':b=a.copy()
    if relation=='negative':b=-3*a
    if relation=='zero':a[:]=0
    if relation=='constant':a[:]=3;b[:]=-5
    if relation=='opposite_scale':a*=1e70;b*=1e-70
    win=signal.windows.hann(l,sym=False) if window=='hann' else np.ones(l)
    options=dict(fs=1/dt,window=win,nperseg=l,noverlap=o,nfft=nfft,detrend=trend)
    f,ab=signal.csd(a,b,**options,scaling='density',average='mean')
    _,aa=signal.welch(a,**options,scaling='density',average='mean')
    _,bb=signal.welch(b,**options,scaling='density',average='mean')
    with np.errstate(divide='ignore',invalid='ignore'):
        _,coherence=signal.coherence(a,b,**options)
    phase=np.angle(ab,deg=True)
    validc=(aa>0)&(bb>0);validp=np.abs(ab)>0
    # Direct DFT is sensitive to cancellation near exact zero; check with an
    # explicit absolute tolerance relative to the largest cross-density bin.
    independent=direct(a,b,dt,l,o,nfft,win,trend)
    err=np.max(np.abs(independent-ab));scale=max(1e-300,np.max(np.abs(ab)))
    assert err <= 2e-12*scale+1e-28,(i,err,scale)
    cases.append(dict(name=f'{i:02d}_{relation}_{window}_{trend}',a=a.tolist(),b=b.tolist(),spacing=dt,
        segment=l,overlap=o,nfft=nfft,window='hann' if window=='hann' else 'rectangular',
        detrend='none' if trend is False else 'mean' if trend=='constant' else 'linear',
        frequency=f.tolist(),auto_a=aa.tolist(),auto_b=bb.tolist(),real=ab.real.tolist(),imaginary=ab.imag.tolist(),
        magnitude=np.abs(ab).tolist(),coherence=np.where(validc,coherence,0).tolist(),
        phase=np.where(validp,phase,0).tolist(),coherence_defined=validc.astype(int).tolist(),
        phase_defined=validp.astype(int).tolist(),direct_dft_max_abs_error=float(err)))
result=dict(schema=1,numpy=np.__version__,scipy=scipy.__version__,convention='mean(conj(FFT(A))*FFT(B))',
    normalization='one-sided density',cases=cases)
Path(__file__).with_name('scipy_reference.json').write_text(json.dumps(result,allow_nan=False,separators=(',',':'))+'\n')
print(f'Wrote {len(cases)} cases; NumPy {np.__version__}, SciPy {scipy.__version__}; direct DFT checks passed')
