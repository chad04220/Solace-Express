// Somnium Volandi: shared identity and bounded, UI-only ambient drawing.
#pragma once
#include "renderer.h"

namespace svui {
inline const vec3 ink(.018f,.026f,.045f), panel(.037f,.052f,.077f), brass(.91f,.74f,.47f);
inline const vec3 text(.96f,.95f,.91f), dim(.64f,.71f,.78f), ice(.49f,.78f,.86f);
inline const vec3 code(.34f,.70f,1.f), codeDim(.48f,.63f,.80f), codeInk(.006f,.014f,.040f);
inline const vec3 codePanel(.018f,.040f,.092f), codeHead(.76f,.88f,1.f);
inline const vec3 field(.025f,.031f,.027f), fieldPanel(.070f,.081f,.065f), olive(.70f,.77f,.49f), amber(.93f,.69f,.36f);
inline uint32_t hash(uint32_t n) { n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; return n ^ (n >> 16); }
inline float lineDistance(float x,float y,float ax,float ay,float bx,float by) {
  const float dx=bx-ax,dy=by-ay,t=clampf(((x-ax)*dx+(y-ay)*dy)/std::max(.0001f,dx*dx+dy*dy),0.f,1.f);
  return hypotf(x-ax-dx*t,y-ay-dy*t);
}
// Resolution and text submissions are bounded. Only the UI's existing glyph atlas
// is used; no textures, simulation work, persistent particles or extra GL context.
inline void matrix(Renderer& r,float x,float y,float w,float h,float time,float alpha,bool motion=true,float quietX=0,float quietY=0,float quietRX=0,float quietRY=0) {
  if(w<=0||h<=0||alpha<=0)return;
  const float t=motion?std::max(0.f,time):7.f;
  const int cols=std::clamp(int(w/18.f),24,76),rows=std::clamp(int(h/20.f),16,40);
  const float dx=w/cols,dy=h/rows,fs=std::min(dx*1.12f,dy*.78f);
  static const char glyphs[]="0123456789ABCDEF<>/:=+{}[]";
  const int frame=int(t*12.f),cycle=int(t/18.f)%3;
  const float phase=fmodf(t,18.f),reveal=smoothstepf(3.f,6.f,phase)*(1.f-smoothstepf(13.f,16.f,phase));
  // Faces, swept wings and a helix resolve out of the same code field.
  for(int row=0;row<rows;++row) {
    for(int col=0;col<cols;++col) {
      const uint32_t seed=hash(uint32_t(col*7919+row*104729));
      const float nx=(col+.5f)/cols*2.f-1.f,ny=(row+.5f)/rows*2.f-1.f;
      const float head=fmodf(t*(3.f+(hash(col+31)%400)/100.f)+(hash(col+7)%rows),float(rows+12));
      const float trail=head-row;int band=trail>=0&&trail<9?(trail<1?3:trail<4?2:1):0;
      // Thin packet traces flow across, independently of the falling columns.
      const float packet=fmodf(t*(8.f+row%4)+row*13.f,float(cols+18));
      if(row%7==0 && col>packet-12 && col<packet)band=std::max(band,2);
      bool motif=false;
      if(cycle==0) {
        const float oval=sqrtf(nx*nx/.20f+ny*ny/.52f);
        motif=fabsf(oval-1.f)<.06f || (ny<-.22f&&ny>-.28f&&fabsf(nx)<.32f&&fabsf(nx)>.06f);
        motif=motif || fabsf((nx-.19f)*(nx-.19f)/.012f+(ny+.14f)*(ny+.14f)/.006f-1.f)<.7f;
        motif=motif || fabsf((nx+.19f)*(nx+.19f)/.012f+(ny+.14f)*(ny+.14f)/.006f-1.f)<.7f;
        motif=motif || lineDistance(nx,ny,0,-.10f,-.055f,.17f)<.027f || lineDistance(nx,ny,-.055f,.17f,.07f,.19f)<.027f;
        motif=motif || (fabsf(nx)<.20f && fabsf(ny-(.39f-.8f*nx*nx))<.027f);
      } else if(cycle==1) {
        const float ax=fabsf(nx);
        motif=lineDistance(ax,ny,0,-.65f,.78f,.16f)<.035f || lineDistance(ax,ny,.78f,.16f,.18f,.03f)<.035f;
        motif=motif || lineDistance(ax,ny,.18f,.03f,0,.62f)<.035f || lineDistance(ax,ny,0,-.65f,0,.62f)<.035f;
      } else {
        const float a=sinf(ny*8.f+t*.15f)*.40f;
        motif=fabsf(nx-a)<.035f||fabsf(nx+a)<.035f;
        motif=motif || (row%4==0&&fabsf(nx)<fabsf(a)&&fabsf(ny)<.8f);
      }
      if(motif&&reveal>.05f)band=std::max(band,reveal>.65f?3:2);
      char c=glyphs[hash(seed+uint32_t(frame+col*17))%(sizeof(glyphs)-1)];
      // The regular UI font is proportional. Fixed cell origins (rather than
      // space-padded strings) keep the face contours and code columns aligned.
      const float intensity=motif?(.12f+1.4f*reveal):(band==0?.08f:band==1?.18f:band==2?.40f:.78f);
      float quiet=1.f;
      if(quietRX>0&&quietRY>0){const float ex=(x+col*dx-quietX)/quietRX,ey=(y+row*dy-quietY)/quietRY;quiet=.035f+.965f*smoothstepf(.65f,1.8f,ex*ex+ey*ey);}
      if(band || seed%4==0)r.text(x+col*dx,y+row*dy,fs,std::string(1,c),band==3?codeHead:code,alpha*intensity*quiet,0,false);
    }
  }
}
inline void fieldSurface(Renderer& r,float x,float y,float w,float h,float s,float a=1.f) {
  r.rectGrad(x,y,w,h,fieldPanel,field,.97f*a,2*s);
  r.rectOutline(x,y,w,h,olive,.24f*a,2*s,s);
  // Anodised rails, restrained wear and recessed fasteners, outside the content.
  r.rect(x,y,3*s,h,olive,.40f*a);
  for(float fy:{y+8*s,y+h-10*s})for(float fx:{x+8*s,x+w-10*s}) {
    r.rect(fx,fy,3*s,3*s,dim,.35f*a,1.5f*s);
    r.line(fx,fy+1.5f*s,fx+3*s,fy+1.5f*s,s,field,a);
  }
  for(int i=0;i<9;++i) {
    const float fx=x+w-52*s+(hash(i+79)%42)*s,fy=y+3*s+(hash(i+81)%4)*s;
    r.line(fx,fy,fx+(2+i%4)*s,fy-.5f*s,s,dim,.10f*a);
  }
}
inline void chevrons(Renderer& r,float x,float y,float w,float s,vec3 c,float a) {
  for(float p=0;p+10*s<w;p+=18*s)r.line(x+p,y+5*s,x+p+8*s,y,s*2,c,a);
}
inline void bar(Renderer& r,float x,float y,float w,float height,float p,vec3 c,float a=1.f) {
  p=clampf(p,0.f,1.f);r.rect(x,y,w,height,c,.13f*a,height*.3f);
  if(p>0)r.rectGrad(x,y,w*p,height,c*.65f,c,a,height*.3f);
}
} // namespace svui
