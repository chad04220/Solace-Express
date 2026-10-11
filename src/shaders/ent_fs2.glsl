//! kEntFS2

layout(location=0) out vec4 oG0; layout(location=1) out vec4 oG1; layout(location=2) out vec4 oG2; layout(location=3) out vec4 oG3;
// triplanar sample in object space: linear albedo, roughness; bumped object-space normal
// Derivatives are captured before any dissolve/cut-out. Explicit gradients remain valid inside
// material/projection branches and keep high-frequency grain mip-filtered at oblique views.
vec3 entDx, entDy;
vec2 entUvDx, entUvDy;
float entNormalWidth;
// Environment-only GB3 flags; mirrored in gbuffer.glsl, never set for parked aircraft.
const int ENTITY_GLASS = 16, ENTITY_CLEARCOAT = 32;
float entityDetailFade(float footprint, float distance){
  return (1.0 - smoothstep(0.025, 0.11, footprint))*(1.0 - smoothstep(90.0, 180.0, distance));
}
float entityLine(float d, float halfWidth, float footprint){
  float w = max(footprint, 0.00001);
  return clamp((min(d + w*0.5, halfWidth) - max(d - w*0.5, -halfWidth))/w, 0.0, 1.0);
}
// a dash at the start of every period (v in periods): on for the duty's share of it, anti-aliased over the footprint f
float entityDash(float v, float duty, float f){
  float t = fract(v);
  return max(entityLine(t - duty*0.5, duty*0.5, f), entityLine(t - 1.0 - duty*0.5, duty*0.5, f));
}
vec3 entityProjectionWeights(vec3 n){
  vec3 w = pow(abs(n), vec3(4.0)); w /= dot(w, vec3(1.0));
  w = max(w - 0.02, 0.0); return w/dot(w, vec3(1.0));
}
vec3 entityDetailNormal(vec3 n, vec3 delta, float bump){
  // The UV projections are not mirrored on negative faces. Project their world gradient
  // into the tangent plane instead of flipping its sign or changing normal length.
  return normalize(n + (delta - n*dot(n, delta))*bump);
}
vec4 environmentAlbGrad(vec2 uv, int layer, vec2 dx, vec2 dy){
  int env = layer == M_GRASS ? 0 : layer == M_ASPHALT ? 1 : layer == M_CONCRETE ? 2 : layer == M_BRICK ? 3
          : layer == M_PLASTER ? 4 : layer == M_TILES ? 5 : layer == M_BARK ? 6 : -1;
  if (uEnvMaterials != 0 && env >= 0) return textureGrad(uEnvAlb, vec3(uv, float(env)), dx, dy);
  return textureGrad(uAlb, vec3(uv, float(layer)), dx, dy);
}
vec4 environmentNrmGrad(vec2 uv, int layer, vec2 dx, vec2 dy){
  int env = layer == M_GRASS ? 0 : layer == M_ASPHALT ? 1 : layer == M_CONCRETE ? 2 : layer == M_BRICK ? 3
          : layer == M_PLASTER ? 4 : layer == M_TILES ? 5 : layer == M_BARK ? 6 : -1;
  if (uEnvMaterials != 0 && env >= 0) return textureGrad(uEnvNrm, vec3(uv, float(env)), dx, dy);
  return textureGrad(uNrm, vec3(uv, float(layer)), dx, dy);
}
vec3 triSGrad(vec3 p, vec3 n, vec3 dx, vec3 dy, int layer, float sc, float bump, inout vec3 nb, out float rough){
  vec3 w = entityProjectionWeights(n);
  vec4 a = vec4(0.0); vec3 delta = vec3(0.0); float ao = 0.0;
  if (w.x > 0.0) {
    vec4 ax = environmentAlbGrad(p.zy/sc, layer, dx.zy/sc, dy.zy/sc);
    vec4 nx = environmentNrmGrad(p.zy/sc, layer, dx.zy/sc, dy.zy/sc);
    a += ax*w.x; ao += nx.w*w.x; delta += vec3(0.0, nx.y*2.0 - 1.0, nx.x*2.0 - 1.0)*w.x;
  }
  if (w.y > 0.0) {
    vec4 ay = environmentAlbGrad(p.xz/sc, layer, dx.xz/sc, dy.xz/sc);
    vec4 ny = environmentNrmGrad(p.xz/sc, layer, dx.xz/sc, dy.xz/sc);
    a += ay*w.y; ao += ny.w*w.y; delta += vec3(ny.x*2.0 - 1.0, 0.0, ny.y*2.0 - 1.0)*w.y;
  }
  if (w.z > 0.0) {
    vec4 az = environmentAlbGrad(p.xy/sc, layer, dx.xy/sc, dy.xy/sc);
    vec4 nz = environmentNrmGrad(p.xy/sc, layer, dx.xy/sc, dy.xy/sc);
    a += az*w.z; ao += nz.w*w.z; delta += vec3(nz.x*2.0 - 1.0, nz.y*2.0 - 1.0, 0.0)*w.z;
  }
  nb = entityDetailNormal(nb, delta, bump);
  rough = a.a;
  return a.rgb*a.rgb*mix(0.75, 1.0, ao);
}
// Retain the parked aircraft's released material sampling exactly; scenery improvements
// do not change either parked aircraft or any flyable aircraft/cockpit shader.
vec3 triSAircraft(vec3 p, vec3 n, int layer, float sc, float bump, inout vec3 nb, out float rough){
  vec3 w = pow(abs(n), vec3(4.0)); w /= dot(w, vec3(1.0));
  vec4 ax = texture(uAlb, vec3(p.zy/sc, float(layer))), ay = texture(uAlb, vec3(p.xz/sc, float(layer))), az = texture(uAlb, vec3(p.xy/sc, float(layer)));
  vec4 nx = texture(uNrm, vec3(p.zy/sc, float(layer))), ny = texture(uNrm, vec3(p.xz/sc, float(layer))), nz = texture(uNrm, vec3(p.xy/sc, float(layer)));
  vec2 tx = nx.xy*2.0 - 1.0, ty = ny.xy*2.0 - 1.0, tz = nz.xy*2.0 - 1.0;
  nb = normalize(nb + (w.x*vec3(0.0, tx.y, tx.x)*sign(n.x) + w.y*vec3(ty.x, 0.0, ty.y)*sign(n.y) + w.z*vec3(tz.x, tz.y, 0.0)*sign(n.z))*bump);
  vec4 a = ax*w.x + ay*w.y + az*w.z;
  rough = a.a;
  return a.rgb*a.rgb*mix(0.75, 1.0, nx.w*w.x + ny.w*w.y + nz.w*w.z);
}
vec3 triS(vec3 p, vec3 n, int layer, float sc, float bump, inout vec3 nb, out float rough){
#if ENT_BUILDINGS
  if (uKind == K_GAPLANE || uKind == K_AIRLINER) return triSAircraft(p, n, layer, sc, bump, nb, rough);
#endif
  return triSGrad(p, n, entDx, entDy, layer, sc, bump, nb, rough);
}
// Keep position, derivative, projection weights and the accumulated normal in one frame.
vec3 triSFrame(vec3 p, vec3 n, int frame, int layer, float sc, float bump, inout vec3 nb, out float rough){
  if (frame == 0) return triS(p, n, layer, sc, bump, nb, rough);
  vec3 q = frame == 1 ? p.xzy : p.zyx;
#if ENT_BUILDINGS
  if (uKind == K_GAPLANE || uKind == K_AIRLINER) return triSAircraft(q, n, layer, sc, bump, nb, rough);
#endif
  vec3 nn = frame == 1 ? n.xzy : n.zyx, bn = frame == 1 ? nb.xzy : nb.zyx;
  vec3 dx = frame == 1 ? entDx.xzy : entDx.zyx, dy = frame == 1 ? entDy.xzy : entDy.zyx;
  vec3 a = triSGrad(q, nn, dx, dy, layer, sc, bump, bn, rough);
  nb = frame == 1 ? bn.xzy : bn.zyx;
  return a;
}
vec2 octEnc(vec3 n){ n /= abs(n.x) + abs(n.y) + abs(n.z); vec2 e = n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); return e; }
vec3 pal(float s, vec3 a, vec3 b, vec3 c, vec3 d){ float k = fract(s)*4.0; return k < 1.0 ? a : k < 2.0 ? b : k < 3.0 ? c : d; }
// procedural windows on a facade: returns 1 inside a pane, frame in .y; cell id in .zw
vec4 windowGrid(vec2 q, vec2 cell, vec2 pane, float y0){
  vec2 g = vec2(q.x/cell.x, (q.y - y0)/cell.y);
  vec2 f = fract(g) - 0.5, id = floor(g);
  vec2 hs = pane/cell*0.5;
  float inside = step(abs(f.x), hs.x)*step(abs(f.y), hs.y)*step(0.0, g.y);
  float fr = step(abs(f.x), hs.x + 0.06)*step(abs(f.y), hs.y + 0.06)*step(0.0, g.y)*(1.0 - inside);
  return vec4(inside, fr, id);
}
#if ENT_BUILDINGS
// Opening layout in nominal mesh metres, matching buildDetailedBuilding. Scaling an
// instance scales its openings too; it must never add windows or move them during LOD fades.
vec4 entityFacadeGrid(vec2 p, vec2 first, vec2 pitch, vec2 count, vec2 size){
  vec2 cell = clamp(floor((p - first)/pitch + 0.5), vec2(0.0), count - 1.0);
  return vec4(first + cell*pitch, size);
}
vec4 entityFacadePane(vec3 p, vec3 n, int kind){
  if (abs(n.y) > 0.5) return vec4(0.0);
  bool side = abs(n.x) > 0.5, front = !side && n.z > 0.0;
  vec2 q = vec2(side ? p.z : p.x, p.y);
  if (kind == K_HOUSE) {
    if (side) return entityFacadeGrid(q, vec2(-3.7,1.895), vec2(2.5,2.6), vec2(4,2), vec2(1.25,1.35));
    if (front && q.y < 3.195) return q.x < -0.85 ? vec4(-3.45,1.895,1.15,1.35) : vec4(1.75,1.895,1.9,1.35);
    return entityFacadeGrid(q, vec2(-2.9,1.895), vec2(2.9,2.6), vec2(3,2), vec2(1.35,1.35));
  }
  if (kind == K_HIP) {
    if (front) return q.x < 0.425 ? vec4(-2.7,1.825,2.35,1.35) : vec4(3.55,1.825,1.15,1.35);
    return entityFacadeGrid(q, vec2(-3.2,1.825), vec2(3.2,1.0), vec2(3,1), vec2(1.5,1.35));
  }
  if (kind == K_LHOUSE) {
    if (front && p.z < 1.0) return entityFacadeGrid(q, vec2(1.35,4.2), vec2(2.9,1.0), vec2(2,1), vec2(1.35,1.3));
    if (front) return entityFacadeGrid(q, vec2(-4.65,1.77), vec2(2.7,2.43), vec2(2,2), vec2(1.35,1.3));
    if (!side) return entityFacadeGrid(q, vec2(-4.45,1.77), vec2(2.95,2.43), vec2(4,2), vec2(1.35,1.3));
    return p.z < 0.5 ? entityFacadeGrid(q, vec2(-4.1,1.77), vec2(2.7,2.43), vec2(2,2), vec2(1.35,1.3))
                     : entityFacadeGrid(q, vec2(1.95,1.77), vec2(2.65,2.43), vec2(2,2), vec2(1.35,1.3));
  }
  if (kind == K_FARM) {
    if (side) return entityFacadeGrid(q, vec2(-2.65,1.85), vec2(5.3,2.9), vec2(2,2), vec2(1.2,1.6));
    if (front && q.y < 3.3) return vec4(q.x < 0.0 ? -3.2 : 3.2,1.85,1.35,1.6);
    return entityFacadeGrid(q, vec2(-3.3,1.85), vec2(3.3,2.9), vec2(3,2), vec2(1.15,1.6));
  }
  if (kind == K_TOWNHOUSE) {
    float y = q.y < 3.425 ? 2.0 : q.y < 6.3 ? 4.85 : 7.75;
    if (side) return entityFacadeGrid(q, vec2(-3.6,y), vec2(3.6,1.0), vec2(3,1), vec2(1.2,1.7));
    float x0 = -9.0 + clamp(floor((q.x + 9.0)/6.0),0.0,2.0)*6.0;
    if (front && y < 3.0) return vec4(x0 + 4.35,y,1.3,1.7);
    return entityFacadeGrid(q, vec2(x0 + 1.6,y), vec2(2.8,1.0), vec2(2,1), vec2(1.2,1.7));
  }
  if (kind == K_SHOP) {
    if (!front) return vec4(0.0);
    float x = abs(q.x) < 3.325 ? 2.1 : 4.55;
    return vec4(q.x < 0.0 ? -x : x,1.85,2.15,2.4);
  }
  if (kind == K_APART) {
    if (front) {
      float row = clamp(floor((q.y - 2.0)/3.4 + 0.5),0.0,5.0);
      float cy = row < 0.5 ? 2.225 : 1.725 + row*3.4;
      vec4 a = entityFacadeGrid(q, vec2(-5.8,cy), vec2(5.6,1.0), vec2(3,1), vec2(2.65,row < 0.5 ? 1.65 : 2.35));
      if (row < 0.5 && abs(a.x + 0.2) < 0.1) return vec4(0.0);
      return a;
    }
    return entityFacadeGrid(q, vec2(side ? -4.7 : -7.0,2.225), vec2(side ? 3.1 : 2.8,3.4), vec2(side ? 4 : 6,6), vec2(1.5,1.65));
  }
  if (kind == K_TOWER) {
    float base = p.y < 31.0 ? 0.3 : p.y < 57.0 ? 31.0 : 57.0;
    float count = p.y < 31.0 ? 8.0 : p.y < 57.0 ? 6.0 : 4.0;
    vec4 a = entityFacadeGrid(q, vec2(-(count - 1.0)*1.13,base + 2.05), vec2(2.26,3.6), vec2(count,count), vec2(1.5,2.1));
    if (front && base < 1.0 && a.y < 3.0 && abs(a.x) < 1.2) return vec4(0.0);
    return a;
  }
  if (kind == K_WAREHOUSE) {
    if (front) return vec4(0.0);
    return entityFacadeGrid(q, vec2(side ? -6.0 : -9.0,5.4), vec2(3.0,1.0), vec2(side ? 5 : 7,1), vec2(2.35,0.7));
  }
  if (kind == K_BARN) {
    if (side) return entityFacadeGrid(q, vec2(-6.3,2.825), vec2(4.2,1.0), vec2(4,1), vec2(1.5,1.35));
    return front ? vec4(q.x < 0.0 ? -4.4 : 4.4,2.975,1.25,1.25) : vec4(0.0);
  }
  if (kind == K_CHURCH) {
    if ((side && abs(p.x) < 3.0) || (!side && p.z > 4.9 && abs(p.x) < 2.7)) return vec4(0.0); // bell tower has louvers, not windows
    if (side) return entityFacadeGrid(q, vec2(-10.6,3.725), vec2(4.4,1.0), vec2(4,1), vec2(1.25,3.35));
    return front ? vec4(q.x < 0.0 ? -3.7 : 3.7,3.725,1.15,3.35) : vec4(0.0,3.725,1.7,3.35);
  }
  if (kind == K_GAS) return front ? vec4(q.x < -1.2 ? -2.6 : 0.2,1.675,2.6,2.15) : vec4(0.0);
  if (kind == K_LIGHTHOUSE && p.x < -3.4) {
    if (!side && !front) return vec4(-4.7,1.9,1.1,1.4);
    if (side) return entityFacadeGrid(q, vec2(-2.2,1.9), vec2(4.4,1.0), vec2(2,1), vec2(1.1,1.4));
  }
  if (kind == K_LIGHTHOUSE && p.y > 1.5 && p.y < 22.0 && p.x > -3.4) {
    float y = clamp(floor((p.y - 6.55)/6.0 + 0.5),0.0,2.0)*6.0 + 6.55;
    float radius = 3.0 - (y - 1.5)*0.9/20.5;
    float center = (side ? -sign(n.x) : sign(n.z))*0.09801714*radius;
    return vec4(center,y,0.19603428*radius,1.1);
  }
  return vec4(0.0);
}

