#!/usr/bin/env python3
"""Generate test-only fleet initialization copies; never edit production sources.

The launch builds no program with an aircraft's code in it (each aircraft's own builds are made as it is drawn), so
nothing is omitted any more: the copies only add the review's diagnostic checks - the aircraft must be one of the
standard fleet, and its production mesh is pre-baked before the first frame. Render geometry, materials, moving parts,
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
        anchor='void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {'
        s=replace_once(s,anchor,anchor+'\n  if (!(fp.plane.model >= 0 && fp.plane.model < kNumAircraft || fp.plane.model == kNightjar || fp.plane.model == kMantis) || kAircraft[fp.plane.model].special != 0) { fprintf(stderr,"DIAGNOSTIC ABORT: aircraft incompatible with exact retained fleet specialization\\n"); abort(); }\n  const auto reviewKey = hullKey(fp, 0);\n  if (!planeMeshes.count(reviewKey)) { printf("DIAGNOSTIC pre-baking selected production mesh\\n"); bakePlaneMesh(fp, 0, reviewKey); }\n  if (!planeMeshes.count(reviewKey) || !planeMeshes.at(reviewKey).ok) { fprintf(stderr,"DIAGNOSTIC ABORT: selected mesh unavailable\\n"); abort(); }')
    (out/name).write_text('// GENERATED TEST-ONLY FLEET ADAPTER: the review\'s diagnostic checks added.\n'+s)
print('Generated fleet-only review adapters; production files unchanged; every startup program built.')
