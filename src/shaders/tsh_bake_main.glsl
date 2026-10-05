//! kTShBakeMain
//! ------------------------------------------------------------------------------------------------
//! Overlay pass: world-space sprites (particles, lights, rings) depth-tested against the ray-traced depth
//! GPS aerial imagery: the ray tracer's own terrain material seen straight down (the full ray-tracer source is linked
//! in with its main() renamed, so the map is exactly the world you fly over), hill-shaded from the north-west like a
//! satellite photo. 4 samples per texel; rendered into a texture only when the map view moves or zooms.
//! Terrain sun-shadow bake (world space, one texel per ~39 m): for the sun direction, the height a point above this
//! texel must reach to see over all the terrain towards the sun (x), and the distance to the terrain that sets it
//! (y, which sets the soft shadow's penumbra). The ray tracer's terrainShadow() reads it instead of marching a shadow
//! ray for every pixel. Rendered in bands of rows over several frames, and only when the sun has moved.

uniform vec3 uBakeSun; uniform float uBakeN;
void main(){
  vec2 p = (gl_FragCoord.xy/uBakeN*2.0 - 1.0)*WH;
  float lxz = length(uBakeSun.xz);
  if (lxz < 0.02 || uBakeSun.y <= 0.0) { oColor = vec4(-6e4, 1.0, 0.0, 1.0); return; }   // sun overhead: no terrain shadows
  vec2 dir = uBakeSun.xz/lxz;
  float slope = uBakeSun.y/lxz;              // the sun ray's rise per horizontal metre
  float H = -6e4, D = 1.0;
  float t = 3.0*WH/uBakeN;                   // start past this texel's own ground
  // out to 30 km (a low sun's shadow from a distant range reaches that far) or the world's edge. Stretches whose
  // conservative max height (the uHMax mip chain) can't rise above the horizon found so far are skipped whole, so
  // the march spends its samples only where a blocker could be.
  vec2 ird = vec2(abs(dir.x) > 1e-6 ? 1.0/dir.x : 1e9, abs(dir.y) > 1e-6 ? 1.0/dir.y : 1e9);
  for (int i = 0; i < 240; i++) {
    vec2 q = p + dir*t;
    if (t > 30000.0 || abs(q.x) > WH || abs(q.y) > WH) break;
    if (uMaxH - t*slope < H) break;          // nothing further away can rise above that
    bool sk = false;
    for (int L = HMAXL - 1; L >= 0; L--) {
      int n = HMAXN >> L; float cs = 2.0*WH/float(n);
      ivec2 ci = clamp(ivec2(floor((q + WH)/cs)), ivec2(0), ivec2(n - 1));
      if (texelFetch(uHMax, ci, L).r - t*slope > H) continue;   // could raise the horizon somewhere in it: look closer
      vec2 c0 = vec2(ci)*cs - WH;
      vec2 te2 = (mix(c0, c0 + cs, step(0.0, dir)) - p)*ird;
      float te = min(te2.x, te2.y);
      if (te > t + 0.01) { t = te + 0.05; sk = true; break; }
    }
    if (sk) continue;
    float y = terrainH(q, 4) - t*slope;      // a point here must reach this height to see over that sample
    if (y > H) { H = y; D = t; }
    t += max(20.0, t*0.03);
  }
  oColor = vec4(H, D, 0.0, 1.0);
}
