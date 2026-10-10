// Mesa/EGL visual inspection of the production aircraft distance fields.
// Build on Linux with the core sources, models.cpp, gl.cpp, -pthread and -ldl.
#include "../src/gl.h"
#include "../src/models.h"
#include "../src/aircraft.h"
#include "../src/shaders.h"
#include "../src/shaders_wraith_cockpit.h"
#include <dlfcn.h>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <string>

static void* lib;
static void* (*eglProc)(const char*);
static void* proc(const char* n) { void* p = eglProc(n); return p ? p : dlsym(lib, n); }
template<class F> static F sym(const char* n) { return reinterpret_cast<F>(dlsym(lib, n)); }
static bool initGL(int w, int h) {
  lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
  if (!lib) return false;
  eglProc = sym<void*(*)(const char*)>("eglGetProcAddress");
  auto display = reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglProc("eglGetPlatformDisplayEXT"))(0x31DD, nullptr, nullptr);
  int ma, mi;
  if (!sym<unsigned(*)(void*,int*,int*)>("eglInitialize")(display,&ma,&mi)) return false;
  sym<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2);
  const int ca[] = {0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038}; void* config; int count;
  if (!sym<unsigned(*)(void*,const int*,void**,int,int*)>("eglChooseConfig")(display,ca,&config,1,&count) || !count) return false;
  const int sa[] = {0x3057,w,0x3056,h,0x3038};
  auto surf = sym<void*(*)(void*,void*,const int*)>("eglCreatePbufferSurface")(display,config,sa);
  const int ctxa[] = {0x3098,3,0x30FB,3,0x30FD,1,0x3038};
  auto ctx = sym<void*(*)(void*,void*,void*,const int*)>("eglCreateContext")(display,config,nullptr,ctxa);
  const char* missing = nullptr;
  return ctx && sym<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent")(display,surf,surf,ctx) && glLoad(proc,&missing);
}
static GLuint compile(GLenum kind, const std::string& source) {
  GLuint s = glCreateShader(kind); const char* p = source.c_str(); glShaderSource(s,1,&p,nullptr); glCompileShader(s);
  GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if (!ok) { char log[16384]; glGetShaderInfoLog(s,sizeof(log),nullptr,log); fprintf(stderr,"%s\n",log); exit(2); }
  return s;
}
static GLuint program(const std::string& source) {
  GLuint vs = compile(GL_VERTEX_SHADER,kFullscreenVS), fs = compile(GL_FRAGMENT_SHADER,source), p = glCreateProgram();
  glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p); GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  glDeleteShader(vs); glDeleteShader(fs);
  if (!ok) { char log[16384]; glGetProgramInfoLog(p,sizeof(log),nullptr,log); fprintf(stderr,"%s\n",log); exit(2); }
  return p;
}
static std::string prefix(const char* s, const char* marker) { std::string a(s); auto p=a.find(marker); if(p==a.npos) { fprintf(stderr,"Missing shader marker: %s\n",marker); exit(2); } return a.substr(0,p); }

