#!/usr/bin/env python3
"""Run the five fixed production validation configurations (standard library only)."""
import argparse, concurrent.futures, json, pathlib, subprocess, os

def main():
 p=argparse.ArgumentParser();p.add_argument('binary',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--jobs',type=int,default=2)
 a=p.parse_args()
 if not 1<=a.jobs<=4:p.error('Use 1..4 workers')
 a.output.mkdir(parents=True,exist_ok=True);binary=a.binary.resolve();base=pathlib.Path(__file__).parent
 names=['standard128_a','standard128_b','fine128_a','standard512_a','lowess128_a']
 def run(name):
  with (a.output/(name+'.log')).open('w') as log:
   r=subprocess.run([str(binary),str(base/(name+'.json')),str((a.output/'cache').resolve()),str((a.output/(name+'.json')).resolve())],stdout=log,stderr=subprocess.STDOUT,env={**os.environ,'OPENBLAS_NUM_THREADS':'1'})
  if r.returncode:raise RuntimeError(name+': calculation failed; no partial result accepted')
  print(name+' completed',flush=True)
  return name
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:list(pool.map(run,names))
if __name__=='__main__':main()
