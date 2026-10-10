#!/usr/bin/env python3
"""Read-only prototype geometry contract. Uses model station data and the actual layout JSON.
This is a conservative CPU fit/mapping check, not a substitute for raster/control review.
"""
import json, math, re, sys, os, hashlib
from pathlib import Path
root=Path(os.environ.get('COCKPIT_CONTRACT_ROOT',Path(__file__).resolve().parents[1]))
layout=json.loads((root/'assets/cockpits/layouts.json').read_text())['aircraft']
source=(root/'src/models.cpp').read_text(); blocks=source.split('// ----------------------------------------------------------------')[1:16]
number=r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
models=[]
for i,block in enumerate(blocks):
    body=re.sub(r'//[^\n]*','',block.split('\n',1)[1]); vals=[float(x.rstrip('f')) for x in re.findall(number+r'f?',body.split('vec3',1)[0])]
    eye=[float(x.rstrip('f')) for x in re.findall(number+r'f?',body.split('vec3(',1)[1].split(')',1)[0])]
    models.append({'stations':[vals[k:k+4] for k in range(0,32,4)],'roundness':vals[32],'eye':eye})
def slope(d0,d1,h0,h1): return 0 if d0*d1<=0 else 3*(h0+h1)/((2*h1+h0)/d0+(h1+2*h0)/d1)
def section(m,z):
    s=m['stations'];z=max(s[0][0],min(s[-1][0],z));i=next((k for k in range(7) if z<=s[k+1][0]),6);a,b=s[i:i+2];h=b[0]-a[0];t=(z-a[0])/h;out=[]
    for c in (1,2,3):
        d=(b[c]-a[c])/h;ma=mb=d*.5
        if i: p=s[i-1];h0=a[0]-p[0];ma=slope((a[c]-p[c])/h0,d,h0,h)
        if i<6: n=s[i+2];h1=n[0]-b[0];mb=slope(d,(n[c]-b[c])/h1,h,h1)
        out.append(a[c]*(2*t**3-3*t*t+1)+ma*h*(t**3-2*t*t+t)+b[c]*(-2*t**3+3*t*t)+mb*h*(t**3-t*t))
    return out
def fuselage(m,p):
    hw,hh,cy=section(m,p[2]);x,y=p[0],p[1]-cy;mn=min(hw,hh);rnd=m['roundness'];ell=(math.hypot(x/hw,y/hh)-1)*mn;r=mn*(.3+.7*rnd);dx,dy=abs(x)-hw+r,abs(y)-hh+r;rr=math.hypot(max(dx,0),max(dy,0))+min(max(dx,dy),0)-r;return rr*(1-rnd)+ell*rnd
def module(model,L,tile):
    m=L[['pilot','copilot','systems','status'][tile]];glass=model in (5,6,9,14);twin=model in (3,8) or glass
    h=([.184,.092] if glass else [.147,.099]) if tile<2 else ([.105,.094] if glass else [.087 if twin else .052,.096]) if tile==2 else [1,.70]
    return m,[x*m[3] for x in h]
def point(E,m,tile,p):
    y,z=p[1],p[2]
    if tile==3: y,z=math.cos(.35)*y+math.sin(.35)*z,-math.sin(.35)*y+math.cos(.35)*z
    x=p[0]
    if len(E)>4 and E[4]==14 and tile<2:
        a=-.16 if tile==0 else .16;x,z=math.cos(a)*x-math.sin(a)*z,math.sin(a)*x+math.cos(a)*z
    return [m[0]+x,E[1]-m[1]+y,E[2]-(.85 if E[3] else .68)+m[2]+z]

