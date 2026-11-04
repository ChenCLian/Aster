import argparse, json, math, sys
from pathlib import Path
import numpy as np
from scipy.interpolate import CubicSpline
from scipy.integrate import cumulative_trapezoid
from scipy.signal import lfilter

ROOT=Path(__file__).resolve().parents[2]
MASK=(1<<64)-1
class Normal:
    def __init__(self, seed): self.state=seed; self.spare=None
    def bits(self):
        self.state=(self.state+0x9e3779b97f4a7c15)&MASK
        z=self.state; z=((z^(z>>30))*0xbf58476d1ce4e5b9)&MASK
        z=((z^(z>>27))*0x94d049bb133111eb)&MASK
        return z^(z>>31)
    def uniform(self): return ((self.bits()>>11)+.5)*2.**-53
    def normal(self):
        if self.spare is not None:
            v=self.spare; self.spare=None; return v
        r=math.sqrt(-2*math.log(self.uniform())); a=2*math.pi*self.uniform()
        self.spare=r*math.sin(a); return r*math.cos(a)

def draw(model,n,seed,index):
    mix=Normal(seed ^ ((index*0xd2b74407b1ce6e93)&MASK))
    freq,coeff,noise,rate=[Normal(mix.bits()) for _ in range(4)]
    fs=[]
    for mean,sd in zip(model['frequency_mean_arcsec_yr'],model['frequency_sd_arcsec_yr']):
        z=freq.normal()
        while abs(z)>3: z=freq.normal()
        fs.append(mean+sd*z)
    s=model['smooth_logit']; x=np.linspace(0,1,8)
    cov=np.exp(-.5*((x[:,None]-x[None,:])/s['length_scale'])**2)+np.eye(8)*1e-8
    intercept=s['intercept_sd']*rate.normal(); slope=s['slope_sd']*rate.normal()
    logits=intercept+slope*(x-.5)+s['residual_sd']*(np.linalg.cholesky(cov)@np.array([rate.normal() for _ in x]))
    return dict(frequency=fs,unit_rates=(1/(1+np.exp(-logits))).tolist(),beta=[coeff.normal() for _ in range(20)],white_noise=[noise.normal() for _ in range(n+256)])

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--original',type=Path);args=parser.parse_args()
    model=json.loads((ROOT/'resources/synthetic/model.json').read_text());cases=[]
    if args.original:
        sys.path.insert(0,str(args.original.resolve()))
        from synthetic_core.vendor import shared_functions, frequency_model
    for name,depth,seed,index,rho,snr in [
        ('template',np.loadtxt(ROOT/'resources/synthetic/depth_m.txt'),20261104,0,.5,2),
        ('uniform',np.linspace(-5,25,257),17,3,-.65,4),
        ('clipped',np.linspace(0,100,129),98765,9,.95,1)]:
        latent=draw(model,len(depth),seed,index)
        if name=='clipped':latent['unit_rates']=[0,1,0,1,0,1,0,1]
        options=dict(rate_min=1,rate_max=5,rho=rho,snr=snr)
        knots=.02+np.array(latent['unit_rates'])*.08
        grid=np.linspace(depth[0],depth[-1],8)
        inv_raw=CubicSpline(grid,knots)(depth); inv=np.clip(inv_raw,.02,.1)
        age=cumulative_trapezoid(inv,depth,initial=0)
        f=np.array(latent['frequency']);p,g1,g2,g3,g4=f;g5=model['g5']
        w=np.array([p+g1,p+g2,p+g3,p+g4,p+g5,g2-g5,g4-g5,g4-g2,g3-g5,g3-g2])*np.pi*1e6/(180*3600)
        design=np.concatenate([np.sin(age[:,None]*w),np.cos(age[:,None]*w)],axis=1)
        if args.original:
            sf=frequency_model.get_freqs_model(f*np.pi*1e6/(180*3600),True,True,False,g5_value=g5*np.pi*1e6/(180*3600))
            np.testing.assert_allclose(sf,w,rtol=2e-15)
            np.testing.assert_allclose(shared_functions.interpolate_CubicSpline([grid,knots],depth),inv_raw,rtol=0,atol=0)
            np.testing.assert_allclose(shared_functions.generate_X_linReg(np.ones(10),w,age),design,rtol=0,atol=0)
        clean=design@np.array(latent['beta']); clean=(clean-clean.mean())/clean.std()
        noise=lfilter([1.],[1.,-rho],latent['white_noise'])[256:];noise=(noise-noise.mean())/noise.std()/np.sqrt(snr)
        raw=clean+noise;proxy=(raw-raw.mean())/raw.std()
        expected=dict(age_ka=age*1000,rate=.1/inv,clean=clean,noise=noise,proxy=proxy,angular_frequency=w,inverse_knots=knots,
                      raw_mean=float(raw.mean()),raw_sd=float(raw.std()),clipped_fraction=float(np.mean((inv_raw<.02-32*np.finfo(float).eps*.1)|(inv_raw>.1+32*np.finfo(float).eps*.1))))
        expected={k:v.tolist() if isinstance(v,np.ndarray) else v for k,v in expected.items()}
        cases.append(dict(name=name,depth=depth.tolist(),seed=seed,index=index,options=options,latent=latent,expected=expected,compare_draw=name!='clipped'))
    doc=dict(reference='NumPy/SciPy CubicSpline not-a-knot, cumulative_trapezoid, lfilter; fixed latent variables',
             original_functions_verified=bool(args.original),model=model,cases=cases)
    path=Path(__file__).with_name('synthetic_reference.json');path.write_text(json.dumps(doc,separators=(',',':'))+'\n')
    print(path, path.stat().st_size, 'bytes; upstream functions verified:', bool(args.original))
if __name__=='__main__':main()