// One analytic box intersection, only on resolved panes. A real reveal surrounds the
// high-detail pane; the ray reaches a back wall, side wall, ceiling or floor behind it.
// No ray-march, textures or derivatives in this divergent branch.
vec3 entityRoomFaceDistances(vec2 origin, vec3 ray, float depth){
  return vec3((0.5 - sign(ray.x)*origin.x)/max(abs(ray.x), 0.0001),
              (0.5 - sign(ray.y)*origin.y)/max(abs(ray.y), 0.0001),
              depth/max(abs(ray.z), 0.0001));
}
vec3 entityRoom(vec2 uv, vec3 view, float seed, float detail, out float lamp){
  vec3 average = vec3(0.10, 0.086, 0.069);
  lamp = 0.45;
  if (detail <= 0.0) return average;
  vec3 origin = vec3(clamp(uv, vec2(0.001), vec2(0.999)) - 0.5, 0.0);
  vec3 ray = vec3(-view.xy, -max(abs(view.z), 0.08));
  vec3 face = entityRoomFaceDistances(origin.xy, ray, 0.7 + 0.9*fract(seed*7.13));
  float t = min(face.x, min(face.y, face.z));
  vec3 hit = origin + ray*t;
  bool side = face.x < min(face.y, face.z), horizontal = face.y < min(face.x, face.z);
  vec3 room = mix(vec3(0.28, 0.24, 0.185), vec3(0.18, 0.21, 0.195), fract(seed*3.17));
  room *= side ? 0.5 : horizontal ? (ray.y > 0.0 ? 0.8 : 0.32) : 0.65;
  // Curtain edges and a low furnishing silhouette sit on the room, not on the glass.
  float curtain = smoothstep(0.24, 0.32, abs(hit.x))*(1.0 - smoothstep(0.4, 0.48, hit.y));
  float furniture = (1.0 - smoothstep(0.27, 0.33, abs(hit.x + (seed - 0.5)*0.25)))
                  *(1.0 - smoothstep(-0.22, -0.16, hit.y));
  if (!side && !horizontal) {
    room = mix(room, vec3(0.42, 0.36, 0.26), curtain*0.65);
    room *= 1.0 - 0.65*furniture;
  }
  float reveal = smoothstep(0.0, 0.09, min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)));
  room *= 0.55 + 0.45*reveal;
  lamp = mix(0.45, (0.35 + 0.65*reveal)*(horizontal ? 0.35 : 1.0), detail);
  return mix(average, room, detail);
}
vec3 entityPaneView(vec3 localView, vec3 normal){
  // Mesh pane U runs +z on X-facing facades and +x on Z-facing facades.
  return vec3(abs(normal.x) > 0.5 ? localView.z : localView.x, localView.y, abs(dot(localView, normal)));
}
#endif
void main(){
  entDx = dFdx(vL); entDy = dFdy(vL);
  entUvDx = dFdx(vAux.zw); entUvDy = dFdy(vAux.zw);
  entNormalWidth = max(length(dFdx(vLN)), length(dFdy(vLN)));
  // dissolving in or out (vFade), or cross-fading between detail levels (vLodK): a screen-door the anti-aliasing resolves
  // to a fade, its pattern turned each frame. The two levels keep complementary parts of the same pattern, so every pixel
  // is one or the other; the thinning's dissolve uses a pattern of its own on top
  if (vFade < 1.0 || vLodK.x > 0.0 || vLodK.y < 1.0) {
    vec2 q = gl_FragCoord.xy + 5.588238*mod(floor(uTime*60.0), 64.0);
    float n = fract(52.9829189*fract(dot(q, vec2(0.06711056, 0.00583715))));
    if (n < vLodK.x || n >= vLodK.y) discard;
    if (vFade < 1.0 && fract(52.9829189*fract(dot(q + vec2(23.0, 41.0), vec2(0.06711056, 0.00583715)))) >= vFade) discard;
  }
  vec3 V = normalize(uCam - vW);
  float yaw = vInst.y, cy = cos(yaw), sy = sin(yaw);
  vec3 n0 = normalize(vLN);
  vec3 wn0 = vec3(cy*n0.x + sy*n0.z, n0.y, -sy*n0.x + cy*n0.z);
  // two-sided: shade the side facing the camera (leaf cards keep their crown-wide normal so the crown stays round)
  if (dot(wn0, V) < 0.0 && (int(vAux.x + 0.5) != P_LEAFCARD || uLod == 3)) { n0 = -n0; wn0 = -wn0; }
  float dist = length(uCam - vW);
  // Retain the near silhouette, then smoothly retire subpixel edge perforation instead of
  // switching every crown in the 700 m band on one frame.
  float edgeDetail = 1.0 - smoothstep(500.0, 700.0, dist);
  if (leafCut(edgeDetail > 0.0 ? pow(1.0 - abs(dot(wn0, V)), 1.5)*edgeDetail : 0.0)) discard;
  int part = int(vAux.x + 0.5);
  // Old storefront meshes place one broad glass sheet in front of their wall. The
  // matched nominal facade shader now draws those panes on that wall instead.
  // Keep the genuinely fitted close panes; discard only the obsolete coarse overlay.
#if ENT_BUILDINGS
  if (uLod != 3 && part == P_GLASS && vAux.z < 1.0 && (uKind == K_SHOP || uKind == K_GAS)) discard;
#endif
  float seed = vInst.x, ao = vAux.y;
  vec3 alb = vec3(0.5); float rough = 0.8, metal = 0.0, cls = 2.0; vec3 emit = vec3(0.0);
  int surfaceFlags = 0;
  vec3 localView = vec3(cy*V.x - sy*V.z, V.y, sy*V.x + cy*V.z);
  vec3 nb = n0; float r0;
  vec3 lp = vL;
  float wy = vW.y;
#if ENT_TREES
  if (part == P_LEAFCARD) {
    cls = 1.0;
    float h = hsh(floor(vec2(fract(vAux.z), vAux.w)*5.0) + floor(vAux.z)*7.0 + vInst.x*13.0);
    vec3 tint = uKind == K_OAK ? vec3(0.07, 0.12, 0.035) : uKind == K_BIRCH ? vec3(0.11, 0.17, 0.045) : uKind == K_PINE ? vec3(0.045, 0.085, 0.06)
              : uKind == K_SPRUCE ? vec3(0.038, 0.072, 0.066) : uKind == K_FIR ? vec3(0.042, 0.08, 0.058) : vec3(0.075, 0.12, 0.04);
    alb = tint*mix(0.75, 1.3, h)*mix(0.82, 1.12, fract(seed*5.3));
    if (uKind >= K_OAK) alb = mix(alb, alb*vec3(1.6, 1.05, 0.55), smoothstep(0.8, 1.0, fract(seed*13.7))*0.8);   // broadleaf trees turning (never the conifers)
    alb *= mix(0.6, 1.0, ao);
    rough = max(0.57, min(0.76, entNormalWidth*0.55));
    nb = n0;
  } else if (part == P_LEAF || part == P_NEEDLE || part == P_FROND) {
    cls = 1.0;
    if (part == P_NEEDLE) {
      alb = triS(lp, n0, M_NEEDLES, 0.9, 0.5, nb, rough);
      // Backing envelopes need fine scan detail, not large inflated noise normals.
      // Only one bounded shade-noise evaluation remains, retired when subpixel.
      float tuft = (1.0 - smoothstep(250.0, 400.0, dist))*(1.0 - smoothstep(0.12, 0.5, max(length(entDx), length(entDy))));
      if (tuft > 0.0) {
        alb *= mix(1.0, mix(0.9, 1.08, vn3(lp*6.1 - vInst.x*3.0)), tuft);
      }
      vec3 tint = (uKind == K_SPRUCE ? vec3(0.5, 0.7, 0.68) : uKind == K_PINE ? vec3(0.5, 0.68, 0.56) : vec3(0.55, 0.76, 0.62))*mix(0.85, 1.1, vAux.z);
      alb *= tint*mix(0.85, 1.15, fract(seed*7.31));
      if (uSnow > 0.05 || wy > 1500.0) alb = mix(alb, vec3(0.85, 0.88, 0.92), smoothstep(0.35, 0.8, n0.y)*max(uSnow, smoothstep(1500.0, 1900.0, wy))*0.85);
    } else if (part == P_FROND) {
      alb = triS(lp, n0, M_LEAVES, 1.2, 0.5, nb, rough)*vec3(0.85, 1.05, 0.55);
      alb = mix(alb, vec3(0.18, 0.135, 0.045), smoothstep(0.75, 1.0, abs(vAux.w))*0.35 + step(abs(vAux.z), 0.07)*0.22);
    } else {
      // Backing envelopes support the middle/far LOD; actual close leaves are cards.
      // Strong low-frequency normal noise made these look like inflated cushions.
      alb = triS(lp, n0, M_LEAVES, 0.75, 0.45, nb, rough);
      float leafDetail = (1.0 - smoothstep(250.0, 400.0, dist))*(1.0 - smoothstep(0.16, 0.65, max(length(entDx), length(entDy))));
      float sprayShade = 0.99;
      if (leafDetail > 0.0) {
        sprayShade = mix(sprayShade, mix(0.9, 1.08, vn3(lp*5.3 - vInst.x*3.0)), leafDetail);
      }
      vec3 tint = uKind == K_OAK ? vec3(0.5, 0.68, 0.34) : uKind == K_BIRCH ? vec3(0.7, 0.86, 0.38) : vec3(0.48, 0.62, 0.32);
      float hue = fract(seed*13.7);
      tint = mix(tint, tint*vec3(1.2, 0.92, 0.6), smoothstep(0.8, 1.0, hue));   // a few trees turning
      tint *= mix(0.78, 1.12, fract(seed*5.3))*mix(0.85, 1.12, vAux.z);       // tree and clump variation
      alb *= tint*sprayShade;
      if (uSnow > 0.05) alb = mix(alb, vec3(0.8), smoothstep(0.5, 0.9, n0.y)*uSnow*0.6);
    }
    alb *= ao;
    rough = 0.72;
  } else if (part == P_BARK) {
    if (uKind == K_BIRCH) {
      alb = vec3(0.78, 0.76, 0.72)*(0.85 + 0.15*vn3(lp*vec3(8.0, 1.0, 8.0)));
      float mark = smoothstep(0.72, 0.8, vn3(vec3(lp.x*6.0, lp.y*9.0, lp.z*6.0)));
      alb = mix(alb, vec3(0.06), mark);
      rough = 0.7;
    } else if (uKind == K_PALM) {
      alb = triS(lp, n0, M_BARK, 1.0, 0.8, nb, rough)*vec3(0.95, 0.85, 0.7);
      alb *= 0.75 + 0.25*smoothstep(0.2, 0.5, fract(lp.y*2.6));
    } else {
      float barkScale = uEnvMaterials != 0 ? 1.0 : 0.9;
      alb = triS(lp, n0, M_BARK, barkScale, 0.65, nb, rough)*(uKind == K_PINE ? vec3(1.0, 0.88, 0.77) : vec3(0.92, 0.9, 0.86));
    }
    alb *= ao;
  } else
#endif
#if ENT_ROCKS
  if (part == P_ROCK) {
    float sc = uKind == K_SEASTACK || uKind == K_SPIRE ? 7.0 : uKind == K_OUTCROP ? 4.5 : 2.2;
    alb = triS(lp, n0, M_ROCK, sc, 1.3, nb, rough);
    vec3 tint = uKind == K_SPIRE ? vec3(0.98, 0.72, 0.55) : uKind == K_SEASTACK ? mix(vec3(0.72, 0.66, 0.58), vec3(0.6, 0.6, 0.62), fract(seed*3.1)) : mix(vec3(0.78, 0.76, 0.74), vec3(0.88, 0.84, 0.78), fract(seed*3.1));
    if (uKind == K_SPIRE || uKind == K_SEASTACK) tint *= 0.88 + 0.07*sin(lp.y*(uKind == K_SPIRE ? 2.4 : 1.3) + lp.x*0.21 - lp.z*0.11 + vn3(lp*0.4)*2.5)
      *(1.0 - smoothstep(0.4, 2.0, length(entDx) + length(entDy)));   // restrained tilted beds retain the scanned geology
    alb *= tint*ao;
    float top = smoothstep(0.55, 0.85, n0.y);
    if (wy < 1100.0 && uKind != K_SEASTACK) alb = mix(alb, alb*vec3(0.65, 0.91, 0.42), top*0.6*smoothstep(0.35, 0.7, vn3(lp*1.3 + seed*9.0)));   // thin moss retains the scanned rock beneath
    if (uKind == K_SEASTACK) { alb = mix(alb, vec3(0.92, 0.9, 0.85), top*smoothstep(20.0, 26.0, lp.y)*0.7); alb *= mix(0.55, 1.0, smoothstep(0.0, 2.5, wy)); }   // guano, wet base
    rough = max(rough, 0.68);
    float sn = max(uSnow, smoothstep(1400.0, 1800.0, wy));
    if (sn > 0.0) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.45, 0.8, nb.y)*sn);
  } else
