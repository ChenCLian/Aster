"""Independent symmetric-DPSS reference; only needed to regenerate the fixture.
Requires NumPy and SciPy. C++ tests consume the checked-in JSON, not Python.
"""
import json
from pathlib import Path
import numpy as np
from scipy.signal.windows import dpss
from scipy.fft import next_fast_len

n, nw, k = 256, 3, 5
x = np.arange(n, dtype=float)
y = np.sin(2*np.pi*x/16) + .4*np.cos(2*np.pi*x/39) + .001*x
centered = y - np.mean(y)
variance = np.mean(centered**2)
tapers, ratios = dpss(n, nw, Kmax=k, sym=True, norm=2, return_ratios=True)
# Aster legacy convention: zero-pad to the next 2/3/5-smooth length
# at least max(4*N, 512); this is denser sampling, not better resolution.
nfft = next_fast_len(max(4*n, 512), real=True)
individual = np.abs(np.fft.rfft(tapers * centered, n=nfft, axis=1))**2
power = individual[:2].mean(axis=0)
for iteration in range(1000):
    weights = (power[None, :] / (ratios[:, None] * power[None, :] + variance * (1-ratios[:, None])))**2 * ratios[:, None]
    new = np.sum(weights * individual, axis=0) / np.sum(weights, axis=0)
    difference = np.mean(np.abs(new-power))
    power = new
    if difference < .0005 * variance / power.size:
        break
power[1:-1] *= 2 # Convert only after adaptive weighting.
Path(__file__).with_name('mtm_scipy.json').write_text(json.dumps({
    'description': 'SciPy symmetric unit-energy DPSS + NumPy real FFT; adaptive Thomson one-sided PSD, fs=1',
    'n': n, 'nfft': nfft, 'nw': nw, 'tapers': k, 'iterations': iteration+1,
    'input': y.tolist(), 'power': power.tolist()
}, indent=2)+'\n', encoding='utf-8')
