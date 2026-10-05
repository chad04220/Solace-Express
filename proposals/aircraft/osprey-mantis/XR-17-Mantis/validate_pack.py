import json,pathlib,re,math
import numpy as np
D=pathlib.Path(__file__).parent
P=json.loads((D/'parts.json').read_text())['parts'];src=(D/'mantis.glsl').read_text();assert 'uTime' not in src
assert len({p['id'] for p in P})==len(P)
# Analytic exact rounded cone, matching GLSL library.
def cone(p,a,b,r1,r2):
 a=np.array(a);b=np.array(b);ba=b-a;l2=np.dot(ba,ba);rr=r1-r2;a2=l2-rr*rr;pa=p-a;y=pa@ba;z=y-l2;xv=pa*l2-y[...,None]*ba;x2=np.sum(xv*xv,axis=-1);k=np.sign(rr)*rr*rr*x2
 mid=(np.sqrt(x2*a2/l2)+y*rr)/l2-r1
 return np.where(np.sign(z)*a2*z*z*l2>k,np.sqrt(x2+z*z*l2)/l2-r2,np.where(np.sign(y)*a2*y*y*l2<k,np.sqrt(x2+y*y*l2)/l2-r1,mid))
step=.0125;x=np.arange(-7,7,step);z=np.arange(-3,5,step);xx,zz=np.meshgrid(x,z);p=np.stack([np.abs(xx),np.zeros_like(xx),zz],-1);wing=cone(p,[.8,0,1.65],[5.88,0,-.22],1.45,.65)<=0;area=wing.sum()*step*step
m=7990;g=9.81;S=30.;vs1=(2*m*g/(1.225*S*1.55))**.5;vs0=(2*m*g/(1.225*S*2.15))**.5
checks={'mass_kg':m,'wing_loading_Nm2':m*g/S,'stall_clean_ms':vs1,'stall_flap_ms':vs0,'vr_ms':50,'vr_ratio':50/vs0,'vref_ms':58,'vref_ratio':58/vs0,'cruise_ms':190,'cruise_ratio':190/58,'thrust_weight':30000/(m*g),'Ixx_ref':.12*m*6.6**2,'Iyy_ref':.18*m*8**2,'Izz_ref':.12*m*6.6**2+.18*m*8**2,'planeBound':10.3,'wing_projected_plan_area_sample_m2':area,'spec_wing_area_m2':S,'wing_area_error_pct':100*(area-S)/S,'deployed_tyre_bottom_y':-.28-.28-1.295-.26,'physics_gear_height':1.05*1.3+.75,'registered_parts':len(P),'geometry_time_dependency':False,'flight_tests':'PASS isolated conventional flight suite at nominal7900kg and max7990kg; full game Tier B rendering/registry not integrated','bounds':'analytic conservative part enclosures inside radius10.3; see AUTHORING'}
(D/'self_checks.json').write_text(json.dumps(checks,indent=2));print(json.dumps(checks,indent=2))

assert 400 < checks['wing_loading_Nm2'] < 4000
assert 1.10 <= checks['vr_ratio'] <= 1.15
assert 1.25 <= checks['vref_ratio'] <= 1.35
assert checks['cruise_ratio'] >= 1.6
assert .25 <= checks['thrust_weight'] <= .4
assert abs(checks['wing_area_error_pct']) <= 10
assert abs(checks['deployed_tyre_bottom_y']+checks['physics_gear_height']) < 1e-6
row=(D/'rows.cpp.inc').read_text()
for token in ['30.0f, 13.2f, 2.27f','50.0f, 58.0f, 190.0f','6100.0f, 1800.0f','16.0f, 1.05f']:
 assert token in row, 'Row drift; recalculate analytical checks: '+token
for p in P:
 if p['kind']!='STATIC': assert p['state'] in ['gPS.x','gPS.y','gCtl.x','gCtl.y','gCtl.z','gCtl.w','uCustom[0]','uCustom[1]','uCustom[2]','uCustom[3]']
assert len(P)==31
print('Enforced analytical/state/row-drift assertions PASS')

# Exact-source geometry regressions; fails validation on a real shader discrepancy.
import subprocess,sys
subprocess.run([sys.executable,str(D/'audit_geometry.py')],check=True,cwd=D)

# The cockpit generator may edit only the static interior; keep a checked exact snippet.
a=src.index('    if(partOn(40))');b=src.index('    if(partOn(41))',a)
assert src[a:b] == (D/'part40_replacement.glsl').read_text()
cabin=json.loads((D/'cavity_clearance.json').read_text())
assert cabin['all_inside'] and cabin['minimum_guaranteed_clearance_m']>.01
import os
subprocess.run([sys.executable,str(D/'probe_control_sweep.py')],check=True,cwd=D,env={**os.environ,'OPENBLAS_NUM_THREADS':'1'})
controls=json.loads((D/'control_sweep.json').read_text())
assert controls['all_pass']
print('Interior containment and control sweep assertions PASS')
