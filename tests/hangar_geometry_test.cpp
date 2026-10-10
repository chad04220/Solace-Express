// The indoor preview's room is finite, consistently wound, bounded geometry.
#include "../src/hangar_preview.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char* message) { if (!ok) { fprintf(stderr, "%s\n", message); std::exit(1); } }
int main() {
  size_t count = 0;
  for (float size : {12.f, 24.f, 50.f}) {
    const auto v = hangarPreview::geometry(size);
    check(!v.empty() && v.size()%3 == 0, "Hangar must contain whole triangles");
    check(v.size() < 5000, "Hangar geometry exceeded its small draw budget");
    if (count) check(count == v.size(), "Hangar tessellation must not grow with aircraft size");
    count = v.size(); bool floor = false, fixtures = false;
    for (size_t i=0; i<v.size(); ++i) {
      const auto& a=v[i];
      check(std::isfinite(a.p.x+a.p.y+a.p.z), "Non-finite vertex");
      check(fabsf(length(a.n)-1.f)<1e-5f, "Invalid normal");
      check(a.p.y >= -.361f && a.p.y <= .72f*size+.101f, "Room height outside bounds");
      floor |= a.kind == 1.f && a.p.y == 0.f;
      fixtures |= a.kind == 3.f;
      if (i%3==0) check(dot(cross(v[i+1].p-a.p,v[i+2].p-a.p),a.n)>0.f,"Inward or degenerate triangle winding");
    }
    check(floor && fixtures, "Hangar floor or emissive fixtures missing");
  }
  printf("Hangar geometry: %zu vertices, finite positions, unit normals, outward winding, fixed budget: PASS\n",count);
}
