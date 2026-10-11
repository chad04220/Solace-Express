#!/usr/bin/env python3
"""Compile a separate load/upload-only diagnostic; never overwrites a visual adapter."""
from pathlib import Path
import argparse,subprocess,json,hashlib
D=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('--adapter',required=True);a=p.parse_args();R=D/'adapters'/a.adapter;P=json.loads((R/'build_provenance.json').read_text());S=Path(P['source']);B=Path(P['build_dir']);O=R/'body-benchmark';O.mkdir(exist_ok=True)
r=(R/'renderer_review.cpp').read_text();anchor='  // DIAGNOSTIC ONLY: read actual uploaded vertices and GPU-computed poses. No geometry writes.';assert r.count(anchor)==1;r=r.replace(anchor,'  return; // Diagnostic load/upload-only: no scene initialization, rendering or shader compilation.\n'+anchor,1);(O/'renderer_body.cpp').write_text(r)
h=(R/'aircraft_review.cpp').read_text().split('struct GameTest {')[0]
h+=r'''
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;
 if(!initGL(32,32))return 77;
 g_shaderCacheDir=argv[1];g_ren.quality=1;
#ifdef REVIEW_PORTABLE_API
 g_ren.prebuiltAircraftDir=argv[2];
#endif
 printf("REVIEW_BODY_BENCHMARK %s | %s | %s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
 for(int model=0;model<kAircraftCount;++model)for(int slot=0;slot<2;++slot){
  FrameParams fp{};fp.plane.on=true;fp.plane.model=model;packModelOf(model,fp.plane.M);fp.plane.PS[3]=float(slot);fp.plane.rot[0]=fp.plane.rot[4]=fp.plane.rot[8]=1.f;
  g_ren.renderScene(fp,{},{});glFinish();auto error=glGetError();printf("BENCH_GL model=%d slot=%d error=%x\n",model,slot,error);if(error)return 3;if(g_ren.bakeBuilt){puts("UNEXPECTED_BAKE: load-only benchmark invalid");return 4;}
 }
 printf("CACHE shader_hits=%d shader_misses=%d meshes_requested=%d meshes_built=%d\n",g_shaderCacheHits.load(),g_shaderCacheMisses.load(),g_ren.bakeCount,g_ren.bakeBuilt);
#ifdef REVIEW_PORTABLE_API
 printf("REVIEW_PREBUILT hits=%d misses=%d\n",g_ren.prebuiltMeshHits,g_ren.prebuiltMeshMisses);
#endif
 return 0;
}
'''
(O/'main.cpp').write_text(h)
flags=['g++','-std=c++17','-O2','-DNDEBUG','-I'+str(S/'src'),'-I'+str(B/'gen')]
if 'prebuiltAircraftDir' in (S/'src/renderer.h').read_text():flags+=['-DREVIEW_PORTABLE_API=1']
subprocess.run(flags+['-c',str(O/'renderer_body.cpp'),'-o',str(O/'renderer_body.o')],check=True)
subprocess.run(flags+[str(O/'main.cpp'),str(S/'src/radio_stub.cpp'),str(O/'renderer_body.o')]+P['linked_objects']+['-ldl','-pthread','-o',str(O/'body_benchmark')],check=True)
(O/'provenance.json').write_text(json.dumps({'visual_adapter':P['binary_sha256'],'production_source':P['source'],'source_manifest':P['sources'],'binary_sha256':hashlib.sha256((O/'body_benchmark').read_bytes()).hexdigest(),'renderer_sha256':hashlib.sha256((O/'renderer_body.cpp').read_bytes()).hexdigest(),'main_sha256':hashlib.sha256((O/'main.cpp').read_bytes()).hexdigest(),'change':'Dedicated renderScene returns immediately after unchanged production bakePlaneMesh/load and GPU upload, before scene drawing. No production source changes.'},indent=2)+'\n')
print(O/'body_benchmark')
