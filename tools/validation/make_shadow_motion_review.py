#!/usr/bin/env python3
"""Deterministic review camera paths, keeping production controller methods untouched."""
import argparse,csv,json,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('frozen_views',type=Path);p.add_argument('output',type=Path);p.add_argument('--kind',type=int,default=13);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
with a.frozen_views.open() as f:row=next(r for r in csv.DictReader(f) if r['name']==f'kind_{a.kind}_front_oblique')
base=[float(row['camera_'+c]) for c in 'xyz'];target=[float(row['target_'+c]) for c in 'xyz']
profiles={}
for name,total,dt in [('ground_orbit',18,.25),('climb_land',70,.25),('accelerate_during_expand',34,.1)]:
 records=[]
 for frame in range(total):
  camera=base.copy()
  if name=='ground_orbit':
   theta=frame*dt*.18;x,z=base[0]-target[0],base[2]-target[2];camera[0]=target[0]+x*math.cos(theta)+z*math.sin(theta);camera[2]=target[2]-x*math.sin(theta)+z*math.cos(theta)
   capture=frame in [0,5,6,7,8,9,12,17]
  elif name=='climb_land':
   rise=0 if frame<10 else min(40,2*(frame-9)) if frame<40 else max(0,40-2*(frame-39))
   camera[1]+=rise;capture=frame in [0,6,7,8,9,10,20,23,24,25,26,27,28,29,35,39,40,49,50,51,52,53,54,55,56,57,58,59,60,64,69]
  else:
   # At dt0.1 the initial base->96 commit is frame17; acceleration begins
   # on frame18 while Expand is active. Speed160m/s is within normal-update
   # bounds, not a camera cut, and is explicitly a validation camera path.
   camera[0]+=16*min(9,max(0,frame-17));capture=frame in [0,14,15,16,17,18,19,20,21,23,24,25,26,27,28,29,33]
  records.append([frame,*camera,*target,dt,int(capture)])
 path=a.output/(name+'.csv')
 with path.open('w') as f:
  w=csv.writer(f);w.writerow(['frame','camera_x','camera_y','camera_z','target_x','target_y','target_z','dt','capture']);w.writerows(records)
 profiles[name]={'frames':total,'simulation_dt':dt,'simulation_seconds':total*dt,'keyframes':sum(r[-1] for r in records),'path':str(path.resolve())}
(a.output/'profiles.json').write_text(json.dumps({'kind':a.kind,'base_view':row,'profiles':profiles,'scope':'test camera motion; no geometry, controller or time-history forcing'},indent=2)+'\n');print(json.dumps(profiles))
