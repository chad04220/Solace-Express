#!/usr/bin/env python3
"""Sample authored L4/Atlas control travel and pilot-to-instrument rays.
Uses the production layout/stations and fails closed if mirrored control formulae change.
This is a CPU geometry contract, not a substitute for native raster inspection.
"""
import hashlib, json, math, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# Reuse the current fleet's source-data parser and section/skin primitive mirror.
env={'__file__':str(ROOT/'tests/conventional_cockpit_layout_test.py')}
exec((ROOT/'tests/conventional_cockpit_layout_test.py').read_text().split('rows=[];fail=[];checks=0')[0],env)
models,point,module,fuselage=map(env.get,['models','point','module','fuselage'])
layouts={x['model']:x for x in json.loads((ROOT/'assets/cockpits/layouts.json').read_text())['aircraft']}
src=(ROOT/'src/shaders/plane_parts.glsl').read_text();compact=lambda x:re.sub(r'\s+','',x)
s=compact(src)
for contract in [
 'X.R=transpose(partRxy(cRoll*.24)*partRyz(-cPitch*.24));',
 'X.T=larkspurStickPivot(sd.x);',
 'vec3larkspurStickPivot(floatside){returncockpitYokeMount(side);}',
 'vec3(0,.151,-.024),.018)',
 'D*transpose(partRxy(-cRoll*0.75))',
 '0.22+pull',
 'floorSupportedYoke()?.050:.075',
 'vec3(0.124,0.0,0.0),vec3(0.13,0.095,0.0),0.02)']:
 assert contract in s,'Control geometry changed; reconcile mirror: '+contract

def sub(a,b):return [x-y for x,y in zip(a,b)]
def rot(x,y,a):return math.cos(a)*x-math.sin(a)*y,math.sin(a)*x+math.cos(a)*y
def dist(a,b):return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def cap(p,a,b,r):
 ab=sub(b,a);ap=sub(p,a);t=max(0,min(1,sum(x*y for x,y in zip(ap,ab))/sum(x*x for x in ab)))
 return dist(p,[x+t*y for x,y in zip(a,ab)])-r

def rb(p,h,r):
 q=[abs(x)-y+r for x,y in zip(p,h)];return math.sqrt(sum(max(x,0)**2 for x in q))+min(max(q),0)-r

def local(p,model,side,pitch,roll):
 E=models[model]['eye'];L=layouts[model];pz=E[2]-(.85 if model==14 else .68)
 pivot=[side*abs(E[0]),E[1]-L['controls'][0],pz+L['controls'][1]]
 if model==13:
  q=sub(p,pivot);q[1],q[2]=rot(q[1],q[2],-pitch*.24);q[0],q[1]=rot(q[0],q[1],roll*.24);return q
 pivot[2]+=.22+pitch*.05;q=sub(p,pivot);q[0]*=-1;q[0],q[1]=rot(q[0],q[1],-roll*.75);return q

def control(p,model,side,pitch,roll):
 q=local(p,model,side,pitch,roll)
 if model==13:return min(cap(q,[0,0,0],[0,.080,-.012],.011),cap(q,[0,.075,-.012],[0,.151,-.024],.018),dist(q,[0,.157,-.032])-.008)
 b=[abs(q[0]),q[1],q[2]]
 # Convex hub, horizontal horns, upright rubber grips, top buttons. Hub/horn
 # smooth union adds at most5mm, conservatively covered by expanding both.
 return min(rb(q,[.06,.03,.022],.015)-.005,cap(b,[.05,0,0],[.118,.012,0],.021),cap(b,[.124,0,0],[.13,.095,0],.02),dist(b,[.128,.105,-.004])-.009)

