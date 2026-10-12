#!/usr/bin/env python3
"""Generate test-only preview initialization copies; never edit production sources.

The launch builds no program with an aircraft's code in it (each aircraft's own builds are made as it is drawn), so
nothing is omitted any more: the copies only add the review's diagnostic checks - the aircraft must be one of the
valid selected airframe, and its production mesh is pre-baked before the first frame. Render geometry, materials, moving parts,
gear, lighting, shadows, and post passes are unchanged.
"""
from pathlib import Path
import sys
src,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
def replace_once(text,old,new):
    if text.count(old)!=1: raise SystemExit('Adapter anchor changed: '+old)
    return text.replace(old,new,1)
for name in ('raster_renderer.cpp','aircraft_mesh.cpp','renderer.cpp'):
    s=(src/name).read_text()
    if name=='renderer.cpp':
        # renderScene now wraps the flat frame before drawing. Inject after that
        # conversion so the diagnostic key uses exactly the production frame.
        anchor='  const FrameParams& fp = roundFrame(fpFlat, fpRound);'
        s=replace_once(s,anchor,anchor+'\n  if (!validAircraft(fp.plane.model) || !fp.plane.on) { fprintf(stderr,"DIAGNOSTIC ABORT: preview has no valid selected aircraft\\n"); abort(); }\n  const auto reviewKey = hullKey(fp, 0);\n  if (!planeMeshes.count(reviewKey)) { printf("DIAGNOSTIC pre-baking selected production mesh\\n"); bakePlaneMesh(fp, 0, reviewKey); }\n  if (!planeMeshes.count(reviewKey) || !planeMeshes.at(reviewKey).ok || !planeMeshes.at(reviewKey).idx) { fprintf(stderr,"DIAGNOSTIC ABORT: selected mesh unavailable\\n"); abort(); }')
        # Inspect the actual aircraft G-buffer and pre-UI lighting result. A
        # diagram, background picture or UI label cannot satisfy these checks.
        verification=r'''
  if (getenv("UI_VERIFY_PREVIEWS")) {
    std::vector<float> geometry(size_t(rw)*rh*4),raw(geometry.size());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fboGB);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,rw,rh,GL_RGBA,GL_FLOAT,geometry.data());
    if(fp.hangarClassified){
      glBindFramebuffer(GL_READ_FRAMEBUFFER,fboScene);glReadBuffer(GL_COLOR_ATTACHMENT0);
      glReadPixels(0,0,rw,rh,GL_RGBA,GL_FLOAT,raw.data());
    }
    // The following native TAA pass binds both framebuffer targets itself.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    size_t count=0,outline=0,hidden=0;int minX=rw,minY=rh,maxX=-1,maxY=-1;
    for(int y=0;y<rh;++y)for(int x=0;x<rw;++x){
      const size_t p=(size_t(y)*rw+x)*4;if(int(geometry[p+3]+.5f)<5)continue;
      ++count;minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
      if(fp.hangarClassified){
        if(raw[p+2]>.05f)++outline;
        else if(raw[p]<.002f&&raw[p+1]<.002f&&raw[p+2]<.002f)++hidden;
      }
    }
    if(count<100 || (fp.hangarClassified&&(outline<12||hidden<count/2))){
      fprintf(stderr,"DIAGNOSTIC ABORT: aircraft preview missing or classified outline invalid: model=%d pixels=%zu outline=%zu hidden=%zu\n",fp.plane.model,count,outline,hidden);abort();
    }
    printf("PREVIEW VERIFIED model=%d classified=%d aircraft_pixels=%zu outline_pixels=%zu hidden_pixels=%zu bounds=%d,%d,%d,%d\n",fp.plane.model,int(fp.hangarClassified),count,outline,hidden,minX,rh-1-maxY,maxX,rh-1-minY);
  }
'''
        s=replace_once(s,'  stamp(6);', '  stamp(6);\n'+verification)
    (out/name).write_text('// GENERATED TEST-ONLY FLEET ADAPTER: the review\'s diagnostic checks added.\n'+s)
print('Generated aircraft preview review adapters; production files unchanged; every startup program built.')
