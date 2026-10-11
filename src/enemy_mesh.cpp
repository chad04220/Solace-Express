// Native enemy-only rigid meshes. No player packed model or cockpit/gear variants.
#include "renderer.h"
#include "shaders.h"
#include "enemy_surface_mesh.h"
#include "enemy_mesh_cache.h"
#include "enemy_bake_guard.h"
#include "aircraft_mesh_build_types.h"
#include "mesh_validation.h"
#include <filesystem>
#include <cstdio>
#include <cstring>

namespace {
thread_local enemyBake::State enemyBakeState;
// A loading-screen yield may draw UI or another frame. Keep its GL state isolated
// from the sampler, and restore the caller's target/viewport even on an early exit.
struct EnemyBakeGLState {
 GLint draw=0,read=0,viewport[4]={},program=0,vao=0,active=0,buffer=0,texture[2]={};
 GLint depth=0,blend=0,cull=0,scissor=0,depthMask=0,color[4]={},scissorBox[4]={};
 EnemyBakeGLState(){
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(0x8CAA,&read);
  glGetIntegerv(0x0BA2,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(0x85B5,&vao);glGetIntegerv(0x84E0,&active);glGetIntegerv(0x8894,&buffer);
  glGetIntegerv(GL_DEPTH_TEST,&depth);glGetIntegerv(GL_BLEND,&blend);glGetIntegerv(GL_CULL_FACE,&cull);glGetIntegerv(GL_SCISSOR_TEST,&scissor);
  glGetIntegerv(0x0B72,&depthMask);glGetIntegerv(0x0C23,color);glGetIntegerv(0x0C10,scissorBox);
  for(int i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(0x8069,&texture[i]);}glActiveTexture(GLenum(active));
 }
 ~EnemyBakeGLState(){
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
  glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));glBindBuffer(GL_ARRAY_BUFFER,GLuint(buffer));
  for(int i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,GLuint(texture[i]));}glActiveTexture(GLenum(active));
  auto restore=[](GLenum state,GLint on){if(on)glEnable(state);else glDisable(state);};
  restore(GL_DEPTH_TEST,depth);restore(GL_BLEND,blend);restore(GL_CULL_FACE,cull);restore(GL_SCISSOR_TEST,scissor);
  glDepthMask(depthMask?GL_TRUE:GL_FALSE);glColorMask(color[0]!=0,color[1]!=0,color[2]!=0,color[3]!=0);glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);
 }
};
mat4 enemyOrtho(float R) {
 mat4 m; m(0,0)=1.f/R; m(1,1)=1.f/R; m(2,2)=-1.f/R; m(2,3)=-2.f; return m;
}
uint64_t enemyFingerprint(int type) {
 std::string driver;
 for(GLenum e:{GL_VENDOR,GL_RENDERER,GL_VERSION}){const auto* p=glGetString(e);driver+=(p?reinterpret_cast<const char*>(p):"?");driver+='|';}
 return enemyCache::fingerprint(kEnemyCraftSpecs[type],type,enemyFieldAssembly(type),enemyBakeFSAssembly(type),
   std::string(enemyMesh::kAlgorithmManifest)+":"+aircraftMesh::kAlgorithmManifest,driver);
}
std::string enemyCachePath(int type,uint64_t fingerprint) {
 if(g_shaderCacheDir.empty())return {};
 char name[96];snprintf(name,sizeof name,"/enemy2_%s_%016llx.bin",kEnemyCraftSpecs[type].id,(unsigned long long)fingerprint);
 return g_shaderCacheDir+name;
}
}
void Renderer::ensureEnemyMesh(int type){
 if(type<0||type>=kEnemyCraftTypes)return;
 enemyBake::Lease lease(enemyBakeState,false);if(!lease)return;
 auto& mesh=enemyMeshes[type];if(mesh.attempted)return;mesh.attempted=true;
 EnemyBakeGLState savedGL;
 setCompileStage((std::string("enemy mesh: ")+kEnemyCraftSpecs[type].id).c_str());
 bakeCount++;
 std::string errorText;
 mesh.material=linkProgramCached(std::string("#version 330 core\n")+kEnemyMeshVS,enemyMeshFSAssembly(type),errorText);
 if(!mesh.material){shaderNote("Enemy material "+std::string(kEnemyCraftSpecs[type].id)+": "+errorText);return;}
 std::vector<float> vertices;std::vector<uint32_t> indices;
 const uint64_t fingerprint=enemyFingerprint(type);
 const std::string path=enemyCachePath(type,fingerprint);
 if(!enemyCache::load(path,fingerprint,vertices,indices)){
  bakeBuilt++; if(onBakeStart)onBakeStart();
  GLuint program=linkProgramCached(kFullscreenVS,enemyBakeFSAssembly(type),errorText);
  if(!program){shaderNote("Enemy surface sampler: "+errorText);return;}
  const GLint pointsLocation=glGetUniformLocation(program,"uPoints"),normalsLocation=glGetUniformLocation(program,"uNormals");
  GLuint tex[2]={},fbo=0;glGenTextures(2,tex);glGenFramebuffers(1,&fbo);
  auto evaluate=[&](const std::vector<vec3>& points,std::vector<float>& out,bool normals){
   out.resize(points.size()*4);
   constexpr size_t cap=512*128;
   for(size_t at=0;at<points.size();at+=cap){
    size_t count=std::min(cap,points.size()-at);int rows=int((count+511)/512);
    std::vector<float> data(size_t(512)*rows*4,0.f);
    for(size_t j=0;j<count;j++){data[j*4]=points[at+j].x;data[j*4+1]=points[at+j].y;data[j*4+2]=points[at+j].z;}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,512,rows,0,GL_RGBA,GL_FLOAT,data.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glActiveTexture(GL_TEXTURE0+1);glBindTexture(GL_TEXTURE_2D,tex[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,512,rows,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,0);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[1],0);
    GLenum c=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&c);glReadBuffer(c);
    glViewport(0,0,512,rows);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(program);glUniform1i(pointsLocation,0);glUniform1i(normalsLocation,normals?1:0);
    glBindVertexArray(vaoEmpty);glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,512,rows,GL_RGBA,GL_FLOAT,data.data());
    std::copy(data.begin(),data.begin()+count*4,out.begin()+at*4);
   }
  };
  const auto& spec=kEnemyCraftSpecs[type];
  bool ok=enemyMesh::build(spec.boundsMin,spec.boundsMax,evaluate,[this]{bakeTick();},vertices,indices);
  glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glActiveTexture(GL_TEXTURE0);
  glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,tex);glDeleteProgram(program);
  if(!ok||!aircraftMesh::valid(vertices,indices)||indices.empty()){shaderNote("Enemy surface extraction failed: "+std::string(spec.id));return;}
  if(!path.empty()&&!enemyCache::save(path,fingerprint,vertices,indices))shaderNote("Enemy cache write failed: "+std::string(spec.id));
 }
 glGenVertexArrays(1,&mesh.vao);glGenBuffers(1,&mesh.vbo);glGenBuffers(1,&mesh.ebo);glBindVertexArray(mesh.vao);
 glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint32_t),indices.data(),GL_STATIC_DRAW);
 for(int a=0;a<3;a++){glEnableVertexAttribArray(a);glVertexAttribPointer(a,a==2?2:3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(uintptr_t((a==0?0:a==1?3:6)*sizeof(float))));}
 mesh.indices=int(indices.size());glBindVertexArray(0);
}
void Renderer::rasterEnemyShadowMaps(const FrameParams& fp){
 enemyShOn=0;
 // Build before any feed/main G-buffer pass changes its state. One shared mesh per type.
 for(int k=0;k<std::clamp(fp.enemyN,0,kMaxEnemyCraft);k++)if(enemyCraftValid(fp.enemies[k]))ensureEnemyMesh(int(fp.enemies[k].type));
 if(!progShMap||fp.sunDir.y<=-.05f||getenv("SHMAPOFF"))return;
 bool bound=false;vec3 d=normalize(fp.sunDir),up=fabsf(d.y)<.99f?vec3(0,1,0):vec3(0,0,1);
 for(int k=0;k<std::clamp(fp.enemyN,0,kMaxEnemyCraft);k++){
  const auto& c=fp.enemies[k];if(!enemyCraftValid(c))continue;
  const auto& m=enemyMeshes[int(c.type)];if(!m.indices)continue;
  float R=kEnemyCraftSpecs[int(c.type)].radius;if(length(c.pos-fp.camPos)>6000.f+R)continue;
  if(!bound){ensureShadowMaps();glBindFramebuffer(GL_FRAMEBUFFER,fboShMap);glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0,0,0);GLenum none=GL_NONE;glDrawBuffers(1,&none);glViewport(0,0,kShMapRes,kShMapRes);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);bound=true;}
  mat4 vp=enemyOrtho(R)*lookAt(c.pos+d*(2.f*R),c.pos,up);enemyShVP[k]=vp;
  glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texShMap,0,4+kMaxTrafficDrawn+k);glClearDepth(1.);glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(progShMap);glUniform1i(U(progShMap,"uPartInst"),-1);glUniformMatrix4fv(U(progShMap,"uVP"),1,GL_FALSE,vp.m);glUniformMatrix3fv(U(progShMap,"uRot"),1,GL_FALSE,c.rot);glUniform3f(U(progShMap,"uPos"),c.pos.x,c.pos.y,c.pos.z);
  glBindVertexArray(m.vao);glDrawElements(GL_TRIANGLES,m.indices,GL_UNSIGNED_INT,nullptr);enemyShOn|=1<<k;
 }
 if(bound){glBindVertexArray(0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_DEPTH_TEST);}
}
void Renderer::drawEnemyMeshes(const FrameParams& fp){
 mat4 vp=viewProjRel(fp,.01f,40000.f),view=viewMat(fp);
 for(int k=0;k<std::clamp(fp.enemyN,0,kMaxEnemyCraft);k++){
  const auto& c=fp.enemies[k];if(!enemyCraftValid(c))continue;
  auto& m=enemyMeshes[int(c.type)];if(!m.indices||!m.material)continue;
  GLuint p=m.material;setRT(p,fp);vec3 rel=c.pos-fp.camPos;
  glUniformMatrix4fv(U(p,"uVP"),1,GL_FALSE,vp.m);glUniformMatrix4fv(U(p,"uPanoView"),1,GL_FALSE,view.m);
  glUniformMatrix3fv(U(p,"uRot"),1,GL_FALSE,c.rot);glUniform3f(U(p,"uPos"),rel.x,rel.y,rel.z);
  glUniform2f(U(p,"uPano"),fp.pano,fp.panoTanY);glUniform2f(U(p,"uJit"),jitX,jitY);glUniform1f(U(p,"uLogC"),2.f/log2f(40001.f));glUniform4fv(U(p,"uEnemyState"),1,c.state);
  glBindVertexArray(m.vao);glDrawElements(GL_TRIANGLES,m.indices,GL_UNSIGNED_INT,nullptr);
 }
 glBindVertexArray(0);glActiveTexture(GL_TEXTURE0);
}

