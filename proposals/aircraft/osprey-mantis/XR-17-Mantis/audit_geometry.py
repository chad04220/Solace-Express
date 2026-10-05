"""Deterministic exact-shader symmetry, fill, closure and rig-axis regression audit."""
import json, pathlib, numpy as np
from audit_gpu import query
D=pathlib.Path(__file__).parent
rng=np.random.default_rng(171005)
p=rng.uniform([-6.8,-2.3,-8.2],[6.8,2.9,8.2],(80000,3)); pm=p*np.array([-1,1,1])
checks={}; states=[('neutral',{},{}),('unequal_stores',{'custom':(.7,.2,.8,.3,1,0,1,0)},{'custom':(.7,.8,.2,.3,0,1,1,0)}),('gear_half',{'gear':.5},{'gear':.5}),('gear_down',{'gear':1},{'gear':1}),('pitch_flaps',{'flap':1,'ctl':(1,0,0,0)},{'flap':1,'ctl':(1,0,0,0)}),('roll_yaw',{'ctl':(0,1,1,0)},{'ctl':(0,-1,-1,0)}),('bay_half',{'custom':(.5,.5,.5,0,1,1,1,0)},{'custom':(.5,.5,.5,0,1,1,1,0)}),('bay_open',{'custom':(1,1,1,1,1,1,1,0)},{'custom':(1,1,1,1,1,1,1,0)})]
for name,a,b in states:
 da=query(p,**a);db=query(pm,**b);err=float(np.max(np.abs(da-db)));assert err<2e-5,(name,err)
 checks[name]={'mirror_pairs':len(p),'max_distance_error_m':err}
# Interior is purposefully handed; nav material colours ignored (distance only).
# Each insert must preserve the full parent volume past its leading split and lateral reveal.
x,z,y=np.meshgrid(np.linspace(-2.3,2.3,130),np.linspace(.63,2.,100),np.linspace(-.18,.18,13),indexing='ij')
q=np.stack([x,y,z],-1).reshape(-1,3)
for id,c,h in [(12,1.16,1.02),(14,-1.13,.79)]:
 mask=(abs(q[:,0]-c)<h-.009)
 qq=q[mask];points=np.stack([-(3.45+.9363291776*qq[:,0]+.3511234416*qq[:,2]),-.62+qq[:,1],.85-.3511234416*qq[:,0]+.9363291776*qq[:,2]],-1)
 parent=query(points,mode=1);inside=parent<-.002
 delta=query(points,part=id);bad=int(np.sum(delta[inside]>1e-5));assert bad==0,(id,bad)
 checks['wing_fill_'+str(id)]={'interior_parent_points':int(inside.sum()),'unfilled_points':bad}
# Fin insert parent preservation over the formerly missing aft strip and full thickness.
x,y,z=np.meshgrid(np.linspace(-.12,.12,25),np.linspace(-.94,.94,70),np.linspace(.50,1.75,100),indexing='ij');q=np.stack([x,y,z],-1).reshape(-1,3)
points=np.stack([q[:,0],1.22+.939693*q[:,1]+.342020*q[:,2],5.17-.342020*q[:,1]+.939693*q[:,2]],-1)
parent=query(points,mode=2);inside=parent<-.002;actual=query(points,part=16);bad=int(np.sum(actual[inside]>1e-5));assert bad==0,bad
checks['rudder_fill']={'interior_parent_points':int(inside.sum()),'unfilled_points':bad}
# Closed bay skins cover complete straight and rounded perimeter, including centre split.
x,z=np.meshgrid(np.linspace(-.605,.605,200),np.linspace(.43,2.77,300));ps=np.stack([x,np.full_like(x,-.94),z],-1).reshape(-1,3)
a=query(ps);bad=int(np.sum(a>0));assert not bad,('bay skin holes',bad)
checks['bay_closed_skin']={'points':len(ps),'holes':bad}
for id,xc,zc in [(23,-1.6,.6),(24,1.6,.6),(25,0,-5.76)]:
 x,z=np.meshgrid(np.linspace(xc-.31,xc+.31,60),np.linspace(zc-.52,zc+.52,100));ps=np.stack([x,np.full_like(x,-.84),z],-1).reshape(-1,3)
 a=query(ps);bad=int(np.sum(a>0));assert not bad,('gear skin holes',id,bad)
 checks['gear_closed_skin_'+str(id)]={'points':len(ps),'holes':bad}
