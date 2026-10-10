// CPU regression coverage for traffic prop geometry, state isolation and bounded perspective/panorama coverage.
#include "../src/prop_disc.h"
#include "../src/models.h"
#include "../src/aircraft.h"
#include "../src/breakup.h"
#include <random>

static int failures = 0, checks = 0;
static void check(bool good, const char* what) { ++checks; if (!good) { ++failures; if (failures < 20) printf("FAIL: %s\n", what); } }
static bool close(float a, float b) { return fabsf(a - b) < 2e-5f; }

int main() {
  // Explicit roster contract: nine prop aircraft, including the appended Larkspur.
  // Keep the expected count independent of both modelProps and the packed shader row.
  static constexpr int propAircraft[] = {0, 1, 2, 3, 4, 5, 7, kOsprey, kLarkspur};
  static_assert(sizeof(propAircraft) / sizeof(propAircraft[0]) == 9, "nine native prop aircraft");
  int types = 0, hubs = 0;
  for (int m = 0; m < kAircraftCount; ++m) {
    float t[128] = {}; packModel(kAircraft[m], m, 1.f, t);
    t[24*4] = 35000.f; t[24*4 + 1] = 1200.f; t[24*4 + 2] = -34000.f;
    t[25*4] = t[26*4 + 1] = t[27*4 + 2] = 1.f;
    t[30*4 + 3] = 1.234f; t[29*4 + 3] = .05f;
    vec3 camera(35000.f, 1200.f, -34000.f);
    float expected[2][4] = {}; const int n = modelProps(kModels[m], expected);
    TrafficPropDisc out[2]; const int got = trafficPropGeometry(t, camera, kAircraft[m].blades, out);
    check(got == n, "hub count equals modelProps, including jets excluded");
    bool expectedProp = false;
    for (int id : propAircraft) expectedProp |= m == id;
    check((n > 0) == expectedProp, "exact nine-aircraft prop roster, including Larkspur; Atlas and research jets excluded");
    if (m == kLarkspur) check(n == 1 && got == 1 && out[0].blades == 3, "Larkspur owns one native three-blade prop disc");
    if (n) { ++types; hubs += n; }
    for (int i = 0; i < n; ++i) {
      check(close(out[i].centre.x, expected[i][0]) && close(out[i].centre.y, expected[i][1]) && close(out[i].centre.z, expected[i][2]), "camera-relative hubs exactly match modelProps at map edge");
      check(close(out[i].radius, expected[i][3]), "prop radius comes from this model");
      check(close(out[i].hubRadius, kModels[m].spinnerR), "blade roots fit this model's existing solid spinner");
      check(out[i].blades == kAircraft[m].blades, "correct 2/3/4-blade aircraft count");
      check(close(out[i].angle, 1.234f), "angle from traffic texel 30.w");
      check(out[i].blur == 0.f, "parked .05 throttle has distinct idle blades");
    }
    t[29*4 + 3] = 0.f; trafficPropGeometry(t, camera, kAircraft[m].blades, out);
    if (n) check(out[0].blur == 0.f, "zero throttle has distinct stopped blades");
    t[29*4 + 3] = .22f; trafficPropGeometry(t, camera, kAircraft[m].blades, out);
    if (n) check(out[0].blur > 0.f && out[0].blur < 1.f, "taxi transitions smoothly between blades and blur");
    t[29*4 + 3] = .8f; t[30*4 + 3] = 4.321f; trafficPropGeometry(t, camera, kAircraft[m].blades, out);
    if (n) check(out[0].blur == 1.f && close(out[0].angle, 4.321f), "running state is this traffic aircraft's state");
    // A nontrivial roll/yaw rotates each hub and its blade basis together.
    quat q = quat::axisAngle(vec3(0,1,0), .72f)*quat::axisAngle(vec3(0,0,1), -.39f);
    vec3 r=q.rotate(vec3(1,0,0)), u=q.rotate(vec3(0,1,0)), b=q.rotate(vec3(0,0,1));
    for (int j=0;j<3;++j) { t[25*4+j]=r[j]; t[26*4+j]=u[j]; t[27*4+j]=b[j]; }
    trafficPropGeometry(t,camera,kAircraft[m].blades,out);
    for (int i=0;i<n;++i) {
      vec3 v=q.rotate(vec3(expected[i][0],expected[i][1],expected[i][2]));
      check(length(out[i].centre-v)<2e-5f && length(out[i].right-r)<2e-5f && length(out[i].up-u)<2e-5f, "hub and blade axes follow aircraft attitude");
    }
  }
  // Every prop follows its own actual breakup body, preserving its separation
  // pose before following that piece's independent displacement and tumble.
  int wreckHubs = 0;
  for (int m = 0; m < kAircraftCount; ++m) {
    float props[2][4] = {}; const int np = modelProps(kModels[m], props);
    Plane plane; plane.spec = &kAircraft[m];
    BreakPiece pieces[kMaxBreakPieces]; const int n = breakPieces(kAircraft[m], 1.f, plane.gearHeight(), pieces);
    vec3 C[kMaxBreakPieces], H[kMaxBreakPieces];
    for (int j = 0; j < n; ++j) { C[j] = pieces[j].C; H[j] = pieces[j].rigidOnly ? vec3(-1) : pieces[j].H; }
    for (int i = 0; i < np; ++i) {
      ++wreckHubs;
      check(propWreckOwner(props[i], 0, C, H) == -1, "intact aircraft cannot acquire a debris prop draw");
      const int owner = propWreckOwner(props[i], n, C, H);
      check(owner >= 0 && owner < n && pieces[owner].kind == BK_PROP, "each prop disc follows a real BK_PROP spinner body");
      check(owner == breakOwner(pieces, n, vec3(props[i][0],props[i][1],props[i][2])), "prop renderer and breakup agree on ownership");
      const vec3 cam(35000.f, 1200.f, -34000.f), origin=cam+vec3(50,23,-100);
      for (int pose = 0; pose < 2; ++pose) {
        const quat q = pose == 0 ? quat::axisAngle(vec3(0,1,0), .72f)*quat::axisAngle(vec3(0,0,1), -.39f)
                                  : quat::axisAngle(normalize(vec3(2,-1,3)), 1.37f);
        const vec3 r=q.rotate(vec3(1,0,0)), u=q.rotate(vec3(0,1,0)), b=q.rotate(vec3(0,0,1));
        const float rot[9]={r.x,r.y,r.z,u.x,u.y,u.z,b.x,b.y,b.z};
        const vec3 offset=pose?vec3(17,-9,31):vec3(0);
        const auto disc=wreckPropGeometry(props[i],rot,origin+offset,cam,1.234f,kAircraft[m].blades,kModels[m].spinnerR);
        const vec3 hub(props[i][0],props[i][1],props[i][2]);
        check(length(disc.centre-((origin-cam)+offset+q.rotate(hub)))<3e-5f, "hub preserves separation placement then follows independent debris translation/tumble");
        check(length(disc.right-r)<2e-5f && length(disc.up-u)<2e-5f, "detached blade plane tumbles with its spinner");
        check(close(disc.angle,1.234f) && disc.blur==0.f, "captured blade phase is retained and distinct stopped blades remain visible");
        check(disc.blades==kAircraft[m].blades && close(disc.radius,props[i][3]) && close(disc.hubRadius,kModels[m].spinnerR), "detached prop preserves its model's blades, disc and solid spinner");
        if (pose==0) {
          // At the separation instant this is exactly the intact geometry, so
          // changing ownership does not teleport or rotate a blade.
          float row[128]={};packModel(kAircraft[m],m,1.f,row);
          for(int a=0;a<3;++a){row[24*4+a]=origin[a];row[25*4+a]=r[a];row[26*4+a]=u[a];row[27*4+a]=b[a];}
          row[30*4+3]=1.234f;
          TrafficPropDisc intact[2];trafficPropGeometry(row,cam,kAircraft[m].blades,intact);
          check(length(disc.centre-intact[i].centre)<2e-5f && length(disc.right-intact[i].right)<2e-5f && close(disc.angle,intact[i].angle), "separation orientation matches the existing intact/traffic disc convention");
        }
      }
    }
  }
  check(wreckHubs==12, "all twelve model prop hubs keep visible blade geometry through breakup");
  check(types == 9 && hubs == 12, "all nine prop types covered with 12 total discs, including Larkspur");
  PropDiscBounds bounds;
  check(propDiscBounds(vec3(0,0,-100),1,.7f,16.f/9,0,vec2(),bounds), "visible perspective disc kept");
  check((bounds.x1-bounds.x0)*(bounds.y1-bounds.y0)<.002f, "distant prop shades a small bound, not a fullscreen draw");
  check(!propDiscBounds(vec3(0,0,100),1,.7f,16.f/9,0,vec2(),bounds), "behind-camera perspective prop rejected");
  check(!propDiscBounds(vec3(100,0,-10),1,.7f,16.f/9,0,vec2(),bounds), "offscreen perspective prop rejected");
  check(propDiscBounds(vec3(0,0,-.1f),1,.7f,16.f/9,0,vec2(),bounds), "near-plane intersecting prop retained");
  std::mt19937 rng(20261008); std::uniform_real_distribution<float> f(-1.f,1.f);
  int covered = 0;
  for (int mode = 0; mode < 3; ++mode) for (int i = 0; i < 250; ++i) {
    const float pano = mode == 0 ? 0.f : mode == 1 ? 1.9f : PI;
    vec3 c(f(rng)*35.f,f(rng)*18.f,f(rng)*40.f); const float radius=.2f+fabsf(f(rng))*3.f;
    bool visible=propDiscBounds(c,radius,.7f,16.f/9,pano,vec2(),bounds);
    for (int j=0;j<250;++j) {
      vec3 v(f(rng),f(rng),f(rng)); v=normalize(v)*radius+c;
      if (pano==0.f && v.z>=-.001f) continue;
      float x=pano>0.f?atan2f(v.x,-v.z)/pano:v.x/(-v.z)/(.7f*16.f/9);
      float y=pano>0.f?v.y/sqrtf(v.x*v.x+v.z*v.z)/.7f:v.y/(-v.z)/.7f;
      if (fabsf(x)>=1.f||fabsf(y)>=1.f) continue;
      ++covered;
      check(visible && x>=bounds.x0-1e-5f && x<=bounds.x1+1e-5f && y>=bounds.y0-1e-5f && y<=bounds.y1+1e-5f, "screen bound contains sphere samples, including panoramic seam and close camera");
    }
  }
  check(covered>10000, "substantial randomized projection coverage");
  printf("%s: %d checks; %d prop aircraft, %d model hubs, %d visible bound samples\n",failures?"FAIL":"PASS",checks,types,hubs,covered);
  return failures?1:0;
}