void Renderer::warmEnemyTypes() {
 enemyBake::Lease lease(enemyBakeState,true);if(!lease)return;
 EnemyBakeGLState savedGL;
 for(int type=0;type<kEnemyCraftTypes;type++)ensureEnemyMesh(type);
 // Warmup must never publish shadows for synthetic instances.
 enemyShOn=0;
}

namespace {
void storeRotation(const float* supplied,float* out){
 EnemyCraftVisual candidate;
 std::copy(supplied,supplied+9,candidate.rot);
 if(enemyCraftValid(candidate))std::copy(supplied,supplied+9,out);
 else{const float identity[9]={1,0,0,0,1,0,0,0,1};std::copy(identity,identity+9,out);}
}
}
const Renderer::PartMesh* Renderer::releasedStoreMesh(const FrameParams& fp,int kind) const {
 using namespace aircraftBuild;
 if(int(fp.plane.M[2]+.5f)!=6)return nullptr;
 auto model=planeMeshes.find(hullKey(fp,0));if(model==planeMeshes.end()||!model->second.ok)return nullptr;
 const int part=kind==1?PT_WR_PENETRATOR:kind==2?PT_WR_EMP:PT_WR_BOMB;
 for(const auto& mesh:model->second.parts)if(mesh.type==part&&mesh.idx>0)return &mesh;
 return nullptr;
}
void Renderer::warmWraithStores(const FrameParams& fp){
 if(int(fp.plane.M[2]+.5f)!=6)return;
 enemyBake::Lease warm(enemyBakeState,true);if(!warm)return;
 enemyBake::Lease bake(enemyBakeState,false);if(!bake)return;
 EnemyBakeGLState savedGL;
 const uint64_t key=hullKey(fp,0);
 if(!planeMeshes.count(key))bakePlaneMesh(fp,0,key);
 if(!releasedStoreTried){
  releasedStoreTried=true;std::string err;
  for(int kind=0;kind<3;kind++){
   progReleasedStore[kind]=linkProgramCached(std::string("#version 330 core\n")+kReleasedStoreVS,releasedStoreFSAssembly(kind),err);
   if(!progReleasedStore[kind])shaderNote("Released rigid store shader "+std::to_string(kind)+": "+err);
  }
 }
}
void Renderer::prepareReleasedStores(const FrameParams& fp){
 storeMeshOn=0;
 if(fp.fx.bombs<=0||int(fp.plane.M[2]+.5f)!=6)return;
 warmWraithStores(fp); // normal loading already did this; recover a missing exterior safely
 for(int i=0;i<std::clamp(fp.fx.bombs,0,8);i++)
  {int kind=std::clamp(int(fp.fx.bombStyle[i][3]+.5f),0,2);if(progReleasedStore[kind]&&releasedStoreMesh(fp,kind))storeMeshOn|=1<<i;}
}
void Renderer::rasterReleasedStoreShadows(const FrameParams& fp){
 storeShOn=0;std::fill(storeShFade,storeShFade+8,0.f);
 if(!storeMeshOn||!progShMap||fp.sunDir.y<=-.05f||getenv("SHMAPOFF"))return;
 ensureShadowMaps();glBindFramebuffer(GL_FRAMEBUFFER,fboShMap);
 glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0,0,0);GLenum none=GL_NONE;glDrawBuffers(1,&none);
 glViewport(0,0,kShMapRes,kShMapRes);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
 const vec3 d=normalize(fp.sunDir),up=std::abs(d.y)<.99f?vec3(0,1,0):vec3(0,0,1);
 for(int i=0;i<std::clamp(fp.fx.bombs,0,8);i++){
  if(!(storeMeshOn&(1<<i)))continue;
  const int kind=std::clamp(int(fp.fx.bombStyle[i][3]+.5f),0,2);
  const auto* mesh=releasedStoreMesh(fp,kind);if(!mesh)continue;
  vec3 pos(fp.fx.bomb[i][0],fp.fx.bomb[i][1],fp.fx.bomb[i][2]);
  const float radius=kind==1?1.03f:kind==2?.61f:.37f;
  float ground=fp.hangarPreview?fp.hangarOrigin.y:g_world.groundHeight(pos.x,pos.z);
  vec3 shadow=pos-d*(std::max(0.f,pos.y-ground)/std::max(d.y,.05f));
  if(!fp.hangarPreview){ground=g_world.groundHeight(shadow.x,shadow.z);shadow=pos-d*(std::max(0.f,pos.y-ground)/std::max(d.y,.05f));}
  const float shadowRadius=radius/std::max(d.y,.1f);
  auto pixelDiameter=[&](vec3 eye,float tanY,int height){
   float body=radius*height/(std::max(length(pos-eye),.1f)*std::max(tanY,.05f));
   float groundPixels=shadowRadius*height/(std::max(length(shadow-eye),.1f)*std::max(tanY,.05f));
   return std::max(body,groundPixels);
  };
  float pixels=pixelDiameter(fp.camPos,tanf(fp.fovY*.5f),rh);
  // A nearby bomb camera keeps full quality even if the main view is far away.
  for(const auto& camera:fp.feeds)if(camera.on)pixels=std::max(pixels,pixelDiameter(camera.pos,camera.tanY,std::max(camera.h,1)));
  float fade=smoothstepf(.45f,2.f,pixels);
  if(fade<=0.f)continue;
  storeShFade[i]=fade;
  mat4 vp=enemyOrtho(radius)*lookAt(pos+d*(2.f*radius),pos,up);storeShVP[i]=vp;
  glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,texShMap,0,4+kMaxTrafficDrawn+kMaxEnemyCraft+i);glClearDepth(1.);glClear(GL_DEPTH_BUFFER_BIT);
  float rot[9];storeRotation(fp.fx.bombRot[i],rot);
  glUseProgram(progShMap);glUniform1i(U(progShMap,"uPartInst"),-1);
  glUniformMatrix4fv(U(progShMap,"uVP"),1,GL_FALSE,vp.m);glUniformMatrix3fv(U(progShMap,"uRot"),1,GL_FALSE,rot);glUniform3f(U(progShMap,"uPos"),pos.x,pos.y,pos.z);
  glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->idx,GL_UNSIGNED_INT,nullptr);storeShOn|=1<<i;
 }
 glBindVertexArray(0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_DEPTH_TEST);
}
void Renderer::drawReleasedStores(const FrameParams& fp){
 if(!storeMeshOn)return;
 const mat4 vp=viewProjRel(fp,.01f,40000.f),view=viewMat(fp);
 for(int i=0;i<std::clamp(fp.fx.bombs,0,8);i++){
  if(!(storeMeshOn&(1<<i)))continue;
  int kind=std::clamp(int(fp.fx.bombStyle[i][3]+.5f),0,2);const auto* mesh=releasedStoreMesh(fp,kind);if(!mesh)continue;
  GLuint p=progReleasedStore[kind];setRT(p,fp);
  float rot[9];storeRotation(fp.fx.bombRot[i],rot);
  const vec3 pos=vec3(fp.fx.bomb[i][0],fp.fx.bomb[i][1],fp.fx.bomb[i][2])-fp.camPos;
  glUniformMatrix4fv(U(p,"uVP"),1,GL_FALSE,vp.m);glUniformMatrix4fv(U(p,"uPanoView"),1,GL_FALSE,view.m);glUniformMatrix3fv(U(p,"uRot"),1,GL_FALSE,rot);
  glUniform3f(U(p,"uPos"),pos.x,pos.y,pos.z);glUniform2f(U(p,"uPano"),fp.pano,fp.panoTanY);glUniform2f(U(p,"uJit"),jitX,jitY);glUniform1f(U(p,"uLogC"),2.f/log2f(40001.f));
  glUniform1i(U(p,"uStoreKind"),kind);glUniform1i(U(p,"uWrBombSet"),kind);
  glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->idx,GL_UNSIGNED_INT,nullptr);
 }
 glBindVertexArray(0);glActiveTexture(GL_TEXTURE0);
}
