#!/usr/bin/env python3
"""Generate test-only fleet initialization copies; never edit production sources.

Exact standard AF_LIGHT programs are compiled. Five unused generic/research/marched
programs are omitted (each aircraft's own mesh build is made as it is drawn, as in the
game); this is NOT full-startup validation. Render geometry,
materials, moving parts, gear, lighting, shadows, and post passes are unchanged.
"""
from pathlib import Path
import sys
import re
src,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
def replace_once(text,old,new):
    if text.count(old)!=1: raise SystemExit('Adapter anchor changed: '+old)
    return text.replace(old,new,1)
for name in ('raster_renderer.cpp','aircraft_mesh.cpp','renderer.cpp'):
    s=(src/name).read_text()
    if name=='raster_renderer.cpp':
        s=replace_once(s,'for (int v = 0; v < 2; v++) {','for (int v = 1; v < 2; v++) {')
        anchor='  progObjects = progObjectsV[0]; progShProxy = progShProxyV[0]; progEffects = progEffectsV[0];'
        s=replace_once(s,anchor,'  progObjectsV[0] = progObjectsV[1]; progShProxyV[0] = progShProxyV[1]; progEffectsV[0] = progEffectsV[1];\n'+anchor)
        start=s.index('    setCompileStage(v ? "aircraft (light aircraft build)"')
        end=s.index('    setCompileStage(v ? "effects (light aircraft build)"',start)
        s=s[:start]+'    std::string first;\n    // Test-only: unused marched objects/proxy shaders are omitted; runtime assertions below forbid their use.\n'+s[end:]
        s=replace_once(s,'  if (!progObjectsNoAf) { error = "Objects (UFO, debris) shader: " + e; return false; }','  if (!progObjectsNoAf) { error = "Objects (UFO, debris) shader: " + e; return false; }\n  progObjectsV[0] = progObjectsV[1] = progObjects = progObjectsNoAf;')
        s=replace_once(s,'  for (int v = 0; v < 2; v++) if (!progShProxyV[v]) { progShProxyV[v] = progShProxyMaps; if (step) step(); }','  for (int v = 0; v < 2; v++) progShProxyV[v] = progShProxyMaps;')
        anchor='  const bool marchAny = afMarch || fp.ufoOn || fp.wreck.debris > 0;'
        if anchor not in s:
            matches=re.findall(r'  const bool marchAny = [^;]+;',s)
            if len(matches)!=1:raise SystemExit('marchAny assertion anchor changed')
            anchor=matches[0]
        s=replace_once(s,anchor,'  if (afMarch) { fprintf(stderr,"DIAGNOSTIC ABORT: aircraft requires omitted objects march\\n"); abort(); }\n'+anchor)
        anchor='  setRT(proxyNeedsMarch(fp) ? progShProxy : progShProxyMaps, fp);'
        s=replace_once(s,anchor,'  if (proxyNeedsMarch(fp)) { fprintf(stderr,"DIAGNOSTIC ABORT: shadows require omitted proxy march\\n"); abort(); }\n'+anchor)

    elif name=='aircraft_mesh.cpp':
        s=replace_once(s,'progPlaneMesh = linkProgramCached(planeMeshVSAssembly(""), planeMeshFSAssembly(""), e);','progPlaneMesh = linkProgramCached(planeMeshVSAssembly(""), planeMeshFSAssembly("#define AF_LIGHT\\n"), e);')
    else:
        s=replace_once(s,'if (completed != kProgramCount)','if (completed != kProgramCount - 5)')
        anchor='void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {'
        s=replace_once(s,anchor,anchor+'\n  if (!(fp.plane.model >= 0 && fp.plane.model < kNumAircraft || fp.plane.model == kNightjar || fp.plane.model == kMantis) || kAircraft[fp.plane.model].special != 0) { fprintf(stderr,"DIAGNOSTIC ABORT: aircraft incompatible with exact retained fleet specialization\\n"); abort(); }\n  const auto reviewKey = hullKey(fp, 0);\n  if (!planeMeshes.count(reviewKey)) { printf("DIAGNOSTIC pre-baking selected production mesh\\n"); setRT(progHullBake, fp); bakePlaneMesh(fp, 0, reviewKey); }\n  if (!planeMeshes.count(reviewKey) || !planeMeshes.at(reviewKey).ok) { fprintf(stderr,"DIAGNOSTIC ABORT: selected mesh unavailable\\n"); abort(); }')
    (out/name).write_text('// GENERATED TEST-ONLY FLEET ADAPTER: five unused startup programs omitted.\n'+s)
print('Generated fleet-only startup adapters; production files unchanged; expected completed units = kProgramCount - 5.')
