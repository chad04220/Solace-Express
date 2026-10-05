import pathlib,json,numpy as np
D=pathlib.Path(__file__).parent;records=json.loads((D/'components.json').read_text())
def sdf(p,o):
 if o['kind']=='box':
  q=abs(p-o['center'])-o['half']+o['radius'];return np.linalg.norm(np.maximum(q,0),axis=1)+np.minimum(q.max(axis=1),0)-o['radius']
 a=np.array(o['a']);b=np.array(o['b']);q=p-a;d=b-a;return np.linalg.norm(q-np.clip(q@d/(d@d),0,1)[:,None]*d,axis=1)-o['radius']
def capsule(x,y,z,length,r):
 # Cylinder surface and both complete endpoint spheres (internal hemispheres harmless conservative).
 ph=np.linspace(0,2*np.pi,64,endpoint=False);ax=np.linspace(0,length,49);a,b=np.meshgrid(ph,ax);p=np.stack([x+r*np.cos(a),y+b,z+r*np.sin(a)],-1).reshape(-1,3)
 u,v=np.meshgrid(ph,np.linspace(0,np.pi,33));sphere=np.stack([r*np.sin(v)*np.cos(u),r*np.cos(v),r*np.sin(v)*np.sin(u)],-1).reshape(-1,3)
 return np.concatenate([p,sphere+[x,y,z],sphere+[x,y+length,z]])
rows=[]
for id in [41,42,43,44]:
 if id<43:
  x,y,z,L,r=(.32,-.23,-3.15,.30,.033) if id==41 else (-.37,-.20,-3.12,.24,.037)
  pts=capsule(x,y,z,L,r);pivot=np.array([x,y,z]);states=np.linspace(-1,1,81) if id==41 else np.linspace(0,1,81)
  exempt='pitch fixed pivot socket' if id==41 else 'throttle fixed pivot socket'
 else:
  side=-1 if id==43 else 1;c=np.array([side*.17,-.28,-3.9]);b=np.array([.1,.04,.16]);r=.025
  # Dense zero-isosurface band generated from volume grid, |distance| <= 1.6 mm.
  pts=np.stack(np.meshgrid(*[np.linspace(-t,t,51) for t in b],indexing='ij'),-1).reshape(-1,3)+c
  d=sdf(pts,dict(kind='box',center=c,half=b,radius=r));pts=pts[abs(d)<.0016];states=np.linspace(-1,1,81);exempt='pedal fixed slider rail'
 minimum=1e3;closest='';worststate=None;hullmin=1e3
 for st in states:
  if id<43:
   angle=.2*st if id==41 else -.6*st+.3;c=np.cos(angle);s=np.sin(angle);p=pts-pivot;p=p@np.array([[1,0,0],[0,c,s],[0,-s,c]])+pivot
  else:p=pts+[0,0,(-.06 if id==43 else .06)*st]
  for ob in records:
   if ob['name']==exempt:continue
   d=float(sdf(p,ob).min())
   if d<minimum:minimum=d;closest=ob['name'];worststate=float(st)
  q=abs(p-[0,.16,-3.35])-[.65,.66,1.05]+.31;ds=np.linalg.norm(np.maximum(q,0),axis=1)+np.minimum(q.max(axis=1),0)-.31;hullmin=min(hullmin,float(-ds.max()))
 rows.append(dict(id=id,state_samples=len(states),surface_samples=len(pts),minimum_static_clearance_m=minimum,closest_static_component=closest,worst_state=worststate,minimum_cavity_clearance_m=hullmin,intentional_mount_overlap_exempt=exempt,pass_no_unintended_collision=minimum>.002))
 print(rows[-1],flush=True)
(D/'control_sweep.json').write_text(json.dumps(dict(method='Dense analytic geometry surface sampling at81 state positions; true inverse of rigHinge used for world points. Fixed bearing sockets and pedal slider tracks intentionally overlap at mechanical mating interfaces; only matching mount excluded. Sampling is evidence, not a continuous mathematical proof.',all_pass=all(r['pass_no_unintended_collision'] for r in rows),results=rows),indent=2))
