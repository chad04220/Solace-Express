#!/usr/bin/env python3
"""Atlas exterior/interior shared windshield mask and sampled pilot forward rays.
A read-only CPU source-data contract; native render QA remains a separate gate.
"""
import hashlib,json,math,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
env={'__file__':str(ROOT/'tests/conventional_cockpit_layout_test.py')}
exec((ROOT/'tests/conventional_cockpit_layout_test.py').read_text().split('rows=[];fail=[];checks=0')[0],env)
compact=env['compact'];models=env['models'];fuselage=env['fuselage']
common=(ROOT/'src/shaders/plane_common.glsl').read_text();s=compact(common)
expected='floatatlasWindow(vec3p){floatfront=max(max(-20.0-p.z,p.z+17.56),max(.42-p.y,p.y-1.53));front=max(front,.032-abs(p.x));front=max(front,.035-abs(abs(p.x)-1.14));floatside=max(max(-17.48-p.z,p.z+15.80),max(.55-p.y,p.y-1.43));side=max(side,.78-abs(p.x));returnmin(front,side);}'
assert expected in s,'Atlas window formula changed: reconcile signed-mask mirror'
assert 'if(gModelId==14){holeWs=atlasWindow(p);holeSide=holeWs;}' in compact((ROOT/'src/shaders/plane_sdf.glsl').read_text()),'Physical inside cut must use exact shared mask'
assert 'floatpane=atlasWindow(lp);ws=pane<0.0;sideW=false;post=1.0;' in compact((ROOT/'src/shaders/plane_material.glsl').read_text()),'Exterior glazing must use exact shared mask'
def window(p):
 x,y,z=p;front=max(-20.0-z,z+17.56,.42-y,y-1.53,.032-abs(x),.035-abs(abs(x)-1.14));side=max(-17.48-z,z+15.80,.55-y,y-1.43,.78-abs(x));return min(front,side)
def shell(p):
 f=fuselage(models[14],p);raw=abs(f+.03)-.03;a,b=-raw,window(p);h=max(0,min(1,.5+.5*(b-a)/.03));return -(b*(1-h)+a*h-.03*h*(1-h))
rows=[];fails=[];checks=0;E=models[14]['eye']
for side in (-1,1):
 for yaw in (-8,0,8):
  for down in (0,1,2,3,4,0.13*180/math.pi):
   worst=1e6;hit=None
   for i in range(1,4001):
    d=i*.001;p=[side*abs(E[0])+math.tan(math.radians(yaw))*d,E[1]-math.tan(math.radians(down))*d,E[2]-d]
    sdf=shell(p);checks+=1;worst=min(worst,sdf)
    if sdf<=0 and hit is None:hit={'point':p,'sdf_m':sdf}
   row={'seat':side,'yaw_deg':yaw,'down_deg':down,'minimum_sampled_shell_sdf_m':worst,'clear':hit is None,'first_hit':hit};rows.append(row)
   if hit:fails.append(row)
result={'checks':checks,'failures':len(fails),'rays':rows,'violations':fails,'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/models.cpp',ROOT/'src/shaders/plane_common.glsl',ROOT/'src/shaders/plane_sdf.glsl',ROOT/'src/shaders/plane_material.glsl']},'limits':'Both crew seats: yaw0,+/-8 degrees; level through4degrees down plus actual -0.13rad review center;4m forward,1mm sample spacing. Verifies windowed shell only. Nose radome blend lies beyond these rays and below their sightline. Panel/control occlusion and native raster checked separately.'}
print(json.dumps(result,indent=2));raise SystemExit(bool(fails))