#endif
#if ENT_BUILDINGS
  if (uKind == K_BRIDGE) {
    // ---------------------------------------------------------------- the road network's bridges (bridge_mesh.h)
    metal = 0.0;
    if (part >= P_DECK) {
      // the deck's road: its class's surface and markings, as the ground's roads paint theirs (terrain_material.glsl's
      // roads; vAux.z across the road, vAux.w the road's distance along, so the dashes run on from the ground's)
      int rc = part - P_DECK;
      float x = abs(vAux.z), along = vAux.w;
      float fx = max(abs(entUvDx.x) + abs(entUvDy.x), 1e-3), fa = max(abs(entUvDx.y) + abs(entUvDy.y), 1e-3);
      float paved = rc == 0 ? 12.4 : rc == 1 ? 4.6 : rc == 2 ? 2.9 : 1.9;   // (road_network.h roadSpec)
      vec3 nbC = n0; float roughC;
      vec3 kerbAlb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nbC, roughC)*vec3(0.84, 0.83, 0.8);
      if (rc == 3) alb = triS(lp, n0, M_GRAVEL, 5.0, 0.4, nb, rough)*0.9;
      else {
        alb = triS(lp, n0, M_ASPHALT, 4.0, 0.35, nb, rough)*(rc == 0 ? 0.72 : rc == 1 ? 0.82 : 0.95);
        if (rc == 2) alb *= 0.9 + 0.2*vn3(lp*vec3(1.0/7.0, 0.0, 1.0/7.0));   // (a lane worn paler, patched: as on the ground)
        float paint = 0.0, yellow = 0.0;
        if (rc == 0) {   // dual carriageway: the central reserve's barrier, the lane lines and the edges
          alb = mix(alb, kerbAlb*0.95, entityLine(x, 2.0, fx));
          paint = max(paint, entityLine(x - 2.35, 0.1, fx));
          paint = max(paint, entityLine(x - 5.95, 0.08, fx)*entityDash(along/12.0, 0.25, fa/12.0));
          paint = max(paint, entityLine(x - 9.55, 0.1, fx));
        } else if (rc == 1) {   // two-lane road: a dashed centre line, solid edge lines
          yellow = entityLine(x, 0.08, fx)*entityDash(along/12.0, 0.5, fa/12.0);
          paint = entityLine(x - 3.5, 0.08, fx);
        }
        alb = mix(alb, vec3(0.78), paint*0.9);
        alb = mix(alb, vec3(0.82, 0.68, 0.22), yellow*0.9);
      }
      float kerb = 1.0 - entityLine(x, paved, fx);   // beyond the paving: the kerb and the walkway
      alb = mix(alb, kerbAlb, kerb); rough = mix(rough, roughC, kerb); nb = normalize(mix(nb, nbC, kerb));
    } else if (part == P_METAL) {   // the lanes' steel rail, galvanised
      alb = triS(lp, n0, M_METAL, 2.0, 0.3, nb, rough)*vec3(0.74, 0.76, 0.77); metal = 0.6; rough = max(rough, 0.38);
    } else {
      // concrete: the parapets, the slab's edges and the girders, the piers and abutments - streaked by the rain, the
      // undersides darker, and over the sea a wet, darker band at the waterline
      alb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nb, rough)*vec3(0.85, 0.84, 0.81);
      alb *= 0.9 + 0.14*vn3(vec3(lp.x*0.8, lp.y*0.05, lp.z*0.8));
      if (n0.y < -0.5) alb *= 0.82;
      alb *= mix(0.5, 1.0, smoothstep(-0.3, 1.8, wy));
      rough = max(rough, 0.7);
    }
    if (uSnow > 0.05) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  } else if (uKind >= K_HANGAR) {
    // ---------------------------------------------------------------- airport buildings, aircraft, vehicles, furniture
    vec3 mp = lp/vScale;   // mesh coordinates (the instance scale removed)
    bool sideX = abs(n0.x) > 0.5;
    float u = sideX ? lp.z : lp.x;
    float s2 = fract(seed*13.31), s3 = fract(seed*3.77);
    vec3 livery = s3 < 0.5 ? pal(s3*2.0, vec3(0.7, 0.07, 0.06), vec3(0.06, 0.2, 0.55), vec3(0.05, 0.4, 0.2), vec3(0.9, 0.5, 0.05))
                           : pal(s3*2.0 - 1.0, vec3(0.04, 0.08, 0.25), vec3(0.0, 0.45, 0.5), vec3(0.45, 0.06, 0.25), vec3(0.15, 0.15, 0.17));
    if (part == P_WALL) {
      if (uKind == K_TERMINAL || uKind == K_CTRL || uKind == K_MAST) alb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nb, rough)*vec3(0.9, 0.89, 0.86);
      else if (uKind == K_FBO) alb = triS(lp, n0, M_PLASTER, 2.5, 0.6, nb, rough)*pal(s2, vec3(0.95, 0.93, 0.88), vec3(0.85, 0.88, 0.92), vec3(0.93, 0.86, 0.74), vec3(0.8, 0.82, 0.8));
      else if (uKind == K_JETBRIDGE) {
        alb = triSFrame(lp, n0, 1, M_CORRUGATED, 2.0, 0.3, nb, rough)*vec3(0.78, 0.8, 0.82); metal = 0.0;
        if (vAux.z < 0.5 && sideX && mp.y > 4.3 && mp.y < 5.3 && mp.z > -7.5 && mp.z < 8.0) { alb = vec3(0.05, 0.07, 0.09); rough = 0.1; cls = 3.0; nb = n0; emit = vec3(1.0, 0.95, 0.85)*uNight*0.8; }
      } else {   // hangars and sheds: profiled steel cladding with vertical ribs
        vec3 tint = pal(s2, vec3(0.82, 0.84, 0.86), vec3(0.6, 0.67, 0.74), vec3(0.84, 0.8, 0.68), vec3(0.6, 0.66, 0.6));
        if (uKind == K_ARCH) tint = vec3(0.74, 0.74, 0.7);
        alb = triSFrame(lp, n0, 1, M_CORRUGATED, 3.0, 0.45, nb, rough)*tint; metal = uKind == K_ARCH ? 0.65 : 0.0;
        if (vAux.z < 0.5 && uKind == K_HANGAR && sideX && mp.y > 7.0 && mp.y < 8.3 && fract(mp.z/4.0) < 0.7 && abs(mp.z) < 14.5) {   // clerestory
          alb = vec3(0.06, 0.08, 0.1); rough = 0.1; cls = 3.0; nb = n0; metal = 0.0; emit = vec3(1.0, 0.92, 0.75)*uNight*0.7; }
        alb *= mix(0.8, 1.0, smoothstep(0.0, 1.2, mp.y));   // splash dirt along the base
      }
      alb *= 1.0 - 0.3*uWet;
    } else if (part == P_ROOF) {
      int roofFrame = abs(n0.x) > abs(n0.z) ? 2 : 0;
      alb = triSFrame(lp, n0, roofFrame, M_CORRUGATED, 2.0, 0.8, nb, rough)*(uKind == K_THANGAR ? vec3(0.58, 0.62, 0.64) : uKind == K_ARCH ? vec3(0.7, 0.71, 0.7) : vec3(0.74, 0.76, 0.77));
      metal = uKind == K_ARCH ? 0.65 : 0.0; rough = max(rough, 0.42);
      alb *= 1.0 - 0.25*smoothstep(0.6, 0.9, vn3(lp*0.15 + seed))*0.6;   // weathering streaks
      if (uSnow > 0.05) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.3, 0.6, n0.y)*uSnow);
    } else if (part == P_DOOR) {
      alb = triSFrame(lp, n0, 1, M_CORRUGATED, 1.2, 0.6, nb, rough);
      float bay = uKind == K_THANGAR ? floor((mp.x + 24.0)/12.0) : 0.0;
      alb *= pal(fract(seed*5.1 + bay*0.37), vec3(0.88, 0.88, 0.86), vec3(0.22, 0.36, 0.58), vec3(0.52, 0.55, 0.58), vec3(0.72, 0.7, 0.62));
      if (uKind == K_HANGAR && abs(fract((mp.x + 19.6)/6.53) - 0.5) > 0.49) alb *= 0.45;   // sliding panel joints
      metal = 0.0; rough = max(rough, 0.42);
    } else if (part == P_GLASS) {
      cls = 3.0; rough = 0.06; metal = 0.1; nb = n0;
      alb = vec3(0.05, 0.08, 0.1);
      if (uKind == K_TERMINAL) {
        float mul = abs(fract(u/1.6) - 0.5), tr = abs(fract(mp.y/3.85) - 0.5);
        if (mul > 0.47 || tr > 0.48) { alb = vec3(0.32, 0.34, 0.36); rough = 0.35; metal = 0.6; cls = 2.0; }
        else { alb = vec3(0.06, 0.1, 0.12); emit = vec3(1.0, 0.9, 0.74)*uNight*(1.1 + 0.4*hsh(floor(vec2(u/1.6, mp.y/3.85))))*1.2; }
      } else if (uKind == K_CTRL) { alb = vec3(0.04, 0.09, 0.08); emit = vec3(0.5, 0.9, 0.7)*uNight*0.25; }
      else if (uKind == K_FBO || uKind == K_ARCH) emit = vec3(1.0, 0.88, 0.68)*uNight*0.6;
    } else if (part == P_METAL) {
      metal = 0.7; rough = 0.4;
      if (uKind == K_FUELTANK || uKind == K_PUMP) { alb = vec3(0.62, 0.62, 0.60); metal = 0.0; rough = 0.45;
        if (uKind == K_PUMP && abs(mp.y - 1.65) < 0.12) alb = vec3(0.75, 0.08, 0.06); }
      else if (uKind == K_CAR && mp.z > 2.0) {
        alb = vec3(0.20, 0.21, 0.20); metal = 0.88; rough = 0.23;   // recessed reflector behind modelled lens
      }
      else if (uKind == K_TRUCK) { alb = vec3(0.78); metal = 0.9; rough = 0.25; }
      else if (uKind == K_AIRLINER) { alb = vec3(0.72, 0.73, 0.75); metal = 0.35; rough = 0.35; }
      else alb = triS(lp, n0, M_METAL, 2.0, 0.3, nb, rough)*0.75;
    } else if (part == P_PAINT) {
      metal = 0.0; rough = 0.3;
      if (uKind == K_CAR) alb = s2 < 0.5 ? pal(s2*2.0, vec3(0.85), vec3(0.6, 0.62, 0.64), vec3(0.04), vec3(0.55, 0.06, 0.05)) : pal(s2*2.0 - 1.0, vec3(0.08, 0.16, 0.38), vec3(0.75, 0.75, 0.78), vec3(0.1, 0.22, 0.14), vec3(0.3, 0.32, 0.35));
      else if (uKind == K_TRUCK) alb = pal(s2, vec3(0.88), vec3(0.75, 0.1, 0.07), vec3(0.85, 0.65, 0.05), vec3(0.88));
      else if (uKind == K_GAPLANE) {
        alb = s2 < 0.75 ? vec3(0.88, 0.88, 0.86) : vec3(0.9, 0.85, 0.65);
        if (abs(n0.y) < 0.6 && mp.y > 1.22 && mp.y < 1.42 && mp.z < 3.0) alb = livery;   // fuselage stripe
        if (mp.y > 1.97 && abs(mp.x) > 4.6) alb = livery;                               // wing tips
      } else {   // airliner: white top, grey belly, cheat line, cabin windows, cockpit
        alb = vec3(0.9, 0.9, 0.9);
        if (mp.y < 2.0 && abs(mp.x) < 2.0) alb = vec3(0.6, 0.62, 0.66);
        if (mp.y > 2.75 && mp.y < 2.95 && abs(mp.x) < 2.1) alb = livery;
        if (abs(mp.x) > 4.0 && mp.y < 2.7) alb = livery*0.9 + 0.05;   // nacelles in the airline colour
        bool win = mp.y > 3.3 && mp.y < 3.68 && mp.z > -10.5 && mp.z < 12.5 && abs(fract(mp.z/0.53) - 0.5) < 0.22 && abs(mp.x) > 1.4;
        bool ck = mp.z > 16.2 && mp.z < 17.7 && mp.y > 3.15 && mp.y < 3.65 && abs(mp.x) > 0.25;
        if (uLod != 3 && (win || ck)) { alb = vec3(0.03, 0.04, 0.05); rough = 0.08; cls = 3.0; nb = n0; emit = win ? vec3(1.0, 0.9, 0.7)*uNight*0.5 : vec3(0.0); }
        if (uLod != 3 && abs(abs(mp.z - 11.6) - 0.5) < 0.04 && mp.y > 2.2 && mp.y < 4.3 && abs(mp.x) > 1.5) alb *= 0.5;   // hero mesh has fitted panes and physical door seams
      }
    } else if (part == P_STRIPE) { alb = uKind == K_CAR ? vec3(0.38, 0.008, 0.006) : livery; rough = uKind == K_CAR ? 0.16 : 0.3; }
    else if (part == P_SOCK) { float b = floor(clamp((mp.x - 0.15)/0.72, 0.0, 4.99)); alb = mod(b, 2.0) < 0.5 ? vec3(0.95, 0.3, 0.03) : vec3(0.92); rough = 0.85; }
    else if (part == P_BEACON) {   // aerodrome beacon: alternating white and green beams sweeping round
      float a = atan(lp.z, lp.x) - uTime*1.6;
      float w = pow(max(cos(a), 0.0), 12.0), g = pow(max(-cos(a), 0.0), 12.0);
      alb = vec3(0.15); rough = 0.05; cls = 3.0; nb = n0;
      emit = (vec3(1.0, 0.97, 0.9)*w + vec3(0.1, 1.0, 0.35)*g)*(0.6 + 14.0*uNight);
    }
    else if (part == P_FENCE) { alb = vec3(0.55, 0.57, 0.58); metal = 0.7; rough = 0.45; }
    else if (part == P_OBST) {   // obstruction marking: red and white bands, a red light on top at night
      float hb = uKind == K_WINDSOCK ? 1.25 : 3.0;
      alb = mod(floor(mp.y/hb), 2.0) < 0.5 ? vec3(0.95) : vec3(0.75, 0.1, 0.06); rough = 0.5;
      if (uKind != K_WINDSOCK && mp.y > (uKind == K_CTRL ? 33.5 : uKind == K_MAST ? 11.5 : 1e9)) emit = vec3(1.0, 0.08, 0.04)*uNight*(4.0 + 3.0*step(0.5, fract(uTime*0.8)));
    }
    else if (part == P_LAMP) { alb = vec3(0.9); rough = 0.1; cls = 3.0; nb = n0; emit = vec3(1.0, 0.93, 0.8)*(0.15 + 10.0*uNight); }
    else if (part == P_SIGN) { alb = uKind == K_PUMP ? vec3(0.75, 0.1, 0.06) : vec3(0.08, 0.22, 0.55); rough = 0.4; emit = alb*uNight*2.5; }
    else if (part == P_CANOPY) { alb = vec3(0.9); if (n0.y < -0.5) emit = vec3(1.0, 0.97, 0.9)*uNight*2.5; rough = 0.4; }
    else if(part>=27 && part<33) {
      // Finish follows the wheel's rest coordinates; its mesh/normal rotate together in both passes.
      vec2 q=mp.yz-vAux.zw;float r=max(vAux.y,.01),rad=length(q)/r;
      float wheelFoot = max(length(entDx.yz), length(entDy.yz))/r;
      alb=vec3(.014,.015,.016);rough=.86;
      if(abs(n0.x)>.38 && rad<.66) {
        // Spokes, rim bevel and hub are geometry now, not a six-spoke decal.
        alb=vec3(.5,.52,.55);metal=.9;rough=.24;
        if ((uKind == K_CAR && abs(mp.x) < 0.885) || (uKind == K_TRUCK && uLod == 3 && abs(mp.x) < 1.145)) {
          alb = vec3(0.075,0.078,0.082); metal = 0.75; rough = 0.5;
        }
      } else {
        float detail = 1.0 - smoothstep(0.015, 0.055, wheelFoot);
        if (detail > 0.0 && abs(n0.x) < 0.75) {
          float tread = sin(atan(q.y,q.x)*46.0 + abs(mp.x)*37.0);
          alb *= 1.0 - 0.12*detail*smoothstep(0.65, 0.95, tread);
        }
        alb *= 0.85 + 0.15*smoothstep(0.67, 0.92, rad);
      }
    }
    else if (part == P_DARK) { alb = uKind == K_GAPLANE || uKind == K_AIRLINER || uKind == K_CAR || uKind == K_TRUCK
      || uKind == K_PUMP || uKind == K_FUELTANK || uKind == K_JETBRIDGE ? vec3(0.03) : triS(lp, n0, M_GRAVEL, 2.0, 0.5, nb, rough)*0.4; rough = 0.8; }
    else if (part == P_TRIM && (uKind == K_CAR || uKind == K_TRUCK)) {
      alb = vec3(0.55,0.545,0.50); rough = 0.32; metal = 0.0; nb = n0;   // enamel registration plate
    }
    else { alb = triS(lp, n0, M_CONCRETE, 3.0, 0.5, nb, rough)*vec3(0.88, 0.86, 0.82); }   // P_TRIM: concrete
    if (uSnow > 0.05 && part != P_GLASS && part != P_ROOF) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  } else {
    // ---------------------------------------------------------------- buildings
    vec3 sc3 = vec3(1.0);
    bool sideX = abs(n0.x) > 0.5;
    float u = sideX ? lp.z : lp.x, v = lp.y;
    float s1 = fract(seed*7.13), s2 = fract(seed*13.31), s3 = fract(seed*3.77);
    if (part == P_WALL) {
      int layer = M_PLASTER; float tsc = 2.5; vec3 tint = vec3(1.0);
      if (uKind == K_HOUSE || uKind == K_HIP || uKind == K_LHOUSE || uKind == K_FARM) {
        if (s1 < 0.3) { layer = M_BRICK; tint = mix(vec3(1.0), vec3(0.85, 0.75, 0.7), s2); }
        else if (s1 < 0.55 || uKind == K_FARM) { layer = M_SIDING; tint = pal(s2, vec3(0.95, 0.95, 0.92), vec3(0.75, 0.85, 0.9), vec3(0.95, 0.88, 0.7), vec3(0.7, 0.8, 0.7)); tsc = 3.0; }
        else tint = pal(s2, vec3(0.97, 0.95, 0.9), vec3(0.98, 0.88, 0.7), vec3(0.92, 0.78, 0.66), vec3(0.88, 0.9, 0.86));
      } else if (uKind == K_TOWNHOUSE) {
        float unit = floor((lp.x/vScale.x + 9.0)/6.0);
        layer = fract(unit*0.37 + seed) < 0.5 ? M_BRICK : M_PLASTER;
        tint = pal(fract(unit*0.61 + seed), vec3(0.95, 0.85, 0.7), vec3(0.85, 0.6, 0.5), vec3(0.75, 0.82, 0.88), vec3(0.95, 0.93, 0.88));
      } else if (uKind == K_SHOP || uKind == K_GAS) { tint = pal(s2, vec3(0.9, 0.88, 0.84), vec3(0.85, 0.7, 0.55), vec3(0.7, 0.75, 0.8), vec3(0.95, 0.9, 0.8)); }
      else if (uKind == K_APART) { layer = s1 < 0.4 ? M_BRICK : M_CONCRETE; tint = s1 < 0.4 ? vec3(0.9, 0.8, 0.75) : pal(s2, vec3(0.92, 0.9, 0.85), vec3(0.85, 0.78, 0.7), vec3(0.8, 0.82, 0.85), vec3(0.95, 0.85, 0.75)); tsc = layer == M_CONCRETE ? 3.0 : 4.0; }
      else if (uKind == K_TOWER) { layer = M_CONCRETE; tint = pal(s2, vec3(0.85, 0.83, 0.8), vec3(0.7, 0.68, 0.66), vec3(0.9, 0.86, 0.78), vec3(0.6, 0.62, 0.66)); tsc = 3.0; }
      else if (uKind == K_WAREHOUSE) { layer = M_CORRUGATED; tint = pal(s2, vec3(0.75, 0.78, 0.8), vec3(0.55, 0.62, 0.72), vec3(0.85, 0.82, 0.72), vec3(0.6, 0.65, 0.6)); tsc = 4.0; }
      else if (uKind == K_CHURCH) { layer = M_CONCRETE; tint = vec3(0.86, 0.8, 0.7); tsc = 1.6; }
      else if (uKind == K_LIGHTHOUSE) { tint = fract((v - 1.5)/5.2) < 0.5 ? vec3(0.95) : vec3(0.75, 0.08, 0.06); }
      int wallFrame = layer == M_CORRUGATED ? 1 : 0;
      if (layer == M_BRICK) tsc = 1.4;
      if (uEnvMaterials != 0 && layer == M_PLASTER) tsc = 2.0;
      alb = triSFrame(lp, n0, wallFrame, layer, tsc, 0.7, nb, rough)*tint;
      if (uKind == K_CHURCH) alb *= 0.85 + 0.15*step(0.06, fract(v/0.55))*step(0.04, fract(u/1.1 + floor(v/0.55)*0.5));   // ashlar courses
      alb *= 1.0 - 0.3*uWet;
    } else if (part == P_ROOF) {
      int layer = M_TILES; vec3 tint = vec3(1.0); float tsc = 3.0;
      if (uKind == K_BARN || uKind == K_WAREHOUSE) { layer = M_CORRUGATED; tint = uKind == K_BARN ? pal(s2, vec3(0.55, 0.15, 0.1), vec3(0.4, 0.42, 0.45), vec3(0.6, 0.6, 0.62), vec3(0.3, 0.32, 0.3)) : vec3(0.8); metal = 0.5; }
      else if (uKind == K_CHURCH) { layer = M_SLATE; tint = lp.y > 18.0*vInst.z ? vec3(0.45, 0.75, 0.62) : vec3(0.8, 0.82, 0.88); metal = lp.y > 18.0*vInst.z ? 0.3 : 0.0; }
      else if (s3 < 0.4) { layer = M_TILES; tint = mix(vec3(1.0, 0.8, 0.7), vec3(0.75, 0.5, 0.42), s2); }
      else if (s3 < 0.65) { layer = M_SLATE; tint = vec3(0.75, 0.77, 0.84); }
      else { layer = M_SHINGLES; tint = pal(s2, vec3(0.45, 0.45, 0.48), vec3(0.5, 0.36, 0.3), vec3(0.32, 0.38, 0.32), vec3(0.6, 0.58, 0.55)); }
      int roofFrame = abs(n0.x) > abs(n0.z) ? 2 : 0;
      if (uEnvMaterials != 0 && layer == M_TILES) { tsc = 2.5; tint = mix(vec3(1.0), tint, 0.35); }
      alb = triSFrame(lp, n0, roofFrame, layer, tsc, 1.0, nb, rough)*tint;
      alb *= 1.0 - 0.25*uWet;
      if (uSnow > 0.05) alb = mix(alb, vec3(0.9, 0.92, 0.95), smoothstep(0.3, 0.6, n0.y)*uSnow);
    } else if (part == P_GLASS) {
      cls = 3.0; rough = 0.05; metal = 0.1;
      vec3 tint = pal(s2, vec3(0.05, 0.09, 0.12), vec3(0.06, 0.1, 0.09), vec3(0.08, 0.08, 0.1), vec3(0.1, 0.08, 0.06));
      alb = tint;
      if (uKind == K_OFFICE || uKind == K_SKY) {
        vec3 nominal = lp/vScale;
        float glassU = sideX ? nominal.z : nominal.x;
        float module = 1.55, start = 0.0, base = 0.3;
        if (uKind == K_SKY) {
          // The octagon's pressure caps follow each actual face tangent, not a
          // projected X/Z grid that changes spacing when the instance is scaled.
          glassU = dot(nominal,vec3(-n0.z,0.0,n0.x));
          module = 1.45; start = -2.323761; base = 6.9;
        }
        float fl = fract((nominal.y - base)/3.7);
        float mul = abs(fract((glassU - start)/module + 0.5) - 0.5)*module;
        bool spandrel = fl < 0.24, mullion = mul < 0.025;
        if (spandrel || mullion) { alb = spandrel ? tint*2.2 + vec3(0.05) : vec3(0.35); rough = 0.35; metal = 0.6; cls = 2.0; }
        else {
          // Stable floor occupancy and three-bay office zones replace isolated white checks.
          // Reuse the nominal curtain-wall grid at every LOD; time and camera never seed rooms.
          vec2 paneId = floor(vec2((glassU-start)/module, (nominal.y-base)/3.7));
          float faceId = sign(n0.x)*5.0 + sign(n0.z)*11.0;
          float floorState = hsh(vec2(paneId.y, seed*29.0 + 13.0));
          float zoneId = floor((paneId.x + floor(floorState*3.0))/3.0);
          float zoneState = hsh(vec2(zoneId,paneId.y) + seed*23.0 + faceId + vec2(19.0,47.0));
          float paneState = hsh(paneId + seed*17.0 + faceId);
          float lit = step(0.22,floorState)*step(mix(0.52,0.68,s2),zoneState)*step(0.10,paneState);
          vec3 roomTint = mix(vec3(1.0,0.78,0.52),vec3(0.76,0.86,1.0),fract(zoneState*11.73 + s2));
          roomTint /= dot(roomTint,vec3(0.2126,0.7152,0.0722));
          // Linear luminance stays within 0.05915..0.28 for a lit pane at full night.
          float roomLuminance = mix(0.065,0.28,fract(zoneState*7.13))*mix(0.9,1.0,paneState);
          emit = roomTint*(lit*uNight*roomLuminance);
        }
      } else if (uKind == K_SHOP) { emit = vec3(1.0, 0.85, 0.6)*uNight*1.6; alb = vec3(0.05, 0.06, 0.07); }
      else if (uKind == K_GAS) emit = vec3(1.0, 0.95, 0.85)*uNight*1.8;
      else if (uKind == K_APART) { alb = vec3(0.12, 0.16, 0.18); rough = 0.12; }
    } else if (part == P_METAL) {
      metal = 0.7;
      if (uKind == K_SILO) { alb = triSFrame(lp, n0, 1, M_CORRUGATED, 2.0, 0.6, nb, rough)*vec3(0.85); rough = 0.4; }
      else if (uKind == K_WATERTOWER) { alb = pal(s2, vec3(0.75, 0.85, 0.9), vec3(0.85), vec3(0.6, 0.75, 0.6), vec3(0.85, 0.8, 0.7))*0.85; rough = 0.45; metal = 0.3; }
      else if (uKind == K_LIGHTHOUSE) { alb = vec3(0.55, 0.08, 0.05); rough = 0.4; metal = 0.3; }
      else { alb = triS(lp, n0, M_METAL, 2.0, 0.3, nb, rough)*0.8; }
    } else if (part == P_DOOR) {
      alb = pal(s3, vec3(0.35, 0.2, 0.1), vec3(0.15, 0.25, 0.4), vec3(0.6, 0.12, 0.1), vec3(0.85)); rough = 0.5;
      if (uKind == K_WAREHOUSE || uKind == K_LHOUSE && abs(lp.x - 4.2*vScale.x) < 1.5) { alb = triSFrame(lp, n0, 1, M_CORRUGATED, 1.0, 0.5, nb, rough)*vec3(0.75, 0.75, 0.72); metal = 0.4; }
      if (uKind == K_BARN) { alb = vec3(0.5, 0.1, 0.07); float d = abs(abs(fract(u/4.4 + 0.5) - 0.5)*4.4 - abs(v - 2.3)*1.1); if (uLod != 3 && d < 0.18) alb = vec3(0.9); }
    } else if (part == P_BRICK) { alb = triS(lp, n0, M_BRICK, 1.4, 0.8, nb, rough)*vec3(0.85, 0.75, 0.7); }
    else if (part == P_TRIM) {
      alb = triS(lp, n0, M_CONCRETE, 3.0, 0.35, nb, rough)*vec3(0.82, 0.80, 0.75);
      if (vAux.z > 1.5) { alb *= 0.82 + 0.18*smoothstep(0.0, 0.6, lp.y/vScale.y); rough = max(rough,0.78); }
      else if (uKind <= K_TOWNHOUSE || uKind == K_BARN) { alb = vec3(0.65,0.645,0.62); rough = 0.46; nb = n0; }
    }
    else if (part == P_WOOD) {
      alb = triS(lp, n0, M_PLANKS, 2.0, 0.8, nb, rough);
      alb *= uKind == K_BARN ? pal(s2, vec3(0.75, 0.18, 0.12), vec3(0.62, 0.16, 0.1), vec3(0.55, 0.45, 0.35), vec3(0.7, 0.2, 0.12)) : vec3(0.75, 0.62, 0.5);
      if (uKind == K_BARN && abs(u) > (sideX ? 9.0*vScale.z : 6.0*vScale.x) - 0.35) alb = vec3(0.9);   // white corner boards
    }
    else if (part == P_AWNING) { alb = mix(pal(s2, vec3(0.7, 0.1, 0.1), vec3(0.1, 0.35, 0.2), vec3(0.15, 0.25, 0.55), vec3(0.8, 0.55, 0.1)), vec3(0.92), step(0.5, fract(lp.x/0.9))); rough = 0.85; }
    else if (part == P_DARK) {
      alb = triS(lp, n0, M_GRAVEL, 2.0, 0.5, nb, rough)*0.45;
      if (n0.y > 0.75 && (uKind == K_SHOP || uKind == K_APART || uKind == K_OFFICE || uKind == K_TOWNHOUSE)) {
        // Seeded flat-roof finishes: weathered bitumen or warm reflective membrane. Keep the existing grain and
        // normal sample; no new texture, pass, instance or sub-pixel pattern is needed to break up the roof field.
        vec3 finish = mix(vec3(0.055, 0.066, 0.078), vec3(0.27, 0.25, 0.21), step(0.58, s2));
        float grain = 0.82 + 0.35*clamp(dot(alb, vec3(0.2126, 0.7152, 0.0722))*5.0, 0.0, 1.0);
        alb = finish*grain*(1.0 - 0.22*uWet);
        rough = mix(0.9, 0.48, uWet);
      }
    }
    else if (part == P_LAMP) { alb = vec3(0.1); rough = 0.05; cls = 3.0; emit = vec3(1.0, 0.9, 0.6)*(0.4 + 9.0*uNight)*(0.6 + 0.4*step(0.0, sin(atan(lp.z, lp.x) - uTime*1.2))); }
    else if (part == P_RLAMP) {   // runway light globe: tinted glass, the lamp glowing through it when the lights are on
      int ci = int(seed);
      vec3 lc = ci == 1 ? vec3(1.0, 0.7, 0.25) : ci == 2 ? vec3(0.15, 1.0, 0.35) : ci == 3 ? vec3(1.0, 0.12, 0.08) : ci == 4 ? vec3(0.15, 0.3, 1.0) : vec3(1.0, 0.93, 0.78);
      alb = mix(vec3(0.6), lc, 0.5)*0.4; rough = 0.05; metal = 0.0; cls = 3.0;
      emit = lc*uRwyLights*(1.0 + 7.0*smoothstep(0.0, 0.03, lp.y - 0.33));
    }
    else if (part == P_PAPI) {   // PAPI lens: white seen from above the unit's threshold angle, red below; sharp transition
      vec3 toC = uCam - vW; float ang = degrees(atan(toC.y, length(toC.xz)));
      vec2 face = vec2(sin(vInst.y), cos(vInst.y));
      float front = smoothstep(0.0, 0.2, dot(normalize(toC.xz), face));
      vec3 lc = mix(vec3(1.0, 0.08, 0.05), vec3(1.0, 0.95, 0.88), smoothstep(-0.05, 0.05, ang - seed));
      alb = vec3(0.05); rough = 0.05; metal = 0.0; cls = 3.0;
      emit = lc*front*8.0;
    }
    else if (part == P_SIGN) { alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; emit = alb*uNight*2.5; }
    else if (part == P_CANOPY) { alb = vec3(0.92); if (n0.y < -0.5) emit = vec3(1.0, 0.98, 0.95)*uNight*3.0; if (abs(n0.y) < 0.5 && lp.y < 4.95) alb = pal(s1, vec3(0.8, 0.1, 0.08), vec3(0.1, 0.3, 0.7), vec3(0.95, 0.75, 0.1), vec3(0.1, 0.55, 0.3)); rough = 0.4; }
    if (uSnow > 0.05 && part != P_GLASS && part != P_ROOF) alb = mix(alb, vec3(0.9), smoothstep(0.6, 0.9, n0.y)*uSnow*0.8);
  }
