#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "gl.h"
#include "shader_prune.h"   // (linkProgramCached prunes each stage before the cache and the compiler see it)
struct AuditRenderer { std::string compileStage(){return "CPU shader lifetime test";} } g_ren;
GLuint linkProgramCached(const std::string&, const std::string&, std::string&, bool* = nullptr);
#include "actual_shader_functions.inc"
namespace {
struct S {GLenum type; bool pending=false;int refs=0;};
struct P {std::set<GLuint> shaders;bool linked=false;};
std::map<GLuint,S> shaders;std::map<GLuint,P> programs;
GLuint nextName=1;GLenum failType=0;bool failLink=false,rejectBinary=false;int deleted=0,detached=0,binaryLoads=0,created=0,checks=0;
void test(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void eraseShader(GLuint x){auto it=shaders.find(x);test(it!=shaders.end(),"delete names a live shader");it->second.pending=true;if(!it->second.refs)shaders.erase(it);++deleted;}
void detach(GLuint p,GLuint x){test(programs[p].shaders.erase(x)==1,"detach names an attached shader");auto it=shaders.find(x);test(it!=shaders.end(),"attached object still alive");if(--it->second.refs==0&&it->second.pending)shaders.erase(it);++detached;}
void deleteProgram(GLuint p){test(programs.count(p)==1,"delete names a live program");while(!programs[p].shaders.empty())detach(p,*programs[p].shaders.begin());programs.erase(p);}
void bind(){
 glf::glCreateShader=[](GLenum t)->GLuint{GLuint s=nextName++;shaders[s]={t};++created;return s;};
 glf::glShaderSource=[](GLuint,GLsizei,const GLchar* const*,const GLint*){};
 glf::glCompileShader=[](GLuint){};
 glf::glGetShaderiv=[](GLuint s,GLenum,GLint* v){*v=shaders[s].type!=failType;};
 glf::glGetShaderInfoLog=[](GLuint,GLsizei n,GLsizei*,GLchar* b){snprintf(b,n,"synthetic compiler failure");};
 glf::glDeleteShader=eraseShader;
 glf::glCreateProgram=[]()->GLuint{GLuint p=nextName++;programs[p]={};return p;};
 glf::glAttachShader=[](GLuint p,GLuint s){test(shaders.count(s)==1,"attach live shader");programs[p].shaders.insert(s);++shaders[s].refs;};
 glf::glDetachShader=detach;
 glf::glLinkProgram=[](GLuint p){programs[p].linked=!failLink;};
 glf::glGetProgramiv=[](GLuint p,GLenum what,GLint* out){*out=what==GL_LINK_STATUS?programs[p].linked:8;};
 glf::glGetProgramInfoLog=[](GLuint,GLsizei n,GLsizei*,GLchar* b){snprintf(b,n,"synthetic linker failure");};
 glf::glDeleteProgram=deleteProgram;
 glf::glGetIntegerv=[](GLenum,GLint* out){*out=1;};
 glf::glGetString=[](GLenum)->const GLubyte*{return reinterpret_cast<const GLubyte*>("CPU mock GL");};
 glf::glProgramParameteri=[](GLuint,GLenum,GLint){};
 glf::glGetProgramBinary=[](GLuint p,GLsizei n,GLsizei* got,GLenum* fmt,void* data){test(programs[p].shaders.empty(),"binary retrieval after shader detachment");*got=std::min(n,8);*fmt=7;std::memcpy(data,"program!",*got);};
 glf::glProgramBinary=[](GLuint p,GLenum,const void*,GLsizei){++binaryLoads;programs[p].linked=!rejectBinary;};
}
}
int main(){bind();const std::string vs="#version 330 core\nvoid main(){}",fs="#version 330 core\nvoid main(){}";std::string error;
 GLuint p=linkProgramCached(vs,fs,error);test(p&&error.empty(),"successful link returns program");test(shaders.empty()&&programs[p].shaders.empty(),"success releases both shaders");test(detached==2&&deleted==2,"success detaches and deletes exactly twice");deleteProgram(p);
 for(GLenum type:{GL_VERTEX_SHADER,GL_FRAGMENT_SHADER}){failType=type;error.clear();test(linkProgramCached(vs,fs,error)==0,"compile failure returned");test(!error.empty()&&shaders.empty()&&programs.empty(),"failed shader and successful sibling cleaned");}failType=0;
 failLink=true;error.clear();test(linkProgramCached(vs,fs,error)==0,"link failure returned");test(!error.empty()&&shaders.empty()&&programs.empty(),"link failure releases program and shaders");failLink=false;
 const auto cache=std::filesystem::current_path()/"cache";std::filesystem::create_directories(cache);g_shaderCacheDir=cache.string();
 // Unique shader text per run avoids a previous unit run changing cold/warm coverage.
 const std::string source=fs+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 error.clear();int made=created;p=linkProgramCached(vs,source,error);test(p&&created==made+2&&shaders.empty(),"cold cache build cleans shaders");deleteProgram(p);
 made=created;int loads=binaryLoads;p=linkProgramCached(vs,source,error);test(p&&created==made&&binaryLoads==loads+1,"warm binary bypasses shader creation");deleteProgram(p);
 rejectBinary=true;made=created;p=linkProgramCached(vs,source,error);test(p&&created==made+2&&shaders.empty(),"rejected binary falls back and cleans shaders");deleteProgram(p);
 test(programs.empty()&&shaders.empty(),"all tested paths release owned GL objects");printf("PASS: %d assertions; successful compile/link, vertex failure, fragment failure, link failure, cold/warm/rejected binary paths\n",checks);
}
