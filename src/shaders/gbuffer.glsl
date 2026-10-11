//! kGBuffer
//! The G-buffer the raster passes write and the lighting pass reads. GB0: view distance, octahedral normal, class;
//! GB1: sqrt(albedo), roughness; GB2: emission (HDR), metalness; GB3: ambient occlusion, the surface's own sun
//! shadow, the terrain's sun shadow at it, flags/255 (GBF_*). A prelit class (water, displays, the cabin) carries its
//! finished colour in GB2 and only gets the aerial perspective.
const int GB_SKY = 0, GB_TERRAIN = 1, GB_WATER = 2, GB_ENTITY = 3, GB_FOLIAGE = 4, GB_PLANE = 5, GB_CABIN = 6, GB_POD = 7, GB_DISPLAY = 8,
          GB_DEBRIS = 9, GB_UFO = 10, GB_WRECK = 11, GB_TRAFFIC = 12, GB_ENEMY = 13;
// flags: a sun glint on painted skin; a display or gauge pixel (the post pass doesn't sharpen it); the TAA's history
// class, moving (0.2) or rigid with the aircraft (0.5) - neither: a world pixel (1)
const int GBF_GLINT = 1, GBF_DISPLAY = 2, GBF_MOVING = 4, GBF_RIGID = 8;
// Environment-only surface lobes; emitted only by scenery, never by an aircraft program.
const int GBF_ENV_GLASS = 16, GBF_ENV_CLEARCOAT = 32;
// The shadow proxy's three light channels go to the brightest shadow-casting lights (the landing lights before the
// strobes, the beacon and the nav lights, whichever are on this frame): light i's channel, or -1
int gbShadowSlot(int i){
  if (uPLD[i].w <= 0.0) return -1;
  float li = max(uPLC[i].r, max(uPLC[i].g, uPLC[i].b)); int slot = 0;
  for (int k = 0; k < 12; k++) {
    if (k >= uPLN) break;
    if (k == i || uPLD[k].w <= 0.0) continue;
    float lk = max(uPLC[k].r, max(uPLC[k].g, uPLC[k].b));
    if (lk > li || (lk == li && k < i)) slot++;
  }
  return slot < 3 ? slot : -1;
}
vec2 gbOctEnc(vec3 n){ n /= abs(n.x) + abs(n.y) + abs(n.z); return n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); }
