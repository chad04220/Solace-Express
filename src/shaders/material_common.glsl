//! kMaterialCommon
//! The material record and the texture-array sampling: planar, anti-tiled ground, triplanar; tangent-space detail normals.
// triplanar/planar texture sampling with anti-tiling (two scales)
#ifdef ENV_MATERIALS
// Seven selective photographic 2k layers, using the same fetch count as the 512 fallback.
// Only environment assemblies define this. Aircraft/cockpit/shared world-library paths
// retain their original sampler, texture sizes, material helpers and array layer mapping.
uniform sampler2DArray uEnvAlb; uniform sampler2DArray uEnvNrm; uniform int uEnvMaterials;
int environmentLayer(int layer){
  return layer == M_GRASS ? 0 : layer == M_ASPHALT ? 1 : layer == M_CONCRETE ? 2 : layer == M_BRICK ? 3
       : layer == M_PLASTER ? 4 : layer == M_TILES ? 5 : layer == M_BARK ? 6 : -1;
}
#endif
vec4 texA(vec2 uv, int layer){
#ifdef ENV_MATERIALS
  int env = environmentLayer(layer);
  if (uEnvMaterials != 0 && env >= 0) return texture(uEnvAlb, vec3(uv, float(env)));
#endif
  return texture(uAlb, vec3(uv, float(layer)));
}
vec4 texN(vec2 uv, int layer){
#ifdef ENV_MATERIALS
  int env = environmentLayer(layer);
  if (uEnvMaterials != 0 && env >= 0) return texture(uEnvNrm, vec3(uv, float(env)));
#endif
  return texture(uNrm, vec3(uv, float(layer)));
}
vec4 matSample(vec2 xz, int layer, float scale, out vec3 nTS){
  vec2 uv1 = xz/scale, uv2 = xz/(scale*4.7) + 0.37;
  vec4 a = mix(texA(uv1, layer), texA(uv2, layer), 0.35);
  vec4 n = mix(texN(uv1, layer), texN(uv2, layer), 0.35);
  nTS = vec3(n.xy*2.0-1.0, 1.0);
  return vec4(a.rgb*a.rgb, a.a);  // texture stored gamma-ish, linearise
}

struct Mat { vec3 alb; float rough; float metal; vec3 nrm; vec3 emit; };

// Aircraft mesh builds do not use terrain materials; omit these helpers before driver compilation.
#ifndef AF_MESH
// Undo the ground sample's UV rotation before blending tangent normals. The height/AO channels stay untouched.
// The matrix is the exact transpose of the coordinate transform below (no extra texture lookups).
vec2 groundRotatedNormal(vec2 detail){
  return vec2(0.8253*detail.x + 0.5646*detail.y, -0.5646*detail.x + 0.8253*detail.y);
}

// Anti-tiled ground sample: two decorrelated (offset + rotated) lookups mixed by low-frequency noise with a
// height-aware seam, plus a macro-scale layer and the texture's ambient occlusion. h returns the surface height.
vec4 groundSample(vec2 xz, int layer, float scale, out vec3 nTS, out float h){
  vec2 uv1 = xz/scale;
  vec2 uv1b = vec2(0.8253*xz.x - 0.5646*xz.y, 0.5646*xz.x + 0.8253*xz.y)/(scale*1.07) + vec2(0.43, 0.71);
  float k = vnoise(xz/(scale*3.3));
  vec4 a1 = texA(uv1, layer), a1b = texA(uv1b, layer);
  vec4 n1 = texN(uv1, layer), n1b = texN(uv1b, layer);
  n1b.xy = groundRotatedNormal(n1b.xy*2.0 - 1.0)*0.5 + 0.5;
  float hb = clamp((k - 0.5)*5.0 + (n1b.z - n1.z)*2.0 + 0.5, 0.0, 1.0);
  vec4 a = mix(a1, a1b, hb), n = mix(n1, n1b, hb);
  vec2 uv2 = xz/(scale*5.3) + 0.37;
  a = mix(a, texA(uv2, layer), 0.3); n = mix(n, texN(uv2, layer), 0.3);
  nTS = vec3(n.xy*2.0 - 1.0, 1.0);
  h = n.z;
  return vec4(a.rgb*a.rgb*mix(0.7, 1.0, n.w), a.a);
}
// Height-based layer blend: inside the transition zone the higher surface wins (sand settles in grass gaps,
// snow fills hollows first, rock pokes through) instead of a flat cross-fade.
float hblend(float w, float hBase, float hLayer){
  w = clamp(w, 0.0, 1.0);
  return smoothstep(0.0, 1.0, clamp(w + (hLayer - hBase)*w*(1.0 - w)*3.2, 0.0, 1.0));
}