checks=0;violations=[];summary=[]
for model in (13,14):
 E=models[model]['eye'];L=layouts[model];minimum=1e6;worst=None
 for side in (-1,1):
  eye=[side*abs(E[0]),E[1],E[2]];tile=0 if side<0 else 1;m,h=module(model,L,tile)
  # Centers and inset corners of both PFD/ND areas, or all six analog dials.
  samples=[]
  if model==14:
   for cx,hw in [(-.09,.085),(.10,.075)]:
    for dx,dy in [(0,0),(-.7,-.7),(.7,-.7),(-.7,.7),(.7,.7)]:samples.append([(cx+dx*hw)*m[3],dy*.075*m[3],.05])
  else:
   for x in (-.095,0,.095):
    for y in (.045,-.05):samples.append([x*m[3],y*m[3],.05])
  for ip in range(9):
   for ir in range(9):
    pitch=ip/4-1;roll=ir/4-1
    for sample in samples:
     target=point(E+[model==14,model],m,tile,sample)
     for j in range(1,81):
      u=j/81;p=[a+(b-a)*u for a,b in zip(eye,target)]
      d=control(p,model,side,pitch,roll);checks+=1
      if d<minimum:minimum=d;worst={'seat':side,'pitch':pitch,'roll':roll,'target':target}
      if d<=0 and len(violations)<20:violations.append({'model':model,'reason':'primary control blocks live instrument ray','sdf':d,**worst})
 summary.append({'model':model,'name':L['name'],'sampled_states_per_seat':81,'minimum_control_to_instrument_ray_m':minimum,'worst':worst})
fittings=compact((ROOT/'src/shaders/cockpit_fittings.glsl').read_text())
assert 'vec3atlasNavFrame(vec3p){vec3q=p-vec3(0,gM[22].y-.590,gM[21].w+.335);q.yz=rot2(q.yz,1.15);returnq;}' in fittings,'Navigation mount changed; reconcile pedestal clearance check'
E=models[14]['eye'];L=layouts[14];pw,ph,pd,shift=L['pedestal'];pz=E[2]-.85
pc=[0,E[1]-1.06+ph,pz+.06+pd+shift];nav_clearance=1e6
for ix in range(11):
 for iy in range(11):
  x=(ix/5-1)*.165;y=(iy/5-1)*.115;z=.022
  y,z=rot(y,z,-1.15);p=[x,E[1]-.590+y,pz+.335+z]
  d=rb(sub(p,pc),[pw,ph,pd],.03);nav_clearance=min(nav_clearance,d);checks+=1
  if d<=.01:violations.append({'model':14,'reason':'navigation glass buried in thrust pedestal','clearance':d})
assert 'X.T=vec3(L.controls.z,E.y-L.controls.w,E.z-.290-.100*cThr)' in s,'Larkspur throttle changed; reconcile visibility contract'
E=models[13]['eye'];L=layouts[13];m,h=module(13,L,3);status=[m[0],E[1]-m[1],E[2]-.68+m[2]];power_clearance=1e6
for side in (-1,1):
 eye=[side*abs(E[0]),E[1],E[2]]
 for it in range(21):
  grip=[L['controls'][2],E[1]-L['controls'][3],E[2]-.290-.100*it/20]
  for j in range(1,101):
   p=[a+(b-a)*j/100 for a,b in zip(eye,grip)];q=sub(p,status);q[1],q[2]=rot(q[1],q[2],.35)
   d=rb(q,[h[0]+.022,h[1]+.022,.05],.02);power_clearance=min(power_clearance,d);checks+=1
   if d<=.005:violations.append({'model':13,'reason':'status screen blocks power-control sightline','clearance':d})
E=models[14]['eye'];L=layouts[14];m,h=module(14,L,3);status_clearance=1e6
for side in (-1,1):
 eye=[side*abs(E[0]),E[1],E[2]]
 for ix in range(5):
  for iy in range(5):
   target=point(E+[True,14],m,3,[(ix/2-1)*h[0]*.90,(iy/2-1)*h[1]*.90,.05])
   for j in range(1,121):
    p=[a+(b-a)*j/121 for a,b in zip(eye,target)];q=sub(p,[0,E[1]-.590,E[2]-.85+.335]);q[1],q[2]=rot(q[1],q[2],1.15)
    d=rb(q,[.187,.137,.022],.012);status_clearance=min(status_clearance,d);checks+=1
    if d<=.003:violations.append({'model':14,'reason':'navigation tablet blocks shared status from a pilot eye','clearance':d,'seat':side})
