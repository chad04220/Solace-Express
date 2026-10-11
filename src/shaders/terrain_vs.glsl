//! kTerrainVS
//! The terrain mesh: a quadtree of 32 x 32-quad chunks around the camera (terrain_mesh.cpp picks them), every vertex
//! set on the heightfield by the same terrainH the physics stands on, with the octave count
//! chosen by distance. Where a chunk nears the distance at which its parent would
//! be drawn instead, the vertices the parent lacks slide onto the parent's edges (CDLOD morphing), so chunks of
//! different levels meet without cracks and without skirts. The normal comes from four more height samples.
layout(location = 0) in vec4 aInst;   // chunk origin x, z (m), cell size (m), the distance its parent stops splitting at (0: the plain levels', uSplit x 2S)
uniform mat4 uVP; uniform vec2 uJit; uniform float uLogC; uniform vec3 uCamPos;
uniform mat4 uPanoView; uniform vec2 uPano;   // a panoramic camera feed: projected onto its cylinder (camRay)
uniform float uSplit;                         // a chunk is split while the camera is within uSplit chunk sizes of it
out vec3 vW; out vec3 vN;
out vec3 vC;   // where it is drawn: on the round world (kPlanet)
const int C = 32;
int octAt(float d){ return d < 600.0 ? 11 : (d < 3000.0 ? 9 : (d < 12000.0 ? 7 : 5)); }   // (terrainNormal's thresholds)
float hAt(vec2 xz, float d){ return terrainH(xz, octAt(d)); }
void main(){
  // grid vertex 0..32 (indexed: terrain_mesh.cpp lays the two triangles per cell, counter-clockwise seen from above);
  // past the grid's, a skirt's lower corner: the k-th vertex of edge e (z = 0, x = 32, z = 32, x = 0), dropped
  int id = gl_VertexID, NG = (C + 1)*(C + 1);
  bool skirt = id >= NG;
  ivec2 g;
  if (skirt) { int e = (id - NG)/(C + 1), k = (id - NG) % (C + 1); g = e == 0 ? ivec2(k, 0) : e == 1 ? ivec2(C, k) : e == 2 ? ivec2(k, C) : ivec2(0, k); }
  else g = ivec2(id % (C + 1), id / (C + 1));
  float cs = aInst.z, S = cs*float(C);
  vec2 xz = aInst.xy + vec2(g)*cs;
  float dh = length(xz - uCamPos.xz);
  float d0 = sqrt(dh*dh + uCamPos.y*uCamPos.y);   // (a first distance for the octave choice; the exact one needs the height)
  float h = hAt(xz, d0);
  float d = length(vec3(xz.x, h, xz.y) - uCamPos);
  // morph: fully onto the parent's grid by the distance at which the parent stops splitting (uSplit x its size 2S; a
  // road's detail its own), so a chunk of the next level across an edge, whose vertices all lie beyond that, matches
  // edge for edge
  float Dm = aInst.w > 0.0 ? aInst.w : uSplit*2.0*S;
  float m = clamp((d - Dm*0.625)/(Dm*0.95 - Dm*0.625), 0.0, 1.0);
  bool ox = (g.x & 1) == 1, oy = (g.y & 1) == 1;
  if (m > 0.0 && (ox || oy)) {
    // the parent's edge through this vertex: between its two neighbours on the parent's grid (both odd: the
    // parent's cell diagonal, which runs from (0, 1) to (1, 0) like every cell's)
    vec2 a = ox && oy ? vec2(-cs, cs) : (ox ? vec2(-cs, 0.0) : vec2(0.0, -cs));
    vec2 pa = xz + a, pb = xz - a;
    float ha = hAt(pa, length(vec3(pa.x, h, pa.y) - uCamPos)), hb = hAt(pb, length(vec3(pb.x, h, pb.y) - uCamPos));
    h = mix(h, 0.5*(ha + hb), m);
  }
  // the sea floor no deeper than just under the sea, seen from above it: the sea is opaque and its colour reads the
  // depth from the heightfield, not the mesh, but a coarse chunk's triangle from a 30 m deep vertex to a beach's 2 m
  // one crossed the sea hundreds of metres inland - beaches, spits and low islands sank under the water far off and
  // rose out of it as they came near
  if (uCamPos.y > 0.5) h = max(h, -0.5);
  // a skirt hangs below the edge, under the neighbouring chunk's ground: where a road's finer chunk meets a coarser one
  // (or one morphing to another distance) the gap between their edges is closed
  if (skirt) h -= 2.0 + cs*0.6 + (aInst.w > 0.0 ? 6.0 : 0.0);
  // the normal at the mesh's own resolution (widening with the morph)
  float e = cs*(1.0 + m);
  vec3 n = normalize(vec3(hAt(xz - vec2(e, 0.0), d) - hAt(xz + vec2(e, 0.0), d), 2.0*e, hAt(xz - vec2(0.0, e), d) - hAt(xz + vec2(0.0, e), d)));
  vec3 wp = vec3(xz.x, h, xz.y);
  vW = wp; vN = n;
  vec3 cp = planetPos(wp, uCamPos); vC = cp;
  gl_Position = uVP*vec4(cp, 1.0);
  bool behind = false;
  if (uPano.x > 0.0) {   // (the whole triangle is dropped where it reaches round behind the camera)
    vec3 c = (uPanoView*vec4(cp, 1.0)).xyz; float a = atan(c.x, -c.z), dd = length(c);
    gl_Position = vec4(a/uPano.x*dd, c.y/max(length(c.xz), 1e-3)/uPano.y*dd, 0.0, dd); behind = abs(a) > 1.9;
  }
  gl_Position.xy -= 2.0*uJit*gl_Position.w;   // the TAA's sub-pixel jitter
  gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;   // logarithmic depth (renderer.h kLogDepthFar)
  if (behind) gl_Position.z = 2.0*gl_Position.w;
}
