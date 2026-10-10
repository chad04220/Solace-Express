// Validation only: export the exact compiled, unposed fixture caster vertices.
// Link with the same src/*.o objects as environment_asset_mesh_test; no GL needed.
#include "entity_mesh.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv) {
  if(argc!=4) { std::fprintf(stderr,"usage: export_shadow_fixture_mesh KIND LOD OUTPUT.f32\n");return 2; }
  int kind=std::atoi(argv[1]),lod=std::atoi(argv[2]);
  if((kind!=13&&kind!=19&&kind!=45)||(lod!=0&&lod!=3))return 2;
  std::vector<EVert>v;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(v,ranges);
  FILE*f=std::fopen(argv[3],"wb");if(!f)return 1;
  const auto&r=ranges[kind];size_t count=std::fwrite(v.data()+r.first[lod],sizeof(EVert),r.count[lod],f);
  int closed=std::fclose(f);bool ok=count==size_t(r.count[lod])&&closed==0;
  return ok?0:1;
}
