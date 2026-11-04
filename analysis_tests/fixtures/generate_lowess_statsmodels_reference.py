"""Offline independent LOWESS reference; not an application dependency.

Regenerate with Python 3, NumPy and statsmodels==0.14.6.
Neither Aster nor CppLowess is imported or executed.
"""
import json
from pathlib import Path
import numpy as np
import statsmodels
from statsmodels.nonparametric.smoothers_lowess import lowess

assert statsmodels.__version__ == '0.14.6'
rng = np.random.default_rng(901)
cases = []

def cleveland_wls(x, y, fraction, iterations):
    """NETLIB conventions, solved independently by dense NumPy least squares.

    Used for the two sparse-robust-neighborhood cases where statsmodels deliberately
    falls back to the observed y with fewer than two nonzero weights. NETLIB uses
    a weighted constant fit when local X spread is small, including one neighbor.
    """
    x = (x-x[0])/(x[-1]-x[0])
    n = len(x)
    count = max(2, min(n, int(n*fraction)))
    robust = np.ones(n)
    for iteration in range(iterations+1):
        fit = []
        for xi, yi in zip(x, y):
            distance = np.abs(x-xi)
            radius = np.partition(distance, count-1)[count-1]
            weights = np.where(distance <= .001*radius, 1.,
                np.where(distance <= .999*radius, (1-(distance/radius)**3)**3, 0.)) * robust
            if weights.sum() == 0:
                fit.append(yi)
                continue
            weights /= weights.sum()
            center = np.sum(weights*x)
            spread = np.sqrt(np.sum(weights*(x-center)**2))
            if spread <= .001:
                fit.append(np.sum(weights*y))
            else:
                design = np.column_stack((np.ones(n), x-xi))
                fit.append(np.linalg.lstsq(design*np.sqrt(weights[:,None]),
                    y*np.sqrt(weights), rcond=None)[0][0])
        fit = np.asarray(fit)
        if iteration < iterations:
            residual = np.abs(y-fit)
            scale = 6*np.median(residual)
            if scale == 0:
                robust = (residual == 0).astype(float)
            else:
                robust = np.where(residual <= .001*scale, 1.,
                    np.where(residual > .999*scale, 0., (1-(residual/scale)**2)**2))
    return fit

def add(name, x, y, fraction, iterations, reference_x=None):
    fit = lowess(y, x if reference_x is None else reference_x,
                 frac=fraction, it=iterations, delta=0., is_sorted=True,
                 missing='raise', return_sorted=False)
    assert np.isfinite(fit).all()
    reference_method = 'statsmodels'
    statsmodels_fit = None
    if name in ('31_regular_0.2_1', '31_regular_0.2_3'):
        statsmodels_fit = fit.tolist()
        fit = cleveland_wls(x, y, fraction, iterations)
        reference_method = 'NETLIB rules with independent NumPy weighted least squares'
    cases.append(dict(name='external_lowess_' + name, method='detrend_lowess',
        x=x.tolist(), y=y.tolist(),
        parameters=dict(fraction=fraction, iterations=iterations),
        # NETLIB clips tricube/bisquare weights near 0 and 1; statsmodels
        # evaluates them directly. Small differences are expected, not bugs.
        absolute_tolerance=2e-6, relative_tolerance=1e-8,
        reference_method=reference_method, statsmodels_variant=statsmodels_fit,
        reference=dict(state='Succeeded', expected={
            'trend': {'y': fit.tolist()}, 'detrended': {'y': (y-fit).tolist()}})))

for n in (31, 128):
    for grid in ('regular', 'irregular'):
        x = np.arange(n, dtype=float) if grid == 'regular' else np.cumsum(rng.uniform(.3, 1.3, n))
        y = np.sin(x*.2) + x*.01 + rng.normal(0, .05, n)
        y[n//3] += 10
        for fraction in (.2, .35, .7):
            for iterations in (0, 1, 3):
                add(f'{n}_{grid}_{fraction}_{iterations}', x, y, fraction, iterations)

x = np.arange(128, dtype=float)
y = np.sin(x*.2) + x*.01
for iterations in (0, 3):
    for name, xx in [('translated', x+1e12), ('tiny_unit', x*1e-200),
                     ('large_unit', x*1e200), ('span_overflow', (2*x/127-1)*1e308)]:
        add(f'{name}_{iterations}', xx, y, .35, iterations, reference_x=x)

document = dict(generator=Path(__file__).name,
    reference_library='statsmodels ' + statsmodels.__version__,
    numpy_version=np.__version__, seed=901,
    source='https://www.statsmodels.org/v0.14.6/generated/statsmodels.nonparametric.smoothers_lowess.lowess.html',
    coordinate_tests='Positive affine X changes must preserve the fit; expected results use ordinary-sized reference X.',
    cases=cases)
Path(__file__).with_name('lowess_statsmodels_reference.json').write_text(
    json.dumps(document, indent=2, allow_nan=False) + '\n')
print(len(cases), 'independent LOWESS cases')