#else
  {}
#endif
#if ENT_BUILDINGS
  // The simple far meshes use exactly the high mesh's nominal opening layout. This
  // preserves pane counts and placement across the complementary0/3 screen-door fade.
  if (uKind <= K_GAS && uLod != 3 && (part == P_WALL || (part == P_WOOD && uKind == K_BARN))) {
    if (vAux.z < 0.5) {
      vec3 nominal = lp/vScale;
      bool side = abs(n0.x) > 0.5;
      vec2 q = vec2(side ? nominal.z : nominal.x, nominal.y);
      vec2 scale = vec2(side ? vScale.z : vScale.x, vScale.y);
      vec2 footprint = (side ? abs(entDx.zy) + abs(entDy.zy) : abs(entDx.xy) + abs(entDy.xy))/scale;
      vec4 opening = entityFacadePane(nominal,n0,uKind);
      if (opening.z > 0.0) {
        vec2 size = max(opening.zw - 0.11, vec2(0.01));
        vec2 delta = q - opening.xy;
        float paneCoverage = entityLine(delta.x,size.x*0.5,footprint.x)*entityLine(delta.y,size.y*0.5,footprint.y);
        float frameCoverage = entityLine(delta.x,opening.z*0.5+0.045,footprint.x)*entityLine(delta.y,opening.w*0.5+0.045,footprint.y);
        bool sash = uKind <= K_TOWNHOUSE || (uKind == K_APART && opening.w < 2.0) || uKind == K_BARN;
        if (sash) paneCoverage *= 1.0 - entityLine(delta.y,0.022,footprint.y);
        if ((sash && opening.z > 1.65) || uKind == K_CHURCH || (uKind == K_APART && opening.w > 2.0))
          paneCoverage *= 1.0 - entityLine(delta.x,0.025,footprint.x);
        if (uKind == K_CHURCH) {
          float spring = opening.w*0.5 - opening.z*0.5;
          if (delta.y > spring) paneCoverage *= 1.0 - smoothstep(opening.z*0.5 - 0.05,opening.z*0.5,length(vec2(delta.x,delta.y-spring)));
        }
        alb = mix(alb, vec3(0.54,0.535,0.51), clamp(frameCoverage-paneCoverage,0.0,1.0));
        if (paneCoverage > 0.01) {
          vec2 paneUV = delta/size + 0.5;
          float paneSeed = hsh(floor(opening.xy*scale*4.0 + 0.5) + seed*31.0 + sign(n0.x+n0.z)*7.0);
          float detail = entityDetailFade(max(footprint.x/size.x,footprint.y/size.y),dist), roomLight;
          vec3 room = entityRoom(paneUV,entityPaneView(localView,n0),paneSeed,detail,roomLight);
          alb = mix(alb,room,paneCoverage); rough = mix(rough,0.08,paneCoverage); metal = 0.0; nb = n0;
          if (paneCoverage > 0.5) { surfaceFlags |= ENTITY_GLASS; cls = 3.0; }
          float lit = step(0.65,paneSeed);
          emit = mix(vec3(1.0,0.73,0.44),vec3(0.85,0.9,1.0),step(0.85,paneSeed))*lit*uNight*roomLight*1.2*paneCoverage;
        }
      }
      if (uKind == K_TOWER && !side && n0.z > 0.0 && nominal.y < 3.5 && abs(nominal.x) < 1.45) {
        alb = vec3(0.035,0.041,0.038); rough = 0.1; metal = 0.0; cls = 3.0; surfaceFlags |= ENTITY_GLASS;
      }
    }
  }
  if (uKind != K_GAPLANE && uKind != K_AIRLINER) {
    if (part == P_GLASS && cls == 3.0) {
      surfaceFlags |= ENTITY_GLASS;
      metal = 0.0; rough = max(0.075, min(0.24, entNormalWidth*0.55)); nb = n0;
      bool vehicle = uKind == K_CAR || uKind == K_TRUCK;
      bool headlamp = (uKind == K_CAR && lp.z/vScale.z > 2.0) || (uKind == K_TRUCK && lp.z/vScale.z > 4.4);
      if (vAux.z >= 1.0 && !vehicle && uKind != K_PUMP) {
        vec2 paneUV = vec2(vAux.z - 1.0, vAux.w);
        bool xFace = abs(n0.x) > 0.5;
        vec2 paneUDeriv = vec2(entUvDx.x, entUvDy.x), paneVDeriv = vec2(entUvDx.y, entUvDy.y);
        vec2 surfaceUDeriv = xFace ? vec2(entDx.z, entDy.z) : vec2(entDx.x, entDy.x);
        vec2 paneSize = vec2(dot(surfaceUDeriv, paneUDeriv)/max(dot(paneUDeriv, paneUDeriv), 1e-10),
                             dot(vec2(entDx.y, entDy.y), paneVDeriv)/max(dot(paneVDeriv, paneVDeriv), 1e-10));
        vec2 paneCenter = vec2(xFace ? lp.z : lp.x, lp.y) - (paneUV - 0.5)*paneSize;
        float paneSeed = hsh(floor(paneCenter*4.0 + 0.5) + seed*31.0 + sign(n0.x + n0.z)*7.0);
        float detail = entityDetailFade(max(length(entUvDx), length(entUvDy)), dist), roomLight;
        alb = entityRoom(paneUV, entityPaneView(localView, n0), paneSeed, detail, roomLight);
        float dust = 1.0 - smoothstep(0.0, 0.12, paneUV.y);
        rough += dust*0.11*detail;
        float lit = step(0.65, paneSeed);
        emit = mix(vec3(1.0, 0.73, 0.44), vec3(0.85, 0.9, 1.0), step(0.85, paneSeed))*lit*uNight*roomLight*1.2;
      }
      if (uKind == K_PUMP) { alb = vec3(0.022,0.034,0.025); rough = 0.2; emit = vec3(0.0); }   // instrument face, not a room
      if (vehicle && !headlamp) {
        // Tinted safety glass transmits a dark cabin; the actual sky reflection is
        // evaluated by the deferred dielectric path, not painted into the base colour.
        alb = vec3(0.045,0.048,0.043);
        if (vAux.z >= 1.0) {
          vec2 paneUV = vec2(vAux.z - 1.0, vAux.w);
          vec3 pv = entityPaneView(localView, n0);
          vec2 interior = paneUV - 0.5 - pv.xy/max(pv.z, 0.18)*0.18;
          float detail = entityDetailFade(max(length(entUvDx), length(entUvDy)), dist);
          bool windshield = abs(n0.z) > 0.45;
          float headX = windshield ? abs(interior.x) - 0.22 : interior.x + 0.06;
          vec2 head = vec2(headX/0.075,(interior.y + 0.045)/0.065);
          float seats = 1.0 - smoothstep(0.7,1.25,dot(head,head));
          float dash = 1.0 - smoothstep(0.16, 0.24, paneUV.y);
          vec3 cabin = mix(vec3(0.070,0.069,0.060),vec3(0.045,0.047,0.043),seats*0.7);
          cabin = mix(cabin,vec3(0.023,0.026,0.026),dash*0.8);
          alb = mix(alb, cabin, detail);
        }
        emit = vec3(0.0); rough = max(0.065, min(0.24, entNormalWidth*0.55));
      }
      if (headlamp) { alb = vec3(0.24, 0.235, 0.215); emit = vec3(0.0); rough = 0.085; }
    }
    if ((uKind == K_CAR || uKind == K_TRUCK) && part == P_PAINT) {
      surfaceFlags |= ENTITY_CLEARCOAT;
      float paint = fract(seed*13.31);
      if (uKind == K_CAR) alb = paint < 0.5 ? pal(paint*2.0, vec3(0.62), vec3(0.27,0.285,0.30), vec3(0.018), vec3(0.26,0.018,0.012))
        : pal(paint*2.0 - 1.0, vec3(0.016,0.047,0.16), vec3(0.45,0.46,0.48), vec3(0.022,0.083,0.044), vec3(0.105,0.115,0.125));
      else alb *= 0.7;
      metal = 0.0; rough = 0.31;
      float roadDust = 1.0 - smoothstep(0.28, 0.82, lp.y/vScale.y);
      alb = mix(alb, vec3(0.10,0.087,0.067), roadDust*0.10);
      rough += 0.12*roadDust;
    }
  }
