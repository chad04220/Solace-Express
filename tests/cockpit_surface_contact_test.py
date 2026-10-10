#!/usr/bin/env python3
"""Source-bound CPU contracts for the reviewed cockpit surface fixes.
Native pixels, silhouettes and shader compilation remain separate acceptance gates.
"""
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
paths = ['plane_sdf.glsl', 'plane_common.glsl', 'plane_material.glsl',
         'cockpit_fittings.glsl', 'cabin_windows.glsl', 'plane_mesh_fs.glsl']
raw = {name: (ROOT / 'src/shaders' / name).read_text() for name in paths}
def compact(s):
    return re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', s))
source = {name: compact(s) for name, s in raw.items()}
def require(name, text):
    assert compact(text) in source[name], 'Surface changed; reconcile contract: ' + text[:100]

def clamp(x, lo, hi): return max(lo, min(hi, x))
def rb(p, h, r):
    d = [abs(x) - y + r for x, y in zip(p, h)]
    return math.sqrt(sum(max(x, 0)**2 for x in d)) + min(max(d), 0) - r

def smin(a, b, k):
    h = clamp(.5 + .5 * (b-a)/k, 0, 1)
    return b*(1-h) + a*h - k*h*(1-h)

def back(p, sw):
    x, y, z = p
    t = clamp((y+.14)/.42, 0, 1); t = t*t*(3-2*t)
    bw = min(sw, .2) + (min(sw, .145)-min(sw, .2))*t
    return smin(rb(p, [bw, .36, .05], .045),
                rb([abs(x)-bw+.025, y+.05, z+.03], [.03, .26, .07], .03), .025)

def web(p, sw):
    x, y, z = p
    offset_y = math.cos(-.18)*(-.03)-math.sin(-.18)*(-.06)
    band = max(abs(abs(x)-.11)-.022, abs(y-offset_y)-.33)
    return max(-smin(-(back(p, sw)-.007), -band, .010), z+.002)

def front(field, x, y):
    lo, hi = -.20, -.002
    for _ in range(42):
        z = (lo+hi)*.5
        if field([x, y, z]) > 0: lo = z
        else: hi = z
    return (lo+hi)*.5

require('plane_sdf.glsl', '''float fleetSeatBackPad(vec3 b,float sw){
 float bw=mix(min(sw,.2),min(sw,.145),smoothstep(-.14,.28,b.y));
 float back=sdRoundBox(b,vec3(bw,.360,.050),.045);
 return smin(back,sdRoundBox(vec3(abs(b.x)-bw+.025,b.y+.050,b.z+.030),vec3(.030,.260,.070),.030),.025);
}''')
require('plane_sdf.glsl', '''float fleetShoulderWeb(vec3 b,float back){
 vec2 offset=rot2(vec2(-.030,-.060),-.180);
 float bands=max(abs(abs(b.x)-.110)-.022,abs(b.y-offset.x)-.330);
 return max(-smin(-(back-.007),-bands,.010),b.z+.002);
}''')
require('plane_sdf.glsl', 'float belt=fleetShoulderWeb(bp,back);')
require('plane_sdf.glsl', 'return fleetShoulderWeb(b,fleetSeatBackPad(b,.180));')
assert source['plane_sdf.glsl'].count('returnfleetShoulderWeb(b,fleetSeatBackPad(b,.180));') == 2
checks = 0
for sw in [.145, .165, .180, .20, .21]:
    for side in [-1, 1]:
        for i in range(119):
            x, y = side*.11, -.32+.59*i/118
            skin = front(lambda p: back(p, sw), x, y)
            fabric = front(lambda p: web(p, sw), x, y)
            assert skin-fabric > .0069
            assert abs(back([x, y, fabric], sw)-.007) < 1e-8
            checks += 1

