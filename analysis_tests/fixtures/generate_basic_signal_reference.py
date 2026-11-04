"""Independent NumPy/SciPy fixtures, not needed to build/run Aster."""
from pathlib import Path
import json
import numpy as np
import scipy
from scipy.signal import savgol_filter, periodogram, welch
cases=[]
for n,w,p in [(31,5,2),(32,7,3),(47,11,4),(51,51,5),(20,1,0)]:
 x=np.arange(n);y=.03*x+np.sin(.7*x)+.2*np.cos(.19*x)
 for sg in [False,True]:
  expected=savgol_filter(y,w,p,mode='interp') if sg else np.array([y[max(0,i-w//2):min(n,i+w//2+1)].mean() for i in range(n)])
  cases.append(dict(kind='sg' if sg else 'mean',y=y.tolist(),window=w,degree=p,result=expected.tolist()))
for n,dt,length,overlap,nfft,taper,trend in [(64,.5,32,16,32,'hann','constant'),(79,2.,31,9,63,'boxcar','linear'),(129,.125,64,0,128,'hann','constant'),(47,1.,47,0,47,'boxcar','constant')]:
 t=np.arange(n)*dt;y=.07*t+1.2*np.sin(2*np.pi*.13*np.arange(n))+.4*np.cos(.62*np.arange(n))
 for method in ['fft','welch']:
  if method=='welch':f,p=welch(y,fs=1/dt,window=taper,nperseg=length,noverlap=overlap,nfft=nfft,detrend=trend,scaling='density',average='mean')
  else:f,p=periodogram(y,fs=1/dt,window=taper,nfft=max(n,nfft),detrend=trend,scaling='density')
  cases.append(dict(kind=method,y=y.tolist(),dt=dt,window=length,overlap=overlap,nfft=nfft if method=='welch' else max(n,nfft),taper=taper,trend=trend,frequency=f.tolist(),power=p.tolist()))
Path(__file__).with_name('basic_signal_reference.json').write_text(json.dumps(dict(reference='NumPy/SciPy; no product code',numpy=np.__version__,scipy=scipy.__version__,cases=cases),indent=2)+'\n')
print(len(cases),'independent cases')
