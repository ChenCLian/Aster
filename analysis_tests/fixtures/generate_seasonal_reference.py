"""Independent time-weighted reference; SciPy quadrature + root finding.

Uses synthetic orbital elements (no Aster code or La2004 interpolation),
retains the separate solar-distance and Kepler-time factors, and obtains
calendar longitude by inverting the numerical time integral, not Kepler's
algebraic equation. Developer-only dependency: scipy; no runtime dependency.
"""
import json, math
from pathlib import Path
from scipy.integrate import quad
from scipy.optimize import brentq
PI = math.pi
YEAR = 365.24219876

def dt(o, longitude):
    e, _, p = o
    return YEAR / (2*PI) * (1-e*e)**1.5 / (1-e*math.cos(longitude-p))**2

def flux(o, latitude, longitude, s0):
    e, obliquity, p = o
    delta = math.asin(math.sin(obliquity)*math.sin(longitude))
    a, b = math.sin(latitude)*math.sin(delta), math.cos(latitude)*math.cos(delta)
    if abs(b) < 1e-14:
        geometry = max(0, a)
    else:
        h = math.acos(max(-1, min(1, -a/b)))
        geometry = max(0, (h*a + b*math.sin(h))/PI)
    return s0 * ((1-e*math.cos(longitude-p))/(1-e*e))**2 * geometry

def integrate(f, a, b):
    # Break at quadrants for polar-day/night transitions; QUADPACK differs
    # from the production adaptive-Simpson implementation.
    points = [q*PI/2 for q in range(-8, 17) if a < q*PI/2 < b]
    return quad(f, a, b, epsabs=2e-7, epsrel=2e-11, points=points, limit=300)[0]

def longitude_day(o, day):
    target = (day-80)*YEAR/365
    return brentq(lambda l: integrate(lambda x: dt(o,x),0,l)-target, -2*PI,2*PI,xtol=1e-14) % (2*PI)

cases=[]
for o, lat, a, b in [((.0167,.4091,1.7966),65,90,180),((.07,.43,5.8),65,270,90),
                     ((.25,.39,2.1),-50,350,10),((.05,.41,0.7),90,0,360),
                     ((.05,.41,0.7),-90,0,90),((0,0,0),0,90,180),
                     ((.6,.6,5.3),30,30,300),((.08,.4,1.2),80,270,360)]:
    a,b=math.radians(a),math.radians(b)
    if b<=a:b+=2*PI
    duration=integrate(lambda x:dt(o,x),a,b)
    energy=integrate(lambda x:flux(o,math.radians(lat),x,1365)*dt(o,x),a,b)*86400/1e6
    cases.append(dict(orbit=o,lat=lat,start=a,end=b%(2*PI) if b>2*PI else b,mean=energy*1e6/(duration*86400),energy=energy,duration=duration))
dates=[]
for o in [(.0167,.4091,1.7966),(.07,.43,5.8),(.6,.6,5.3)]:
    for day in [1,80,172,365]:
        dates.append(dict(orbit=o,day=day,longitude=longitude_day(o,day)))
calendar=[]
for o,lat,first,last in [((.0167,.4091,1.7966),65,152,244),((.07,.43,5.8),-40,335,60)]:
    a,b=longitude_day(o,first-.5),longitude_day(o,last-.5)
    if b<=a:b+=2*PI
    duration=((last-first)%365)*YEAR/365
    energy=integrate(lambda x:flux(o,math.radians(lat),x,1365)*dt(o,x),a,b)*86400/1e6
    calendar.append(dict(orbit=o,lat=lat,first=first,last=last,energy=energy,duration=duration,mean=energy*1e6/(duration*86400)))
Path(__file__).with_name('seasonal_reference.json').write_text(json.dumps(dict(reference='SciPy QUADPACK time-weighted flux + numerical time inversion', seasons=cases,dates=dates,calendar=calendar),indent=2)+'\n')
