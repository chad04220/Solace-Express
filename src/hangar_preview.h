// Dedicated showroom scenery. The aircraft itself uses the normal specialized mesh,
// moving-part and shadow paths; this geometry replaces only the outdoor world.
#pragma once
#include "common.h"
#include "font_data.h"
#include <array>
#include <vector>

namespace hangarPreview {
struct Vertex { vec3 p, n, colour; float kind; };
inline std::vector<Vertex> geometry(float s) {
  std::vector<Vertex> v;
  auto box = [&](vec3 c, vec3 h, vec3 col, float kind = 0.f) {
    for (int axis = 0; axis < 3; ++axis) for (int sign : {-1, 1}) {
      vec3 n(0), u(0), w(0);
      if (axis == 0) { n.x = (float)sign; u.z = h.z; w.y = h.y; }
      if (axis == 1) { n.y = (float)sign; u.x = h.x; w.z = h.z; }
      if (axis == 2) { n.z = (float)sign; u.x = h.x; w.y = h.y; }
      vec3 f = c + vec3(n.x*h.x, n.y*h.y, n.z*h.z);
      vec3 p[4] = {f-u-w, f+u-w, f+u+w, f-u+w};
      const bool reverse = dot(cross(u, w), n) < 0.f;
      for (int i : {0, 1, 2, 0, 2, 3}) v.push_back({p[reverse ? (4-i)%4 : i], n, col, kind});
    }
  };
  const vec3 concrete(.22f,.26f,.29f), wall(.13f,.19f,.23f), steel(.055f,.085f,.105f);
  box(vec3(0,-.18f,0), vec3(1.35f*s,.18f,1.6f*s), concrete, 1);
  box(vec3(0,.36f*s,1.15f*s), vec3(1.35f*s,.36f*s,.12f), wall, 2);
  for (int side : {-1,1}) {
    box(vec3(side*1.35f*s,.36f*s,0), vec3(.12f,.36f*s,1.6f*s), wall, 2);
    // Human-height safety stripe: the room grows, its metre-scale cues do not.
    box(vec3(side*(1.35f*s-.13f),1.2f,0),vec3(.016f,.08f,1.6f*s),vec3(.58f,.37f,.09f));
  }
  box(vec3(0,.72f*s,0),vec3(1.35f*s,.1f,1.6f*s),vec3(.065f,.085f,.10f));
  for (int i=0;i<7;++i) {
    float z=(-1.45f+i*.42f)*s;
    for (int side : {-1,1}) box(vec3(side*1.29f*s,.36f*s,z),vec3(.035f*s,.36f*s,.025f*s),steel);
    box(vec3(0,.69f*s,z),vec3(1.3f*s,.027f*s,.025f*s),steel);
    box(vec3(0,.62f*s,z),vec3(1.3f*s,.012f*s,.015f*s),steel);
    for (int j=-3;j<=3;++j) box(vec3(j*.37f*s,.655f*s,z),vec3(.009f*s,.035f*s,.015f*s),steel);
  }
  for (int side : {-1,1}) for (float z : {-.35f,.45f}) {
    box(vec3(side*.55f*s,.665f*s,z*s),vec3(.019f*s,.012f*s,.26f*s),steel);
    box(vec3(side*.55f*s,.651f*s,z*s),vec3(.013f*s,.003f*s,.25f*s),vec3(.75f,.88f,1.f),3);
  }
  // Rear rolling doors and architectural light reveal.
  for (int i=-4;i<=4;++i) box(vec3(i*.245f*s,.285f*s,1.015f*s),vec3(.12f*s,.28f*s,.027f*s),vec3(.18f,.23f,.27f),2);
  box(vec3(0,.58f*s,.98f*s),vec3(1.1f*s,.006f*s,.008f*s),vec3(.14f,.65f,.82f),3);
  // All vertices are in world metres. Only the furniture's location follows
  // the aircraft-sized room; a 0.90 m workbench must not become a Galleon-sized
  // cabinet. Keep these rear service bays clear of the full rotating airframe.
  const vec3 bench(-.98f*s,0,.74f*s);
  box(bench+vec3(0,.425f,0),vec3(1.10f,.425f,.38f),vec3(.11f,.24f,.30f));
  box(bench+vec3(0,.875f,0),vec3(1.20f,.025f,.425f),steel);
  for (int i=0;i<3;++i) box(bench+vec3(0,.18f+i*.24f,-.404f),vec3(.90f,.018f,.028f),vec3(.42f,.50f,.53f));
  for (int i=0;i<3;++i) box(vec3(.91f*s+i*1.1f,.45f,.82f*s),vec3(.40f,.45f,.50f),vec3(.25f,.29f,.28f));
  return v;
}

// Measured exterior-mesh bounds in aircraft body coordinates, not catalogue
// specifications. The renderer supplies the same body-to-world axes as the
// parked model; positions here are relative to the hangar floor origin.
struct DimensionInput {
  float minX=0, maxX=0, noseZ=0, tailZ=0;
  float wingTipY=0, wingTipZ=0, noseY=0, tailY=0;
  vec3 position, spanAxis=vec3(1,0,0), upAxis=vec3(0,1,0), lengthAxis=vec3(0,0,1);
  vec3 eye=vec3(0,3,-10), cameraRight=vec3(-1,0,0);
  bool visible=true;
};
inline constexpr float kDimensionFloorY=.012f;
struct FloorDimension {
  float metres=0, cellWidth=0, cellHeight=0;
  vec3 witnessA, witnessB, a, b, labelCentre, textRight, textUp;
  std::string label, name, value;
};
inline std::array<FloorDimension,2> dimensionLayout(const DimensionInput& in) {
  std::array<FloorDimension,2> result{};
  const float span=in.maxX-in.minX, fuselage=in.tailZ-in.noseZ;
  if (!in.visible || !std::isfinite(span+fuselage) || span<=0 || fuselage<=0) return result;
  auto finite=[](vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
  if (!finite(in.position) || !finite(in.spanAxis) || !finite(in.upAxis) || !finite(in.lengthAxis) ||
      !finite(in.eye) || !finite(in.cameraRight) || !std::isfinite(in.wingTipY+in.wingTipZ+in.noseY+in.tailY)) return result;
  auto onFloor=[](vec3 p) { return vec3(p.x,kDimensionFloorY,p.z); };
  auto body=[&](vec3 p) { return onFloor(in.position+in.spanAxis*p.x+in.upAxis*p.y+in.lengthAxis*p.z); };
  const vec3 across=normalize(vec3(in.spanAxis.x,0,in.spanAxis.z));
  const vec3 along=normalize(vec3(in.lengthAxis.x,0,in.lengthAxis.z));
  vec3 right=normalize(vec3(in.cameraRight.x,0,in.cameraRight.z));
  if (length(across)<.99f || length(along)<.99f || length(right)<.99f) return result;
  vec3 up=cross(vec3(0,1,0),right);
  // Face the viewer on the floor even after an aircraft half-turn. This is
  // actual horizontal lettering, not a screen overlay or vertical billboard.
  const vec3 view=in.eye-in.position;
  if (dot(up,view)>0) up=-up;
  const float size=std::max(span,fuselage), gap=std::max(.55f,size*.045f);
  const float foreshorten=clampf(fabsf(view.y)/std::max(length(view),.001f),.16f,1.f);
  const float letterHeight=size*.027f;
  const float cellWidth=letterHeight/7.f, cellHeight=letterHeight/(7.f*foreshorten);
  const float sideAcross=dot(view,across)>=0?1.f:-1.f, sideAlong=dot(view,along)>=0?1.f:-1.f;
  const vec3 points[]={body(vec3(in.minX,in.wingTipY,in.wingTipZ)),body(vec3(in.maxX,in.wingTipY,in.wingTipZ)),
                       body(vec3(0,in.noseY,in.noseZ)),body(vec3(0,in.tailY,in.tailZ))};
  // Keep both captions within the clear central preview stage, rather than
  // behind its menu sidebars. Leaders retain the connection to each ruler.
  const vec3 foreground=-up, centre=body(vec3((in.minX+in.maxX)*.5f,0,(in.noseZ+in.tailZ)*.5f));
  float front=dot(points[0],foreground);
  for (const vec3 p:points) front=std::max(front,dot(p,foreground));
  const vec3 labelOrigin=centre+foreground*(front-dot(centre,foreground)+gap+cellHeight*7.5f);
  for (int i=0;i<2;++i) {
    auto& d=result[i];
    const vec3 outward=i==0?along*sideAlong:across*sideAcross;
    d.metres=i==0?span:fuselage;
    d.name=i==0?"WINGSPAN":"LENGTH"; d.value=fmt("%.2f m",d.metres); d.label=d.name+" "+d.value;
    d.witnessA=points[i*2]; d.witnessB=points[i*2+1];
    float edge=dot(points[0],outward);
    for (const vec3 p:points) edge=std::max(edge,dot(p,outward));
    edge+=gap;
    d.a=d.witnessA+outward*(edge-dot(d.witnessA,outward));
    d.b=d.witnessB+outward*(edge-dot(d.witnessB,outward));
    d.cellWidth=cellWidth; d.cellHeight=cellHeight; d.textRight=right; d.textUp=up;
    d.labelCentre=labelOrigin+right*((i==0?-.14f:.14f)*size);
    // Compensate at the lettering, not at the more distant aircraft. Otherwise
    // foreground paint becomes unnaturally stretched as the eye approaches it.
    for (int fit=0;fit<3;++fit) {
      const vec3 toEye=in.eye-d.labelCentre;
      const float labelForeshorten=clampf(fabsf(toEye.y)/std::max(length(toEye),.001f),.18f,1.f);
      d.cellHeight=cellWidth/labelForeshorten;
      d.labelCentre=centre+foreground*(front-dot(centre,foreground)+gap+d.cellHeight*9.f)
        +right*((i==0?-.14f:.14f)*size);
    }
  }
  return result;
}

// The same proportional, linearly filtered SDF font as the rest of the UI,
// placed on horizontal world-space quads. Kind 5 carries atlas UV in n.xz;
// the fragment shader restores the physical floor normal before lighting.
inline std::vector<Vertex> dimensionGeometry(const DimensionInput& in) {
  std::vector<Vertex> v;
  const auto layout=dimensionLayout(in);
  if (layout[0].label.empty()) return v;
  const vec3 paint(.70f,.40f,.09f), normal(0,1,0);
  auto quad=[&](vec3 a,vec3 b,vec3 c,vec3 d) {
    if (dot(cross(b-a,c-a),normal)<0) std::swap(b,d);
    for (const vec3 p:{a,b,c,a,c,d}) v.push_back({p,normal,paint,4.f});
  };
  auto line=[&](vec3 a,vec3 b,float width) {
    const vec3 direction=normalize(b-a);
    if (length(direction)<.99f) return;
    const vec3 side=cross(direction,normal)*(width*.5f);
    quad(a-side,b-side,b+side,a+side);
  };
  const float size=std::max(layout[0].metres,layout[1].metres), width=std::max(.05f,size*.006f);
  for (const auto& d:layout) {
    const vec3 tangent=normalize(d.b-d.a), across=cross(tangent,normal);
    line(d.a,d.b,width);
    line(d.witnessA,d.a+normalize(d.a-d.witnessA)*size*.015f,width*.65f);
    line(d.witnessB,d.b+normalize(d.b-d.witnessB)*size*.015f,width*.65f);
    for (const vec3 p:{d.a,d.b}) line(p-across*size*.018f,p+across*size*.018f,width);
    line((d.a+d.b)*.5f,d.labelCentre+d.textUp*(d.cellHeight*9.f),width*.65f);
    for (int textRow=0;textRow<2;++textRow) {
      const std::string& text=textRow==0?d.name:d.value;
      const float scale=textRow==0?.82f:1.f;
      const float sx=d.cellWidth*7.f/FONT_EM*scale, sy=d.cellHeight*7.f/FONT_EM*scale;
      float textWidth=0;for (unsigned char ch:text) textWidth+=FONT_ADV[ch-32]*sx;
      const vec3 origin=d.labelCentre-d.textRight*(textWidth*.5f)
        +d.textUp*(d.cellHeight*7.f*(textRow==0?1.10f:-.20f));
      float cursor=0;
      for (unsigned char ch:text) {
        const int gi=ch-32;
        if(ch!=' ') {
          const float u0=float((gi%16)*FONT_CELLW)/FONT_W, v0=float((gi/16)*FONT_CELLH)/FONT_H;
          const float u1=u0+float(FONT_CELLW)/FONT_W, v1=v0+float(FONT_CELLH)/FONT_H;
          const vec3 a=origin+d.textRight*(cursor-FONT_PAD*sx)+d.textUp*(FONT_PAD*sy);
          const vec3 b=a+d.textRight*(FONT_CELLW*sx), down=d.textUp*(-FONT_CELLH*sy);
          Vertex q[4]={{a,vec3(u0,0,v0),paint,5},{b,vec3(u1,0,v0),paint,5},
            {b+down,vec3(u1,0,v1),paint,5},{a+down,vec3(u0,0,v1),paint,5}};
          const bool reverse=dot(cross(q[1].p-q[0].p,q[2].p-q[0].p),normal)<0;
          for(int i:{0,1,2,0,2,3})v.push_back(q[reverse?(4-i)%4:i]);
        }
        cursor+=FONT_ADV[gi]*sx;
      }
    }
  }
  return v;
}
static const char* kVS = R"GLSL(#version 330 core
layout(location=0) in vec3 aP; layout(location=1) in vec3 aN;
layout(location=2) in vec3 aColour; layout(location=3) in float aKind;
uniform mat4 uVP; uniform vec3 uOrigin,uEye; uniform vec2 uJit;
out vec3 vP,vN,vColour; flat out int vKind;
void main(){ vP=aP; vN=aN; vColour=aColour; vKind=int(aKind+.5);
 gl_Position=uVP*vec4(aP+uOrigin-uEye,1);
 gl_Position.xy-=2.0*uJit*gl_Position.w;
 // Keep conventional homogeneous clipping: room quads may cross behind the eye.
 // The fragment shader writes the shared logarithmic depth after correct clipping.
}
)GLSL";
static const char* kFS = R"GLSL(#version 330 core
in vec3 vP,vN,vColour; flat in int vKind;
uniform vec3 uOrigin,uEye,uBack; uniform float uSize; uniform sampler2D uFont;
layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1;
layout(location=2) out vec4 oG2; layout(location=3) out vec4 oG3;
vec2 oct(vec3 n){n/=abs(n.x)+abs(n.y)+abs(n.z);return n.y>=0.0?n.xz:(1.0-abs(n.zx))*sign(n.xz+vec2(1e-9));}
float stripe(float p,float width){float aa=max(fwidth(p),.001);return 1.0-smoothstep(width-aa,width+aa,abs(p));}
vec3 floorColour(vec3 c){
 vec2 grid=abs(fract(vP.xz/3.0+.5)-.5)*3.0;
 float seam=1.0-smoothstep(.008,.008+max(fwidth(vP.x),fwidth(vP.z)),min(grid.x,grid.y));
 c*=1.0-.2*seam;
 // The parking envelope fits the airframe; slab spacing, paint widths and
 // the 3.6 m transverse stop bar stay in metres as useful scale references.
 float bounds=max(stripe(abs(vP.x)-.64*uSize,.035),stripe(abs(vP.z)-.63*uSize,.035));
 bounds*=float(abs(vP.x)<.65*uSize && abs(vP.z)<.64*uSize);
 float lead=stripe(vP.x,.025)*float(vP.z<-.40*uSize);
 float stop=stripe(vP.z+.42*uSize,.04)*float(abs(vP.x)<1.8);
 c=mix(c,vec3(.72,.48,.10),max(bounds,max(lead,stop))*.82);
 return c;
}
void main(){
 vec3 n=vKind==5?vec3(0,1,0):normalize(vN),c=vColour,em=vec3(0); float rough=.65;
 if(vKind==1 && n.y>.5){c=floorColour(c);rough=.43;
 } else if(vKind==2){
   float rib=.5+.5*cos(vP.x*15.0+vP.z*15.0);
   c*=.92+.08*rib;
 } else if(vKind==3){em=c*5.0;rough=.25;}
 else if(vKind==4){em=c*.10;rough=.65;}
 else if(vKind==5){
   float distance=texture(uFont,vN.xz).r;
   float aa=max(fwidth(distance)*.75,.002);
   float coverage=smoothstep(.5-aa,.5+aa,distance);
   if(coverage<.005)discard;
   // Blend edge coverage into the same concrete paint/grid material rather
   // than using screen-space text, hard alpha cuts, or alpha in a G-buffer.
   c=mix(floorColour(vec3(.22,.26,.29)),c,coverage);
   em=vColour*(.10*coverage);rough=mix(.43,.65,coverage);
 }
 vec3 rel=vP+uOrigin-uEye; float t=length(rel);
 oG0=vec4(t,oct(n),3.0);oG1=vec4(sqrt(clamp(c,0.0,1.0)),rough);
 oG2=vec4(em,0.05);oG3=vec4(1,1,1,0);
 // Same logarithmic convention as the aircraft hull pass, exact per pixel.
 float w=-dot(rel,uBack);gl_FragDepth=log2(max(1e-6,1.0+w))/log2(40001.0);
}
)GLSL";
// Classified showroom: a clear screen-space outline of the actual aircraft.
// Only geometry/depth are inputs; materials, screens and emission stay hidden.
static const char* kClassifiedFS = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uGeometry; uniform vec3 uRight,uUp,uBack;
uniform vec2 uJit; uniform float uTanHalf,uAspect;
layout(location=0) out vec4 oColor; layout(location=1) out float oDepth;
layout(location=2) out float oCloudMask;
void main(){
 ivec2 pixel=ivec2(gl_FragCoord.xy),limit=textureSize(uGeometry,0)-ivec2(1);
 vec4 g=texelFetch(uGeometry,pixel,0);
 int cls=int(g.w+.5);oDepth=cls==0?1e6:g.x;oCloudMask=0.0;
 bool aircraft=cls>=5;
 float edge=0.0;
 if(aircraft)for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
   vec4 nearG=texelFetch(uGeometry,clamp(pixel+ivec2(x,y)*2,ivec2(0),limit),0);
   if(int(nearG.w+.5)<5)edge=1.0;
 }
 // The interior is solid ink, even if the source mesh has emissive materials.
 // This outline remains legible at low exposure and after the terminal tint.
 vec3 c=vec3(.0003,.0005,.0010);
 if(aircraft)c=mix(c,vec3(.30,.60,.90)*.30,edge);
 oColor=vec4(c,aircraft?.5:1.0);
}
)GLSL";
} // namespace hangarPreview