# Atlas thrust shafts/handles must clear the navigation housing and both crew sightlines.
# These guards bind the mirror to the production rigid-part pose and unchanged local shape.
for contract in [
 'X.R=Dq*partLever(mix(-0.55,0.6,cThr));',
 'X.T=Dq*vec3(MODEL_IS(4)?0.0:0.035,pc.y+ph-0.02,pc.z-pd*0.35+(MODEL_IS(14)?0.095:0.0));',
 'sdCapsule(l,vec3(0.0),vec3(0.0,0.16,0.0),0.008)',
 'sdRoundBox(l-vec3(0.0,0.16,0.0),vec3(0.03,0.014,0.02),0.009)']:
 assert contract in s,'Thrust geometry changed; reconcile mirror: '+contract
E=models[14]['eye'];L=layouts[14];pw,ph,pd,shift=L['pedestal'];pz=E[2]-.85
pc=[0,E[1]-1.06+ph,pz+.06+pd+shift];nav=[0,E[1]-.590,pz+.335]
def nav_distance(p):
 q=sub(p,nav);q[1],q[2]=rot(q[1],q[2],1.15);return rb(q,[.187,.137,.022],.012)
def lever_world(q,T,a,side):
 y,z=rot(q[1],q[2],-a);return [T[0]+side*q[0],T[1]+y,T[2]+z]
def lever_distance(p,T,a,side):
 q=sub(p,T);q[1],q[2]=rot(q[1],q[2],a);q[0]*=side
 return min(cap(q,[0,0,0],[0,.16,0],.008),rb(sub(q,[0,.16,0]),[.03,.014,.02],.009))
nav_rays=[]
for side in (-1,1):
 eye=[side*abs(E[0]),E[1],E[2]]
 for ix in range(5):
  for iy in range(5):
   y,z=rot((iy/2-1)*.115,.022,-1.15);target=[(ix/2-1)*.165,nav[1]+y,nav[2]+z]
   for j in range(1,62):nav_rays.append([a+(b-a)*j/61 for a,b in zip(eye,target)])
thrust_min={'shaft_to_navigation_m':1e6,'handle_bound_to_navigation_m':1e6,'two_crew_navigation_ray_m':1e6}
for it in range(21):
 a=-.55+1.15*it/20
 for side in (-1,1):
  T=[side*.035,pc[1]+ph-.02,pc[2]-pd*.35+.095]
  for j in range(81):
   d=nav_distance(lever_world([0,.16*j/80,0],T,a,side))-.008;checks+=1
   thrust_min['shaft_to_navigation_m']=min(thrust_min['shaft_to_navigation_m'],d)
  for ix in range(5):
   for iy in range(5):
    for iz in range(5):
     q=[(ix/2-1)*.03,.16+(iy/2-1)*.014,(iz/2-1)*.02]
     d=nav_distance(lever_world(q,T,a,side));checks+=1
     thrust_min['handle_bound_to_navigation_m']=min(thrust_min['handle_bound_to_navigation_m'],d)
  for p in nav_rays:
   d=lever_distance(p,T,a,side);checks+=1
   thrust_min['two_crew_navigation_ray_m']=min(thrust_min['two_crew_navigation_ray_m'],d)
for kind,d in thrust_min.items():
 if d<=.003:violations.append({'model':14,'reason':kind,'clearance_m':d})

result={'atlas_thrust_clearance':thrust_min,'checks':checks,'failures':len(violations),'control_scan':summary,'atlas_navigation_pedestal_clearance_m':nav_clearance,'larkspur_power_sightline_clearance_m':power_clearance,'atlas_status_sightline_clearance_m':status_clearance,'violations':violations,'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/shaders/plane_parts.glsl',ROOT/'src/shaders/cockpit_fittings.glsl',ROOT/'assets/cockpits/layouts.json',ROOT/'src/models.cpp']},'limits':'Sampled primary-control/display-ray, power/status visibility and navigation/pedestal contract. Native raster, pedal floor contact, physical reach and performance are separate checks.'}
print(json.dumps(result,indent=2));raise SystemExit(bool(violations))
