"""Independent direct-DFT reference: TC98 wavelet bases, local chi-square test.
No Aster/wavelib calls. NumPy dense DFT (not the production FFT); SciPy quantiles.
Nyquist frequency is positive, as in the authors' Fourier convention.
"""
import json, math
from pathlib import Path
import numpy as np
from scipy.stats import chi2

def reference(name, mother, parameter, n=48, dt=.25, dj=.5, amplitude=1, offset=0):
    t=np.arange(n,dtype=float)
    y=amplitude*(np.sin(2*np.pi*t/13)+.3*np.cos(2*np.pi*t/7)+.012*t)+offset
    centered=y-y[0];centered-=centered.mean()
    factor={'morlet':4*np.pi/(parameter+np.sqrt(2+parameter**2)),
            'paul':4*np.pi/(2*parameter+1),'dog':2*np.pi*np.sqrt(2/(2*parameter+1))}[mother]
    s0=max(2,2/factor);last=math.floor(math.log2(n/s0)/dj+1e-13)
    scales=s0*2**(np.arange(last+1)*dj)
    length=2**(1+int(.499999+np.log2(n)))
    # Dense mathematical DFT: X_k=sum_t x_t exp(-2*pi*i*k*t/L).
    dft=np.exp(-2j*np.pi*np.outer(np.arange(length),np.arange(length))/length)
    padded=np.pad(centered,(0,length-n));spectrum=dft@padded
    k=np.concatenate([np.arange(length//2+1),np.arange(-length//2+1,0)])*2*np.pi/length
    power=[]
    for scale in scales:
        z=scale*k
        if mother=='morlet':
            daughter=np.sqrt(2*np.pi*scale)*np.pi**(-.25)*np.exp(-.5*(z-parameter)**2)*(k>0)
        elif mother=='paul':
            m=int(parameter);daughter=np.zeros(length);positive=k>0
            daughter[positive]=np.sqrt(2*np.pi*scale)*2**m/math.sqrt(m*math.factorial(2*m-1))*z[positive]**m*np.exp(-z[positive])
        else:
            m=int(parameter);daughter=-((1j)**m)*np.sqrt(2*np.pi*scale/math.gamma(m+.5))*z**m*np.exp(-z*z/2)
        wave=dft.conj()@(spectrum*daughter)/length
        power.append(np.abs(wave[:n])**2)
    power=np.array(power);variance=np.var(y,ddof=1)
    raw=float(centered[1:]@centered[:-1]/(centered[:-1]@centered[:-1]));rho=float(np.clip(raw,-.99,.99))
    dof=1 if mother=='dog' else 2
    background=(1-rho*rho)/(1-2*rho*np.cos(2*np.pi/(scales*factor))+rho*rho)
    ratio=power/(variance*background[:,None]*chi2.ppf(.95,dof)/dof)
    coi=factor*(np.sqrt(2) if mother=='paul' else 1/np.sqrt(2))*np.minimum(t,n-1-t)*dt
    return dict(name=name,mother=mother,parameter=parameter,dj=dj,x=(t*dt).tolist(),y=y.tolist(),power=power.ravel().tolist(),
                ratio=ratio.ravel().tolist(),period=(scales*factor*dt).tolist(),coi=coi.tolist(),rows=len(scales),padding=length,rho=rho)

cases=[]
for mother,parameters in [('morlet',[5,6,8,20]),('paul',[1,4,10,20]),('dog',[2,6,20])]:
    for n in [47,64]:
        for dt in [.25,3.5]:
            for parameter in parameters:
                cases.append(reference(f'{mother}_{parameter}_n{n}_dt{dt}',mother,parameter,n,dt))
# A shift plus integer-valued input has an exact floating-point translation test elsewhere.
Path(__file__).with_name('wavelet_reference.json').write_text(json.dumps({'reference':'Dense DFT + TC98 analytical wavelets; local AR1 test; N-1 variance','cases':cases},separators=(',',':'))+'\n')
print(len(cases),'wavelet reference cases')
