//! kMaterialCommon
//! The material record and the texture-array sampling: planar, anti-tiled ground, triplanar; tangent-space detail normals.
// triplanar/planar texture sampling with anti-tiling (two scales)
vec4 texA(vec2 uv, int layer){ return texture(uAlb, vec3(uv, float(layer))); }
vec4 texN(vec2 uv, int layer){ return texture(uNrm, vec3(uv, float(layer))); }
vec4 matSample(vec2 xz, int layer, float scale, out vec3 nTS){
  vec2 uv1 = xz/scale, uv2 = xz/(scale*4.7) + 0.37;
  vec4 a = mix(texA(uv1, layer), texA(uv2, layer), 0.35);
  vec4 n = mix(texN(uv1, layer), texN(uv2, layer), 0.35);
  nTS = vec3(n.xy*2.0-1.0, 1.0);
  return vec4(a.rgb*a.rgb, a.a);  // texture stored gamma-ish, linearise
}

struct Mat { vec3 alb; float rough; float metal; vec3 nrm; vec3 emit; };

// Anti-tiled ground sample: two decorrelated (offset + rotated) lookups mixed by low-frequency noise with a
// height-aware seam, plus a macro-scale layer and the texture's ambient occlusion. h returns the surface height.
vec4 groundSample(vec2 xz, int layer, float scale, out vec3 nTS, out float h){
  vec2 uv1 = xz/scale;
  vec2 uv1b = vec2(0.8253*xz.x - 0.5646*xz.y, 0.5646*xz.x + 0.8253*xz.y)/(scale*1.07) + vec2(0.43, 0.71);
  float k = vnoise(xz/(scale*3.3));
  vec4 a1 = texA(uv1, layer), a1b = texA(uv1b, layer);
  vec4 n1 = texN(uv1, layer), n1b = texN(uv1b, layer);
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

// triplanar PBR sample (world or local coordinates)
vec4 triSample(vec3 p, vec3 n, int layer, float scale, out vec3 nTS){
  vec3 bw = pow(abs(n), vec3(4.0)); bw /= dot(bw, vec3(1.0));
  vec3 n1, n2, n3;
  vec4 r = matSample(p.zy, layer, scale, n1)*bw.x + matSample(p.xz, layer, scale, n2)*bw.y + matSample(p.xy, layer, scale, n3)*bw.z;
  nTS = n1*bw.x + n2*bw.y + n3*bw.z;
  return r;
}

vec3 applyTS(vec3 n, vec3 nTS, float strength){
  vec3 t = normalize(cross(n, abs(n.z) < 0.9 ? vec3(0.0,0.0,1.0) : vec3(1.0,0.0,0.0)));
  vec3 b = cross(t, n);
  return normalize(n + (t*nTS.x + b*nTS.y)*strength);
}