# The extracted 2D passenger pane is byte-equivalent to the old material formula.
# Only the model13 physical aperture adds the material's existing side/z gates.
require('plane_common.glsl', '''float fleetPassengerPane(vec3 p,vec3 sec){
 int count=int(gM[20].x+.5);if(count<=0)return 1e5;
 float pw=(gM[20].z-gM[20].y)/float(count);
 vec2 q=vec2(mod(p.z-gM[20].y,pw)-pw*.5,p.y-(sec.z+gM[20].w));
 vec2 h=gM[21].xy;float radius=min(h.x,h.y)*.7;
 vec2 d=abs(q)-h+radius;
 float pane=length(max(d,0.0))+min(max(d.x,d.y),0.0)-radius;return pane;
}''')
require('plane_material.glsl', 'float wd=fleetPassengerPane(lp,sec);')
require('plane_sdf.glsl', '''if(MODEL_IS(13)){
 float rearPane=max(fleetPassengerPane(p,sec),max(max(gM[20].y-p.z,p.z-gM[20].z),sec.x*.4-abs(p.x)));
 holeSide=min(holeSide,rearPane);
}''')
require('plane_sdf.glsl', 'shell=-smin(-shell,winHole,0.03);')
env = {'__file__': str(ROOT/'tests/conventional_cockpit_layout_test.py')}
exec((ROOT/'tests/conventional_cockpit_layout_test.py').read_text().split('rows=[];fail=[];checks=0')[0], env)
body = re.sub(r'//[^\n]*', '', env['blocks'][13].split('vec3', 1)[0])
count,z0,z1,dy,hw,hh = [float(x.rstrip('f')) for x in re.findall(env['number']+'f?', body)[-6:]]
assert count == 1 and z1 > z0
for iz in range(41):
    z = z0-.10+(z1-z0+.20)*iz/40
    sec = env['section'](env['models'][13], z)
    for iy in range(41):
        y = sec[2]+dy+(iy/20-1)*hh*1.2
        pw = (z1-z0)/count
        q = [(z-z0)%pw-pw*.5, y-sec[2]-dy]
        radius = min(hw, hh)*.7
        d = [abs(q[0])-hw+radius, abs(q[1])-hh+radius]
        pane = math.hypot(max(d[0],0),max(d[1],0))+min(max(d),0)-radius
        for side in [-1,1]:
            x = side*sec[0]*.90
            exterior_glass = pane < 0 and z0 < z < z1 and abs(x) > sec[0]*.4
            physical_aperture = max(pane,z0-z,z-z1,sec[0]*.4-abs(x)) < 0
            assert physical_aperture == exterior_glass
            checks += 1

# Atlas vents now contact their existing console's flat top, with the complete footprint on it.
require('cockpit_fittings.glsl', 'if(MODEL_IS(14)) vent=vec3(float(side)*1.170,E.y-.548,E.z-.630);')
require('cockpit_fittings.glsl', 'sdRoundBox(q,vec3(.023,.019,.028),.014)')
require('cockpit_fittings.glsl', 'vec3 sq=p-vec3(side*1.170,E.y-.615,E.z-.330);')
require('cockpit_fittings.glsl', 'sdRoundBox(sq,vec3(.170,.048,.410),.032)')
assert abs((-.548-.019)-(-.615+.048)) < 1e-12
assert .023 < .170-.032 and abs(-.630+.330)+.028 < .410-.032
checks += 3

# Normal recovery is bounded to static own-cockpit surfaces and selected primary housings.
require('cockpit_fittings.glsl', 'if((!MODEL_IS(14) && !MODEL_IS(5)) || mid!=145) return false;')
require('cockpit_fittings.glsl', 'if(MODEL_IS(14)) n.xz=rot2(n.xz,i==0?-.16:.16);')
require('plane_mesh_fs.glsl', 'if(FLEET_ON && !traf && gPS.w>.5 && uPartInst<0 && fleetPrimaryBezelNormal(vB,mid,bezelN)) ln=bezelN;')
require('plane_mesh_fs.glsl', 'if(JET_ON && !traf && gPS.w>.5 && uPartInst<0 && specterFittingNormal(vB-gM[22].xyz,mid,specterN)) ln=specterN;')
require('plane_sdf.glsl', 'sdCapsule(tq,vec3(0.0),vec3(0.0,-0.022,0.008),0.0045)')
require('cabin_windows.glsl', 'vec3 axis=vec3(0,-.022,.008);')
require('cabin_windows.glsl', 'n=radial/r;n.yz=mat2(c,-s,s,c)*n.yz;')
require('cabin_windows.glsl', 'if(r<.46 || r>.506 || q.y<-.568 || q.y>-.499 || abs(a)>1.19) return false;')
require('cabin_windows.glsl', 'v=.47+clamp(-.54-q.y,0.0,.02)-r;')
# Finite-difference witnesses for the unchanged support's inner cylindrical and sloped lower walls.
def saddle(p):
    x,y,z=p;r=math.hypot(x,z);a=math.atan2(x,-z)
    return max(r-.71,.47-r,abs(a)-1.18,y+.505,-.56-y,.47+clamp(-.54-y,0,.02)-r)
