"""Independent oracle: integrate instantaneous flux numerically over hour angle.
Requires NumPy/SciPy only to regenerate; C++ tests use the checked-in fixture.
La2004 source bytes remain the official IMCCE file, including D exponents.
"""
from pathlib import Path
import json, math
import numpy as np
from scipy.integrate import quad
from scipy.optimize import brentq
root = Path(__file__).resolve().parents[2]
a = np.loadtxt((root/'resources/astronomy/la2004/INSOLN.LA2004.BTL.ASC').read_text().replace('D','E').splitlines())
cases = []
for age, lat, solar_long in [(0,65,90),(0,65,270),(100,65,90),(1000,-45,180),(2000,0,0),(50000,65,90),(50,90,90),(50,90,270),(10,-90,270),(10,-90,0),(150,80,30),(150,-80,30)]:
    e,eps,peri = a[age,1:]
    phi,lam = np.deg2rad([lat,solar_long])
    delta = math.asin(math.sin(eps)*math.sin(lam))
    # Geocentric solar perihelion = Earth's heliocentric perihelion + pi.
    nu = lam - (peri + math.pi)
    distance = (1-e*e)/(1+e*math.cos(nu))
    def instant(h):
        zenith_cos = math.sin(phi)*math.sin(delta) + math.cos(phi)*math.cos(delta)*math.cos(h)
        return 1365/distance**2 * max(0,zenith_cos)
    # Explicitly split at the daylight transition, found by root search.
    # Otherwise adaptive quadrature can underestimate its error at a clipped horizon.
    def altitude(h):
        return math.sin(phi)*math.sin(delta) + math.cos(phi)*math.cos(delta)*math.cos(h)
    points = [0]
    if altitude(0)*altitude(math.pi) < 0:
        horizon = brentq(altitude,0,math.pi,xtol=1e-14)
        points += [-horizon,horizon]
    value,err = quad(instant,-math.pi,math.pi,epsabs=1e-9,epsrel=1e-12,limit=400,points=points)
    assert err/(2*math.pi) < 1e-7
    cases.append({'age_ka':age,'latitude_deg':lat,'solar_longitude_deg':solar_long,'solar_constant':1365,'expected':value/(2*math.pi)})
knots=[{'age_ka':i,'eccentricity':float(a[i,1]),'obliquity':float(a[i,2]),'perihelion':float(a[i,3])} for i in [0,1,2,100,1000,2000,50000,51000]]
(Path(__file__).parent/'astronomy_reference.json').write_text(json.dumps({'oracle':'SciPy quadrature of instantaneous top-of-atmosphere flux over a complete rotation','knots':knots,'insolation':cases},indent=2)+'\n')
