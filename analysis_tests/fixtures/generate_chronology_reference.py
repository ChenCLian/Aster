"""Independent chronology fixtures: Decimal (70 digits) and SciPy quadrature.
Run explicitly to regenerate; not required to build Aster. No Aster calls.
"""
from decimal import Decimal, localcontext
from pathlib import Path
import json
import scipy
from scipy.integrate import quad

def D(x):
    return Decimal(str(x))

cases = []
with localcontext() as ctx:
    ctx.prec = 70
    for name, x, r, factor, start in [
        ('constant', [12, 12.5, 13, 15], [2, 2, 2, 2], 100, 7),
        ('nonuniform', [-3, -1, 0, 4, 6], [.25, 2, .5, 8, 3], 1, -20),
        ('unit_Ma', [0, 1, 3, 6], [1, 4, 2, .5], .001, 2),
        ('near_equal', [0, 1, 2], [1, 1.000000000001, .999999999999], 1, 0),
        ('large_rate_ratio', [0, 1, 3], [1e-6, 1e6, 2e6], 1, 0),
        ('decreasing', [0, 1, 2, 4], [20, 10, 1, .25], 1, 10),
    ]:
        for rule in ['left', 'linear_rate']:
            ages = [D(start)]
            for i in range(1, len(x)):
                dz, a, b = D(x[i])-D(x[i-1]), D(r[i-1]), D(r[i])
                integral = dz/a if rule == 'left' or a == b else dz*(b.ln()-a.ln())/(b-a)
                ages.append(ages[-1] + D(factor)*integral)
                # Independent numerical integration for well-conditioned rates.
                if rule == 'linear_rate' and max(float(a/b),float(b/a)) < 100:
                    q, err = quad(lambda t: 1/(float(a)+(float(b)-float(a))*t), 0, 1,
                                  epsabs=1e-12, epsrel=1e-12)
                    assert abs(q*float(dz)-float(integral)) < 1e-10
            cases.append(dict(kind='rate',name=name+'_'+rule,x=x,rate=r,factor=factor,
                              first_age=start,rule=rule,age=list(map(float,ages))))
    for name, x, d, a in [
        ('knots', [0, 1, 2, 4, 8], [0, 1, 2, 4, 8], [0, 2, 5, 8, 12]),
        ('piecewise', [0, .25, .5, 1.5, 3, 7, 8], [0, 1, 2, 4, 8], [-10, 0, 2, 3, 10]),
        ('extrapolate', [-3, -1, 0, .5, 2, 4, 6], [0, 1, 4], [10, 11, 20]),
        ('nonzero_origin', [100, 103, 111, 117, 120], [100, 110, 120], [400, 430, 450]),
    ]:
        ages=[]
        for z in x:
            j = 0
            while j+2 < len(d) and z > d[j+1]: j += 1
            ages.append(D(a[j])+(D(z)-D(d[j]))*(D(a[j+1])-D(a[j]))/(D(d[j+1])-D(d[j])))
        rates=[(D(d[i+1])-D(d[i]))/(D(a[i+1])-D(a[i])) for i in range(len(d)-1)]
        cases.append(dict(kind='map',name=name,x=x,depth=d,controls_age=a,age=list(map(float,ages)),
                          interval_rate=list(map(float,rates)),extrapolated=sum(z<d[0] or z>d[-1] for z in x)))
result=dict(reference='Python Decimal 70 digits; scipy.integrate.quad cross-check on ordinary linear-rate cases',
            scipy=scipy.__version__,cases=cases)
Path(__file__).with_name('chronology_reference.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'{len(cases)} chronology fixtures generated')
