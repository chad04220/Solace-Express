// The airports the ground shaders paint and trace (Renderer::airportSlots): with 16 or fewer, every one in its own
// slot, as before; with more - a real-Earth chart holds up to 40 - the 16 nearest the camera, nearest first, the rest
// changing in as it moves. Each slot carries its airport's runway and size, the side its apron lies on (aptSide: by
// its index, or its own, as the real Earth's are), and its buildings' world bounds and box range, as traceBoxes reads
// them.
#include "../src/renderer.h"
#include "../src/airport_layout.h"
#include <cstdio>
#include <set>

static int failed = 0;
static void check(bool pass, const std::string& label) { printf("Airport slots: %s: %s\n", label.c_str(), pass ? "ok" : "FAIL"); fflush(stdout); failed += !pass; }

int main() {
  World w;
  auto airport = [](int i) {
    Airport a{"X", "X", -30000.f + 1500.f * i, (i % 3) * 200.f, 5.f + i, (float)(i * 37 % 360), 900.f + 10.f * i, 30.f, i % 2, i % 3, ""};
    return a;
  };
  for (int i = 0; i < 12; i++) w.airports.push_back(airport(i));
  {   // twelve: all of them, each in its own slot
    const auto S = Renderer::airportSlots(w, 0.f, 0.f);
    bool own = S.n == 12;
    for (int k = 0; k < S.n; k++) own &= S.index[k] == k && S.ap[k * 4] == w.airports[k].x;
    check(own, "twelve airports: each in its own slot");
  }
  for (int i = 12; i < 40; i++) w.airports.push_back(airport(i));
  // the buildings of two airports: one near the camera, one far
  for (int ai : {20, 39})
    for (int k = 0; k < 3; k++) {
      Box b; b.c = vec3(40.f * k, 6.f, 120.f); b.h = vec3(10.f, 6.f, 8.f); b.airport = ai; b.kind = k % 3;
      w.boxes.push_back(b);
    }
  {   // forty, the camera at the 20th: the 16 nearest it, nearest first; the sides by index, then the airports' own
    const float cx = w.airports[20].x, cz = w.airports[20].z;
    const auto S = Renderer::airportSlots(w, cx, cz);
    std::vector<std::pair<float, int>> all;
    for (int i = 0; i < 40; i++) all.push_back({hypotf(w.airports[i].x - cx, w.airports[i].z - cz), i});
    std::sort(all.begin(), all.end());
    std::set<int> want, got;
    for (int k = 0; k < 16; k++) { want.insert(all[k].second); got.insert(S.index[k]); }
    bool nearest = S.n == 16 && want == got, ordered = true, packed = true, sides = true;
    float last = -1.f;
    for (int k = 0; k < S.n; k++) {
      const Airport& a = w.airports[S.index[k]];
      const float d = hypotf(a.x - cx, a.z - cz);
      ordered &= d >= last; last = d;
      sides &= ((S.sides >> k) & 1) == (S.index[k] & 1);
      packed &= S.ap[k * 4] == a.x && S.ap[k * 4 + 1] == a.z && S.ap[k * 4 + 2] == a.elev && std::fabs(S.ap[k * 4 + 3] - a.heading * DEG) < 1e-5f;
      packed &= S.dim[k * 4] == a.length && S.dim[k * 4 + 1] == a.width && S.dim[k * 4 + 2] == (float)a.surface;
      packed &= (int)S.dim[k * 4 + 3] == a.size;
    }
    check(nearest && S.index[0] == 20, "forty airports: the 16 nearest the camera, its own first");
    check(ordered, "nearest first");
    check(sides, "each slot's side its airport's, by its index");
    {   // (the real Earth's: each its own side, wherever it is in the list)
      World& e = w;
      for (int i = 0; i < 40; i++) e.airports[i].side = (i * 7 % 5) < 2 ? 1 : -1;
      const auto E = Renderer::airportSlots(e, cx, cz);
      bool own = true;
      for (int k = 0; k < E.n; k++) own &= ((E.sides >> k) & 1) == (e.airports[E.index[k]].side > 0 ? 1 : 0) &&
                                           aptLayout(e.airports[E.index[k]], E.index[k]).side == (float)e.airports[E.index[k]].side;
      check(own && E.sides != S.sides, "an airport's own side, in its slot and its layout");
      for (auto& a : e.airports) a.side = 0;
    }
    check(packed, "each slot its airport's runway and size");
    // its buildings: the slot of the 20th holds their bounds and box range; the far one's isn't among the slots
    const float* box = S.box;   // (slot 0: the 20th)
    bool inside = box[3] == 0.f && box[7] == 3.f;
    const Airport& A = w.airports[20];
    const float s = sinf(A.heading * DEG), c = cosf(A.heading * DEG);
    for (int k = 0; k < 3; k++) {
      const Box& b = w.boxes[k];
      const float wx = A.x + b.c.x * c + b.c.z * s, wz = A.z + b.c.x * s - b.c.z * c;
      inside &= wx >= box[0] && wx <= box[4] && wz >= box[2] && wz <= box[6] && A.elev + b.c.y >= box[1] && A.elev + b.c.y <= box[5];
    }
    check(inside, "its buildings' bounds and their boxes (0 to 3)");
    bool others = true;
    for (int k = 1; k < S.n; k++) others &= S.box[k * 8 + 7] == 0.f && S.index[k] != 39;
    check(others, "no other slot claims buildings");
  }
  {   // the camera flown to the far end: the 39th and its buildings come in
    const auto S = Renderer::airportSlots(w, w.airports[39].x, w.airports[39].z);
    check(S.n == 16 && S.index[0] == 39 && S.box[3] == 3.f && S.box[7] == 3.f, "flown to the far end: its airport first, its buildings (3 to 6)");
  }
  printf("airport_slots_test: %s\n", failed ? "FAILED" : "passed");
  return failed ? 1 : 0;
}
