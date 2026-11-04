"""Independent test fixtures: SciPy periodogram/DPSS + full complex NumPy FFT.
Regeneration only; neither Python nor SciPy is an application dependency.
"""
import json
from pathlib import Path
import numpy as np
import scipy
from scipy.signal import periodogram, detrend
from scipy.signal.windows import dpss


def adaptive_mtm(segment, fs, nw, nfft):
    """Symmetric DPSS and full-frequency Thomson iteration (no Aster calls)."""
    k = 2 if nw == 1 else int(np.floor(2 * nw + 0.5)) - 1
    tapers, eigenvalues = dpss(len(segment), nw, Kmax=k, sym=True, norm=2, return_ratios=True)
    centered = segment - segment.mean()
    variance = np.mean(centered ** 2)
    if variance == 0:
        return np.zeros(nfft // 2 + 1)
    individual = np.abs(np.fft.fft(tapers * centered, n=nfft, axis=1)) ** 2
    power = individual[:2].mean(axis=0)
    for _ in range(1000):
        denominator = eigenvalues[:, None] * power + (1 - eigenvalues[:, None]) * variance
        weights = eigenvalues[:, None] * np.divide(power, denominator, out=np.zeros_like(denominator), where=denominator > 0) ** 2
        updated = (weights * individual).sum(axis=0) / weights.sum(axis=0)
        distance = np.abs(updated - power).sum()
        power = updated
        if distance < 0.0005 * variance:
            break
    power = power[:nfft // 2 + 1] / fs
    power[1: nfft // 2 if nfft % 2 == 0 else None] *= 2
    return power

rng = np.random.default_rng(74021)
cases = []
for method, length, step, nfft, dt, taper, trend, nw, ratio in [
    ('fft', 32, 9, 32, .25, 'hann', 'mean', 2, 1),
    ('fft', 31, 10, 31, .8, 'hann', 'linear', 2, 1),
    ('fft', 16, 16, 48, 2, 'boxcar', 'mean', 2, 1),
    ('fft', 23, 7, 47, .13, 'boxcar', 'linear', 2, .65),
    ('mtm', 32, 13, 32, .25, 'hann', 'mean', 2, 1),
    ('mtm', 31, 9, 47, 2, 'hann', 'linear', 2, 1),
    ('mtm', 32, 8, 64, .5, 'hann', 'mean', 1, .6),
    ('mtm', 41, 17, 83, .7, 'hann', 'mean', 3, 1),
]:
    n = length * 3 + 5
    x = np.arange(n)
    y = .4 * rng.normal(size=n) + np.sin(2*np.pi*x/9) + .002*x*x
    frequencies = np.fft.rfftfreq(nfft, dt)
    keep = frequencies <= .5/dt * ratio + (1/dt/nfft) * 1e-10
    starts = list(range(0, n-length+1, step))
    psds = []
    for start in starts:
        segment = y[start:start+length]
        if method == 'fft':
            _, power = periodogram(segment, fs=1/dt, window=taper, nfft=nfft,
                detrend='constant' if trend == 'mean' else 'linear', scaling='density')
        else:
            if trend == 'linear': segment = detrend(segment, type='linear')
            power = adaptive_mtm(segment, 1/dt, nw, nfft)
        psds.append(power[keep])
    grid = np.asarray(psds).T
    cases.append(dict(name=f'{method}_{length}_{nfft}_{trend}', method=method, length=length,
        step=step, nfft=nfft, dt=dt, taper=taper, trend=trend, nw=nw, ratio=ratio, input=y.tolist(),
        centers=((np.array(starts)+(length-1)/2)*dt).tolist(), frequency=frequencies[keep].tolist(),
        power=grid.ravel().tolist(), mean_power=grid.mean(axis=1).tolist(), tail=n-(starts[-1]+length)))
Path(__file__).with_name('evolution_reference.json').write_text(json.dumps({
    'reference': 'SciPy periodogram and symmetric DPSS; independent full complex FFT adaptive MTM',
    'numpy_version': np.__version__, 'scipy_version': scipy.__version__, 'cases': cases}, indent=2)+'\n')
