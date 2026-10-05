#!/usr/bin/env python3
"""Apply the prepared optional wheel integration to a fresh aircraft LAB only, never the production checkout."""
import argparse, hashlib, json
from pathlib import Path
HERE=Path(__file__).resolve().parent

def once(s,old,new):
    if s.count(old)!=1: raise ValueError(f'Anchor must occur once: {old[:100]}')
    return s.replace(old,new,1)

def gitblob(s):
    d=s.encode();return hashlib.sha1(b'blob '+str(len(d)).encode()+b'\0'+d).hexdigest()

def normal(name,s):
    if name=='src/aircraft.cpp':
        s=s.replace('#include "../prototypes/aircraft/swift_spec.inc"\n#include "../prototypes/aircraft/nightjar_spec.inc"\n','')
        s=s.replace('const int kNumAircraft = 7; // laboratory candidates are not in career or save files','const int kNumAircraft = sizeof(kAircraft) / sizeof(kAircraft[0]) - 2;')
    if name=='src/aircraft.h':
        s=s.replace('kResearchJet = 9','kResearchJet = 7').replace('kWraith = 10','kWraith = 8')
    return s

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',required=True,type=Path)
    root=p.parse_args().source.resolve()
    if not (root/'candidate-stage.json').exists(): raise SystemExit('Only an isolated prepare_lab.py stage is accepted; production files are not edited.')
    expected=json.loads((HERE/'parent-files.json').read_text())
    files={n:(root/n).read_text() for n in expected}
    for n,s in files.items():
        if gitblob(normal(n,s))!=expected[n]: raise SystemExit(f'Pinned wheel integration parent differs: {n}; rebase deliberately.')

    n='src/aircraft.h';s=files[n]
    s=once(s,'#include "world.h"','#include "world.h"\n#include "wheel_motion.h"')
    s=once(s,'  bool onGround = false, wasOnGround = false;','  WheelMotion wheelMotion[3]; // main left, main right, nose/tail; cosmetic simulation state\n  bool onGround = false, wasOnGround = false;')
    files[n]=s
    n='src/aircraft.cpp';s=files[n]
    s=once(s,'  spec = s; pos = position; fuel = fuelKg; payload = payloadKg;','  spec = s; pos = position; fuel = fuelKg; payload = payloadKg;\n  for (auto& wheel : wheelMotion) wheel.reset();')
    s=once(s,'  bool anyWheel = false;','  bool wheelContact[3] = {}; float wheelSpeed[3] = {};\n  bool anyWheel = false;')
    s=once(s,'      float vlong = dot(vc, wf), vlat = dot(vc, wr);','      float vlong = dot(vc, wf), vlat = dot(vc, wr);\n      wheelContact[c.kind] = true; wheelSpeed[c.kind] = vlong;')
    s=once(s,'  wasOnGround = onGround;','  for (int i = 0; i < 3; ++i) wheelMotion[i].step(dt, wheelSpeed[i], wheelContact[i], i < 2 ? ctl.brake : 0.f);\n  wasOnGround = onGround;')
    files[n]=s
    n='src/traffic.h';s=files[n]
    s=once(s,'#include "common.h"','#include "common.h"\n#include "wheel_motion.h"')
    s=once(s,'  float timer = 0;','  WheelMotion wheelMotion[3]; // same rolling state as the player\n  float timer = 0;')
    files[n]=s
    n='src/traffic.cpp';s=files[n]
    s=once(s,'    vec3 pos0 = c.pos;   // for the swept collision test below','    vec3 pos0 = c.pos; quat q0 = c.q; // also the previous contact pose for wheel travel')
    anchor='    // effects: display smoke, reheat embers'
    block='''    // Per-wheel contact travel, including differential travel while turning. Never derive spin from airspeed.
    const float track = std::max(1.2f, s.span*0.13f), gh = gearH(s), len = s.fusLen;
    vec3 cp[3] = {vec3(-track,-gh,s.taildragger?-.10f*len:.04f*len),
                  vec3(track,-gh,s.taildragger?-.10f*len:.04f*len),
                  s.taildragger?vec3(0,-gh+.11f*len,.45f*len):vec3(0,-gh,-.36f*len)};
    float steer = s.special ? 0.f : c.ctlYaw*.45f*smoothstepf(30.f,4.f,c.speed)*(s.taildragger?-1.f:1.f);
    vec3 fw = q0.rotate(vec3(0,0,-1)) + c.q.rotate(vec3(0,0,-1)); fw.y = 0;
    fw = length(fw)>1e-5f ? normalize(fw) : c.q.rotate(vec3(0,0,-1));
    for (int wi=0;wi<3;++wi) {
      vec3 pt = c.pos+c.q.rotate(cp[wi]), old = pos0+q0.rotate(cp[wi]);
      bool contact = c.role==TrafficCraft::AIRPORT && c.gear>.95f &&
                     pt.y <= g_world.height(pt.x,pt.z)+.08f;
      vec3 wf = wi==2 ? quat::axisAngle(vec3(0,1,0),-steer).rotate(fw) : fw;
      float roll = dt>0.f ? dot(pt-old,wf)/dt : 0.f;
      c.wheelMotion[wi].step(dt,roll,contact);
    }
'''
    s=once(s,anchor,block+anchor)
    s=once(s,'    float v[32] = {c.pos.x, c.pos.y, c.pos.z, bound, r.x, r.y, r.z, 0, u.x, u.y, u.z, 0, b.x, b.y, b.z, 0,',
'''    const ModelDef& m=kModels[c.spec];
    float wr=s.special?.38f:m.wheelR;
    float nr=s.special?.33f:s.taildragger?.10f:m.gear==3?wr*.75f:wr*.85f;
    float steer=s.special?0.f:c.ctlYaw*.45f*smoothstepf(30.f,4.f,c.speed)*(s.taildragger?-1.f:1.f);
    // The rotation-column .w components were unused; the existing 32-texel traffic stride stays unchanged.
    float v[32] = {c.pos.x, c.pos.y, c.pos.z, bound,
                   r.x, r.y, r.z, c.wheelMotion[0].angle(wr),
                   u.x, u.y, u.z, c.wheelMotion[1].angle(wr),
                   b.x, b.y, b.z, c.wheelMotion[2].angle(nr),''')
    s=once(s,'c.spec == kResearchJet ? 0.f : c.gear, c.flaps, 0, 0,','c.spec == kResearchJet ? 0.f : c.gear, c.flaps, steer, 0,')
    files[n]=s
    n='src/game.cpp';s=files[n]
    s=once(s,'  pv.Pr[0] = propAngle;','  float wr = s.special ? .38f : md.wheelR;\n  float nr = s.special ? .33f : s.taildragger ? .10f : md.gear == 3 ? wr*.75f : wr*.85f;\n  pv.wheel[0] = p.wheelMotion[0].angle(wr); pv.wheel[1] = p.wheelMotion[1].angle(wr); pv.wheel[2] = p.wheelMotion[2].angle(nr);\n  pv.Pr[0] = propAngle;')
    files[n]=s
    n='src/renderer.h';s=files[n]
    s=once(s,'#include "entity_mesh.h"','#include "entity_mesh.h"\n#include "ground_vehicle.h"')
    s=once(s,'  vec3 colBase, colStripe;','  float wheel[3] = {}; // radians about local +X; main L/R and nose/tail\n  vec3 colBase, colStripe;')
    s=once(s,'struct FrameParams {','struct FrameParams {\n  std::vector<GroundVehicleVisual> groundVehicles; // explicitly driven vehicles only; no automatic scenery motion')
    s=once(s,'  std::vector<Ent> entStage;','  std::vector<Ent> entStage;\n  uint64_t groundShadowKey[2] = {};')
    files[n]=s
    n='src/renderer.cpp';s=files[n]
    s=once(s,'glUniform4fv(U(p, "uPr"), 1, pv.Pr);','glUniform4fv(U(p, "uPr"), 1, pv.Pr); glUniform3f(U(p, "uWheel"), pv.wheel[0], pv.wheel[1], pv.wheel[2]);')
    files[n]=s
    n='src/shaders/scene_uniforms.glsl';s=files[n]
    s=once(s,'uniform vec4 uM[24];','uniform vec3 uWheel; // player wheel pose, advanced by simulation\nuniform vec4 uM[24];')
    s=once(s,'vec4 gM[24];','vec3 gWheel;\nvec4 gM[24];')
    s=once(s,'void loadMain(){ gOwn = true;','void loadMain(){ gOwn = true; gWheel = uWheel;')
    s=once(s,'  gOwn = false; gTrafK = k;','  gOwn = false; gTrafK = k;\n  gWheel = vec3(texelFetch(uTraffic,ivec2(25,k),0).w,texelFetch(uTraffic,ivec2(26,k),0).w,texelFetch(uTraffic,ivec2(27,k),0).w);')
    files[n]=s
    n='src/shaders/plane_material.glsl';s=files[n]
    s=once(s,'  vec3 q = f.xyz; float r = f.w, rad = length(q.yz);','  vec3 q = f.xyz;\n  float angle = braked ? (p.x < 0.0 ? gWheel.x : gWheel.y) : gWheel.z;\n  q.yz = rot2(q.yz, -angle); // inverse SPIN in the deployed/steered wheel rest frame\n  float r = f.w, rad = length(q.yz);')
    files[n]=s
    n='src/entity_mesh.h';s=files[n]
    s=once(s,'P_BEACON, P_FENCE, P_OBST };','P_BEACON, P_FENCE, P_OBST, P_WHEEL0, P_WHEEL1, P_WHEEL2, P_WHEEL3, P_WHEEL4, P_WHEEL5 };')
    files[n]=s
    n='src/entity_mesh.cpp';s=files[n]
    s=once(s,'  std::vector<EVert>& o;','  std::vector<EVert>& o;\n  int wheelSlot = 0;')
    s=once(s,'void wheel(MB& mb, vec3 c, float r, float w, int segs) { mb.cyl(c - vec3(w * 0.5f, 0, 0), c + vec3(w * 0.5f, 0, 0), r, r, segs, P_DARK, true, true); }',
'''void wheel(MB& mb, vec3 c, float r, float w, int segs) {
  const size_t start=mb.o.size();
  mb.cyl(c-vec3(w*.5f,0,0),c+vec3(w*.5f,0,0),r,r,segs,P_WHEEL0+mb.wheelSlot++,true,true);
  // Wheel vertices alone use the auxiliary fields as radius + pivot Y/Z. Ent and EVert strides stay unchanged.
  for(size_t i=start;i<mb.o.size();++i) { mb.o[i].ao=r;mb.o[i].u=c.y;mb.o[i].v=c.z; }
}''')
    s=once(s,'      ranges[k].first[l] = (int)out.size();','      mb.wheelSlot = 0;\n      ranges[k].first[l] = (int)out.size();')
    files[n]=s
    n='src/shaders/ent_vs.glsl';s=files[n]
    s=once(s,'uniform mat4 uVP;','uniform vec4 uWheel0; uniform vec2 uWheel1; // dynamic packet; all zero for parked scenery\nuniform mat4 uVP;')
    s=once(s,'  vec3 lp = aPos*iB.xyz;',
'''  vec3 posed=aPos, posedN=aNrm;
  int wheel=int(aAux.x+.5)-27;
  if(wheel>=0 && wheel<6) {
    float a=wheel<4?uWheel0[wheel]:uWheel1[wheel-4], c=cos(a),s=sin(a);
    vec2 q=aPos.yz-aAux.zw;
    posed.yz=aAux.zw+vec2(c*q.x-s*q.y,s*q.x+c*q.y);
    posedN.yz=vec2(c*aNrm.y-s*aNrm.z,s*aNrm.y+c*aNrm.z);
  }
  vec3 lp = posed*iB.xyz;''')
    s=once(s,'  vec3 ln = normalize(aNrm/iB.xyz);','  vec3 ln = normalize(posedN/iB.xyz);')
    s=once(s,'  vW = wp; vL = lp; vLN = ln; vAux = aAux;','  vW = wp; vL = wheel>=0 && wheel<6 ? aPos*iB.xyz : lp; vLN = ln; vAux = aAux;')
    files[n]=s
    n='src/shaders/ent_fs2.glsl';s=files[n]
    s=once(s,'    else if (part == P_DARK) { alb = uKind == K_GAPLANE',
'''    else if(part>=27 && part<33) {
      // Finish follows the wheel's rest coordinates; its mesh/normal rotate together in both passes.
      vec2 q=mp.yz-vAux.zw;float r=max(vAux.y,.01),rad=length(q)/r;
      float aa=max(fwidth(rad),.002);
      alb=vec3(.025);rough=.85;
      if(abs(n0.x)>.85 && rad<.62) {
        alb=vec3(.48,.50,.53);metal=.8;rough=.3;
        float a=atan(q.y,q.x),sector=1.04719755;
        float spoke=.5+.5*cos(a*6.0);alb*=mix(.32,1.0,smoothstep(.35,.65,spoke));
        float hub=1.0-smoothstep(.18,.18+aa,rad);alb=mix(alb,vec3(.6),hub);
      }
    }
    else if (part == P_DARK) { alb = uKind == K_GAPLANE''')
    files[n]=s
    n='src/entity_render.cpp';s=files[n]
    s=once(s,'struct Draw { int kind, lod; size_t first; int count; };','struct Draw { int kind, lod; size_t first; int count; int vehicle = -1; };')
    anchor='  // ------------------------------------------------ gather instances into (pass, kind, lod) buckets'
    s=once(s,anchor,
'''  // Refresh only cascades affected by a changed dynamic vehicle. Removing a vehicle refreshes its old shadow.
  uint64_t nextGroundKey[2] = {};
  for(int c=0;c<2 && sunUp && !feedPass;++c) {
    vec3 center=shDirty[c]?newCenter[c]:shCenter[c];
    nextGroundKey[c]=groundVehicleShadowKey(fp.groundVehicles,center,cR[c],shReach);
    if(nextGroundKey[c]!=groundShadowKey[c]) shDirty[c]=true;
  }
''' + anchor)
    anchor='  if (!feedPass) { entDrawn = 0; for (auto& d : draws[0]) entDrawn += d.count; }'
    s=once(s,anchor,
'''  // Dynamic vehicles use the same meshes/Ent instance layout, with a separate pose per small draw.
  for(int vi=0;vi<(int)fp.groundVehicles.size();++vi) {
    const auto& v=fp.groundVehicles[vi];if(!validGroundVehicle(v)) continue;
    const Ent& e=v.entity;int k=v.kind;float d=length(vec3(e.x,e.y,e.z)-cam),l0,l1;lodLimits(R,k,l0,l1);
    int lod=d<l0?0:d<l1?1:2;
    size_t first=entStage.size();entStage.push_back(e);
    if(d<rangeOf(R,k) && entRange[k].count[lod]>0) draws[0].push_back({k,lod,first,1,vi});
    for(int c=0;c<2 && sunUp && !feedPass;++c) {
      const auto& info=kEntInfo[k];float pad=std::max(info.hx*e.sx,info.hz*e.sz)+info.h*e.sy*shReach+60.f;
      if(shDirty[c] && std::fabs(e.x-newCenter[c].x)<cR[c]+pad && std::fabs(e.z-newCenter[c].z)<cR[c]+pad)
        draws[1+c].push_back({k,0,first,1,vi});
    }
  }
''' + anchor)
    s=once(s,'    for (const Draw& d : list) {',
'''    GLint uw0=glGetUniformLocation(prog,"uWheel0"),uw1=glGetUniformLocation(prog,"uWheel1");
    for (const Draw& d : list) {
      if(d.vehicle>=0) { const float* a=fp.groundVehicles[d.vehicle].angle;glUniform4fv(uw0,1,a);glUniform2f(uw1,a[4],a[5]); }
      else { glUniform4f(uw0,0,0,0,0);glUniform2f(uw1,0,0); }''')
    s=once(s,'    issue(progEntSh, draws[1 + c]);','    issue(progEntSh, draws[1 + c]);\n    groundShadowKey[c]=nextGroundKey[c];')
    files[n]=s

    # All identity checks and patches above complete before the first write.
    for n,s in files.items(): (root/n).write_text(s)
    for name in ('wheel_motion.h','ground_vehicle.h'): (root/'src'/name).write_text((HERE/name).read_text())
    import shutil
    shutil.copytree(HERE,root/'prototypes/wheel_animation')
    cm=root/'CMakeLists.txt';cm.write_text(cm.read_text()+'\n'+(HERE/'targets.cmake').read_text())
    changes=list(files)+['src/wheel_motion.h','src/ground_vehicle.h']
    (root/'wheel-stage.json').write_text(json.dumps({'base':'db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6','optional_wheel_integration':True,'files':changes,'existing_instance_and_vertex_strides_changed':False},indent=2)+'\n')
    print('Prepared wheel integration:',len(changes),'source files; production checkout unchanged.')

if __name__=='__main__':main()
