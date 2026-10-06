//! kSceneUniforms
//! Scene uniforms shared by every scene program: quality / debug switches, materials, airports, the player aircraft
//! (packed model, state, controls, lights, weapons) and the traffic, with the globals that hold the aircraft being shaded.
uniform float uMaxH; uniform int uQuality;
uniform int uScrWin;   // 1: the research craft's displays are windows (a screen pixel lets the world through; only the bomb camera's picture is drawn)
uniform int uDbg;
uniform float uPlaneTSh;   // terrain's sun shadow at the player's aircraft (computed once per frame on the CPU)   // profiling: each set bit switches one feature off (see Renderer::dbgOff)
uniform sampler2DArray uAlb; uniform sampler2DArray uNrm;
// airports
uniform int uApCount; uniform vec4 uAp[16]; uniform vec4 uApDim[16];
uniform int uBoxCount;
uniform sampler2D uData;  // static scene data: [0,64) roads, [64,192) box centres, [192,320) box sizes, [320,344) town xz bounds, [352,376) town y range
vec4 dataAt(int i){ return texelFetch(uData, ivec2(i, 0), 0); }
// aircraft
uniform int uPlaneOn; uniform vec3 uPlanePos; uniform mat3 uPlaneRot;
uniform vec4 uHud; uniform vec4 uHud2; uniform vec3 uHudV; uniform vec4 uHud3;  // research jet HUD: ias m/s, alt m, hdg, mach | g, throttle, nozzle, gear | velocity dir (body)
// wreckage: pieces of the airframe, each the aircraft SDF clipped to a body-space box with its own transform
uniform int uWreck; uniform vec3 uPcPos[5]; uniform mat3 uPcRot[5]; uniform vec3 uPcC[5]; uniform vec3 uPcH[5];
uniform int uDebN; uniform vec4 uDeb[16]; uniform vec4 uDebQ[16];
int gPI = -1; vec3 gPP; mat3 gPR; vec3 gPC;   // transform of the piece being traced / shaded
uniform vec4 uM[24]; uniform vec4 uPS; uniform vec4 uCtl; uniform vec4 uPr; uniform vec4 uI0; uniform vec4 uI1; uniform vec4 uI2;
uniform vec3 uColBase; uniform vec3 uColStripe;
uniform vec3 uReg;   // the player's registration letters (character codes; registrationOf in aircraft.h)
// copilot instrument cluster: the pilot's layout one seat over, shifted inboard so its bezel stays on the panel
// (steam-gauge copilots get the six-pack only: engine gauges and radios stay with the pilot)
vec2 coCluster(int ck){ return ck == 2 ? vec2(0.02, 0.2) : vec2(0.0, 0.16); }   // centre offset, half width
float coShift(vec4 E, int ck){
  float cx = coCluster(ck).x, hx = coCluster(ck).y;
  float c = -E.x + cx, lim = E.w - 0.035;
  return min(0.0, lim - (c + hx)) + max(0.0, -lim - (c - hx));
}
uniform vec4 uProp[2]; uniform int uPropCount;
uniform vec3 uLandLightPos; uniform vec3 uLandLightDir; uniform float uLandLight;
// point / spot lights (everything but the sun and moon): position + source radius | radiance + spot cutoff cosine
// (-2 = omni) | spot axis + shadow flag (> 0: casts the aircraft's shadow, stopping that far short of the light)
uniform float uRwyLights;   // airport lighting on (night / low visibility)
uniform int uPLN; uniform vec4 uPLP[12]; uniform vec4 uPLC[12]; uniform vec4 uPLD[12];
// the player aircraft's light fixtures (body space): lens centre | lens emission | outward axis + glass tint (0 red,
// 1 green, 2 clear)
uniform int uLensN; uniform vec4 uLensP[6]; uniform vec4 uLensC[6]; uniform vec4 uLensD[6];
uniform vec4 uWr[7];   // XR-40 Wraith animation and weapons state (see mapWraith)
uniform int uFxBeams; uniform vec4 uBeamA[16]; uniform vec4 uBeamB[16];   // laser bolts: tail + radius, head + intensity
uniform int uFxBombs; uniform vec4 uBombs[8];                           // dark-energy bombs in flight: centre + radius
uniform int uFxBlasts; uniform vec4 uBlast[6]; uniform vec4 uBlastI[6]; // detonations: centre + radius, age 0..1 + intensity
uniform vec4 uVapor;   // transonic vapour cone: density, start z, start radius, length (body space)
uniform vec4 uFlame; uniform vec3 uFlameLP; uniform vec3 uFlameLI;  // research jet exhaust: spool, reheat, vector angle, mach | light pos, radiance
// Per-aircraft data the SDF and its shading read: your aircraft (uniforms) or a traffic aircraft (uTraffic row k:
// texels 0-23 model, 24 position + bound radius, 25-27 rotation columns, 28 state, 29 controls, 30 base colour + prop
// angle, 31 stripe colour + reheat)
uniform int uModelId;   // the player's type (index into kAircraft; -1 unknown): the Osprey's cabin trim is gated by it
int gModelId = -1;
uniform vec3 uWheel;   // the player's wheels' roll (main left, main right, nose / tail), from the simulation
vec3 gWheel;           // the wheels' roll of the aircraft loaded (the player's, or a traffic aircraft's from its rotation columns' .w)
vec4 gM[24]; vec4 gPS; vec4 gCtl; vec3 gColBase; vec3 gColStripe; vec4 gFlame;
vec4 gWr[7];   // the XR-40's animation state the field reads (uWr, or a bake's state)
// Fitted cabin mounts, cached when the model is loaded rather than at every ray-march sample.
vec4 gCab0, gCab1;  // seat half width, headrest y, dome-light y, armrest x | visor y / slope, overhead y, vent x
void loadCabinFit();
uniform sampler2D uTraffic; uniform int uTrafficN;
bool gOwn = true;   // the globals hold the player's aircraft (not a traffic one)

// ---------------------------------------------------------------- materials (texture array layers)
const int M_GRASS=0, M_FOREST=1, M_ROCK=2, M_SAND=3, M_SNOW=4, M_ASPHALT=5, M_GRAVEL=6, M_DIRT=7;
const int M_CONCRETE=8, M_TILES=9, M_SLATE=10, M_PLASTER=11, M_BRICK=12, M_LEAVES=13, M_NEEDLES=14, M_PAINT=15;
const int M_METAL=16, M_RUBBER=17, M_PLASTIC=18, M_FABRIC=19, M_CARPET=20, M_LEATHER=21, M_CORRUGATED=22, M_CROP=23, M_WHEAT=24;
const int M_BARK=25, M_PLANKS=26, M_LITTER=27, M_SHINGLES=28, M_SIDING=29;
