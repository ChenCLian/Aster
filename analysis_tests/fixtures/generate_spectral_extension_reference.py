"""Independent GLS least squares / SciPy filters. Not a runtime dependency."""
from pathlib import Path
import json
import numpy as np
import scipy
from scipy.signal import lombscargle, butter, sosfiltfilt, freqz_sos
cases=[]
for n,dt,origin,irregular,spp,lo,hi in [(41,1.,0.,True,5,0.,0.),(64,.5,-250.,True,4,.07,.85),(91,2.,1000.,True,7,.008,.21),(80,1.,0.,False,5,.03,.43),(101,.25,1e5,True,3,.1,1.7),(19,1.,-13.,True,10,.001,.4)]:
 i=np.arange(n,dtype=float)
 t=np.cumsum(dt*(1+.27*np.sin(i*.87)+.08*np.cos(i*.4))) if irregular else i*dt
 x=t+origin;y=4+1.3*np.sin(2*np.pi*.133*t/dt+.4)+.2*np.cos(.37*i)+.003*i
 duration=x[-1]-x[0];step=1/duration/spp
 low=lo or 1/duration;high=hi or .5/np.median(np.diff(x))
 f=low+step*np.arange(int(np.floor((high-low)/step+1e-10))+1)
 yc=y-y.mean();total=yc@yc;power=[]
 for frequency in f:
  phase=2*np.pi*frequency*(x-x[0]);a=np.column_stack((np.ones(n),np.cos(phase),np.sin(phase)))
  coef=np.linalg.lstsq(a,yc,rcond=1e-12)[0];residual=yc-a@coef
  power.append(1-(residual@residual)/total)
 power=np.array(power);independent=lombscargle(x-x[0],y,2*np.pi*f,normalize=True,floating_mean=True)
 assert np.allclose(power,independent,rtol=1e-8,atol=1e-10),np.max(abs(power-independent))
 cases.append(dict(kind='lomb',x=x.tolist(),y=y.tolist(),minimum=lo,maximum=hi,samples_per_peak=spp,frequency=f.tolist(),power=power.tolist(),scipy_crosscheck_max_abs=float(np.max(abs(power-independent)))))
for order,n,dt,ratio in [(1,73,1.,.2),(2,80,.5,.3),(3,97,2.,.1),(4,128,.25,.4),(5,151,1.,.02),(8,193,.5,.8),(10,256,1.,.98),(10,512,1.,.0001),(10,512,1.,.9999)]:
 i=np.arange(n,dtype=float);y=2+.01*i+np.sin(.13*i)+.3*np.cos(.8*i);y[n//3]+=1.7
 cutoff=ratio*(.5/dt)
 f=np.unique(np.r_[np.linspace(0,.5/dt,513),cutoff])
 for high in [False,True]:
  sos=butter(order,cutoff,btype='highpass' if high else 'lowpass',fs=1/dt,output='sos')
  filtered=sosfiltfilt(sos,y,padtype='odd',padlen=3*(order+1))
  _,h=freqz_sos(sos,worN=f,fs=1/dt)
  cases.append(dict(kind='highpass' if high else 'lowpass',order=order,dt=dt,cutoff=cutoff,y=y.tolist(),filtered=filtered.tolist(),frequency=f.tolist(),gain=(abs(h)**2).tolist(),edge_stress=ratio<.001 or ratio>.999))
Path(__file__).with_name('spectral_extension_reference.json').write_text(json.dumps(dict(reference='Independent NumPy linear least squares and SciPy lombscargle/butter/sosfiltfilt/freqz_sos; no product code',numpy=np.__version__,scipy=scipy.__version__,cases=cases),indent=2)+'\n')
print(len(cases),'cases; GLS crosscheck max abs',max(c['scipy_crosscheck_max_abs'] for c in cases if c['kind']=='lomb'))
