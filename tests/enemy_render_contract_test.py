#!/usr/bin/env python3
"""Pass-order/depth/capacity guards; complements actual EGL image inspection."""
from pathlib import Path
import math
root=Path(__file__).resolve().parents[1]
def read(p): return (root/p).read_text()
s=read('src/raster_renderer.cpp')
world=s.split('void Renderer::rasterWorld(',1)[1].split('void Renderer::rasterObjects(',1)[0]
objects=s.split('void Renderer::rasterObjects(',1)[1].split('static mat4 orthoMat',1)[0]
assert 'drawEnemyMeshes' not in world, 'enemy pixels must not precede the world G-buffer clear'
draw=objects.index('drawEnemyMeshes(fp)')
for token in ['ensureEnemyMesh(', 'glBindFramebuffer(GL_FRAMEBUFFER, fboGB)', 'glDrawBuffers(4, gb)', 'glEnable(GL_DEPTH_TEST)', 'glDepthMask(GL_TRUE)']:
    assert objects.index(token)<draw, ('required enemy draw setup missing/late',token)
assert s.count('drawEnemyMeshes(fp)')==1, 'one native enemy draw per object pass'
feeds=read('src/camera_feeds.cpp')
assert 'rasterObjects(cf)' in feeds
render=read('src/renderer.cpp')
assert 'rasterObjects(fp)' in render, 'main/inspection views execute object pass'
vs=read('src/shaders/enemy_mesh_vs.glsl')
assert 'gl_Position.z=(log2' in vs and 'uPanoView*vec4(vW,0.)' in vs
# GLSL explicitly replaces projection z; perspective far=2 km was immaterial.
# Production now declares 40 km too. Test actual logarithmic clip expression.
for distance in [.1, 2, 2000, 4300, 8000, 39999]:
    z_over_w=math.log2(1+distance)*(2/math.log2(40001))-1
    assert -1<z_over_w<1, ('valid mission distance clipped',distance)
assert 'viewProjRel(fp,.01f,40000.f)' in read('src/enemy_mesh.cpp')
assert 'GB_ENEMY' in read('src/shaders/enemy_mesh_fs.glsl')
assert 'GBF_MOVING' in read('src/shaders/enemy_mesh_fs.glsl')
assert 'raymarch' not in read('src/shaders/enemy_mesh_fs.glsl').lower()
assert 'warmEnemyTypes' in read('src/enemy_mesh.cpp')
assert 'fp.hiveOrdnanceN' in read('src/hive_ordnance_sprites.h')
print('PASS native enemies draw after world clear with bound G-buffer/depth; feeds share path; 8 km/40 km logarithmic visibility; independent ordnance')

store=read('src/enemy_mesh.cpp')
assert 'planeMeshes.find(hullKey(fp,0))' in store, 'released stores need exterior parts even from cockpit'
assert 'glBindVertexArray(mesh->vao);glDrawElements(GL_TRIANGLES,mesh->idx' in store, 'reuse actual cached store mesh'
assert 'warmWraithStores' in store
assert 'pixelDiameter(camera.pos' in store, 'near bomb camera preserves shadow quality'
assert 'smoothstepf(.45f,2.f,pixels)' in store
shadow=read('src/shaders/enemy_shadow.glsl')
assert 'if(uStoreShOn==0)return 1.' in shadow and 'mix(1.,lit*.25,uStoreShFade[k])' in shadow
print('PASS released stores reuse exterior hardware; explicit warmup; feed-aware subpixel shadow culling/fade and zero-mask fast path')

# Authored micrograin and panel seams must be filtered at screen-space Nyquist.
material=(root/"src/shaders/enemy_material.glsl").read_text()
assert "dFdx(phase)" in material and "dFdy(phase)" in material
assert "sin(phase)*grainWeight" in material and "smoothstep(1.5,3.14159265,grainFootprint)" in material
assert "fwidth(p.z*.60)" in material and "mix(seam,.025" in material
print("PASS enemy authored micrograin and panel seams retain close detail with derivative filtering")

# Large preexisting showroom triangles span the eye plane. Vertex log z used to
# clip them against a spurious far plane, exposing sky through the floor.
hangar=read("src/hangar_preview.h")
hvs=hangar.split('static const char* kVS',1)[1].split('static const char* kFS',1)[0]
hfs=hangar.split('static const char* kFS',1)[1].split('static const char* kClassifiedFS',1)[0]
assert "gl_Position=uVP*vec4" in hvs and "gl_Position.z=" not in hvs
assert "gl_FragDepth=log2" in hfs
near,far=.1,40000.
w0,w1=-5.,20.
def linear_z(w): return (far+near)/(far-near)*w-2*far*near/(far-near)
a,b=linear_z(w0)+w0,linear_z(w1)+w1
t=-a/(b-a)
assert abs((w0+(w1-w0)*t)-near)<1e-9
def old_log_z(w): return (math.log2(max(1e-6,1+w))*2/math.log2(40001)-1)*w
a,b=old_log_z(w0)-w0,old_log_z(w1)-w1
t=-a/(b-a)
assert w0+(w1-w0)*t>5 # old shader spuriously clipped meters of visible foreground
print("PASS showroom eye-crossing quads use conventional clipping and fragment logarithmic depth")

effects=read("src/raster_renderer.cpp").split("void Renderer::rasterEffects",1)[1].split("bool Renderer::rasterCloak",1)[0]
assert "const bool wraith = (p.on || weapons)" in effects
assert "wraith ? afModelOf(p.M, p.model)" in effects
print("PASS live Wraith ordnance keeps its specialized effects shader when the hull is hidden")