#endif // !AF_MESH

// triplanar PBR sample (world or local coordinates)
vec4 triSample(vec3 p, vec3 n, int layer, float scale, out vec3 nTS){
  vec3 bw = pow(abs(n), vec3(4.0)); bw /= dot(bw, vec3(1.0));
  // (a projection that adds under 2% is left out and the rest share its weight: on a flat face - most of a cockpit, most
  // of a fuselage's flanks - one projection of the three is read, 4 texture reads instead of 12. The weight fades to
  // nothing at the cut, so where a 2x2 pixel quad splits across it the skipped read's level of detail doesn't matter)
  bw = max(bw - 0.02, 0.0); bw /= dot(bw, vec3(1.0));
  vec4 r = vec4(0.0); vec3 nn; nTS = vec3(0.0);
  if (bw.x > 0.0) { r += matSample(p.zy, layer, scale, nn)*bw.x; nTS += nn*bw.x; }
  if (bw.y > 0.0) { r += matSample(p.xz, layer, scale, nn)*bw.y; nTS += nn*bw.y; }
  if (bw.z > 0.0) { r += matSample(p.xy, layer, scale, nn)*bw.z; nTS += nn*bw.z; }
  return r;
}

vec3 applyTS(vec3 n, vec3 nTS, float strength){
  vec3 t = normalize(cross(n, abs(n.z) < 0.9 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0)));
  vec3 b = cross(t, n);
  return normalize(n + (t*nTS.x + b*nTS.y)*strength);
}

#ifndef AF_MESH
// Environment-only triplanar relief. Each projection's XY detail lives in a different world plane; blend there,
// then express it in applyTS's terrain frame. A flat map gives exactly zero relief on every axis and slope.
// UVs are not mirrored on negative faces, so their world-space gradients keep the same sign there too.
vec3 terrainTriNormal(vec3 n, vec3 nx, vec3 ny, vec3 nz, vec3 weight){
  vec3 delta = vec3(ny.x*weight.y + nz.x*weight.z,
                    nx.y*weight.x + nz.y*weight.z,
                    nx.x*weight.x + ny.y*weight.y);
  vec3 tangent = normalize(cross(n, abs(n.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
  vec3 bitangent = cross(tangent, n);
  return vec3(dot(delta, tangent), dot(delta, bitangent), 1.0);
}
vec4 terrainTriSample(vec3 p, vec3 n, int layer, float scale, out vec3 nTS){
  vec3 weight = pow(abs(n), vec3(4.0)); weight /= dot(weight, vec3(1.0));
  // Preserve the release's negligible-projection skip while reorienting each surviving normal.
  weight = max(weight - 0.02, 0.0); weight /= dot(weight, vec3(1.0));
  vec3 nx = vec3(0.0), ny = vec3(0.0), nz = vec3(0.0);
  vec4 r = vec4(0.0);
  if (weight.x > 0.0) r += matSample(p.zy, layer, scale, nx)*weight.x;
  if (weight.y > 0.0) r += matSample(p.xz, layer, scale, ny)*weight.y;
  if (weight.z > 0.0) r += matSample(p.xy, layer, scale, nz)*weight.z;
  nTS = terrainTriNormal(n, nx, ny, nz, weight);
  return r;
}
// Keep close relief and the far endpoint, but avoid a lighting ring at the former hard 2 km cutoff.
float terrainBumpStrength(float distanceToEye){
  return mix(0.6, 0.25, smoothstep(1600.0, 2400.0, distanceToEye));
}

#endif // !AF_MESH
