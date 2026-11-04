"""Acycle 3.1.1 conventions, independently evaluated with NumPy/SciPy.

This is NOT output captured from MATLAB. The optional MATLAB bridge performs
that separate check. Never calls the production C++ implementation.
"""
from pathlib import Path
import ast
import json
import math
import numpy as np
from scipy.signal.windows import dpss
from scipy.optimize import least_squares
from scipy.stats import chi2


def swa(f, logp):
    # Reuse the independent vectorized reference, not a production function.
    source = ast.parse(Path(__file__).with_name('generate_spectral_reference.py').read_text())
    function = next(n for n in source.body if isinstance(n, ast.FunctionDef) and n.name == 'swa')
    namespace = {'np': np}
    exec(compile(ast.Module(body=[function], type_ignores=[]), '<independent SWA reference>', 'exec'), namespace)
    return namespace['swa'](f, logp)


def mtm(n, nw, nfft, dt, logfit=False):
    rng = np.random.default_rng(7265+n)
    t = np.arange(n)
    y = np.zeros(n)
    noise = rng.normal(size=n)
    for i in range(n):
        y[i] = noise[i]+(.7*y[i-1] if i else 0)
    y += .8*np.sin(2*np.pi*t/13)+3
    centered = y-y.mean()
    variance = np.mean(centered**2)
    k = 2 if nw == 1 else math.floor(2*nw+.5)-1
    tapers, eigen = dpss(n, nw, Kmax=k, sym=True, norm=2, return_ratios=True)
    individual = np.abs(np.fft.rfft(tapers*centered, n=nfft))**2
    p = individual[:min(2,k)].mean(axis=0)
    for iteration in range(1,1001):
        weights = eigen[:,None]*(p/(eigen[:,None]*p+variance*(1-eigen[:,None])))**2
        new = (weights*individual).sum(axis=0)/weights.sum(axis=0)
        delta = np.abs(new-p)
        change = 2*np.sum(delta)-delta[0]-(delta[-1] if nfft%2 == 0 else 0)
        p = new
        if change < .0005*variance:
            break
    p *= dt
    p[1:] *= 2
    if nfft % 2 == 0:
        p[-1] /= 2
    f = np.arange(len(p))/(nfft*dt)
    width = math.floor(.2*len(p)+.5)
    before, after = ((width//2,width//2) if width%2 else (width//2-1,width//2))
    smooth = np.array([np.median(p[max(0,i-before):min(len(p),i+after+1)]) for i in range(len(p))])
    target = smooth/smooth.max()
    def residual(v):
        amplitude, rho = v
        g = (1-rho*rho)/(1-2*rho*np.cos(2*np.pi*f*dt)+rho*rho)
        return np.log(target)-np.log(amplitude*g) if logfit else target-amplitude*g
    candidates = [least_squares(residual, [target.mean(), rho], bounds=([1e-20,-.999999999],[np.inf,.999999999]),
                               xtol=1e-13, ftol=1e-13, gtol=1e-13, max_nfev=3000) for rho in [-.8,0,.8,.98]]
    fit = min(candidates, key=lambda r: r.cost)
    amplitude, rho = fit.x
    background = amplitude*smooth.max()*(1-rho*rho)/(1-2*rho*np.cos(2*np.pi*f*dt)+rho*rho)
    expected = dict(spectrum=p.tolist(), ar1_smoothed=smooth.tolist(), ar1_background=background.tolist(),
                    ar1_95=(background*chi2.isf(.05,2*k)/(2*k)).tolist())
    if len(p) >= 33:
        bg = swa(f,np.log10(p))
        expected['swa_background'] = bg.tolist()
        expected['swa_95'] = (bg*chi2.isf(.05,2*k)/(2*k)).tolist()
        sorted_p = np.sort(chi2.sf(2*k*p/bg,2*k))
        correction = sum(1/i for i in range(1,math.floor(nw)+1))
        for q,key in zip([.05,.01,.001,.0001],['fdr_05','fdr_01','fdr_001','fdr_0001']):
            passes = np.flatnonzero(sorted_p < np.arange(1,len(p)+1)*q/len(p)/correction)
            expected[key] = (bg*chi2.isf(sorted_p[passes[-1]],2*k)/(2*k)).tolist() if len(passes) else []
    return dict(name=f'mtm_n{n}_nw{nw}_fft{nfft}_log{logfit}', method='mtm',
                x=(t*dt).tolist(), y=y.tolist(), parameters=dict(nw=nw,nfft=nfft,logfit=logfit),
                expected=expected, frequency=f.tolist(), tapers=k, iterations=iteration, rho=float(rho))


def wavelet(n,dt,mother,parameter):
    t = np.arange(n,dtype=float)
    y = np.sin(2*np.pi*t/13)+.3*np.cos(2*np.pi*t/7)+.012*t
    z = y-y.mean()
    dj = .1
    factor = {'morlet':4*np.pi/(parameter+np.sqrt(2+parameter**2)),
              'paul':4*np.pi/(2*parameter+1),'dog':2*np.pi*np.sqrt(2/(2*parameter+1))}[mother]
    s0 = max(2,2/factor)
    last = math.floor(np.log2(.34*n/s0)/dj+.5)
    scales = s0*2**(np.arange(last+1)*dj)
    length = 2**(1+int(.499999+np.log2(n)))
    transform = np.exp(-2j*np.pi*np.outer(np.arange(length),np.arange(length))/length)
    spectrum = transform@np.pad(z,(0,length-n))
    frequencies = np.r_[np.arange(length//2+1),np.arange(-length//2+1,0)]*2*np.pi/length
    raw=[]
    for scale in scales:
        u=scale*frequencies
        if mother=='morlet':
            kernel=np.sqrt(2*np.pi*scale)*np.pi**(-.25)*np.exp(-.5*(u-parameter)**2)*(frequencies>0)
        elif mother=='paul':
            m=int(parameter); kernel=np.zeros(length); positive=frequencies>0
            kernel[positive]=np.sqrt(2*np.pi*scale)*2**m/math.sqrt(m*math.factorial(2*m-1))*u[positive]**m*np.exp(-u[positive])
        else:
            m=int(parameter); kernel=-1j**m*np.sqrt(2*np.pi*scale/math.gamma(m+.5))*u**m*np.exp(-u*u/2)
        w=transform.conj()@(spectrum*kernel)/length
        raw.append(np.abs(w[:n])**2)
    raw=np.array(raw)
    corrected=raw/(scales[:,None]*dt)
    variance=np.var(y,ddof=1)
    rho=(z[:-1]@z[1:]/(n-1))/(z@z/n)
    dof=1 if mother=='dog' else 2
    bg=(1-rho*rho)/(1-2*rho*np.cos(2*np.pi/(scales*factor))+rho*rho)
    ratio=raw/(variance*bg[:,None]*chi2.isf(.05,dof)/dof)
    coi=factor*(np.sqrt(2) if mother=='paul' else 1/np.sqrt(2))*np.minimum(t,n-1-t)*dt
    return dict(name=f'cwt_{mother}_n{n}_dt{dt}', method='cwt',x=(t*dt).tolist(),y=y.tolist(),
                parameters=dict(method='acycle_cwt',mother=mother,parameter=parameter,dj=dj),
                expected=dict(power=(corrected/variance).ravel().tolist(),raw_power=raw.ravel().tolist(),
                              scale_corrected_power=corrected.ravel().tolist(),significance_ratio=ratio.ravel().tolist()),
                period=(scales*factor*dt).tolist(),coi=coi.tolist(),rows=len(scales),rho=float(rho))


if __name__=='__main__':
    cases=[mtm(*values) for values in [(64,1,64,1),(64,1.25,64,.25),(96,2,96,1),
           (127,2.5,127,3.5),(128,3.5,256,.25),(256,2,256,1),(96,2,96,1,True)]]
    cases += [wavelet(n,dt,m,p) for n in [47,64] for dt in [.25,3.5]
              for m,p in [('morlet',6),('paul',4),('dog',2)]]
    output=dict(reference=__doc__,acycle_zip_sha256='7c34258597734fdee61e6c183177ce8573f0a3312809c7203cb382939a63e6fd',cases=cases)
    Path(__file__).with_name('acycle_reference.json').write_text(json.dumps(output,separators=(',',':'))+'\n')
    print(len(cases),'independent Acycle-convention reference cases')
