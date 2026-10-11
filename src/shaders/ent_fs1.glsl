//! kEntFS1
//! Shared material code (G-buffer pass; the shadow pass only uses the cut-outs)

in vec3 vW; in vec3 vL; in vec3 vLN; in vec4 vAux; in vec3 vC; flat in vec4 vInst; flat in vec3 vScale; flat in float vFade; flat in vec2 vLodK;
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
uniform sampler2DArray uEnvAlb; uniform sampler2DArray uEnvNrm; uniform int uEnvMaterials;
uniform int uKind; uniform int uLod; uniform int uShadowPass; uniform vec3 uCam; uniform float uRwyLights; uniform float uNight; uniform float uWet; uniform float uSnow; uniform float uTime;
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;
const int M_CONCRETE=8, M_TILES=9, M_SLATE=10, M_PLASTER=11, M_BRICK=12, M_LEAVES=13, M_NEEDLES=14, M_PAINT=15;
const int M_METAL=16, M_CORRUGATED=22, M_BARK=25, M_PLANKS=26, M_LITTER=27, M_SHINGLES=28, M_SIDING=29;
const int K_FIR=0, K_SPRUCE=1, K_PINE=2, K_OAK=3, K_BIRCH=4, K_PALM=5, K_BUSH=6, K_BOULDER=7, K_BLOCK=8, K_SLAB=9, K_OUTCROP=10, K_SPIRE=11, K_SEASTACK=12;
const int K_HOUSE=13, K_HIP=14, K_LHOUSE=15, K_FARM=16, K_TOWNHOUSE=17, K_SHOP=18, K_APART=19, K_OFFICE=20, K_TOWER=21, K_SKY=22;
const int K_SUPERTALL=23, K_ROUND=24, K_SLABTOWER=25, K_MIDRISE=26;   // (entities.h EntKind, in step)
const int K_WAREHOUSE=27, K_BARN=28, K_SILO=29, K_CHURCH=30, K_WATERTOWER=31, K_LIGHTHOUSE=32, K_GAS=33;
const int P_BARK=0, P_LEAF=1, P_FROND=2, P_NEEDLE=3, P_ROCK=4, P_WALL=5, P_ROOF=6, P_TRIM=7, P_GLASS=8, P_METAL=9, P_DOOR=10, P_BRICK=11;
const int P_AWNING=12, P_WOOD=13, P_DARK=14, P_LAMP=15, P_SIGN=16, P_CANOPY=17, P_LEAFCARD=18, P_RLAMP=19, P_PAPI=20;
const int P_PAINT=21, P_STRIPE=22, P_SOCK=23, P_BEACON=24, P_FENCE=25, P_OBST=26;
const int K_HANGAR=36, K_ARCH=37, K_THANGAR=38, K_TERMINAL=39, K_CTRL=40, K_FBO=41, K_FUELTANK=42, K_PUMP=43, K_WINDSOCK=44, K_BEACON=45;
const int K_GAPLANE=46, K_AIRLINER=47, K_JETBRIDGE=48, K_CAR=49, K_TRUCK=50, K_FENCE=51, K_LOC=52, K_RADAR=53, K_MAST=54, K_FLOOD=55, K_BRIDGE=56;
const int P_DECK=33;   // a bridge deck's road surface, P_DECK + its road class (bridge_mesh.h): vAux.zw across and along it (m)
float hsh(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float hsh3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)))*43758.5453); }
float vn3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f);
  return mix(mix(mix(hsh3(i), hsh3(i + vec3(1,0,0)), f.x), mix(hsh3(i + vec3(0,1,0)), hsh3(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(hsh3(i + vec3(0,0,1)), hsh3(i + vec3(1,0,1)), f.x), mix(hsh3(i + vec3(0,1,1)), hsh3(i + vec3(1,1,1)), f.x), f.y), f.z); }
#if ENT_TREES
float coniferShootCoverage(vec2 uv, float shoot, float distanceFade){
  float x = uv.x*2.0 - 1.0, u = abs(x), v = uv.y;
  if (u < 0.023*(1.0 - 0.65*v)) return 1.0;
  // Each card holds four pairs of short leafy shoots. Needle length is measured
  // around those narrow branchlets, never from the central axis to the card edge.
  float lean = 0.16 + 0.05*shoot, stagger = x < 0.0 ? 0.034 : 0.0;
  float row = clamp(floor((v - u*lean - stagger)*4.0 - 0.45 + 0.5),0.0,3.0);
  float center = (row + 0.45)*0.25 + stagger;
  float across = abs(v - center - u*lean);
  float extent = (0.92 - row*0.12)*(0.84 + 0.16*fract(shoot*7.13 + row*0.61));
  if (u > extent || across > 0.083*(1.0 - 0.45*u)) return 0.0;
  if (across < 0.009) return 1.0;
  float needle = (u + across*0.8)*18.0 + shoot*3.0 + row*0.37;
  return fract(needle) <= mix(0.58,0.86,distanceFade) ? 1.0 : 0.0;
}
#endif
// foliage cut-outs: holes through the clumps towards their silhouettes (leafy edges, dappled shadows)
bool leafCut(float viewEdge){
  int part = int(vAux.x + 0.5);
#if ENT_TREES
  if (part == P_LEAF) {
    float n = vn3(vL*2.3 + vInst.x*17.0)*0.55 + vn3(vL*7.3 - vInst.x*9.0)*0.45;
    return n < 0.24 + 0.4*viewEdge;
  }
  if (part == P_NEEDLE) {   // needle masses: ragged towards their silhouettes - tufts and branch ends, not pillows or paper cones
    if (viewEdge <= 0.1) return false;   // (no cut can fall below it: the noise isn't paid for)
    float n = vn3(vL*3.1 + vInst.x*13.0)*0.6 + vn3(vL*9.0 - vInst.x*7.0)*0.4;
    return n < 0.6*viewEdge - 0.06;
  }
  if (part == P_LEAFCARD && uKind == K_PINE) {   // elongated needle shoots, not radial pinwheel fans
    float u = fract(vAux.z)*2.0 - 1.0, v = vAux.w;
    float shoot = hsh(vec2(floor(vAux.z), vInst.x));
    float axis = (shoot - 0.5)*0.22*v, across = abs(u - axis);
    float envelope = (0.7 + 0.22*shoot)*(1.0 - 0.7*v*v)*smoothstep(0.0, 0.12, v);
    if (across > envelope) return true;
    if (across < 0.027*(1.0 - 0.6*v)) return false;   // tapered shoot
    // Long needles slant toward the shoot tip, alternating and varying length along the stem.
    float needle = (v + across*0.34)*14.0 + (u < axis ? 0.4 : 0.0) + shoot*3.0;
    float pair = hsh(vec2(floor(needle), floor(vAux.z) + shoot*9.0));
    if (across > envelope*(0.63 + 0.37*pair)) return true;
    float far = smoothstep(25.0, 110.0, length(uCam - vW));
    return fract(needle) > mix(0.32, 0.64, far);
  }
  if (part == P_LEAFCARD && uKind <= K_SPRUCE) {
    float far = smoothstep(25.0, 110.0, length(uCam - vW));
    return coniferShootCoverage(vec2(fract(vAux.z),vAux.w),hsh(vec2(floor(vAux.z),vInst.x)),far) < 0.5;
  }
  if (part == P_LEAFCARD) {   // the same jittered leaf ellipses, with only the four possible covering cells
    vec2 uv0 = vec2(fract(vAux.z), vAux.w);
    if (length(uv0 - 0.5) > 0.47 - 0.12*vn3(vec3(uv0*5.0, floor(vAux.z) + vInst.x*7.0))) return true;
    vec2 uv = uv0*5.0, c0 = floor(uv - 0.5);
    // Centres are cell + [0.2, 0.8], and each rotated ellipse fits a radius-0.5 circle. A cell
    // outside this 2x2 neighborhood is at least 0.7 away on one axis, so it cannot cover uv.
    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
      vec2 c = c0 + vec2(i, j);
      float h = hsh(c + floor(vAux.z)*7.0 + vInst.x*13.0);
      vec2 ctr = c + 0.5 + (vec2(h, fract(h*17.3)) - 0.5)*0.6;
      vec2 d = uv - ctr;
      if (dot(d, d) > 0.250001) continue;   // conservative circle bound before the trigonometry
      float a = h*6.2832, ca = cos(a), sa = sin(a);
      d = vec2(ca*d.x + sa*d.y, -sa*d.x + ca*d.y);
      if (length(d/vec2(0.5, 0.23)) + 0.15*step(abs(d.y), 0.02) <= 1.0) return false;
    }
    return true;
  }
#endif
#if ENT_BUILDINGS
  if (part == P_FENCE) {   // chain link: real diamonds up close, a dithered see-through panel further away
    // (in the sun's shadow maps a fixed pattern on the panel itself, about as dense as the mesh: the view-dependent
    // cut-outs below would follow the camera and the frame-to-frame dither would make the shadow crawl)
    if (uShadowPass != 0) return hsh(floor(vL.xy/0.12) + vInst.x*31.0) > 0.22;
    float d = length(uCam - vW);
    if (d < 35.0) { vec2 q = vec2(vL.x + vL.y, vL.x - vL.y)/0.17; vec2 f = abs(fract(q) - 0.5); return min(f.x, f.y) > 0.07*(1.0 + d/35.0); }
    return hsh(gl_FragCoord.xy + fract(uTime*7.31)*vec2(17.0, 41.0)) > 0.06 + 0.2*smoothstep(500.0, 40.0, d);
  }
#endif
#if ENT_TREES
  if (part == P_FROND) {   // leaflets either side of the midrib
    if (vAux.w < 0.0) return false;   // highest detail has individual modelled leaflets
    float a = abs(vAux.z);
    if (a < 0.07) return false;
    float l = fract(vAux.w*34.0 + a*2.2);
    return l > 0.58 || a > 0.97 - 0.25*vAux.w*vAux.w;
  }
#endif
  return false;
}
