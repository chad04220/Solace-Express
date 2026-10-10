#!/usr/bin/env python3
"""Exact Atlas native-camera regression for a thin slab's vertex-log depth ordering.
Uses the three actual pre-fix PT36 triangles, with no GPU or external packages.
The simplifier edge-bound behavior is separately covered by mesh_simplify_test.
"""
import argparse, math, os, re
from pathlib import Path
arg=argparse.ArgumentParser();arg.add_argument('--edge',type=float);args=arg.parse_args()
root=Path(os.environ.get('SOLACE_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
if args.edge is None:
 source=(root/'src/mesh_validation.h').read_text()
 match=re.search(r'kAtlasDoorMaxEdge\s*=\s*([0-9.]+)f',source)
 assert match,'Production Atlas door edge policy is missing'
 edge=float(match.group(1))
else:edge=args.edge
assert .05<=edge<=.25,'Revalidate the close-door camera envelope before changing this bound'
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,k):return tuple(x*k for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def norm(a):return mul(a,1/math.sqrt(dot(a,a)))
# Local rigid door mesh, before pose; surface tag0 is the near underside.
triangles=[(0,((1.53000009,-.024,1.54800010),(-1.52699995,-.024,.003),(1.53000009,-.024,.004))),
 (1,((1.52699995,0,1.54499996),(1.52699995,0,.009),(-1.52699995,0,1.54499996))),
 (0,((1.51499999,-.024,1.54799998),(-1.52699995,-.024,1.54799998),(-1.52699995,-.024,.003)))]
def refine(tris):
 out=[];stack=list(tris)
 while stack:
  kind,p=stack.pop();k=max(range(3),key=lambda k:dot(sub(p[k],p[(k+1)%3]),sub(p[k],p[(k+1)%3])))
  a,b,c=p[k],p[(k+1)%3],p[(k+2)%3]
  if dot(sub(a,b),sub(a,b))<=edge*edge:out.append((kind,p));continue
  m=mul(add(a,b),.5);stack.extend(((kind,(a,m,c)),(kind,(m,b,c))))
 return out
eye=(8.,-9.35,-3.7);target=(0.,-2.35,3.8);back=norm(sub(eye,target));right=norm(cross((0,1,0),back));up=cross(back,right);f=540/math.tan(math.radians(21))
def projected(tris):
 out=[]
 for kind,tri in tris:
  p=[(x+2.07,y-1.85,5.4433427-z) for x,y,z in tri];d=[sub(q,eye) for q in p]
  z=[-dot(q,back) for q in d];s=[(960+dot(q,right)/zz*f,540-dot(q,up)/zz*f) for q,zz in zip(d,z)]
  a,b,c=s;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
  if abs(den)<1e-12:continue
  out.append((kind,s,z,den))
 return out
def hits(mesh,x,y):
 out=[];x+=.5;y+=.5
 for kind,(a,b,c),z,den in mesh:
  if x<min(a[0],b[0],c[0]) or x>max(a[0],b[0],c[0]) or y<min(a[1],b[1],c[1]) or y>max(a[1],b[1],c[1]):continue
  u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
  v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den;w=1-u-v
  if min(u,v,w)<-1e-8:continue
  bary=(u,v,w);true_z=1/sum(l/zz for l,zz in zip(bary,z));logged=sum(l*math.log2(1+zz) for l,zz in zip(bary,z))
  out.append((logged,true_z,kind))
 return out
before=projected(triangles);after_tri=refine(triangles);after=projected(after_tri)
witness=hits(before,724,433);assert min(witness)[2]==1 and min(witness,key=lambda q:q[1])[2]==0,'Fixture must reproduce actual bright far-face patch'
checks=0;bad_before=0;bad_after=0
for y in range(410,458,6):
 for x in range(688,760,6):
  a=hits(before,x,y);b=hits(after,x,y)
  if len({q[2] for q in a})<2:continue
  checks+=1;bad_before+=min(a)[2]!=min(a,key=lambda q:q[1])[2]
  assert len({q[2] for q in b})==2,'Refinement must retain both original surfaces'
  bad_after+=min(b)[2]!=min(b,key=lambda q:q[1])[2]
assert checks>=20 and bad_before>=8,(checks,bad_before)
assert bad_after==0,(edge,bad_after)
print(f'PASS: {checks} native-camera overlap samples; {bad_before} coarse depth reversals -> 0 with {edge:.2f}m edge bound. Fixture triangles3 -> {len(after_tri)}; native bake cost measured separately.')
