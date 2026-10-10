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
for file,fun in [('renderer.cpp','compilePrograms'),('aircraft_hull.cpp','compileHull'),('raster_renderer.cpp','compileRaster'),('aircraft_mesh.cpp','compilePlaneMesh'),('terrain_mesh.cpp','compileTerrainMesh')]:
 s=(root/'src'/file).read_text();a=s.index('bool Renderer::'+fun+'(');b=s.index('\n}\n',a)+3;parts.append(s[a:b])
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
  {"success",{},0,true,41,41},
  {"proxy maps fallback",{22,23,24,25},0,true,41,44},
  {"optional cloud accumulation unavailable",{15},0,true,41,41},
  {"optional bake unavailable",{17},0,true,41,40},
  {"coupled normal retry",{},18,true,41,42},
  {"optional XR mesh unavailable",{34},0,true,41,41},
  {"fatal light",{20},0,false,19,20},
  {"fatal fleet mesh",{32},0,false,31,32}
}){calls=0;fail=c.fail;normalFallbackAt=c.retry;Renderer r;std::atomic<int> done{0};bool ok=r.compilePrograms(&done);printf("%s: ok=%d completed=%d attempts=%d\\n",c.name,ok,done.load(),calls);if(ok!=c.ok||done!=c.completed||calls!=c.calls){puts(r.error.c_str());return 1;}}
puts("Logical shader completion contract: PASS");}
'''
with tempfile.TemporaryDirectory(prefix='solace-shader-progress-') as tmp:
 source = Path(tmp) / 'progress_mock.cpp'
 binary = Path(tmp) / 'progress_mock'
 source.write_text(pre + hull + '\n'.join(parts) + main)
 subprocess.run([args.cxx, '-std=c++17', '-O0', '-I'+str(root/'src'),
                 '-I'+str(args.generated.resolve()), str(source), str(root/'src/gl.cpp'),
                 '-ldl', '-pthread', '-o', str(binary)], check=True)
 subprocess.run([str(binary)], check=True)