# Hinge axis must lie in the matching split plane (not aft of it).
parts={p['id']:p for p in json.loads((D/'parts.json').read_text())['parts']}
for id in [12,13,14,15]:
 p=parts[id];side=-1 if id in [12,14] else 1
 o=np.array(p['pivot'])-[side*3.45,-.62,.85];z=.3511234416*side*o[0]+.9363291776*o[2]
 assert abs(z-.62)<1e-6,(id,z)
 checks['hinge_'+str(id)]={'split_plane_m':float(z),'target':.62}
o=np.array(parts[16]['pivot'])-[0,1.22,5.17];z=.342020*o[1]+.939693*o[2];assert abs(z-.49)<1e-6
checks['hinge_16']={'split_plane_m':float(z),'target':.49}
# Sample interior material points through the complete control travel, check fixed-part overlap.
for id in [12,13,14,15,16]:
 part=parts[id];o=np.array(part['pivot']);axis=np.array(part['axis']);axis/=np.linalg.norm(axis)
 extent=np.array([1.5,.2,1.] if id!=16 else [.12,1.,1.])
 points=rng.uniform(o-extent,o+extent,(120000,3));points=points[query(points,part=id)<-.002]
 angles=np.linspace(-1,1,9) if id not in [14,15] else np.linspace(0,1,9)
 maximum_penetration=0.;n=0
 for state in angles:
  theta=part['k']*state;v=points-o
  transformed=o+v*np.cos(theta)+np.cross(axis,v)*np.sin(theta)+np.outer(v@axis,axis)*(1-np.cos(theta))
  d=query(transformed,part=1);maximum_penetration=max(maximum_penetration,float(-d.min()));n+=len(points)
 assert maximum_penetration<2e-5,(id,maximum_penetration)
 checks['motion_clearance_'+str(id)]={'volume_probes':n,'states':len(angles),'maximum_penetration_m':maximum_penetration}
# Sample actual cavity boundary with spherical ray marching, then measure parent shell margin.
dirs=rng.normal(size=(6000,3));dirs/=np.linalg.norm(dirs,axis=1)[:,None]
eye=np.array([0,.65,-3.35]);t=np.zeros(len(dirs))
for _ in range(160):
 ps=eye+dirs*t[:,None];d=query(ps,mode=4);t+=np.maximum(-d*.85,0)
ps=eye+dirs*t[:,None];cavity_residual=float(np.max(np.abs(query(ps,mode=4))))
margin=-query(ps,mode=3);assert cavity_residual<2e-5;assert margin.min()>.04,float(margin.min())
checks['closed_cabin']={'boundary_rays':len(dirs),'max_boundary_residual_m':cavity_residual,'minimum_parent_hull_margin_m':float(margin.min())}
# Engine attachment struts overlap both nacelle and body (sampled centre paths).
for side in [-1,1]:
 ps=np.stack([side*np.linspace(.70,1.55,300),np.full(300,.03),np.full(300,3.)],-1)
 d=query(ps);assert d.max()<0
 checks['engine_join_'+str(side)]={'path_points':300,'maximum_distance_m':float(d.max())}
# Carrier guide ends must remain inside the fixed housing/body across their full slide.
for id,side,channel in [(32,-1,1),(33,1,2)]:
 worst=-1e3
 for st in np.linspace(0,1,21):
  points=np.array([[side*(1.08-.16),-.59+.53-.48*st,-1.85+z] for z in [-.55,.55]])
  d=np.minimum(query(points,part=0),query(points,part=2));worst=max(worst,float(d.max()))
 assert worst<.019,(id,worst) # 24 mm spherical guide tip retains at least 5 mm overlap.
 checks['carrier_guide_capture_'+str(id)]={'states':21,'guide_tips_per_state':2,'maximum_tip_center_distance_m':worst,'tip_radius_m':.024,'minimum_tip_overlap_m':.024-worst}
# External feed-camera mounts must not sit inside pressure skin or exterior geometry.
feed=json.loads((D/'feed_cameras.json').read_text());mounts=np.array([p['camera_mount_body'] for p in feed['panes']])
d=query(mounts);h=query(mounts,mode=3);assert d.min()>0 and h.min()>0
checks['camera_mounts']={'exterior_distance_m':d.tolist(),'hull_distance_m':h.tolist(),'minimum_external_clearance_m':float(d.min())}
(D/'geometry_audit.json').write_text(json.dumps({'status':'PASS','method':'actual GLSL executed with RGBA32F distance readback in software Mesa; deterministic probes, not a watertight mesh proof','checks':checks},indent=2));print(json.dumps(checks,indent=2))
