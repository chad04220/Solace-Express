#!/usr/bin/env python3
"""Guard complete-gear ownership in the production bake/color/depth/shadow path.

Numerical debris tests live in breakup_test.cpp. This source contract prevents a
renderer change from silently restoring attached wheels or duplicating fixed
mains in the static fuselage mesh. Actual raster inspection remains separate.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def read(name):
    text = (ROOT / name).read_text()
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text))

sdf = read("src/shaders/plane_sdf.glsl")
parts = read("src/shaders/plane_parts.glsl")
mesh = read("src/aircraft_mesh.cpp")
fleet = read("src/aircraft_mesh_build_fleet.h")
clip = read("src/shaders/wreck_clip.glsl")
vs = read("src/shaders/plane_mesh_vs.glsl")
shadow = read("src/raster_renderer.cpp")
game = read("src/game.cpp")
checks = 0
def has(source, text):
    global checks
    assert text in source, "Gear breakup render contract changed: " + text
    checks += 1

# The fixed main's original tyre/pant/leg function is used by both the direct
# field and PT33 extraction, with no static-bake duplicate. Both sides are posed
# from the same right-side shape, exactly as existing retracting main legs.
has(sdf, "vec2gearFixedMainShape(vec3ap)")
has(sdf, "if(k==PT_GEAR_MAIN&&gtype<=2)returngearFixedMainShape(l);")
has(sdf, "elseif(gPartMode==-1){res=opU(res,gearFixedMainShape(ap));}")
assert sdf.count("gearFixedMainShape(ap)") == 1
checks += 1
has(parts, "if(gtype<=2){X.R=S;returnX;}")
has(fleet, "for(ints=-1;s<=1;s+=2){out[n++]={PT_GEAR_MAIN,(float)s,0};if(gtype>=3)")
has(fleet, "if(type==PT_GEAR_MAIN&&gtype<=2){lo=vec3(0.f,-gh-.1f,mz-wr-.7f);hi=vec3(track+.3f,1.5f,mz+wr+.7f);")

# Explicit assembly identity wins over position; a long leg's midpoint may be
# inside the fuselage, and a partly folding one can cross another owner's box.
has(clip, "if(uPartType==33||uPartType==39||uPartType==40)gear=int(b.x<0.0?uGearOwner.x:uGearOwner.y);")
has(clip, "elseif(uPartType==34||uPartType==35||uPartType==41||uPartType==42)gear=int(uGearOwner.z);")
has(clip, "returngear>=0?gear:brkOwner(b);")
assert not re.search(r"uPartType==(?:36|37|43|44)(?!\d)", clip), "Bay doors must remain with the bay"
checks += 1
for source in (vs, shadow):
    has(source, "vPc=brkRigidOwner(R*uPartC+T);")
has(mesh, 'glUniform1i(U(prog,"uPartType"),type);')
has(mesh, 'glUniform3f(U(p,"uGearOwner"),(float)wv.gearOwner[0],(float)wv.gearOwner[1],(float)wv.gearOwner[2]);')
has(mesh, "if(wv.H[i].x<0.f)continue;")
has(game, "wv.H[i]=w.rigidOnly?vec3(-1.f):w.H;")
has(game, "if(w.kind==BK_GEAR)wv.gearOwner[w.side<0?0:w.side>0?1:2]=i;")
has(game, "std::fill(std::begin(wv.gearOwner),std::end(wv.gearOwner),-1);")
# Visible blades share the existing depth-tested disc path only for wrecks. The
# original intact player effect remains exclusive, so no blade copy stays behind.
props = read("src/prop_disc.cpp")
effects = read("src/shaders/effects_fs.glsl")
has(props, "constboolwreckProps=fp.plane.on&&fp.wreck.pieces>0&&fp.plane.propCount>0&&fp.pano<=0.f;")
has(props, "if(!progTrafficProps||((off||fp.trafficN<=0)&&!wreckProps))return;")
has(props, "propWreckOwner(fp.plane.prop[i],fp.wreck.pieces,fp.wreck.C,fp.wreck.H)")
has(props, "wreckPropGeometry(fp.plane.prop[i],fp.wreck.rot[owner],fp.wreck.pos[owner],")
has(effects, "if(FLEET_ON&&uPlaneOn==1&&uWreck==0)")
has(game, "fillPlaneVisual(captured.plane,plane,propAngle,false);")
has(game, "wraithVisual(captured);wreckPlanePose=captured.plane;")
has(game, "wraithVisual(fp);if(!wreck.empty()){constvec3worldPos=fp.plane.pos;")
has(game, "fp.plane=wreckPlanePose;fp.plane.pos=worldPos;memcpy(fp.plane.rot,worldRot,sizeofworldRot);")
has(game, "if(plane.spec&&wreck.empty()){floatrps=")
print(f"PASS: {checks} production gear/prop debris contracts; unique owners and captured separation poses")

models = read("src/models.cpp")
traffic = read("src/traffic.cpp")
has(parts, "returnisAtlas()?gM[18].y*.74:")
has(models, "idx==kAtlas?mainRadius*.74f:m.gear==3?mainRadius*.75f:mainRadius*.85f;")
has(game, "modelWheelRadii(idx,wr,nr);")
has(traffic, "modelWheelRadii(c.spec,wr,nr);")
has(traffic, "if(s.gearHeightM>0.f)returns.gearHeightM;")
has(traffic, "if(s.gearHeightM>0.f||s.gearTrackM>0.f){constGearStationsgs=gearStations(s);")
has(traffic, "cp[0]=vec3(-gs.track,-gh,gs.mainZ);cp[1]=vec3(gs.track,-gh,gs.mainZ);")
has(traffic, "cp[2]=s.taildragger?vec3(0,-gh+gs.tailY,gs.tailZ):vec3(0,-gh,gs.noseZ);")
print(f"PASS: {checks} combined render contracts, including authored tyre radii and traffic stations")
