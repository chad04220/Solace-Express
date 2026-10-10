#!/usr/bin/env python3
"""Generate test-only environment renderer copies. Never alters production files.

Retains exact production entity, terrain, water, lighting, shadow and post programs.
Current lazy-aircraft renderer: zero omitted programs. Older baselines skip only
unused aircraft/marched-object/mesh-builder/display startup programs.
Runtime assertions require aircraft, traffic, debris, weapons, feeds and cockpit off.
This validates environment rendering, not full game startup or GPU performance.
"""
from pathlib import Path
import sys
src,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
def once(s,a,b):
    if s.count(a)!=1:raise SystemExit('Adapter anchor changed: '+a)
    return s.replace(a,b,1)
s=(src/'raster_renderer.cpp').read_text()
lazy_aircraft = 'GLuint Renderer::afPassProgram(' in s
if not lazy_aircraft:
    a=s.index('bool Renderer::compileRaster(');b=s.index('// The light build',a)
    s=s[:a]+'''bool Renderer::compileRaster(const std::function<void()>& step) {
      std::string e;
      setCompileStage("environment lighting (exact production)");
      progLight=linkProgramCached(kFullscreenVS,lightFSAssembly(""),e);
      if(!progLight){error="Lighting shader: "+e;return false;} if(step)step();
      setCompileStage("environment shadow proxy (production maps-only variant)");
      e.clear();progShProxyMaps=linkProgramCached(kFullscreenVS,shadowProxyFSAssembly("#define AF_LIGHT\\n#define PROXY_MAPS_ONLY\\n"),e);
      if(!progShProxyMaps){error="Shadow proxy shader: "+e;return false;}if(step)step();
      progShProxyV[0]=progShProxyV[1]=progShProxy=progShProxyMaps;
      return compileTerrainMesh(step);
    }

    '''+s[b:]
    s=once(s,'  setRT(proxyNeedsMarch(fp) ? progShProxy : progShProxyMaps, fp);','  if(proxyNeedsMarch(fp)){fprintf(stderr,"ENVIRONMENT REVIEW ABORT: omitted aircraft shadow march requested\\n");abort();}\n  setRT(proxyNeedsMarch(fp) ? progShProxy : progShProxyMaps, fp);')
(out/'raster_renderer.cpp').write_text('// GENERATED TEST-ONLY ENVIRONMENT ADAPTER\n'+s)
s=(src/'renderer.cpp').read_text()
if not lazy_aircraft:
    a=s.index('    setCompileStage("the aircraft mesh builder")');b=s.index('    setCompileStage("the renderer:',a)
    s=s[:a]+'    // Environment review: aircraft mesh bakes and cockpit display programs unused.\n'+s[b:]
    s=once(s,'  if (completed != kProgramCount)', '  if(!progTShBake || !progClouds || !progCloudComp || !progCloudAcc){error="Environment review requires all production environmental programs";return false;}\n  if (completed != kProgramCount)')
    s=once(s,'if (completed != kProgramCount)','if (completed != kProgramCount - 20)')
if lazy_aircraft:
    s=once(s,'  if (completed != kProgramCount)', '  if(!progTShBake || !progClouds || !progCloudComp || !progCloudAcc){error="Environment review requires all production environmental programs";return false;}\n  if (completed != kProgramCount)')
