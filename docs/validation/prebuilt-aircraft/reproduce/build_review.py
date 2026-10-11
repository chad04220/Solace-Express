#!/usr/bin/env python3
"""Read-only current-release native review adapter. No repository files are edited."""
from pathlib import Path
import argparse,subprocess,hashlib,json,re,time,os
D=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--name',required=True);p.add_argument('--jobs',default='1');p.add_argument('--cmake',default=os.environ.get('CMAKE','cmake'));p.add_argument('--harness',type=Path,default=D/'aircraft_review.cpp');a=p.parse_args();S=a.source.resolve();R=D/'adapters'/a.name;B=R/'build';R.mkdir(parents=True,exist_ok=True)
cmake=a.cmake
files=sorted(x for root in [S/'src',S/'assets/materials'] for x in root.rglob('*') if x.is_file())
def hashes():return {str(x.relative_to(S)):hashlib.sha256(x.read_bytes()).hexdigest() for x in files}
before=hashes();start=time.time()
subprocess.run([str(cmake),'-S',str(S),'-B',str(B),'-DCMAKE_BUILD_TYPE=Release'],check=True)
subprocess.run([str(cmake),'--build',str(B),'--target','game_objs','env_objs','core_objs','mesh_objs','-j'+a.jobs],check=True)
harness=R/'aircraft_review.cpp';harness.write_bytes(a.harness.read_bytes())
s=(S/'src/renderer.cpp').read_text();anchor='void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {';assert s.count(anchor)==1
s=s.replace(anchor,anchor+'''\n  // DIAGNOSTIC ONLY: force requested exact production body before drawing.\n  if (fp.plane.on) { const int reviewSlot=fp.plane.PS[3]>.5f?1:0; const auto reviewKey=hullKey(fp,reviewSlot);\n    if (!planeMeshes.count(reviewKey)) { const auto reviewStart=std::chrono::steady_clock::now();const auto reviewHits=g_shaderCacheHits.load(),reviewMisses=g_shaderCacheMisses.load();printf("REVIEW_BODY_BEGIN model=%d slot=%d\\n",fp.plane.model,reviewSlot); bakePlaneMesh(fp,reviewSlot,reviewKey);glFinish();printf("REVIEW_BODY_END model=%d slot=%d seconds=%.9f shader_hits=%d shader_misses=%d built=%d\\n",fp.plane.model,reviewSlot,std::chrono::duration<double>(std::chrono::steady_clock::now()-reviewStart).count(),g_shaderCacheHits.load()-reviewHits,g_shaderCacheMisses.load()-reviewMisses,bakeBuilt); }\n    if (!planeMeshes.count(reviewKey)||!planeMeshes.at(reviewKey).ok) { fprintf(stderr,"DIAGNOSTIC mesh unavailable\\n"); abort(); }\n  }\n''',1)
anchor='  const bool classified = fp.hangarPreview';assert s.count(anchor)==1
s=s.replace(anchor,(D/'mesh_readback.inc').read_text()+'\n'+(D/'mesh_exact_readback.inc').read_text()+'\n'+anchor,1)
shader_anchor='GLuint linkProgramCached(const std::string& vs, const std::string& fs, std::string& err, bool* usedSafeGear) {'
assert s.count(shader_anchor)==1
s=s.replace(shader_anchor,shader_anchor+'''\n  struct ReviewShaderClock { std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now(); std::string stage=g_ren.compileStage();int hits=g_shaderCacheHits.load(),misses=g_shaderCacheMisses.load();~ReviewShaderClock(){printf("REVIEW_SHADER seconds=%.9f hits=%d misses=%d stage=%s\\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),g_shaderCacheHits.load()-hits,g_shaderCacheMisses.load()-misses,stage.c_str());}} reviewShaderClock;\n''',1)
(R/'renderer_review.cpp').write_text(s)
flags=['g++','-std=c++17','-O2','-DNDEBUG','-I'+str(S/'src'),'-I'+str(B/'gen')]
if 'prebuiltAircraftDir' in (S/'src/renderer.h').read_text():flags += ['-DREVIEW_PORTABLE_API=1']
subprocess.run(flags+['-c',str(R/'renderer_review.cpp'),'-o',str(R/'renderer_review.o')],check=True)
objs=[]
for target in ['env_objs','core_objs','mesh_objs','game_objs']:
 deps=(B/f'CMakeFiles/{target}.dir/DependInfo.cmake').read_text()
 objects=list(dict.fromkeys(re.findall(r'"(CMakeFiles/[^\"]+\.o)"',deps)))
 objs += [str(B/x) for x in objects if Path(x).name!='renderer.cpp.o']
assert objs and all(Path(x).is_file() for x in objs)
subprocess.run(flags+[str(harness),str(S/'src/radio_stub.cpp'),str(R/'renderer_review.o')]+objs+['-ldl','-pthread','-o',str(R/'aircraft_review')],check=True)
after=hashes();changed=[p for p in after if before.get(p)!=after[p]]
manifest={'source':str(S),'build_dir':str(B),'adapter':str(R),'started_epoch':start,'source_stable_during_build':not changed,'sources_changed_during_build':changed,'sources':after,'harness_sha256':hashlib.sha256(harness.read_bytes()).hexdigest(),'readback_sha256':hashlib.sha256((D/'mesh_exact_readback.inc').read_bytes()).hexdigest(),'shader_embed_sha256':hashlib.sha256((B/'gen/shaders_gen.h').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256((R/'aircraft_review').read_bytes()).hexdigest(),'linked_objects':objs}
(R/'build_provenance.json').write_text(json.dumps(manifest,indent=2)+'\n');assert not changed
print(json.dumps({'pass':True,'binary':str(R/'aircraft_review'),'sources':len(after),'objects':len(objs)}))