for y in [-.512,-.520,-.536,-.547,-.553]:
    radius=.47+clamp(-.54-y,0,.02)
    for i in range(41):
        a=-1.1+2.2*i/40;p=[math.sin(a)*radius,y,-math.cos(a)*radius]
        expected=[-math.sin(a), -1.0 if -.56<y<-.54 else 0.0, math.cos(a)]
        gradient=[]
        for k in range(3):
            lo=p[:];hi=p[:];lo[k]-=1e-6;hi[k]+=1e-6
            gradient.append((saddle(hi)-saddle(lo))/2e-6)
        assert max(abs(a-b) for a,b in zip(gradient,expected)) < 1e-7
        checks += 1
# Verified light-aircraft deck and Swift/Nightjar firewall contacts overlap the
# 60 mm shell by 20 mm. They cannot escape its outer 40 mm or alter window cuts.
require('plane_sdf.glsl', 'float shell = abs(f + 0.03) - 0.03;')
require('cockpit_fittings.glsl', 'if(MODEL_IS(14)) deckFit=-smin(-deck,-(fuselage+.065),.020);')
require('cockpit_fittings.glsl', 'if(MODEL_IS(0) || MODEL_IS(1) || MODEL_IS(2)) deckFit=max(deck,fuselage+.040);')
require('cockpit_fittings.glsl', 'if(MODEL_IS(0) || MODEL_IS(1) || MODEL_IS(2) || MODEL_IS(7) || MODEL_IS(9)) firewallFit=max(firewall,fuselage+.040);')
for model in [0, 1, 2, 7, 9]:
    for fixture in [-.001, -.005, -.015]:
        for i in range(601):
            f = -.080 + .080*i/600
            shell = abs(f+.03)-.03
            fit = max(fixture, f+.040)
            assert min(shell, fit) <= 1e-12, (model, fixture, f)
            if f >= -.040:
                assert fit >= -1e-12
            checks += 1
# A positive witness proves this catches the prior 5 mm empty contact band.
assert min(abs(-.0625+.03)-.03, max(-.015, -.0625+.065)) > .0024
checks += 1

