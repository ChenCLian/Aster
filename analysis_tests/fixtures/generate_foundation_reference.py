# Independent NumPy/SciPy references for Aster foundation task audits.
# Run with Python 3, NumPy and SciPy; never calls the production C++ algorithms.
from pathlib import Path
import json
import numpy as np
from scipy.interpolate import CubicSpline,Akima1DInterpolator
root=Path(__file__).parent
cases=[]; checks={}
def case(name,method,x=None,y=None,p=None,expected=None,expect_state='Succeeded',summary=False):
 c={'name':name,'method':method,'parameters':p or {}}
 if x is not None:c['x']=np.asarray(x,dtype=float).tolist();c['y']=np.asarray(y,dtype=float).tolist()
 if summary:c['summary_only']=True
 cases.append(c);checks[name]={'state':expect_state,'expected':expected or {}}
def reference_lowess(x,y,frac):
 # Independent weighted least squares; no CppLowess implementation calls.
 n=len(x);k=max(2,min(n,int(n*frac)));out=[]
 for xi in x:
  dx=x-xi; radius=np.partition(abs(dx),k-1)[k-1]
  w=np.maximum(0,1-(abs(dx)/radius)**3)**3
  design=np.column_stack([np.ones(n),dx])
  beta=np.linalg.lstsq(design*np.sqrt(w[:,None]),y*np.sqrt(w),rcond=None)[0]
  out.append(float(beta[0]))
 return out
x=np.cumsum(np.linspace(.6,1.4,41));y=4+.07*x+np.sin(x*.7)
for name,xx in [('irregular_ols',x),('translated_ols',x+1e9)]:
 X=np.column_stack([np.ones(len(xx)),xx-xx.mean()]);fit=X@np.linalg.lstsq(X,y,rcond=None)[0]
 case(name,'linear_detrend',xx,y,expected={'trend':{'y':fit.tolist()},'detrended':{'y':(y-fit).tolist()}})
xx=np.array([2,0,1,0]);yy=np.array([5,1,3,3]);X=np.column_stack([np.ones(4),xx]);fit=X@np.linalg.lstsq(X,yy,rcond=None)[0]
case('unsorted_duplicate_ols','linear_detrend',xx,yy,expected={'trend':{'y':fit.tolist()},'detrended':{'y':(yy-fit).tolist()}})
case('degenerate_ols','linear_detrend',[2,2],[1,3],expect_state='Failed')
for name,yy in [('summary_population',[1,3,5,7]),('summary_single',[7]),('summary_offset',[1e9,1e9+1,1e9+2])]:
 case(name,'series_summary',np.arange(len(yy)),yy,expected={'summary':{'rows':[[len(yy),float(np.mean(yy)),float(np.var(yy)),min(yy),max(yy)]]}})
case('affine_negative','affine',[0,1,2],[2,-3,0],{'factor':-2,'offset':3},expected={'series':{'x':[0,1,2],'y':[-1,9,3]}})
case('affine_overflow_rejected','affine',[0,1],[1e308,1e308],{'factor':1e100},expect_state='Failed')
case('sine_quarters','sine_source',p={'count':9,'step':1,'period':4,'amplitude':2,'slope':.5},expected={'series':{'x':list(range(9)),'y':[0,2.5,1,-.5,2,4.5,3,1.5,4]}})
case('sorted_duplicate_average','preprocess',[2,0,1,0],[20,2,10,4],{'spacing_mode':'none'},expected={'series':{'x':[0,1,2],'y':[3,10,20]}})
case('duplicate_error','preprocess',[0,0,1],[2,4,10],{'duplicate_policy':'error'},expect_state='Failed')
x=np.array([0,1,4,7,10]);y=3*x+2
for mode,spacing in [('mean',2.5),('median',3),('min',1),('max',3),('fixed',4)]:
 xx=np.arange(int(10/spacing)+1)*spacing
 case('spacing_'+mode,'preprocess',x,y,{'spacing_mode':mode,'spacing':spacing,'interpolation':'linear'},expected={'series':{'x':xx.tolist(),'y':(3*xx+2).tolist()}})
x=np.array([0,1,2,4,5]);y=np.array([0,2,0,3,1]);xx=np.arange(21)*.25
for method,yy in [('linear',np.interp(xx,x,y)),('cubic',CubicSpline(x,y,bc_type='natural')(xx)),('akima',Akima1DInterpolator(x,y,method='akima')(xx))]:
 case('interp_'+method,'preprocess',x,y,{'spacing_mode':'fixed','spacing':.25,'interpolation':method},expected={'series':{'x':xx.tolist(),'y':yy.tolist()}})
case('two_point_linear','preprocess',[0,1],[0,2],{'interpolation':'linear','spacing_mode':'fixed','spacing':.25},expected={'series':{'x':[0,.25,.5,.75,1],'y':[0,.5,1,1.5,2]}})
case('two_point_cubic_rejected','preprocess',[0,1],[0,2],{'interpolation':'cubic'},expect_state='Failed')
case('akima_insufficient','preprocess',[0,1,2,3],[0,1,2,3],{'interpolation':'akima'},expect_state='Failed')
case('unrepresentable_grid','preprocess',[1e12,1e12+.001,1e12+.002],[0,1,2],{'spacing_mode':'fixed','spacing':1e-7,'interpolation':'linear'},expect_state='Failed',summary=True)
x=np.cumsum(.7+.5*(1+np.sin(np.arange(61)*.31)));y=4+.03*x+np.sin(x*.4)
fit=np.array(reference_lowess(x,y,.35))
case('lowess_weighted_ls','detrend_lowess',x,y,{'fraction':.35,'iterations':0},expected={'trend':{'y':fit.tolist()},'detrended':{'y':(y-fit).tolist()}})
for iterations in [0,3]:
 case('lowess_linear_'+str(iterations),'detrend_lowess',x,3+2*x,{'fraction':.35,'iterations':iterations},expected={'trend':{'y':(3+2*x).tolist()},'detrended':{'y':np.zeros(len(x)).tolist()}})
case('lowess_duplicate_rejected','detrend_lowess',[0,1,1,2],[0,1,2,3],expect_state='Failed')
y=2+.03*x+np.sin(x*.4);y[27]+=40
case('lowess_robust_outlier','detrend_lowess',x,y,{'fraction':.35,'iterations':3})
case('lowess_ordinary_outlier','detrend_lowess',x,y,{'fraction':.35,'iterations':0})
case('lowess_retain_mean','detrend_lowess',x,y,{'fraction':.35,'iterations':3,'retain_mean':True},expected={'detrended':{'mean':float(y.mean())}})
case('lowess_tiny_window','detrend_lowess',[0,1,2,3],[0,3,-2,4],{'fraction':.35})
fixture=[dict(c,reference=checks[c['name']]) for c in cases]
(root/'foundation_reference.json').write_text(json.dumps(fixture,indent=2)+'\n')
print(len(cases),'independent scientific cases generated')
