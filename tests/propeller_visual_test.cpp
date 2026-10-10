// Production propeller GLSL regression: tapered stopped blades, energy-conserving blur,
// phase stability, and parity with the bounded AI/airline traffic shader. No scene compilation.
#include "../src/gl.h"
#include "../src/shaders.h"
#include <dlfcn.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>
static void* lib;
static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib, n); }
template <class F> static F sym(const char* n) { return reinterpret_cast<F>(dlsym(lib, n)); }
static bool initGL() {
  lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL); if (!lib) return false;
  eglProc = sym<void* (*)(const char*)>("eglGetProcAddress"); if (!eglProc) return false;
  auto gd = reinterpret_cast<void* (*)(unsigned, void*, const int*)>(eglProc("eglGetPlatformDisplayEXT")); if (!gd) return false;
  void* d = gd(0x31DD, nullptr, nullptr); int ma, mi;   // EGL_PLATFORM_SURFACELESS_MESA
  if (!sym<unsigned (*)(void*, int*, int*)>("eglInitialize")(d, &ma, &mi)) return false;
  sym<unsigned (*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[] = {0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038}; void* c; int n;
  if (!sym<unsigned (*)(void*, const int*, void**, int, int*)>("eglChooseConfig")(d, ca, &c, 1, &n) || !n) return false;
  const int sa[] = {0x3057, 16, 0x3056, 16, 0x3038}, at[] = {0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038};
  void* s = sym<void* (*)(void*, void*, const int*)>("eglCreatePbufferSurface")(d, c, sa);
  void* x = sym<void* (*)(void*, void*, void*, const int*)>("eglCreateContext")(d, c, nullptr, at);
  const char* m = nullptr;
  return x && sym<unsigned (*)(void*, void*, void*, void*)>("eglMakeCurrent")(d, s, s, x) && glLoad(proc, &m);
}
static GLuint compile(GLenum type, const std::string& src) {
  GLuint sh = glCreateShader(type); const char* p = src.c_str(); glShaderSource(sh, 1, &p, nullptr); glCompileShader(sh);
  GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); printf("shader compile failed: %s\n", log); return 0; }
  return sh;
}