# Atlas navigation housing normals include its visible side/back bevel as well
# as the front. Its exact rounded-box field and inclined frame remain authored.
require('cockpit_fittings.glsl', 'vec3 atlasNavFrame(vec3 p){vec3 q=p-vec3(0,gM[22].y-.590,gM[21].w+.335);q.yz=rot2(q.yz,1.15);return q;}')
require('cockpit_fittings.glsl', 'float box=sdRoundBox(nav,vec3(.187,.137,.022),.012);')
require('cockpit_fittings.glsl', '''bool atlasNavHousingNormal(vec3 p,int mid,out vec3 n){
 if(!MODEL_IS(14) || mid!=145) return false;
 vec3 q=atlasNavFrame(p);
 if(abs(q.x)>.193 || abs(q.y)>.143 || abs(q.z)>.028) return false;
 vec3 d=abs(q)-vec3(.187,.137,.022)+.012,outer=max(d,vec3(0));
 float field=length(outer)+min(max(d.x,max(d.y,d.z)),0.0)-.012;
 if(abs(field)>.006) return false;
 if(dot(outer,outer)>1e-12) n=normalize(outer)*sign(q);
 else if(d.x>d.y && d.x>d.z) n=vec3(sign(q.x),0,0);
 else if(d.y>d.z) n=vec3(0,sign(q.y),0);
 else n=vec3(0,0,sign(q.z));
 n.yz=rot2(n.yz,-1.15);
 return true;
}''')
require('plane_mesh_fs.glsl', 'if(FLEET_ON && !traf && gPS.w>.5 && uPartInst<0 && atlasNavHousingNormal(vB,mid,tabletN)) ln=tabletN;')
import random
rng = random.Random(148)
for _ in range(6000):
    q = [rng.uniform(-h, h) for h in [.193, .143, .028]]
    d = [abs(v)-h+.012 for v,h in zip(q, [.187, .137, .022])]
    if abs(rb(q,[.187,.137,.022],.012)) > .006 or min(abs(v) for v in q+d) < 1e-5:
        continue
    o = [max(v,0) for v in d];length=math.sqrt(sum(v*v for v in o))
    if length > 1e-6:
        normal = [v/length*(1 if x>0 else -1) for v,x in zip(o,q)]
    else:
        axis=max(range(3),key=lambda k:d[k]);normal=[0.,0.,0.];normal[axis]=1 if q[axis]>0 else -1
    gradient=[]
    for k in range(3):
        lo=q[:];hi=q[:];lo[k]-=1e-7;hi[k]+=1e-7
        gradient.append((rb(hi,[.187,.137,.022],.012)-rb(lo,[.187,.137,.022],.012))/2e-7)
    length=math.sqrt(sum(v*v for v in gradient));gradient=[v/length for v in gradient]
    assert math.sqrt(sum((a-b)**2 for a,b in zip(normal,gradient))) < 1e-7
    # Inverse normal rotation uses the same angle as the unchanged tablet frame.
    c,s=math.cos(1.15),math.sin(1.15)
    world=[normal[0],c*normal[1]+s*normal[2],-s*normal[1]+c*normal[2]]
    local=[world[0],c*world[1]-s*world[2],s*world[1]+c*world[2]]
    assert max(abs(a-b) for a,b in zip(local,normal)) < 1e-12
    checks += 1

# Kestrel's pilot brow gets only an outboard structural return. The unchanged
# inboard half remains exactly identical, and the return stays below glazing.
require('cockpit_fittings.glsl', 'float brow=sdRoundBox(lip,vec3(h.x+.027,.014,.083),.012);')
require('cockpit_fittings.glsl', 'if(MODEL_IS(0) && i==0) brow=max(sdRoundBox(lip+vec3(.025,0,0),vec3(h.x+.052,.014,.083),.012),fuselage+.040);')
layout=json.loads((ROOT/'assets/cockpits/layouts.json').read_text())['aircraft'][0]
assert layout['model']==0
m=layout['pilot'];eye=env['models'][0]['eye'];h=[.147*m[3],.099*m[3]]
centre=[m[0],eye[1]-m[1]+h[1]+.027,eye[2]-.68+m[2]+.008]
old_left=centre[0]-h[0]-.027
assert centre[1]+.014 < .34 and centre[2]+.083 < -1.55
for iy in range(5):
    y=centre[1]+(iy-2)*.002
    for iz in range(15):
        z=centre[2]+(iz-7)*.010
        lo,hi=-.60,-.35
        for _ in range(42):
            x=(lo+hi)*.5
            if env['fuselage'](env['models'][0],[x,y,z]) > -.040:lo=x
            else:hi=x
        for ix in range(81):
            x=old_left+(hi-old_left)*ix/80
            f=env['fuselage'](env['models'][0],[x,y,z])
            q=[x-centre[0]+.025,y-centre[1],z-centre[2]]
            fitted=max(rb(q,[h[0]+.052,.014,.083],.012),f+.040)
            shell=abs(f+.03)-.03
            assert min(shell,fitted) <= 1e-10,(x,y,z,shell,fitted)
            checks+=1
for ix in range(21):
    for iy in range(11):
        for iz in range(11):
            q=[ix*.015,(iy-5)*.008,(iz-5)*.025]
            old=rb(q,[h[0]+.027,.014,.083],.012)
            new=rb([q[0]+.025,q[1],q[2]],[h[0]+.052,.014,.083],.012)
            assert abs(old-new)<1e-12
            checks+=1

print(json.dumps({'checks':checks,'failures':0,'scope':'Shoulder contact, shared Larkspur glazing, Atlas vent/tablet normals, hidden deck/firewall shell overlap, bounded normal dispatch and Specter support gradients; native rendering remains required.', 'source_sha256':{name:hashlib.sha256(raw[name].encode()).hexdigest() for name in paths}},indent=2))
