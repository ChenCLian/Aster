from pathlib import Path
import argparse, ctypes as c, numpy as np, scipy, scipy.signal as sig, json
parser=argparse.ArgumentParser(description='Independent SciPy spectral-peak comparison')
parser.add_argument('library', type=Path, help='Compiled reference_bridge DLL/shared library')
parser.add_argument('--output', type=Path, default=Path('peak_reference.json'))
args=parser.parse_args()
lib=c.CDLL(str(args.library.resolve())); f=lib.compare_peaks
P=c.POINTER(c.c_double);I=c.POINTER(c.c_int)
f.argtypes=[c.c_int,P,c.c_double,c.c_double,c.c_int,I,P,P];f.restype=c.c_int
rng=np.random.default_rng(3403);count=0
for trial in range(300):
 n=int(rng.integers(20,400));y=rng.normal(size=n)
 if trial%4==0:y=np.round(y,1) # plateaus
 if trial%4==1:y=np.exp(-np.arange(n)/20)*(1+.05*rng.random(n))
 scale=10.**int(rng.integers(-150,150));y=np.asarray(y*scale,dtype='float64')
 for rel in [0,.001,.05]:
  absolute=.01*scale if trial%3==0 else 0
  radius=0 if trial%2 else 12
  threshold=max(absolute,rel*(y.max()-y.min()),64*np.finfo(float).eps*np.max(np.abs(y)))
  expected,props=sig.find_peaks(y,prominence=threshold,width=0,wlen=2*radius+1 if radius else None)
  indices=np.empty(n,np.int32);prom=np.empty(n);width=np.empty(n)
  size=f(n,y.ctypes.data_as(P),rel,absolute,radius,indices.ctypes.data_as(I),prom.ctypes.data_as(P),width.ctypes.data_as(P))
  assert size>=0 and np.array_equal(indices[:size],expected),(trial,rel,size,expected,indices[:size])
  np.testing.assert_allclose(prom[:size],props['prominences'],rtol=2e-13,atol=0)
  np.testing.assert_allclose(width[:size],props['widths'],rtol=2e-12,atol=2e-12)
  count+=1
out={'reference':'scipy.signal.find_peaks / peak_prominences / peak_widths','scipy':scipy.__version__,'cases':count,'all_passed':True,'seed':3403,'coverage':'random, decay tails, plateaus, units 1e-150..1e149, absolute and relative prominence, local/full radius; distance=1 (Aster suppression order intentionally differs)','prominence_rtol':2e-13,'width_rtol':2e-12}
args.output.write_text(json.dumps(out,indent=2)+'\n');print(out)
