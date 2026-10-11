// Dedicated showroom scenery. The aircraft itself uses the normal specialized mesh,
// moving-part and shadow paths; this geometry replaces only the outdoor world.
#pragma once
#include "common.h"
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
    // Narrow horizontal safety stripe along each wall.
    box(vec3(side*(1.35f*s-.13f),.12f*s,0),vec3(.016f,.014f*s,1.6f*s),vec3(.58f,.37f,.09f));
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
  // Service cabinet / workbench and stacked storage, kept outside the wingspan.
  box(vec3(-.98f*s,.06f*s,.74f*s),vec3(.15f*s,.06f*s,.08f*s),vec3(.11f,.24f,.30f));
  box(vec3(-.98f*s,.125f*s,.74f*s),vec3(.16f*s,.005f*s,.085f*s),steel);
  for (int i=0;i<3;++i) box(vec3(-.98f*s,(.025f+i*.032f)*s,.655f*s),vec3(.115f*s,.003f*s,.004f*s),vec3(.42f,.50f,.53f));
  for (int i=0;i<3;++i) box(vec3((.91f+i*.13f)*s,.06f*s,.82f*s),vec3(.055f*s,.06f*s,.07f*s),vec3(.25f,.29f,.28f));
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
uniform vec3 uOrigin,uEye,uBack; uniform float uSize;
layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1;
layout(location=2) out vec4 oG2; layout(location=3) out vec4 oG3;
vec2 oct(vec3 n){n/=abs(n.x)+abs(n.y)+abs(n.z);return n.y>=0.0?n.xz:(1.0-abs(n.zx))*sign(n.xz+vec2(1e-9));}
float stripe(float p,float width){float aa=max(fwidth(p),.001);return 1.0-smoothstep(width-aa,width+aa,abs(p));}
void main(){
 vec3 n=normalize(vN),c=vColour,em=vec3(0); float rough=.65;
 if(vKind==1 && n.y>.5){
   vec2 grid=abs(fract(vP.xz/3.0+.5)-.5)*3.0;
   float seam=1.0-smoothstep(.008,.008+max(fwidth(vP.x),fwidth(vP.z)),min(grid.x,grid.y));
   c*=1.0-.2*seam;
   // Painted stand bounds, centre lead-in and transverse stop bar.
   float bounds=max(stripe(abs(vP.x)-.64*uSize,.035),stripe(abs(vP.z)-.63*uSize,.035));
   bounds*=float(abs(vP.x)<.65*uSize && abs(vP.z)<.64*uSize);
   float lead=stripe(vP.x,.025)*float(vP.z<-.40*uSize);
   float stop=stripe(vP.z+.42*uSize,.04)*float(abs(vP.x)<.15*uSize);
   c=mix(c,vec3(.72,.48,.10),max(bounds,max(lead,stop))*.82);
   rough=.43;
 } else if(vKind==2){
   float rib=.5+.5*cos(vP.x*15.0+vP.z*15.0);
   c*=.92+.08*rib;
 } else if(vKind==3){em=c*5.0;rough=.25;}
 vec3 rel=vP+uOrigin-uEye; float t=length(rel);
 oG0=vec4(t,oct(n),3.0);oG1=vec4(sqrt(clamp(c,0.0,1.0)),rough);
 oG2=vec4(em,0.05);oG3=vec4(1,1,1,0);
 // Same logarithmic convention as the aircraft hull pass, exact per pixel.
 float w=-dot(rel,uBack);gl_FragDepth=log2(max(1e-6,1.0+w))/log2(40001.0);
}
)GLSL";
// Classified showroom: only geometry/depth are inputs. Albedo, registrations,
// cockpit displays, navigation lights and emission cannot leak into this image.
static const char* kClassifiedFS = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uGeometry; uniform vec3 uRight,uUp,uBack;
uniform vec2 uJit; uniform float uTanHalf,uAspect;
layout(location=0) out vec4 oColor; layout(location=1) out float oDepth;
layout(location=2) out float oCloudMask;
vec3 decodeNormal(vec2 e){vec3 n=vec3(e.x,1.0-abs(e.x)-abs(e.y),e.y);
 if(n.y<0.0)n.xz=(1.0-abs(n.zx))*sign(n.xz+vec2(1e-9));return normalize(n);}
void main(){
 vec4 g=texelFetch(uGeometry,ivec2(gl_FragCoord.xy),0);
 int cls=int(g.w+.5);oDepth=cls==0?1e6:g.x;oCloudMask=0.0;
 vec2 ndc=(vUV+uJit)*2.0-1.0;
 vec3 ray=normalize(uRight*ndc.x*uAspect*uTanHalf+uUp*ndc.y*uTanHalf-uBack);
 vec3 n=decodeNormal(g.yz);
 bool aircraft=cls>=5;
 float rim=pow(1.0-clamp(abs(dot(n,ray)),0.0,1.0),3.0);
 float backlight=max(dot(n,normalize(vec3(-.4,.7,.6))),0.0);
 // Low-key silhouette with a visible cool rim after tone mapping; material detail stays hidden.
 vec3 c=aircraft?vec3(.28,.48,.65)*(.0015+.080*rim+.003*backlight):
                 vec3(.35,.45,.55)*(.0006+.0012*max(n.y,0.0));
 if(cls==0)c=vec3(.0003,.0005,.0007);
 oColor=vec4(c,aircraft?.5:1.0);
}
)GLSL";
} // namespace hangarPreview
