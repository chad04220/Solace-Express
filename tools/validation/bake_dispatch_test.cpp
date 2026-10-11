// No GL library or context is used. Production methods are extracted verbatim by extract.py.
#include <bits/stdc++.h>
#define private public
#include "renderer.h"
#undef private
#include "models.h"

namespace audit {
int checks = 0, draws = 0, yields = 0, uploads = 0;
void check(bool ok, const std::string& why) { ++checks; if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
GLuint current = 0, fbo = 0, vao = 0, nextObject = 1000;
int unit = 0, viewW = 0, viewH = 0, scY = 0, scH = 0;
bool scissor = false, blend = false, depth = false, driveYield = false;
Renderer* renderer = nullptr;
std::map<std::pair<GLuint, std::string>, GLint> locations;
std::map<GLint, std::pair<GLuint, std::string>> locationNames;
std::map<GLuint, std::map<std::string, std::vector<float>>> uniforms;
std::map<int, GLuint> bound;
struct Texture { int w = 0, h = 0; std::vector<float> data; };
std::map<GLuint, Texture> textures;
std::map<GLuint, GLuint> attachments;
std::vector<std::tuple<GLuint, int, int, int>> subUploads;
std::set<GLuint> normalPrograms{102};
std::set<GLuint> livePrograms;
struct LinkCall { bool normal, safe; bool retry; };
std::vector<LinkCall> links;
std::map<std::pair<bool, bool>, std::string> variantSources;
bool failField = false, failNormal = false, failSafeField = false, failSafeNormal = false, nonVendorFailure = false, historicalReject = false;
GLint location(GLuint p, const char* name) {
  if (normalPrograms.count(p) && (std::string(name) == "uHMode" || std::string(name) == "uHNormals")) return -1;
  auto key = std::make_pair(p, std::string(name));
  if (!locations.count(key)) { GLint id = GLint(locations.size()) + 1; locations[key] = id; locationNames[id] = key; }
  return locations[key];
}
void uniform(GLint loc, const float* values, size_t n) {
  if (loc == -1) return;
  auto key = locationNames.at(loc);
  check(key.first == current, "uniform upload targets its bound program: " + key.second);
  uniforms[current][key.second] = std::vector<float>(values, values + n);
}
float scalar(GLuint p, const char* name) { auto& a = uniforms[p][name]; check(a.size() == 1, std::string("initialized scalar ") + name); return a[0]; }
void generate(GLsizei n, GLuint* out) { while (n--) *out++ = nextObject++; }
void allocate(GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const void* ptr) {
  auto& t = textures[bound[unit]]; t.w = w; t.h = h; t.data.assign(size_t(w) * h * 4, -123456.f);
  if (ptr) std::copy_n(static_cast<const float*>(ptr), t.data.size(), t.data.begin());
  ++uploads;
}
void subImage(GLenum, GLint, GLint x, GLint y, GLsizei w, GLsizei h, GLenum, GLenum, const void* ptr) {
  auto& t = textures[bound[unit]];
  check(x >= 0 && y >= 0 && x + w <= t.w && y + h <= t.h, "subimage stays within texture allocation");
  const float* source = static_cast<const float*>(ptr);
  for (int row = 0; row < h; ++row) std::copy_n(source + size_t(row) * w * 4, size_t(w) * 4, t.data.begin() + (size_t(y + row) * t.w + x) * 4);
  subUploads.emplace_back(bound[unit], y, w, h);
}
void draw(GLenum, GLint, GLsizei) {
  Renderer& r = *renderer;
  const int m = r.hullBakeMode;
  const GLuint expected = r.hullBakeProg[m == 3 ? 1 : 0];   // (the pair beginHullBake chose: the aircraft's own, or the shared one)
  check(current == expected, "correct program selected for mode " + std::to_string(m));
  check(fbo == r.fboHOut && vao == r.vaoEmpty && !blend && !depth && scissor, "draw restores FBO, VAO, blend/depth and scissor state");
  check(m == 3 || scalar(current, "uHMode") == m, "uniform mode follows CPU selector");
  check(scalar(current, "uHState") == r.hullBakeState && scalar(current, "uHPart") == r.hullBakePart, "state/part selectors follow CPU inputs");
  check(uniforms[current]["uHPartSide"] == std::vector<float>{r.hullBakeSideX, r.hullBakeSideY}, "part side follows CPU inputs");
  check(uniforms[current]["uM"] == std::vector<float>(r.hullBakeFrame.plane.M, r.hullBakeFrame.plane.M + 96), "common model geometry restored");
  check(scalar(current,"uWakeN")==r.hullBakeFrame.wakeN,"weather wake count copied/restored");
  check(uniforms[current]["uCloudDet"]==std::vector<float>{r.hullBakeFrame.cloudDet.x,r.hullBakeFrame.cloudDet.y,r.hullBakeFrame.cloudDet.z},"cloud detail copied/restored");
  check(scalar(current,"uCloudBoil")==r.hullBakeFrame.cloudBoil,"cloud boil copied/restored");
  if(r.hullBakeFrame.wakeN>1){
    check(uniforms[current]["uWake"]==std::vector<float>(&r.hullBakeFrame.wake[0][0],&r.hullBakeFrame.wake[0][0]+r.hullBakeFrame.wakeN*4),"all wake points copied/restored");
    check(uniforms[current]["uWakeP"]==std::vector<float>(&r.hullBakeFrame.wakeP[0][0],&r.hullBakeFrame.wakeP[0][0]+r.hullBakeFrame.wakeN*4),"wake path copied/restored");
    check(uniforms[current]["uWakeG"]==std::vector<float>(&r.hullBakeFrame.wakeG[0][0],&r.hullBakeFrame.wakeG[0][0]+r.hullBakeFrame.wakeN*4),"wake geometry copied/restored");
    check(uniforms[current]["uWakeB"]==std::vector<float>(r.hullBakeFrame.wakeB,r.hullBakeFrame.wakeB+4),"wake bounds copied/restored");
  }
  check(uniforms[current]["uHStPS"] == std::vector<float>(r.hullBakePS.begin(), r.hullBakePS.end()), "PS state array restored");
  check(uniforms[current]["uHStCtl"] == std::vector<float>(r.hullBakeCtl.begin(), r.hullBakeCtl.end()), "control state array restored");
  check(uniforms[current]["uHStWr"] == std::vector<float>(r.hullBakeWr.begin(), r.hullBakeWr.end()), "XR state array restored");
  check(uniforms[current]["uHStWr2"] == std::vector<float>(r.hullBakeWr2.begin(), r.hullBakeWr2.end()), "XR2 state array restored");
  check(scalar(current, "uHStN") == r.hullBakeStates, "state count restored");
  check(scalar(current, "uHPts") == 19 && bound[19] == r.texHPts, "point input rebound");
  auto& pt = textures.at(bound[19]); auto& out = textures.at(attachments.at(fbo));
  check(viewW == 512 && viewH == pt.h && out.w == pt.w && out.h == pt.h, "viewport matches whole batch");
  check(scY >= 0 && scH > 0 && scH <= 32 && scY + scH <= pt.h, "watchdog band stays in rows");
  if (m == 2) check(scalar(current, "uHNormals") == 18 && bound[18] == r.texHNormals && bound[18] != attachments[fbo], "mode2 normals rebound without feedback");
  else check(bound[18] != attachments[fbo], "output texture is not sampled");
  for (int y = scY; y < scY + scH; ++y) for (int x = 0; x < 512; ++x) {
    size_t i = (size_t(y) * 512 + x) * 4;
    if (m == 3) { out.data[i] = pt.data[i] + .25f; out.data[i+1] = pt.data[i+1] + .5f; out.data[i+2] = pt.data[i+2] + .75f; out.data[i+3] = 0.f; }
    else if (m == 2) { auto& n = textures.at(bound[18]).data; out.data[i] = pt.data[i]; out.data[i+1] = n[i]; out.data[i+2] = n[i+1]; out.data[i+3] = n[i+2]; }
    else { out.data[i] = pt.data[i] + float(m); out.data[i+1] = out.data[i+2] = out.data[i+3] = 0; }
  }
  ++draws;
}
void bindMock() {
  glf::glUseProgram = [](GLuint p) { current = p; };
  glf::glGetUniformLocation = location;
  glf::glUniform1i = [](GLint l, GLint x) { float v = float(x); uniform(l, &v, 1); };
  glf::glUniform1f = [](GLint l, GLfloat x) { uniform(l, &x, 1); };
  glf::glUniform2f = [](GLint l, GLfloat x, GLfloat y) { float v[]{x,y}; uniform(l,v,2); };
  glf::glUniform3f = [](GLint l, GLfloat x, GLfloat y, GLfloat z) { float v[]{x,y,z}; uniform(l,v,3); };
  glf::glUniform4f = [](GLint l, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { float v[]{x,y,z,w}; uniform(l,v,4); };
  glf::glUniform1fv = [](GLint l, GLsizei n, const GLfloat* p) { uniform(l,p,size_t(n)); };
  glf::glUniform4fv = [](GLint l, GLsizei n, const GLfloat* p) { uniform(l,p,size_t(n)*4); };
  glf::glUniform3fv = [](GLint l, GLsizei n, const GLfloat* p) { uniform(l,p,size_t(n)*3); };
  glf::glUniform1iv = [](GLint l, GLsizei n, const GLint* p) { std::vector<float> v(p,p+n); uniform(l,v.data(),v.size()); };
  glf::glUniformMatrix3fv = [](GLint l, GLsizei n, GLboolean, const GLfloat* p) { uniform(l,p,size_t(n)*9); };
  glf::glUniformMatrix4fv = [](GLint l, GLsizei n, GLboolean, const GLfloat* p) { uniform(l,p,size_t(n)*16); };
  glf::glActiveTexture = [](GLenum u) { unit = int(u - GL_TEXTURE0); };
  glf::glBindTexture = [](GLenum, GLuint t) { bound[unit] = t; };
  glf::glGenTextures = generate; glf::glGenFramebuffers = generate;
  glf::glTexImage2D = allocate; glf::glTexSubImage2D = subImage;
  glf::glTexParameteri = [](GLenum, GLenum, GLint) {};
  glf::glBindFramebuffer = [](GLenum, GLuint f) { fbo=f; };
  glf::glFramebufferTexture2D = [](GLenum, GLenum, GLenum, GLuint t, GLint) { attachments[fbo] = t; };
  glf::glDrawBuffers = [](GLsizei, const GLenum*) {};
  glf::glDisable = [](GLenum v) { if(v==GL_BLEND)blend=false;if(v==GL_DEPTH_TEST)depth=false;if(v==GL_SCISSOR_TEST)scissor=false; };
  glf::glEnable = [](GLenum v) { if(v==GL_BLEND)blend=true;if(v==GL_DEPTH_TEST)depth=true;if(v==GL_SCISSOR_TEST)scissor=true; };
  glf::glBindVertexArray = [](GLuint v) { vao=v; };
  glf::glScissor = [](GLint, GLint y, GLsizei, GLsizei h) { scY=y;scH=h; };
  glf::glViewport = [](GLint, GLint, GLsizei w, GLsizei h) { viewW=w;viewH=h; };
  glf::glDrawArrays = draw;
  glf::glFinish = [] { if(driveYield)renderer->bakeYieldAt={}; };
  glf::glReadBuffer = [](GLenum) {};
  glf::glReadPixels = [](GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum, void* dst) { auto& t=textures.at(attachments.at(fbo));check(w==t.w&&h==t.h,"readback dimensions");std::copy(t.data.begin(),t.data.end(),static_cast<float*>(dst)); };
  glf::glGetIntegerv = [](GLenum p, GLint* dst) { *dst = p==GL_MAX_TEXTURE_SIZE ? 1024 : int(current); };
  glf::glDeleteProgram = [](GLuint p) { check(livePrograms.erase(p)==1,"delete only live compiled programs"); };
}
}

World g_world;
FeedMounts g_feedMounts[4];
GLint U(GLuint p, const char* n) { return glGetUniformLocation(p,n); }
void modelCabinFit(int model, float panel, float foot[4], float seat[2]) { for(int i=0;i<4;i++)foot[i]=float(model+i)+panel;seat[0]=panel;seat[1]=float(model); }
void packCockpitLayout(int model, float v[36]) { for(int i=0;i<36;i++)v[i]=float(model*100+i); }
std::string g_shaderNotes, g_compileWhat;
static std::atomic<bool> s_safeUseless{false};
void shaderNote(const std::string& s) { g_shaderNotes += s + '\n'; }
static std::string firstLine(const std::string& s) { return s.substr(0,s.find('\n')); }
static std::string programName(const std::string&) { return "CPU paired-fallback test"; }
static GLuint linkOnce(const std::string&, const std::string& fs, std::string& err, bool& before, const char* retry) {
  using namespace audit;
  const bool field = fs.find("mapPlaneBody(") != std::string::npos;
  const bool normal = fs.find("#define HULL_BAKE_NORMALS") != std::string::npos;
  const bool safe = fs.find("#define NV_SAFE_GEAR") != std::string::npos;
  if(field) {
    links.push_back({normal,safe,retry!=nullptr});
    check(fs.rfind("#version 330 core\n",0)==0,"variant keeps #version as first directive");
    if(normal)check(fs.find("#define HULL_BAKE_NORMALS\n")>fs.find('\n')&&fs.find("#define HULL_BAKE_NORMALS\n")<fs.find("mapPlaneBody("),"normal selector inserted after #version and before source");
    if(safe)check(fs.find("#define NV_SAFE_GEAR\n")>fs.find('\n')&&fs.find("#define NV_SAFE_GEAR\n")<fs.find("mapPlaneBody("),"safe selector inserted after #version and before source");
    variantSources.emplace(std::make_pair(normal,safe),fs);
  }
  const bool fail = field && (normal ? (safe ? failSafeNormal : failNormal) : (safe ? failSafeField : failField));
  before = historicalReject;
  if(fail) { err += nonVendorFailure ? "synthetic ordinary compile error\n" : "(0) : fatal error C9999: synthetic compiler rejection\n"; return 0; }
  GLuint p = nextObject++;livePrograms.insert(p);if(normal)normalPrograms.insert(p);return p;
}
#include "actual_link_cache.inc"
#include "actual_dispatch.inc"
// (what the extracted methods call and these checks don't exercise: the environment materials and the wreck boxes of the
// shared uniforms, and the per-aircraft builders - the mock aircraft has none of its own, so the shared pair is used)
void Renderer::bindEnvironmentMaterials(GLuint, int, int) {}
void Renderer::setWreckBoxes(GLuint, const WreckVisual&) {}
int Renderer::afModelOf(const float*, int) { return -1; }
bool Renderer::afBakePrograms(int, int, GLuint*) { return false; }
bool Renderer::sharedBakePrograms(GLuint out[2]) { if (!progHullBake) return false; out[0] = progHullBake; out[1] = progHullBakeNormal; return true; }

using namespace audit;
void setup(Renderer& r, FrameParams& fp) {
  renderer=&r;r.progHullBake=101;r.progHullBakeNormal=102;r.sharedBakeFullQuality=true;r.vaoEmpty=99;r.texTraffic=901;r.W=r.rw=800;r.H=r.rh=600;r.texTSh[0]=777;r.tshFront=0;
  fp.plane.on=true;fp.plane.model=7;fp.feedRig=2;
  fp.cloudDet=vec3(.25f,.5f,.75f);fp.cloudBoil=2.f;fp.wakeN=FrameParams::kWakeMax;
  for(int i=0;i<fp.wakeN;i++)for(int j=0;j<4;j++){fp.wake[i][j]=float(i*4+j);fp.wakeP[i][j]=float(i*4+j)*.03125f;fp.wakeG[i][j]=float(i*4+j)*.0625f;}
  for(int i=0;i<4;i++)fp.wakeB[i]=float(i+1);
  for(int i=0;i<96;i++)fp.plane.M[i]=float(i)*.03125f;
  for(int i=0;i<28;i++)(&fp.plane.wr[0][0])[i]=float(i)*.0625f;
  fp.plane.PS[0]=.2f;fp.plane.PS[1]=.3f;fp.plane.PS[2]=.4f;fp.plane.PS[3]=1.f;
}
std::vector<vec3> points(size_t n) {std::vector<vec3> p(n);for(size_t i=0;i<n;i++)p[i]=vec3(float(i),float(i%17),-float(i%31));return p;}
void assertNormalTransport(Renderer& r, size_t n) {
  auto p=points(n);std::vector<float> normals,out;r.hullBakeMode=3;r.hullEval4(p,normals);r.hullBakeMode=2;r.hullEval4(p,out,&normals);
  check(out.size()==n*4,"mode2 output size");
  bool exact=true;for(size_t i=0;i<n;i++)exact=exact&&out[i*4]==p[i].x&&out[i*4+1]==p[i].x+.25f&&out[i*4+2]==p[i].y+.5f&&out[i*4+3]==p[i].z+.75f;
  check(exact,"every normal and point remains aligned across batch/tail n="+std::to_string(n));
  if(n%512) {auto& t=textures[r.texHNormals];for(size_t i=(n%512)*4;i<512*4;i++)check(t.data[(size_t(t.h)-1)*512*4+i]==0.f,"tail padding initialized to zero");}
  check(bound[19]==0&&bound[18]==777&&unit==0&&fbo==0,"normal completion restores shared units and framebuffer");
}
void resetLinkTest() {links.clear();failField=failNormal=failSafeField=failSafeNormal=nonVendorFailure=historicalReject=false;s_safeUseless=false;g_shaderNotes.clear();}
void cleanup(Renderer& r) {for(GLuint p:{r.progHull,r.progHullBake,r.progHullBakeNormal})if(p)glDeleteProgram(p);}
// the field / normal pair as a builder links it (Renderer::linkBakePair: each aircraft's own, or the shared one)
static bool linkPair(Renderer& r, const std::string& vs, const std::string& fs) {
  GLuint o[2] = {0, 0}; std::string e; r.sharedBakeFullQuality = true; // prove every reduced/failed result clears old provenance
  const bool ok = r.linkBakePair(vs, fs, o, e, nullptr, &r.sharedBakeFullQuality); r.progHullBake = o[0]; r.progHullBakeNormal = o[1]; return ok;
}
void fallbacks() {
  const std::string vs="#version 330 core\nvoid main(){}",fs="#version 330 core\nvec2 mapPlaneBody(vec3 p); void main(){}";
  {resetLinkTest();Renderer r;check(linkPair(r,vs,fs),"pair succeeds normally");check(links.size()==2&&!links[0].safe&&!links[1].safe,"both normal builds retain full field");check(r.sharedBakeFullQuality,"normal pair is eligible for canonical geometry export");cleanup(r);}
  {resetLinkTest();failField=true;Renderer r;check(linkPair(r,vs,fs),"field fallback succeeds");check(!r.sharedBakeFullQuality,"safe field pair cannot be canonical export provenance");check(links.size()==3&&!links[0].normal&&links[1].safe&&links[1].retry&&links[2].normal&&links[2].safe,"field fallback forces same safe normal field");cleanup(r);}
  {resetLinkTest();failNormal=true;Renderer r;check(linkPair(r,vs,fs),"normal fallback succeeds");check(!r.sharedBakeFullQuality,"safe normal pair cannot be canonical export provenance");check(links.size()==4&&links[1].normal&&!links[1].safe&&links[2].normal&&links[2].safe&&links[2].retry&&!links[3].normal&&links[3].safe,"normal fallback rebuilds field with same safe geometry");check(livePrograms.size()==2,"replaced field program deleted (the pair alone: no hull program in a builder)");cleanup(r);}
  {resetLinkTest();failNormal=historicalReject=true;Renderer r;check(linkPair(r,vs,fs),"historical rejection follows matched fallback");check(g_shaderNotes.find("an earlier launch")!=std::string::npos,"historical fallback note retained");cleanup(r);}
  {resetLinkTest();failField=failSafeField=true;Renderer r;check(!linkPair(r,vs,fs),"field terminal fallback failure propagated");check(!r.sharedBakeFullQuality,"failed pair clears canonical provenance");check(!r.progHullBake&&!r.progHullBakeNormal&&s_safeUseless,"failed pair disabled with upstream useless guard");cleanup(r);}
  {resetLinkTest();failNormal=failSafeNormal=true;Renderer r;check(!linkPair(r,vs,fs),"normal terminal fallback failure propagated");check(!r.progHullBake&&!r.progHullBakeNormal,"successful sibling deleted after normal failure");cleanup(r);}
  {resetLinkTest();failNormal=failSafeField=true;Renderer r;check(!linkPair(r,vs,fs),"field rebuild failure after successful normal fallback propagated");check(!r.progHullBake&&!r.progHullBakeNormal,"all surviving programs deleted on pair rebuild failure");cleanup(r);}
  {resetLinkTest();failNormal=nonVendorFailure=true;Renderer r;check(!linkPair(r,vs,fs),"ordinary failure propagated");check(links.size()==2,"ordinary error gets no NVIDIA retry");cleanup(r);}
  {resetLinkTest();bool safe=true;std::string err;GLuint p=linkProgramCached(vs,fs,err,&safe);check(p&&!safe,"optional safe output reset on first-link success");glDeleteProgram(p);}
  {resetLinkTest();Renderer r;const std::string safeFs=fs.substr(0,fs.find('\n')+1)+"#define NV_SAFE_GEAR\n"+fs.substr(fs.find('\n')+1);check(linkPair(r,vs,safeFs),"explicit reduced pair still links for runtime fallback");check(!r.sharedBakeFullQuality,"explicit reduced source cannot be canonical export provenance");cleanup(r);}
  check(livePrograms.empty(),"all fallback test programs released");
  std::set<std::string> distinct;for(const auto& x:variantSources)distinct.insert(x.second);
  check(variantSources.size()==4&&distinct.size()==4,"field/normal and full/safe source variants all distinct for cache identity");
}
int main() {
  bindMock();Renderer r;FrameParams fp{};setup(r,fp);
  float ps[8]={1,2,3,1,5,6,7,1},ctl[8]={9,10,11,12,13,14,15,16},wr[8]={17,18,19,20,21,22,23,24},wr2[8]={25,26,27,28,29,30,31,32};
  r.beginHullBake(fp,2,ps,ctl,wr,wr2);fp.plane.M[0]=999;
  check(r.hullBakeFullQuality&&!r.hullBakeOwnBuilder,"shared quality propagates but cannot masquerade as per-aircraft export provenance");
  check(r.hullBakeFrame.plane.M[0]!=999,"frame is copied before caller mutation");
  check(r.hullBakePS[7]==1&&r.hullBakePS[8]==0&&r.hullBakeWr2[7]==32&&r.hullBakeWr2[8]==0,"state upload copies active rows and zeros inactive rows");
  r.hullBakeState=1;r.hullBakePart=14;r.hullBakeSideX=-1;r.hullBakeSideY=.25f;
  std::vector<float> out;auto p=points(3);
  for(int mode:{0,1,4,3}) {r.hullBakeMode=mode;r.hullEval4(p,out);check(out[0]==(mode==3?.25f:float(mode)),"mode dispatch produces expected mock result");}
  for(const char* n:{"uM","uModelId","uCabinFootFit","uCabinSeatFit","uCockpitLayout","uPS","uCtl","uWr","uFlame","uQuality","uDbg"})check(uniforms[101][n]==uniforms[102][n],std::string("common input equal in both programs: ")+n);
  for(size_t n:{size_t(1),size_t(511),size_t(512),size_t(513),size_t(512*1024+17)})assertNormalTransport(r,n);
  int oldDraws=draws;r.hullBakeMode=2;r.hullEval4(p,out);check(draws==oldDraws&&out[0]==1e9f,"missing normals fail closed without draw");std::vector<float> bad(8);r.hullEval4(p,out,&bad);check(draws==oldDraws&&out[0]==1e9f,"mismatched normals fail closed without draw");r.hullEval4({},out);check(out.empty()&&draws==oldDraws,"empty input does not draw");
  driveYield=true;r.bakeYield=[] {++yields;current=888;fbo=889;vao=890;blend=depth=true;bound[18]=891;bound[19]=892;uniforms[101].clear();uniforms[102].clear();};
  assertNormalTransport(r,512*65+7);check(yields>=6,"yield exercised after multiple bands in both programs");driveYield=false;
  r.bakeYieldAt={};r.bakeTick();check(!r.hullBakeUploaded[0]&&!r.hullBakeUploaded[1],"CPU-side yield invalidates both common uploads");r.bakeYield=nullptr;
  r.hullBakeMode=3;r.hullEval4(p,out);r.hullBakeMode=1;r.hullEval4(p,out);
  r.hullBakeMode=3;r.hullBakeState=19;r.hullBakePart=21;r.hullBakeWr.fill(99);r.hullBakeWr2.fill(99);r.measureFeedMounts(fp);
  check(r.hullBakeMode==0&&r.hullBakeState==0&&r.hullBakePart==-1&&r.hullBakeStates==1,"feed measurement resets stale normal/state/part mode");
  check(r.hullBakePS[3]==0&&r.hullBakeWr==std::array<float,512>{}&&r.hullBakeWr2==std::array<float,512>{},"feed measurement clears cockpit and prior XR state arrays");check(g_feedMounts[2].ok,"feed result marked available");
  r.hullBakeProg[1]=0;r.hullBakeMode=3;oldDraws=draws;r.hullEval4(p,out);check(draws==oldDraws&&out[0]==1e9f,"missing selected program fails closed");
  fallbacks();
  std::cout<<"PASS: "<<checks<<" assertions; "<<draws<<" mock draws; "<<yields<<" adversarial yields. Selector/common state, point-normal alignment, tail padding, bounded batching, feed reset, fail-closed inputs, paired NVIDIA fallback and cleanup.\n";
}