#endif
  if (uKind != K_GAPLANE && uKind != K_AIRLINER) {
    float exposure = 0.35 + 0.65*clamp(n0.y, 0.0, 1.0);
    float wet = clamp(uWet, 0.0, 1.0)*exposure*(1.0 - uSnow*smoothstep(0.3, 0.8, n0.y));
    if (cls == 1.0) {
      alb *= 1.0 - 0.08*wet; rough = mix(rough, max(0.45, rough*0.78), wet);
    } else if (cls != 3.0 && dot(emit, emit) < 0.001) {
      if (part != P_WALL && part != P_ROOF && part != P_DARK) alb *= 1.0 - 0.22*wet*(1.0 - metal);
      rough = mix(rough, max(0.23, rough*0.58), wet);
    }
  }
  vec3 wn = vec3(cy*nb.x + sy*nb.z, nb.y, -sy*nb.x + cy*nb.z);
  if (dot(wn, V) < -0.2) wn = normalize(wn + V*0.5);   // bumped normals must not face away from the camera
  oG0 = vec4(dist, octEnc(normalize(wn)), cls == 1.0 ? 4.0 : 3.0);   // GB_FOLIAGE / GB_ENTITY (kGBuffer)
  oG1 = vec4(sqrt(clamp(alb, 0.0, 1.0)), clamp(rough, 0.03, 1.0));
  oG2 = vec4(max(emit, vec3(0.0)), metal);
  oG3 = vec4(1.0, 1.0, 1.0, float(surfaceFlags)/255.0);
}
