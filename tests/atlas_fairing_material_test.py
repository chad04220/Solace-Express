#!/usr/bin/env python3
"""Atlas fairing ownership must ignore cancellation in the blended distance.
Uses an allowed float32 mix schedule that reproduces the native failure mode;
requires the production predicate to classify local primitive ownership instead.
"""
from pathlib import Path
import math,re,struct,sys
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(ROOT/'src/shaders/plane_sdf.glsl').read_text()
assert re.search(r'bool atlasFairingOwns\(float fair,float footprint,float base\)\{return max\(fair,footprint\)<base;\}',s)
assert 'if(atlasFairingOwns(fair,footprint,fd))res.y=2.0;' in s
assert 'if(res.x<fd)res.y=2.0;' not in s
f=lambda x:struct.unpack('f',struct.pack('f',x))[0]
def old_blend(a,b):
 h=f(max(0,min(1,f(.5+f(.5*f(f(b-a)/f(.08)))))))
 # GLSL mix(b,a,h) can compile as b+(a-b)*h; cancellation at h=1
 # is harmless for geometry but cannot be used as a material ownership test.
 return f(f(b+f(f(a-b)*h))-f(f(.08*h)*f(1-h)))
def owns(fair,foot,base):return max(fair,foot)<base
checks=failures=0
# Distant nacelle/exhaust field witnesses. Both signs around their surface are
# included: these represent valid nearby sampling offsets, not arbitrary colors.
for material in [5,17]:
 for fair in [f(.45+i*.037) for i in range(1024)]:
  for base in [f(t) for t in [-.002,-.0001,-1e-6,-1e-8,0,1e-8,1e-6,.0001,.002]]:
   for foot in [-2.,-.2,.1,2.]:
    joined=old_blend(base,fair);dist=min(base,max(joined,foot))
    if foot<base and dist<base:failures+=1
    result=2 if owns(fair,foot,base) else material
    assert result==material;checks+=1
assert failures>100,'Regression must demonstrate the removed cancellation-sensitive predicate'
for fair,foot,base,expected in [(-.1,-.2,.1,True),(.01,-.2,.1,True),(-.2,.3,.1,False),(.2,-.5,.1,False),(.1,.1,.1,False)]:
 assert owns(fair,foot,base)==expected;checks+=1
# Exact native-cache vertices: bit-identical positions had changed 5/17 -> 2.
# Test the candidate primitive at those points with the complete surface sampling
# band. This does not pretend that CPU float intermediates were GPU readbacks.
def fairing_at(p):
 x,y,z=p;angle=math.atan((9.25+1.235-5.70)/19.9);c,s=math.cos(angle),math.sin(angle)
 dx,dz=x-2.30,z-1.24;q=(c*dx+s*dz,y+2.16,-s*dx+c*dz)
 ft,reach,aft,halfZ=1.45,1.75,.24,1.34
 centre=(0,ft*.5,.5*(aft-reach));half=(1.94,ft*.5,halfZ+.5*(aft+reach));r=.22
 d=[abs(q[i]-centre[i])-(half[i]-r) for i in range(3)]
 box=math.sqrt(sum(max(t,0)**2 for t in d))+min(max(d),0)-r
 ahead=max(-q[2]-halfZ-.04,0);curve=ft/(reach*reach)
 ramp=(curve*ahead*ahead-q[1])/math.sqrt(1+4*curve*curve*ahead*ahead)
 fair=max(box,ramp,(q[2]-halfZ-.04-1.5*q[1])*.5547)
 le=-3.55+9.25*x/19.9;ch=5.70+(1.235-5.70)*x/19.9
 footprint=max(max(-x,x-19.9,le-z,z-le-ch),min(x-4.,z-le-.74*ch+.05))
 return fair,footprint
native=[(5,(6.936733245849609,-2.200817584991455,.6494560837745667)),
        (5,(6.164056301116943,-2.5980587005615234,.3046923577785492)),
        (17,(6.3520002365112305,-1.9865951538085938,.8035345673561096)),
        (17,(6.274051666259766,-1.6863962411880493,.6790085434913635))]
for material,point in native:
 fair,foot=fairing_at(point);assert fair>.08
 for k in range(-100,101):
  base=k*.0001;assert not owns(fair,foot,base);checks+=1
print(f'PASS: {checks} Atlas primitive-ownership checks; old predicate misclassifies {failures} distant samples')
