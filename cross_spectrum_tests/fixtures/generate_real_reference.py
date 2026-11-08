"""Generate cross-spectrum reference cases from the bundled, attributed ODP tables.

SciPy/NumPy are validation dependencies only. Source bytes are checked against
resources/examples/catalog.json. This does not estimate causal lead/lag or
significance, and the published tuned age models are not independent evidence.
"""
# Copyright (C) 2026 ZiJie Chen
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import hashlib
import json
import numpy as np
import scipy
from scipy import signal

root = Path(__file__).resolve().parents[2] / 'resources/examples'
catalog = json.loads((root/'catalog.json').read_text())
def load(key):
    entry = next(e for e in catalog['examples'] if e['id'] == key)
    spec = catalog['datasets'][entry['dataset']]
    raw = (root/(entry['dataset']+'.tsv')).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec['sha256']
    rows = [r.split('\t') for r in raw.decode().split('*/\n', 1)[1].splitlines()[1:] if r]
    xy = np.array([(float(r[spec['age_column']]), float(r[entry['y_column']]))
                   for r in rows if r[spec['age_column']] and r[entry['y_column']]])
    # The application preprocessing sorts and averages duplicate coordinates.
    x, inv = np.unique(xy[:,0], return_inverse=True)
    y = np.bincount(inv, weights=xy[:,1])/np.bincount(inv)
    return x, y, dict(id=key, doi=spec['doi'], sha256=spec['sha256'],
                     citation=spec['citation'], license=spec['license'],
                     chronology=spec['chronology'], x_name=spec['age_name'],
                     x_unit=spec['age_unit'], y_name=entry['y_name'], y_unit=entry['y_unit'])

records=[]
for aid, bid, dt, mode in [('odp846_d18o','odp846_d13c',1.,'native_fixed_resample'),
                          ('odp926_d13c','odp926_lightness',5.,'explicit_common_grid')]:
    xa,ya,ma=load(aid);xb,yb,mb=load(bid)
    start=max(xa[0],xb[0]);end=min(xa[-1],xb[-1])
    x=start+np.arange(int(np.floor((end-start)/dt))+1)*dt
    a=np.interp(x,xa,ya);b=np.interp(x,xb,yb)
    cases=[]
    for seg,overlap,nfft,win,trend in [(128,64,128,'hann','constant'),
                                     (127,63,255,'hann','linear'),
                                     (256,0,512,'boxcar',False),
                                     (64,48,64,'boxcar','constant')]:
        window=signal.windows.hann(seg,sym=False) if win=='hann' else np.ones(seg)
        opt=dict(fs=1/dt,window=window,nperseg=seg,noverlap=overlap,nfft=nfft,detrend=trend)
        f,ab=signal.csd(a,b,**opt,scaling='density',average='mean')
        _,aa=signal.welch(a,**opt,scaling='density',average='mean')
        _,bb=signal.welch(b,**opt,scaling='density',average='mean')
        _,coh=signal.coherence(a,b,**opt)
        cases.append(dict(name=f'{aid}_{seg}_{win}_{trend}',
           parameters=dict(psd_window=seg,psd_overlap=overlap,psd_nfft=nfft,
                           psd_taper='hann' if win=='hann' else 'rectangular',
                           psd_trend='none' if trend is False else 'mean' if trend=='constant' else 'linear'),
           columns=['frequency','cross_magnitude','coherence','phase_degrees','cross_real','cross_imaginary','auto_a','auto_b'],
           rows=np.column_stack((f,np.abs(ab),coh,np.angle(ab,deg=True),ab.real,ab.imag,aa,bb)).tolist()))
    records.append(dict(a_source=ma,b_source=mb,alignment=mode,spacing=dt,
                        x=x.tolist(),a=a.tolist(),b=b.tolist(),cases=cases))
output=dict(schema=1,numpy=np.__version__,scipy=scipy.__version__,records=records,
            interpretation='Software validation on published proxy records; no significance or causal interpretation.')
Path(__file__).with_name('real_reference.json').write_text(json.dumps(output,ensure_ascii=False,allow_nan=False,separators=(',',':'))+'\n')
print('Generated',sum(len(r['cases']) for r in records),'real-data reference cases')
