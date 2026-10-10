#!/usr/bin/env python3
"""Run the production compile control flow with a fake linker, never a GPU compiler.
Usage: python3 tools/validation/shader_progress_contract.py --generated build/gen
Requires a native C++17 compiler and the generated shaders_gen.h. Failure injection
checks settled logical units independently of fallback/retry link-attempt counts.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--generated', required=True, type=Path)
parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
parts=[]
for file,fun in [('renderer.cpp','compilePrograms'),('aircraft_hull.cpp','compileHull'),('raster_renderer.cpp','compileRaster'),('raster_renderer.cpp','linkAfPass'),('aircraft_mesh.cpp','compilePlaneMesh'),('terrain_mesh.cpp','compileTerrainMesh')]:
 s=(root/'src'/file).read_text();a=s.index('Renderer::'+fun+'(');a=s.rindex('\n',0,a)+1;b=s.index('\n}\n',a)+3;parts.append(s[a:b])
s=(root/'src/aircraft_hull.cpp').read_text();a=s.index('namespace {');b=s.index('bool Renderer::compileHull');hull=s[a:b]
pre='''#include "renderer.h"
#include "shaders.h"
#include "entity_shaders.h"
#include "hangar_preview.h"
#include <set>
static int calls=0, normalFallbackAt=0; static std::set<int> fail;
GLuint linkProgramCached(const std::string&,const std::string&,std::string&,bool* safe){++calls;if(safe)*safe=calls==normalFallbackAt;return fail.count(calls)?0:calls;}
void shaderNote(const std::string&) {}
static GLuint program(const std::string& v,const std::string& f,std::string& e){return linkProgramCached(v,f,e);}
static void finish(){} static void del(GLuint){}
'''
main='''int main(){glFinish=finish;glDeleteProgram=del;
struct Case {const char* name;std::set<int> fail;int retry;bool ok;int completed;int calls;};
for(const auto& c:std::vector<Case>{
  {"success",{},0,true,33,33},
  {"optional cloud accumulation unavailable",{19},0,true,33,33},
  {"optional hull unavailable",{20},0,true,33,33},
  {"fatal scenery class",{3},0,false,3,4},
  {"fatal light",{22},0,false,21,22},
  {"fatal UFO and debris",{24},0,false,23,24},
  {"fatal maps-only shadows",{25},0,false,24,25}
}){calls=0;fail=c.fail;normalFallbackAt=c.retry;Renderer r;std::atomic<int> done{0};bool ok=r.compilePrograms(&done);printf("%s: ok=%d completed=%d attempts=%d\\n",c.name,ok,done.load(),calls);if(ok!=c.ok||done!=c.completed||calls!=c.calls){puts(r.error.c_str());return 1;}}
puts("Logical shader completion contract: PASS");
// the bodies' builder, made when a body is built (not at launch): its distance and normal programs are one field
{Renderer r;GLuint p[2];std::string e;
 calls=0;fail={};normalFallbackAt=2;bool ok=r.linkBakePair("v","f",p,e);   // (the normal needed the reduced field: the distance is built again with it)
 printf("bake pair, coupled retry: ok=%d programs=%u,%u attempts=%d\\n",ok,p[0],p[1],calls);if(!ok||p[0]!=3||p[1]!=2||calls!=3)return 1;
 calls=0;fail={2};normalFallbackAt=0;ok=r.linkBakePair("v","f",p,e);   // (no normal: no pair)
 printf("bake pair, normal unavailable: ok=%d programs=%u,%u attempts=%d\\n",ok,p[0],p[1],calls);if(ok||p[0]||p[1]||calls!=2)return 1;
 puts("Bake pair coupling: PASS");}
// the airframes' full-screen builds, made when a frame needs one (not at launch): the reduced builds in turn
{Renderer r;std::string e;
 calls=0;fail={1,2};normalFallbackAt=0;GLuint p=r.linkAfPass(Renderer::kAfProxy,"","test");
 printf("shadow proxy, two reduced builds failing: program=%u attempts=%d\\n",p,calls);if(p!=3||calls!=3)return 1;
 calls=0;fail={1};p=r.linkAfPass(Renderer::kAfObjects,"","test");
 printf("objects, failing: program=%u attempts=%d\\n",p,calls);if(p||calls!=1)return 1;
 puts("Full-screen builds: PASS");}}
'''
with tempfile.TemporaryDirectory(prefix='solace-shader-progress-') as tmp:
 source = Path(tmp) / 'progress_mock.cpp'
 binary = Path(tmp) / 'progress_mock'
 source.write_text(pre + hull + '\n'.join(parts) + main)
 subprocess.run([args.cxx, '-std=c++17', '-O0', '-I'+str(root/'src'),
                 '-I'+str(args.generated.resolve()), str(source), str(root/'src/gl.cpp'),
                 '-ldl', '-pthread', '-o', str(binary)], check=True)
 subprocess.run([str(binary)], check=True)