# Bushmaster's raised primary panel intentionally fails the generic eye-drop
# proxy. Test the actual forward path instead, without relaxing face/skin fit.
# Source-shape assertions fail closed when these mirrored GLSL fields change.
# (MODEL_IS(n), plane_common.glsl, is gModelId==n in every build that carries the code: the contracts read it so)
def compact(s): return re.sub(r'MODEL_IS\((\d+)\)', r'gModelId==\1', re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', s)))
def rb(p,h,r=0):
    q=[abs(p[k])-h[k]+r for k in range(3)]
    return math.sqrt(sum(max(v,0)**2 for v in q))+min(max(q),0)-r
def bushmaster_forward_contract(md,L):
    shader_paths=list((root/'src/shaders').glob('*.glsl'))
    shader_text={p:compact(p.read_text()) for p in shader_paths}
    fittings=shader_text[root/'src/shaders/cockpit_fittings.glsl']
    housing='floatfleetHousing(vec3q,vec2h,floatradius,floatchamfer){floatbody=sdRoundBox(q,vec3(h+vec2(.022),.050),radius);floatcorner=(abs(q.x)+abs(q.y)-(h.x+h.y+.044-chamfer))*.70710678;returnmax(body,corner);}'
    assert housing in fittings, 'fleetHousing changed: update CPU mirror before accepting model2'
    assert 'floatbody=fleetHousing(q,h,min(L.structure.y,.025),L.structure.z);' in fittings
    assert 'vec3fleetModuleFrame(vec3p,inti,vec4mount){vec3q=p-cockpitMount(mount);if(i==3)q.yz=rot2(q.yz,.35);if(gModelId==14&&i<2)q.xz=rot2(q.xz,i==0?.16:-.16);returnq;}' in fittings
    layout_shader=shader_text[root/'src/shaders/cockpit_layout.glsl']
    assert 'vec2(.147,.099)' in layout_shader and 'vec3(m.x,gM[22].y-m.y,gM[21].w+m.z)' in layout_shader
    # Parse dimensions from the actual helper, not the historical result JSON.
    definitions=[]
    for path,s in shader_text.items():
        definitions += [(path,b) for b in re.findall(r'floatbushShellFittedPilotBrow\(vec3p,floatfuselage\)\{([^{}]+)\}',s)]
    assert len(definitions)==1, 'exactly one integrated low-brow helper required'
    brow_path,body=definitions[0]
    n='('+number+')'
    pattern=(r'vec4mount=fleetModule\(0\);vec2h=fleetModuleHalf\(0,mount\);'
             r'vec3q=fleetModuleFrame\(p,0,mount\)-vec3\(0,h.y\+'+n+','+n+r'\);'
             r'returnmax\(sdRoundBox\(q,vec3\(h.x\+'+n+','+n+','+n+r'\),'+n+r'\),fuselage\+'+n+r'\);')
    match=re.fullmatch(pattern,body)
    assert match, 'unsupported low-brow formula: reconcile CPU mirror'
    by,bz,bx,hy,hz,br,inset=map(float,match.groups())
    assert inset>=.066, 'pilot brow must retain its 6 mm inner-shell separation'
    # Generic brows are now named before dispatch so the Kestrel-only return
    # can be expressed once. Both Bushmaster compilation branches must still
    # select its own parsed low-brow helper, never that generic fallback.
    assert 'floatbrow=sdRoundBox(lip,vec3(h.x+.027,.014,.083),.012);' in fittings
    assert 'if(gModelId==0&&i==0)brow=max(sdRoundBox(lip+vec3(.025,0,0),vec3(h.x+.052,.014,.083),.012),fuselage+.040);' in fittings
    dispatches=[
        'if(fleetIndividualBrow(i))outv=opU(outv,vec2(gModelId==2?bushShellFittedPilotBrow(p,fuselage):brow,14.0));',
        'if(fleetIndividualBrow(i))outv=opU(outv,vec2(gModelId==2?bushShellFittedPilotBrow(p,fuselage):gModelId==7?swiftShellFittedPilotBrow(p,fuselage):brow,14.0));'
    ]
    assert all(v in fittings for v in dispatches), 'model2 low brow not dispatched in both panel-renderer compilation branches'
    assert 'if(gModelId==2||gModelId==4||gModelId==7)returntile==0;' in fittings, 'individual brow selection changed'
    assert 'sdRoundBox(q-vec3(0,h.y+.012,.060),vec3(h.x*.92,.005,.008),.004)' in fittings, 'satin rim changed'
    primitives=shader_text[root/'src/shaders/plane_common.glsl']
    assert 'floatsdRoundBox(vec3p,vec3b,floatr){vec3q=abs(p)-b+r;returnlength(max(q,0.0))+min(max(q.x,max(q.y,q.z)),0.0)-r;}' in primitives
    assert 'floatsmin(floata,floatb,floatk){floath=clamp(0.5+0.5*(b-a)/k,0.0,1.0);returnmix(b,a,h)-k*h*(1.0-h);}' in primitives
    # This checks local analytic panel/brow geometry plus the true window cut.
    # It is deliberately not an entire-cockpit/exterior/continuous-ray proof.
    tail=re.sub(r'//[^\n]*','',blocks[2].split('vec3(',1)[1].split(')',1)[1])
    ws=[float(v.rstrip('f')) for v in re.findall(number+'f?',tail)][1:5]
    plane=shader_text[root/'src/shaders/plane_sdf.glsl']
    for snippet in [
        'floatshell=abs(f+0.03)-0.03;',
        'floatholeWs=sdBox(p-vec3(0.0,WS.z+1.0,0.5*(WS.x+WS.y)),vec3(sec.x*1.25,1.0,0.5*(WS.y-WS.x)));',
        'abs(p.x)-0.025;', 'holeWs=max(holeWs,-post);',
        'floatsideTop=sec.z+sec.y*0.78;',
        'floatholeSide=sdBox(p-vec3(0.0,0.5*(WS.z-0.12+sideTop),0.5*(WS.y+WS.w)),vec3(5.0,0.5*(sideTop-WS.z+0.12),0.5*(WS.w-WS.y)));',
        'holeSide=max(holeSide,0.3-abs(p.x));',
        'holeSide=max(holeSide,-(abs(p.z-WS.y-0.04)-0.025));',
        'shell=-smin(-shell,winHole,0.03);']:
        assert snippet in plane, 'windowed-shell formula changed: reconcile CPU mirror'
    def shell(p,f):
        hw,hh,cy=section(md,p[2]);raw=abs(f+.03)-.03
        hole=max(rb([p[0],p[1]-ws[2]-1,p[2]-(ws[0]+ws[1])*.5],[hw*1.25,1,(ws[1]-ws[0])*.5]),-(abs(p[0])-.025))
        top=cy+hh*.78
        side=max(rb([p[0],p[1]-(ws[2]-.12+top)*.5,p[2]-(ws[1]+ws[3])*.5],[5,(top-ws[2]+.12)*.5,(ws[3]-ws[1])*.5]),.3-abs(p[0]),-(abs(p[2]-ws[1]-.04)-.025))
        a,b=-raw,min(hole,side);u=max(0,min(1,.5+.5*(b-a)/.03))
        return -(b*(1-u)+a*u-.03*u*(1-u))
    E=md['eye']+[False];mods=[module(2,L,t) for t in range(4)]
    mounts=[point(E,m,t,[0,0,0]) for t,(m,h) in enumerate(mods)]
    def fields(p):
        f=fuselage(md,p);out={'windowed_shell':shell(p,f)}
        for t,((m,h),mount) in enumerate(zip(mods,mounts)):
            q=[p[k]-mount[k] for k in range(3)]
            if t==3: q[1],q[2]=math.cos(.35)*q[1]-math.sin(.35)*q[2],math.sin(.35)*q[1]+math.cos(.35)*q[2]
            out['housing_'+str(t)]=max(rb(q,[h[0]+.022,h[1]+.022,.050],min(L['structure'][1],.025)),(abs(q[0])+abs(q[1])-sum(h)-.044+L['structure'][2])*.70710678)
            if t<2: out['satin_rim_'+str(t)]=rb([q[0],q[1]-h[1]-.012,q[2]-.060],[h[0]*.92,.005,.008],.004)
            if t==0: out['pilot_brow']=max(rb([q[0],q[1]-h[1]-by,q[2]-bz],[h[0]+bx,hy,hz],br),f+inset)
        return out
    rows=[]
    for side in (-1,1):
        eye=[side*abs(E[0]),E[1],E[2]]
        for down in range(5):
            worst={};hits={}
            for j in range(1,1750):
                distance=j*.002;p=[eye[0],eye[1]-distance*math.tan(math.radians(down)),eye[2]-distance]
                for name,d in fields(p).items():
                    worst[name]=min(worst.get(name,math.inf),d)
                    if d<=0 and name not in hits: hits[name]={'point':p,'sdf_m':d}
            rows.append({'seat':side,'down_degrees':down,'required':down<=3,'clear':not hits,'minimum_sampled_sdf_m':worst,'first_hits':hits})
    return rows, brow_path

rows=[];fail=[];checks=0
for L in layout:
    i=L['model'];md=models[i];E=md['eye']+[i in (5,6,9,14),i];worst=(-1e9,None)
    for tile in range(4):
        m,h=module(i,L,tile)
        # The original visible face / outer-skin requirement remains 45 mm for EVERY model.
        for ix in range(9):
            for iy in range(7):
                p=point(E,m,tile,[(ix/8*2-1)*h[0],(iy/6*2-1)*h[1],.05]);d=fuselage(md,p);checks+=1
                if d>worst[0]: worst=(d,(tile,p))
                if d>-.045: fail.append({'model':i,'tile':tile,'point':p,'skin_clearance_m':-d})
        checks+=1
        if i!=2 and tile<2 and m[1]-h[1] < .125: fail.append({'model':i,'tile':tile,'reason':'primary housing reaches eye sightline'})
    rows.append({'model':i,'name':L['name'],'minimum_glass_to_skin_m':-worst[0],'worst_tile':worst[1][0]})
forward=[];brow_path=None
try:
    forward,brow_path=bushmaster_forward_contract(models[2],layout[2])
    for row in forward:
        if row['required']:
            checks+=1
            if not row['clear']: fail.append({'model':2,'reason':'0..3 degree forward ray blocked','ray':row})
except AssertionError as e:
    fail.append({'model':2,'reason':'source contract mismatch','detail':str(e)})
hash_paths=[root/'src/models.cpp',root/'assets/cockpits/layouts.json',root/'src/shaders/cockpit_layout.glsl',root/'src/shaders/cockpit_fittings.glsl',root/'src/shaders/plane_sdf.glsl',root/'src/shaders/plane_common.glsl']
if brow_path: hash_paths.append(brow_path)
source_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in hash_paths}
result={'source_hashes':source_hashes,'model2_forward_rays':forward,'checks':checks,'failures':len(fail),'fit':rows,'violations':fail[:40],'limits':'Conservative skin and source-data contract only. Production raster, moving control envelopes and GPU performance remain separate gates.'}
print(json.dumps(result,indent=2))
sys.exit(bool(fail))