a='void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {'
s=once(s,a,a+'''
  if(fp.plane.on || fp.trafficN || fp.ufoOn || fp.wreck.pieces || fp.wreck.debris || fp.fx.beams || fp.fx.bombs || fp.fx.blasts || fp.feedRig || fp.dispMode || fp.hangarPreview){fprintf(stderr,"ENVIRONMENT REVIEW ABORT: scene requests omitted non-environment programs\\n");abort();}
''')
mode = ('Full production startup: every program is built, zero omitted programs. Environment scene assertions and read-only counters only.' if lazy_aircraft else 'Legacy baseline adapter: twenty unused non-environment startup programs omitted; not full-startup validation.')
s += '\nconst char* reviewStartupMode(){return "' + mode + '";}\n'
# Optional read-only GPU diagnostics: copy state when the actual lighting program
# receives its uniforms. The fixture may then read the existing buffers; no pass,
# shader, texture, filtering parameter or production source is changed.
diagnostics=src.parent/'tests/environment_review_diagnostics.h'
if diagnostics.exists():
    a='void Renderer::setRT(GLuint p, const FrameParams& fp) {'
    s=once(s,a,a+'''
  if(p == progLight && (getenv("ASSET_REVIEW_SHADOW_DUMP") || getenv("ASSET_REVIEW_COVERAGE_LOG"))) {
    auto& d=g_environmentReviewShadowState;
    d.gbuffer=fboGB;d.shadow[0]=fboSh[0];d.shadow[1]=fboSh[1];
    d.width=rw;d.height=rh;d.shadowResolution=shRes;
    for(int c=0;c<2;++c){d.radii[c]=shR[c];for(int i=0;i<16;++i)d.matrices[c*16+i]=shVP[c].m[i];}
    d.centers[0]=shIdeal[0].x;d.centers[1]=shIdeal[0].z;d.centers[2]=shIdeal[1].x;d.centers[3]=shIdeal[1].z;
    d.jitter[0]=jitX;d.jitter[1]=jitY;
    d.camera[0]=fp.camPos.x;d.camera[1]=fp.camPos.y;d.camera[2]=fp.camPos.z;
    const float basis[9]={fp.camRight.x,fp.camRight.y,fp.camRight.z,fp.camUp.x,fp.camUp.y,fp.camUp.z,fp.camBack.x,fp.camBack.y,fp.camBack.z};
    for(int i=0;i<9;++i)d.basis[i]=basis[i];
    d.sun[0]=fp.sunDir.x;d.sun[1]=fp.sunDir.y;d.sun[2]=fp.sunDir.z;
    d.tanHalf=tanf(fp.fovY*0.5f);d.aspect=float(W)/H;d.valid=1;
  }
''')
    adaptive='nearShadowCoverage.fadeRadii(shR[0])' in s
    if adaptive:
        anchor='    d.tanHalf=tanf(fp.fovY*0.5f);d.aspect=float(W)/H;d.valid=1;'
        s=once(s,anchor,anchor+"""
    d.committedRadius=nearShadowCoverage.radius();d.fadeScale=nearShadowCoverage.fadeScale();
    d.filteredSpeed=nearShadowCoverage.speed();d.agl=nearShadowCoverage.agl();
    d.phase=int(nearShadowCoverage.phase());d.tier=nearShadowCoverage.tier();
    const vec2 reviewNearFade=nearShadowCoverage.fadeRadii(shR[0]);
    d.nearFade[0]=reviewNearFade.x;d.nearFade[1]=reviewNearFade.y;
""")
    else:
        anchor='    d.tanHalf=tanf(fp.fovY*0.5f);d.aspect=float(W)/H;d.valid=1;'
        s=once(s,anchor,anchor+'\n    d.committedRadius=shR[0];d.nearFade[0]=shR[0]*kShFade0;d.nearFade[1]=shR[0]*kShFade1;')
    s='#include "'+str(diagnostics.resolve())+'"\nEnvironmentReviewShadowState g_environmentReviewShadowState;\n'+s
(out/'renderer.cpp').write_text('// GENERATED TEST-ONLY ENVIRONMENT ADAPTER\n'+s)
s=(src/'entity_render.cpp').read_text()
# Explicit diagnostic only: same two allocations and shader programs, narrower
# first cascade. Never enabled in ordinary captures or production sources.
adaptive_anchor='  cR[0] = nearShadowCoverage.radius() > 0.f ? nearShadowCoverage.radius() : R.sh0;'
a=adaptive_anchor if adaptive_anchor in s else '  float cR[2] = {R.sh0, R.sh1};'
s=once(s,a,a+'''
  if(const char* radius=getenv("ASSET_REVIEW_NEAR_SHADOW_RADIUS")) {
    const float value=float(atof(radius));
    if(!std::isfinite(value)||value<32.f||value>2000.f){fprintf(stderr,"Invalid review-only shadow radius\\n");abort();}
    cR[0]=value;
  } else if(getenv("ASSET_REVIEW_NEAR_SHADOW_96")) cR[0]=96.f;
''')
a='  if (!feedPass) { entDrawn = 0; for (auto& d : draws[0]) entDrawn += d.count; }'
s=once(s,a,a+'''
  if(!feedPass) {
    extern unsigned long long reviewEntityTriangles[3],reviewEntityInstances[3],reviewEntityDrawCalls[3],reviewEntityUploadBytes;
    reviewEntityUploadBytes=entStage.size()*sizeof(Ent);
    for(int p=0;p<3;++p){reviewEntityTriangles[p]=reviewEntityInstances[p]=0;reviewEntityDrawCalls[p]=draws[p].size();
      for(const auto& d:draws[p]){reviewEntityTriangles[p]+=(unsigned long long)d.count*entRange[d.kind].count[d.lod]/3;reviewEntityInstances[p]+=d.count;}}
  }
''')
if diagnostics.exists():
    anchor='  double tGather = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();'
    s=once(s,anchor,"""
  if(!feedPass && (getenv("ASSET_REVIEW_SHADOW_DUMP") || getenv("ASSET_REVIEW_COVERAGE_LOG"))) {
    auto& d=g_environmentReviewShadowState;
    d.nearDirty=int(shDirty[0]);d.farDirty=int(shDirty[1]);
    d.radiusChanged=shValid[0] && shR[0]!=cR[0];
  }
"""+anchor)
    s='#include "'+str(diagnostics.resolve())+'"\n'+s
(out/'entity_render.cpp').write_text('// GENERATED TEST-ONLY READ-ONLY DRAW COUNTERS\n'+s)
print(mode)