static const char* inspection = R"(
uniform vec3 vEye, vRight, vUp, vBack;
uniform float vScale;
uniform int vPerspective, vMode, vIds;
vec4 fitProbe() {
  vec3 E=gM[22].xyz;
  vec2 uv=vec2((mod(gl_FragCoord.x,32.0)/32.0)*2.0-1.0,gl_FragCoord.y/16.0*2.0-1.0);
  float side=gl_FragCoord.x<32.0?-1.0:1.0;
  if(vMode==1) {
    float hy=E.y+0.08, radius=0.04;
#ifdef FIT_PATCH
    hy=gCab0.y-0.005; radius=0.035; if(gCab0.y<-50.0) return vec4(-1.0,0.0,0.0,1.0);
#endif
    float dx=uv.x*0.11,dz=uv.y*0.05;
    vec2 rim=max(abs(vec2(dx,dz))-vec2(0.11-radius,0.05-radius),0.0);
    float h2=radius*radius-dot(rim,rim);
    if(h2<0.0)return vec4(0.0);
    vec3 p=vec3(side*abs(E.x)+dx,hy+0.08-radius+sqrt(h2),E.z+0.455+dz);
    return vec4(sdFuselage(p)+0.05,0.0,0.0,1.0);
  }
  if(vMode==2) {
    float z=0.0, halfZ=0.2;
#ifdef TOUCH_PATCH
    z=0.145; halfZ=0.09;
#endif
    vec3 p=E+vec3(side*(0.575+uv.x*0.1),-0.4275,z+uv.y*halfZ);
    gCkSkip=2;return vec4(mapWraithCockpit(p).x,0.0,0.0,1.0);
  }
  if(vMode==4) {   // skin height probe: lowest surface (scanning up from below) at bay hinge points, gear up
    int k=int(gl_FragCoord.x)%4; vec4 G0=gM[18];
    vec2 xz = k==0 ? vec2(G0.x-0.2,G0.z) : k==1 ? vec2(G0.x+0.2,G0.z) : k==2 ? vec2(0.24,G0.w) : vec2(0.0,G0.w);
    if(k<2 && int(gl_FragCoord.y)==1) xz.y=G0.z+0.4; if(k>=2 && int(gl_FragCoord.y)==1) xz.y=G0.w+0.4;
    float y=-2.0; for(int i=0;i<400;i++){ if(mapPlane(vec3(xz.x,y,xz.y)).x<0.0) break; y+=0.006; }
    return vec4(y,0.0,0.0,1.0);
  }
  float z=side<0.0?gM[1].x:gM[8].x;
  vec3 sec=fusSection(z);
  vec3 p=vec3(uv.x*sec.x*0.7,sec.z+uv.y*sec.y*0.7,z);
  float delta=abs(sdFuselage(p+vec3(0,0,0.0001))-sdFuselage(p-vec3(0,0,0.0001)));
  return vec4(delta,0.0,0.0,1.0);
}
vec3 inspectColor(int id, vec3 p) {
  if(id == 1 && gPS.w < 0.5) {
    vec3 sec=fusSection(p.z); vec4 WS=gM[23];
    vec3 paint=fuselagePaint(p,sec);
    float post=gM[21].z > 1.5 ? min(abs(p.x)-0.03,abs(abs(p.x)-abs(gM[22].x)-0.42)-0.035) : abs(p.x)-0.025;
    bool ws=p.z>WS.x && p.z<WS.y && p.y>WS.z && post>0.0;
    bool side=p.z>WS.y && p.z<WS.w && p.y>WS.z-0.12 && p.y<sec.z+sec.y*0.78 && abs(p.x)>0.3 && abs(p.z-WS.y-0.04)>0.025;
    int nw=int(gM[20].x+0.5);
    if(nw>0 && abs(p.x)>sec.x*0.4 && p.z>gM[20].y && p.z<gM[20].z) {
      float pw=(gM[20].z-gM[20].y)/float(nw);
      vec2 wq=vec2(mod(p.z-gM[20].y,pw)-pw*0.5,p.y-(sec.z+gM[20].w));
      vec2 hs=gM[21].xy; float rr=min(hs.x,hs.y)*0.7;
      vec2 dq=abs(wq)-hs+rr; float wd=length(max(dq,0.0))+min(max(dq.x,dq.y),0.0)-rr;
      side=side||wd<0.0;
    }
    return ws||side ? vec3(0.12,0.24,0.32) : paint;
  }
  if(id == 6 || id == 61 || id == 75) return vec3(0.07,0.085,0.11);
  if(id == 10 || id == 14 || id == 44 || id == 65 || id == 66) return vec3(0.13,0.17,0.22);
  if(id == 11 || id == 40 || id == 64) return vec3(0.48,0.50,0.54);
  if(id == 12 || id == 46) return vec3(0.18,0.24,0.33);
  if(id == 8 || id == 13 || id == 17 || id == 60 || id == 92) return vec3(0.45,0.50,0.57);
  if(id == 32 || id == 82) return vec3(0.42,0.29,0.10);
  if(id == 41 || id == 42 || id == 43 || id == 62 || id == 63) return vec3(0.14,0.37,0.49);
  if(id == 34 || id == 48 || id == 57 || id == 58 || id == 64 || id == 67 || id == 73 || id == 87) return vec3(0.25,0.72,0.95);
  if(id == 45 || id == 52 || id == 53 || id == 68 || id == 69) return vec3(0.12,0.45,0.58);
  if(id == 18 || id == 19) return vec3(0.9,0.25,0.12);
  if(id >= 80) return id == 84 || id == 85 ? vec3(0.4,0.48,0.58) : vec3(0.22,0.26,0.32);
  if(id >= 30) return vec3(0.25,0.31,0.38);
  return mix(gColBase, gColStripe, id == 2 || id == 3 ? 0.12 : 0.0);
}
vec3 normalAt(vec3 p, float e) {
  vec2 k=vec2(1,-1);
  return normalize(k.xyy*mapPlane(p+k.xyy*e).x+k.yyx*mapPlane(p+k.yyx*e).x+k.yxy*mapPlane(p+k.yxy*e).x+k.xxx*mapPlane(p+k.xxx*e).x);
}
void main() {
  loadMain();
  if(vMode>0){oColor=fitProbe();return;}
  vec2 uv=(gl_FragCoord.xy/uRes*2.0-1.0)*vec2(uAspect,1.0);
  vec3 ro=vEye, rd;
  if(vPerspective == 1) rd=normalize(vRight*uv.x*vScale+vUp*uv.y*vScale-vBack);
  else { ro+=vRight*uv.x*vScale+vUp*uv.y*vScale; rd=-vBack; }
  float t=0.0, limit=vPerspective == 1 ? 8.0 : 100.0; vec2 hit=vec2(1e5,0);
  for(int i=0;i<360;i++) {
    hit=mapPlane(ro+rd*t);
    if(abs(hit.x)<0.001 || t>limit) break;
    t+=max(abs(hit.x)*0.65,0.0004);
  }
  vec3 col=mix(vec3(0.08,0.11,0.16),vec3(0.19,0.24,0.31),gl_FragCoord.y/uRes.y);
  if(t<=limit && abs(hit.x)<0.003) {
    vec3 p=ro+rd*t, n=normalAt(p,0.0015);
    if(dot(n,rd)>0.0) n=-n;
    vec3 l=normalize(vec3(-0.5,0.8,-0.5));
    float diffuse=max(dot(n,l),0.0), rim=pow(1.0-max(dot(n,-rd),0.0),3.0);
    float ao=1.0;
    for(int j=1;j<=3;j++) { float d=float(j)*0.035; ao-=max(0.0,d-mapPlane(p+n*d).x)*1.8/float(j); }
    col=inspectColor(int(hit.y+0.5),p)*(0.42+0.60*diffuse)*clamp(ao,0.45,1.0)+rim*0.045;
    if(vIds==1){oColor=vec4(float(int(hit.y+0.5))/255.0,0,0,1);return;}   // part id in red (AVT_IDS=1)
  }
  if(vIds==1){oColor=vec4(0,0,0,1);return;}
  oColor=vec4(pow(clamp(col,0.0,1.0),vec3(1.0/2.2)),1);
}
)";
static void ppm(const std::filesystem::path& path,int w,int h) {
  std::vector<unsigned char> px(w*h*3); glPixelStorei(GL_PACK_ALIGNMENT,1); glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,px.data());
  FILE* f=fopen(path.string().c_str(),"wb"); if(!f) { perror(path.string().c_str()); exit(2); } fprintf(f,"P6\n%d %d\n255\n",w,h);
  for(int y=h-1;y>=0;y--) fwrite(px.data()+y*w*3,1,w*3,f); fclose(f);
}
int main(int argc,char**argv) {
  setvbuf(stdout,nullptr,_IONBF,0);
  if(argc<2) { puts("Usage: aircraft_visual_test OUTPUT_DIR [WIDTH HEIGHT] [first-aircraft last-aircraft]"); return 2; }
  int W=argc>2?atoi(argv[2]):480,H=argc>3?atoi(argv[3]):300;
  int first=argc>4?atoi(argv[4]):0,last=argc>5?atoi(argv[5]):kAircraftCount-1;   // (all stable roster identities, including appended conventional aircraft)
  if(W<16||H<16||first<0||last>=kAircraftCount||first>last) return 2;
  std::filesystem::path out(argv[1]); std::filesystem::create_directories(out);
  if(!initGL(W,H)) { puts("EGL failed"); return 2; }
  printf("Renderer: %s\n",glGetString(GL_RENDERER));
  std::string defines;
  if(std::string(kSceneUniforms).find("vec4 gCab0")!=std::string::npos)defines+="#define FIT_PATCH\n";
  if(std::string(kWraithCockpitSDF).find("abs(cq.z - 0.265)")!=std::string::npos)defines+="#define TOUCH_PATCH\n";
  std::string fs=sdfAssembly(defines)+inspection;
  GLuint prog=program(fs),vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao); glUseProgram(prog); glViewport(0,0,W,H);
  auto loc=[&](const char* n){ return glGetUniformLocation(prog,n); };
  auto v3=[&](const char* n,vec3 v){ glUniform3f(loc(n),v.x,v.y,v.z); };
  glUniform2f(loc("uRes"),(float)W,(float)H); glUniform1i(loc("vIds"),getenv("AVT_IDS")?1:0); glUniform1f(loc("uAspect"),(float)W/H);
  int frames=0;
  for(int a=first;a<=last;a++) {
    const ModelDef& m=kModels[a]; const AircraftSpec& s=kAircraft[a]; Plane plane; plane.spec=&s; float M[96]; packModel(s,a,plane.gearHeight(),M);
    glUniform4fv(loc("uM"),24,M); v3("uColBase",s.colBase); v3("uColStripe",s.colStripe);
    const char* only=getenv("AVT_VIEWS");   // comma-separated subset of views (quick iteration)
    for(const char* view:{"front","side","rear","top","belly","controls","controls-negative","cockpit","down","left","right","aft","overhead","transition","retracted","hover","profile","quarter","gearbelly","gearside","gearfront","pedals"}) {
      if(only && (","+std::string(only)+",").find(","+std::string(view)+",")==std::string::npos) continue;
      bool inside=std::string(view)=="pedals"||std::string(view)=="cockpit"||std::string(view)=="down"||std::string(view)=="left"||std::string(view)=="right"||std::string(view)=="aft"||std::string(view)=="overhead";
      bool defl=std::string(view)=="controls"||std::string(view)=="controls-negative",trans=std::string(view)=="transition";
      float ctl=std::string(view)=="controls-negative"?-1.f:defl?1.f:0.f;
      bool retracted=std::string(view)=="retracted",hover=std::string(view)=="hover";
      if(std::string(view)=="gearbelly"||std::string(view)=="gearside") trans=false;
      float tilt=hover?PI*.5f:trans?PI*.25f:0.f;
      glUniform4f(loc("uPS"),retracted?0.f:trans?0.5f:1.f,defl?1.f:trans?0.5f:0.f,ctl*0.5f,inside?1.f:0.f);
      glUniform4f(loc("uCtl"),ctl,ctl,ctl,0.7f);
      float wr[28]={}; for(int i=0;i<4;i++) { wr[i]=tilt; wr[8+i]=0.7f; } wr[17]=defl?1.f:0.f; wr[18]=defl?1.f:0.f; wr[20]=wr[21]=wr[22]=ctl; wr[24]=1.f;
      glUniform4fv(loc("uWr"),7,wr); glUniform4f(loc("uFlame"),.7f,0.f,tilt,0.f);
      vec3 eye,target;
      if(inside) {
        eye=m.eye; vec3 direction(0,-.13f,-1);
        if(std::string(view)=="down") direction=vec3(0,-.8f,-.65f);
        if(std::string(view)=="left") direction=vec3(-1,-.2f,-.25f);
        if(std::string(view)=="right") direction=vec3(1,-.2f,-.25f);
        if(std::string(view)=="aft") direction=vec3(.1f,-.25f,1);
        if(std::string(view)=="overhead") direction=vec3(-m.eye.x,.35f,-.05f);
        if(std::string(view)=="pedals") { eye=vec3(0.f,m.eye.y-.55f,m.eye.z+.05f); direction=vec3(m.eye.x*1.4f,-.55f,-.75f); }
        if(std::string(view)=="cockpit"&&getenv("AVT_DIR")) sscanf(getenv("AVT_DIR"),"%f,%f,%f",&direction.x,&direction.y,&direction.z);   // (aim the cockpit view: AVT_DIR=x,y,z in body space)
        target=eye+direction; glUniform1f(loc("vScale"),.68f); glUniform1i(loc("vPerspective"),1);
      } else {
        float size=std::max(s.fusLen,s.span);
        vec3 offset(1,.5f,-1.5f);
        if(std::string(view)=="side") offset=vec3(1,.08f,0);
        if(std::string(view)=="rear"||defl) offset=vec3(-1,.45f,1.4f);
        if(std::string(view)=="top") offset=vec3(.01f,1,.01f);
        if(std::string(view)=="belly") offset=vec3(1,-.7f,-1.2f);
        float zoom=.38f;
        if(std::string(view)=="profile") { offset=vec3(1,0,0); zoom=.36f*s.fusLen/std::max(s.fusLen,s.span); }   // true side elevation, fuselage filling the frame
        if(std::string(view)=="gearbelly") { offset=vec3(.35f,-1,-.25f); zoom=.26f*s.fusLen/std::max(s.fusLen,s.span); }
        if(std::string(view)=="gearside") { offset=vec3(1,-.12f,-.35f); zoom=.24f*s.fusLen/std::max(s.fusLen,s.span); }
        if(std::string(view)=="quarter") { offset=vec3(1,.22f,-1.2f); zoom=.42f*s.fusLen/std::max(s.fusLen,s.span); }
        if(std::string(view)=="gearfront") { offset=vec3(.25f,-.12f,-1); zoom=.05f; }
        eye=normalize(offset)*size*1.8f; target=vec3(0,(std::string(view)=="profile"||std::string(view)=="quarter")?.1f:-.15f,0);
        if(std::string(view)=="gearfront") { target=vec3(M[18*4+0],-M[19*4+0]*0.5f,M[18*4+2]); eye=target+normalize(offset)*size*1.8f; } glUniform1f(loc("vScale"),size*zoom); glUniform1i(loc("vPerspective"),0);
      }
      vec3 back=normalize(eye-target),right=normalize(cross(vec3(0,1,0),back)),up=cross(back,right);
      v3("vEye",eye);v3("vRight",right);v3("vUp",up);v3("vBack",back);
      glDrawArrays(GL_TRIANGLES,0,3);glFinish();
      auto path=out/(std::string(s.id)+"-"+view+".ppm"); ppm(path,W,H); frames++;
      if(glGetError()!=0) { fprintf(stderr,"GL error at %s\n",path.string().c_str()); return 1; }
      printf("%s\n",path.string().c_str());
    }
  }
  printf("Rendered %d production geometry views.\n",frames);
  // Numerical checks use the actual GLSL geometry, rather than a CPU copy of its formulas.
  GLuint tex,fbo;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,64,16,0,GL_RGBA,GL_FLOAT,nullptr);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
  glViewport(0,0,64,16);std::vector<float> px(64*16*4);int failures=0;
  auto readProbe=[&](int mode){glUniform1i(loc("vMode"),mode);glDrawArrays(GL_TRIANGLES,0,3);glFinish();glReadPixels(0,0,64,16,GL_RGBA,GL_FLOAT,px.data());};
  for(int a=first;a<=last;a++){
    if(a==kWraith)continue;   // (its cockpit is its own field: the touchpad probe below)
    Plane p;p.spec=&kAircraft[a];float M[96];packModel(kAircraft[a],a,p.gearHeight(),M);glUniform4fv(loc("uM"),24,M);glUniform4f(loc("uPS"),1,0,0,1);
    readProbe(1);int buried=0;float worst=-1e5;
    for(size_t i=0;i<px.size();i+=4)if(px[i+3]>.5f){worst=std::max(worst,px[i]);if(!std::isfinite(px[i])||px[i]>0.001f)buried++;}
    printf("HEADREST %s: %d skin intersections; worst clearance %.4f m\n",kAircraft[a].id,buried,-worst);if(buried)failures++;
    readProbe(3);float jump=0;for(size_t i=0;i<px.size();i+=4){if(!std::isfinite(px[i]))failures++;else jump=std::max(jump,px[i]);}
    printf("ENDCAP %s: maximum seam jump %.6f m\n",kAircraft[a].id,jump);if(jump>.001f)failures++;
  }
  if(first<=kWraith&&last>=kWraith){
    Plane p;p.spec=&kAircraft[kWraith];float M[96];packModel(kAircraft[kWraith],kWraith,p.gearHeight(),M);glUniform4fv(loc("uM"),24,M);glUniform4f(loc("uPS"),1,0,0,1);
    int buried=0;float worst=1e5;int states=0;
    for(float pitch:{-1.f,0.f,1.f})for(float roll:{-1.f,0.f,1.f})for(float throttle:{0.f,.5f,1.f}){
      glUniform4f(loc("uCtl"),pitch,roll,0,throttle);readProbe(2);states++;
      for(size_t i=0;i<px.size();i+=4){worst=std::min(worst,px[i]);if(!std::isfinite(px[i])||px[i]<-.0001f)buried++;}
    }
    printf("WRAITH TOUCHPADS (%s): %d buried samples in %d stick/throttle states; minimum clearance %.4f m\n",kAircraft[kWraith].id,buried,states,worst);if(buried)failures++;
  }
  if(getenv("AVT_PROBE")) for(int a=5;a<=kOsprey;a++){
    Plane p;p.spec=&kAircraft[a];float M[96];packModel(kAircraft[a],a,p.gearHeight(),M);glUniform4fv(loc("uM"),24,M);glUniform4f(loc("uPS"),0,0,0,0);
    readProbe(4); printf("SKIN %s: main hinge in %.3f out %.3f (aft %.3f %.3f) | nose hinge %.3f centre %.3f (aft %.3f %.3f)\n",kAircraft[a].id,px[0],px[4],px[64*4],px[64*4+4],px[8],px[12],px[64*4+8],px[64*4+12]);
  }
  if(glGetError()!=0)failures++;
  glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);glDeleteProgram(prog);
  printf("Geometry checks: %d failures\n",failures);return failures?1:0;
}