static int failures = 0, checks = 0;
static void check(bool good, const char* why) { ++checks; if (!good) { ++failures; printf("FAIL: %s\n", why); } }
static GLuint program(const std::string& fs) {
  GLuint vs=compile(GL_VERTEX_SHADER,kFullscreenVS), f=compile(GL_FRAGMENT_SHADER,fs), p=glCreateProgram();
  if (!vs || !f) return 0;
  glAttachShader(p,vs); glAttachShader(p,f); glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  glDeleteShader(vs); glDeleteShader(f); return ok?p:0;
}
static constexpr int N=384;
static constexpr float scale=1.15f, tau=6.28318530718f;
static float alphaAt(const std::vector<float>& p, float r, float a) {
  int x=std::clamp(int((r*cosf(a)/scale*.5f+.5f)*N),0,N-1), y=std::clamp(int((r*sinf(a)/scale*.5f+.5f)*N),0,N-1);
  return p[(y*N+x)*4+3];
}
static void preview(const std::string& dir, const std::string& name, const std::vector<float>& p) {
  if(dir.empty()) return; std::filesystem::create_directories(dir);
  FILE* f=fopen((dir+"/"+name+".ppm").c_str(),"wb"); if(!f) return;
  fprintf(f,"P6\n%d %d\n255\n",N,N);
  for(int y=N-1;y>=0;--y) for(int x=0;x<N;++x) {
    size_t k=(y*N+x)*4; float a=p[k+3];
    for(int c=0;c<3;++c) { float bg=(x/24+y/24)%2?.64f:.75f;
      unsigned char v=(unsigned char)(255*powf(std::clamp(bg*(1-a)+p[k+c]*a,0.f,1.f),1.f/2.2f)); fwrite(&v,1,1,f); }
  }
  fclose(f);
}
int main(int argc, char** argv) {
  if(!initGL()) { puts("SKIP propeller visual: no EGL context"); return 77; }
  const std::string dir=argc>1?argv[1]:"";
  const std::string fs=std::string("#version 330 core\n")+kPropellerGLSL+
    "out vec4 oColor; uniform float uAngle,uBlur,uBlades,uProjectionScale;\n"
    "void main(){ vec2 q=(gl_FragCoord.xy/384.0*2.0-1.0)*1.15*uProjectionScale; oColor=propellerVisual(q,dFdx(q),dFdy(q),uAngle,uBlur,uBlades,.15,vec3(1)); }\n";
  GLuint p=program(fs), traffic=program(propDiscFSAssembly()); if(!p||!traffic) return 1;
  GLuint fbo,tex,depth,vao; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,N,N,0,GL_RGBA,GL_FLOAT,nullptr);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  GLenum out=GL_COLOR_ATTACHMENT0; glDrawBuffers(1,&out);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 1;
  std::vector<float> depths(N*N,1000.f); glGenTextures(1,&depth); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,depth);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,N,N,0,GL_RED,GL_FLOAT,depths.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glGenVertexArrays(1,&vao); glBindVertexArray(vao); glViewport(0,0,N,N); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
  glClearColor(0,0,0,0);
  auto render=[&](int blades,float blur,float angle,bool ai=false,float projectionScale=1.f) {
    GLuint prog=ai?traffic:p; glUseProgram(prog);
    auto u=[&](const char* n){return glGetUniformLocation(prog,n);};
    if(ai) {
      glUniform1i(u("uSceneDepth"),0); glUniform2f(u("uRes"),N,N); glUniform2f(u("uJit"),0,0); glUniform2f(u("uPano"),0,0);
      glUniform1f(u("uTanHalf"),scale*projectionScale/5); glUniform1f(u("uAspect"),1); glUniform1f(u("uFogB"),0);
      glUniform3f(u("uDiscCentre"),0,0,-5); glUniform3f(u("uDiscRight"),1,0,0); glUniform3f(u("uDiscUp"),0,1,0);
      glUniform4f(u("uDisc"),1,angle,blur,(float)blades); glUniform3f(u("uPropLight"),1,1,1); glUniform1f(u("uPropHub"),.15f); glUniform1i(u("uClassOnly"),0);
    } else { glUniform1f(u("uAngle"),angle); glUniform1f(u("uBlur"),blur); glUniform1f(u("uBlades"),(float)blades); glUniform1f(u("uProjectionScale"),projectionScale); }
    glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES,0,3); glFinish(); std::vector<float> pixels(N*N*4);
    glReadPixels(0,0,N,N,GL_RGBA,GL_FLOAT,pixels.data()); return pixels;
  };
  for(int blades:{2,3,4}) {
    auto stopped=render(blades,0,0), low=render(blades,.35f,0), high=render(blades,1,0), shifted=render(blades,1,1.79f);
    double energy[3]={}; int sectors=0; bool prev=alphaAt(stopped,.55f,-.003f)>.5f;
    for(int i=0;i<2048;++i) { bool cur=alphaAt(stopped,.55f,tau*i/2048)>.5f; sectors+=cur&&!prev; prev=cur; }
    check(sectors==blades,"stopped silhouette preserves exact model blade count");
    int broad=0,tip=0; float maxHigh=0; double maxPhase=0;
    for(int i=0;i<N*N;++i) {
      float r=hypotf(((i%N+.5f)/N*2-1)*scale,((i/N+.5f)/N*2-1)*scale);
      for(int s=0;s<3;++s) {
        const auto& frame=s==0?stopped:s==1?low:high;
        check(std::isfinite(frame[i*4+3])&&frame[i*4+3]>=-.00001f&&frame[i*4+3]<=1.00001f,"finite bounded coverage");
        energy[s]+=frame[i*4+3];
      }
      if(r>.3f) maxHigh=std::max(maxHigh,high[i*4+3]);
      maxPhase=std::max(maxPhase,double(fabsf(high[i*4+3]-shifted[i*4+3])));
      if(r>1.02f) check(stopped[i*4+3]<.0001f&&high[i*4+3]<.0001f,"no blade outside model radius");
    }
    for(int i=0;i<2048;++i) { broad+=alphaAt(stopped,.50f,tau*i/2048)>.5f; tip+=alphaAt(stopped,.94f,tau*i/2048)>.5f; }
    check(tip*.94f<broad*.50f*.8f,"physical chord tapers toward rounded tips rather than widening as a wedge");
    check(fabs(energy[0]-energy[1])/energy[0]<.025&&fabs(energy[0]-energy[2])/energy[0]<.025,"blur conserves integrated blade coverage");
    check(maxPhase<.00001&&maxHigh<.4f,"high RPM is translucent and phase stable");
    check(alphaAt(stopped,.55f,.04f)>.95f&&alphaAt(low,.55f,.04f)<.7f,"low RPM spreads opaque blades into continuous translucent arcs");
    for(float blur:{0.f,.35f,1.f}) {
      auto ref=render(blades,blur,.23f), ai=render(blades,blur,.23f,true); float error=0;
      for(int i=0;i<N*N;++i) error=std::max(error,fabsf(ref[i*4+3]-ai[i*4+3]));
      check(error<.0035f,"player shared profile and production AI/airline shader match");
    }
    for(float projectionScale:{4.f,8.f,16.f}) {
      auto small=render(blades,0,.11f,false,projectionScale), smallShift=render(blades,0,.74f,false,projectionScale);
      double e0=0,e1=0; for(int i=0;i<N*N;++i) {e0+=small[i*4+3];e1+=smallShift[i*4+3];}
      check(fabs(e0-e1)/std::max(e0,1.0)<.12,"small projected stopped props preserve coverage across rotation");
      auto fast0=render(blades,1,.11f,false,projectionScale), fast1=render(blades,1,.74f,false,projectionScale);
      float err=0; for(int i=0;i<N*N*4;++i) err=std::max(err,fabsf(fast0[i]-fast1[i]));
      check(err<.000001f,"distant full-speed props are phase-stable, without angular aliasing");
    }
    printf("%d blades: integrated alpha stopped/low/high %.2f %.2f %.2f; high-RPM phase error %.8f\n",blades,energy[0],energy[1],energy[2],maxPhase);
    preview(dir,std::to_string(blades)+"-blade-stopped",stopped); preview(dir,std::to_string(blades)+"-blade-low-rpm",low); preview(dir,std::to_string(blades)+"-blade-high-rpm",high);
  }
  printf("%s: %d production shader checks; 2/3/4 blades, stopped/low/high RPM, AI/airline parity\n",failures?"FAIL":"PASS",checks);
  return failures?1:0;
}
