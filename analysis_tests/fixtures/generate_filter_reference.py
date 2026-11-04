"""Independent O(N^2) DFT oracle; no Aster / PocketFFT / filter implementation import.
Matches the formulas in the user-provided Acycle corrected-bandpass-v2, not an
unverified official release. scipy.signal.hilbert is an additional cross-check.
Run this file to regenerate filter_reference.json (NumPy + SciPy).
"""
import json, pathlib
import numpy as np
from scipy.signal import hilbert

def dft(x, length):
    return np.exp(-2j*np.pi*np.outer(np.arange(length), np.arange(len(x)))/length)@x

def idft(X, count):
    return (np.exp(2j*np.pi*np.outer(np.arange(count), np.arange(len(X)))/len(X))@X)/len(X)

def analytic(x):
    n=len(x); h=np.zeros(n); h[0]=1
    h[1:(n+1)//2]=2
    if n%2==0:h[n//2]=1
    return idft(dft(x,n)*h,n)

cases=[]; max_cross=0
for n in [15,16,33,64]:
 for method in ['gaussian','taner']:
  for q in ([12] if method=='gaussian' else [2,12]):
   dt=.75; x=np.arange(n)*dt
   y=3.2+1.7*np.cos(2*np.pi*.19*x+.4)+.3*np.sin(2*np.pi*.38*x)+.01*np.arange(n)
   nfft=10*2**int(np.ceil(np.log2(n))) if method=='gaussian' else n
   low,high=.1,.3
   frequency=np.minimum(np.arange(nfft),nfft-np.arange(nfft))/(nfft*dt)
   exponent=2 if method=='gaussian' else np.log2(1+q*np.log(10)/(np.log(2)/2))
   response=np.exp(-np.log(2)/2*(np.abs(frequency-(low+high)/2)/((high-low)/2))**exponent)
   filtered=idft(dft(y-y.mean(),nfft)*response,n).real
   a=analytic(filtered)
   max_cross=max(max_cross,float(np.max(np.abs(a-hilbert(filtered)))))
   cases.append(dict(n=n,spacing=dt,method=method,q=q,low=low,high=high,nfft=nfft,
                     input=y.tolist(),filtered=filtered.tolist(),envelope=np.abs(a).tolist(),quadrature=a.imag.tolist()))
result={'oracle':'Explicit complex-exponential DFT matrix; independent from production FFT',
        'additional_check':'scipy.signal.hilbert', 'maximum_hilbert_cross_difference':max_cross,'cases':cases}
pathlib.Path(__file__).with_name('filter_reference.json').write_text(json.dumps(result,indent=2)+'\n')
print('Cases',len(cases),'max SciPy difference',max_cross)
