//! kEntFS1
//! Shared material code (G-buffer pass; the shadow pass only uses the cut-outs)

in vec3 vW; in vec3 vL; in vec3 vLN; in vec4 vAux; flat in vec4 vInst; flat in vec3 vScale; flat in float vFade; flat in vec2 vLodK;
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
uniform int uKind; uniform int uShadowPass; uniform vec3 uCam; uniform float uRwyLights; uniform float uNight; uniform float uWet; uniform float uSnow; uniform float uTime;
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;
const int M_CONCRETE=8, M_TILES=9, M_SLATE=10, M_PLASTER=11, M_BRICK=12, M_LEAVES=13, M_NEEDLES=14, M_PAINT=15;
const int M_METAL=16, M_CORRUGATED=22, M_BARK=25, M_PLANKS=26, M_LITTER=27, M_SHINGLES=28, M_SIDING=29;
const int K_FIR=0, K_SPRUCE=1, K_PINE=2, K_OAK=3, K_BIRCH=4, K_PALM=5, K_BUSH=6, K_BOULDER=7, K_BLOCK=8, K_SLAB=9, K_OUTCROP=10, K_SPIRE=11, K_SEASTACK=12;
const int K_HOUSE=13, K_HIP=14, K_LHOUSE=15, K_FARM=16, K_TOWNHOUSE=17, K_SHOP=18, K_APART=19, K_OFFICE=20, K_TOWER=21, K_SKY=22;
const int K_WAREHOUSE=23, K_BARN=24, K_SILO=25, K_CHURCH=26, K_WATERTOWER=27, K_LIGHTHOUSE=28, K_GAS=29;
const int P_BARK=0, P_LEAF=1, P_FROND=2, P_NEEDLE=3, P_ROCK=4, P_WALL=5, P_ROOF=6, P_TRIM=7, P_GLASS=8, P_METAL=9, P_DOOR=10, P_BRICK=11;
const int P_AWNING=12, P_WOOD=13, P_DARK=14, P_LAMP=15, P_SIGN=16, P_CANOPY=17, P_LEAFCARD=18, P_RLAMP=19, P_PAPI=20;
const int P_PAINT=21, P_STRIPE=22, P_SOCK=23, P_BEACON=24, P_FENCE=25, P_OBST=26;
const int K_HANGAR=32, K_ARCH=33, K_THANGAR=34, K_TERMINAL=35, K_CTRL=36, K_FBO=37, K_FUELTANK=38, K_PUMP=39, K_WINDSOCK=40, K_BEACON=41;
const int K_GAPLANE=42, K_AIRLINER=43, K_JETBRIDGE=44, K_CAR=45, K_TRUCK=46, K_FENCE=47, K_LOC=48, K_RADAR=49, K_MAST=50, K_FLOOD=51;
float hsh(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
float hsh3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7)))*43758.5453); }
float vn3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f*f*(3.0 - 2.0*f);
  return mix(mix(mix(hsh3(i), hsh3(i + vec3(1,0,0)), f.x), mix(hsh3(i + vec3(0,1,0)), hsh3(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(hsh3(i + vec3(0,0,1)), hsh3(i + vec3(1,0,1)), f.x), mix(hsh3(i + vec3(0,1,1)), hsh3(i + vec3(1,1,1)), f.x), f.y), f.z); }
// foliage cut-outs: holes through the clumps towards their silhouettes (leafy edges, dappled shadows)
bool leafCut(float viewEdge){
  int part = int(vAux.x + 0.5);
  if (part == P_LEAF) {
    float n = vn3(vL*2.3 + vInst.x*17.0)*0.55 + vn3(vL*7.3 - vInst.x*9.0)*0.45;
    return n < 0.24 + 0.4*viewEdge;
  }
  if (part == P_NEEDLE) {   // needle masses: ragged towards their silhouettes - tufts and branch ends, not pillows or paper cones
    if (viewEdge <= 0.1) return false;   // (no cut can fall below it: the noise isn't paid for)
    float n = vn3(vL*3.1 + vInst.x*13.0)*0.6 + vn3(vL*9.0 - vInst.x*7.0)*0.4;
    return n < 0.6*viewEdge - 0.06;
  }
  if (part == P_LEAFCARD && uKind == K_PINE) {   // a tuft of needles radiating from the shoot
    vec2 q = vec2(fract(vAux.z), vAux.w)*2.0 - 1.0; float r = length(q);
    float a = atan(q.y, q.x)/6.2832 + 0.5;
    float k = fract(a*46.0 + r*0.6 + hsh(vec2(floor(vAux.z), vInst.x))*7.0);
    return r > 0.95 - 0.35*hsh(vec2(floor(a*46.0), floor(vAux.z))) || r < 0.06 || k > 0.5;
  }
  if (part == P_LEAFCARD && uKind <= K_SPRUCE) {   // a fir's flat spray: needles either side of the shoot, slanting forward, to a point at its tip
    float u = abs(fract(vAux.z)*2.0 - 1.0), v = vAux.w;
    if (u > 0.95*(1.0 - 0.8*v*v)*smoothstep(0.0, 0.1, v)) return true;
    if (u < 0.06) return false;   // the shoot
    float far = smoothstep(25.0, 110.0, length(uCam - vW));   // (needles finer than a pixel: a denser spray instead of shimmer)
    return fract((v + u*0.4)*24.0 + hsh(vec2(floor(vAux.z), vInst.x))*3.0) > mix(0.45, 0.8, far);
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
  if (part == P_FENCE) {   // chain link: real diamonds up close, a dithered see-through panel further away
    // (in the sun's shadow maps a fixed pattern on the panel itself, about as dense as the mesh: the view-dependent
    // cut-outs below would follow the camera and the frame-to-frame dither would make the shadow crawl)
    if (uShadowPass != 0) return hsh(floor(vL.xy/0.12) + vInst.x*31.0) > 0.22;
    float d = length(uCam - vW);
    if (d < 35.0) { vec2 q = vec2(vL.x + vL.y, vL.x - vL.y)/0.17; vec2 f = abs(fract(q) - 0.5); return min(f.x, f.y) > 0.07*(1.0 + d/35.0); }
    return hsh(gl_FragCoord.xy + fract(uTime*7.31)*vec2(17.0, 41.0)) > 0.06 + 0.2*smoothstep(500.0, 40.0, d);
  }
  if (part == P_FROND) {   // leaflets either side of the midrib
    float a = abs(vAux.z);
    if (a < 0.07) return false;
    float l = fract(vAux.w*34.0 + a*2.2);
    return l > 0.58 || a > 0.97 - 0.25*vAux.w*vAux.w;
  }
  return false;
}
