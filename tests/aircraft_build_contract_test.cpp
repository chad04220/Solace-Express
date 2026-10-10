// Frozen pre-refactor inputs verify that each family implements the common build contract
// without changing mesh/pose ownership, part order, survey bounds, resolution or hull sweeps.
#include "../src/aircraft_mesh_build.h"
#include "../src/hull_mesh.h"
#include "aircraft_build_golden.inc"
#include <cstdio>
#include <cstring>

namespace {
uint64_t hashInt(uint64_t h, int64_t value) {
  for (int i = 0; i < 8; i++) { h ^= (uint64_t(value) >> (i * 8)) & 255; h *= 1099511628211ull; }
  return h;
}
uint64_t instanceHash(const aircraftBuild::PartInst* parts, int count) {
  uint64_t h = 1469598103934665603ull;
  for (int j = 0; j < count; j++) {
    h = hashInt(h, parts[j].type); h = hashInt(h, llround(parts[j].sx)); h = hashInt(h, llround(parts[j].sy));
  }
  return h;
}
uint64_t stateHash(const std::vector<HullState>& states) {
  uint64_t h = 1469598103934665603ull;
  // A ten-thousandth unit resolves every authored sweep step without hashing padding
  // or depending on last-bit differences between platform math libraries.
  for (const auto& state : states) for (const auto* v : {state.ps, state.ctl, state.wr, state.wr2})
    for (int i = 0; i < 4; i++) h = hashInt(h, llround(double(v[i]) * 10000.));
  return h;
}
}

int main() {
  using namespace aircraftBuild;
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char* what, int model, int part = -1) {
    checks++;
    if (!ok) { printf("FAIL: %s, model %d, part %d\n", what, model, part); failures++; }
  };
  for (int model = 0; model < 15; model++) {
    const auto family = model == 10 ? Family::Specter : model == 12 ? Family::Wraith : Family::Fleet;
    const auto& builder = meshBuilderFor(buildGolden::kPacked[model]);
    check(builder.family == family && familyOf(buildGolden::kPacked[model]) == family, "build-family selection", model);
    const char* define = model == 10 ? "#define AF_JET\n" : model == 12 ? "#define AF_WRAITH\n" : "#define AF_LIGHT\n";
    check(strcmp(shaderDefine(family), define) == 0, "unchanged shader-family define", model);
  }
  for (const auto& gold : buildGolden::lists) {
    PartInst parts[kMaxPartInst + 1]{};
    parts[kMaxPartInst] = {-17, 91.f, 92.f};
    const int count = partList(buildGolden::kPacked[gold.model], gold.inside != 0, parts, gold.claimed);
    check(count >= 0 && count <= kMaxPartInst && count == gold.count, "bounded instance count", gold.model);
    check(parts[kMaxPartInst].type == -17 && parts[kMaxPartInst].sx == 91.f && parts[kMaxPartInst].sy == 92.f,
          "instance capacity sentinel", gold.model);
    if (count >= 0 && count <= kMaxPartInst)
      check(instanceHash(parts, count) == gold.hash, "ordered part types and sides", gold.model);
  }
  for (const auto& gold : buildGolden::plans) {
    const auto plan = meshBuilderFor(buildGolden::kPacked[gold.model]).partPlan(gold.type, buildGolden::kPacked[gold.model]);
    check(int(plan.sampling) == gold.sampling, "part sampling mode", gold.model, gold.type);
    const float actual[] = {plan.lattice, plan.lo.x, plan.lo.y, plan.lo.z, plan.hi.x, plan.hi.y, plan.hi.z};
    for (int i = 0; i < (gold.sampling == 0 || gold.sampling == 1 ? 7 : 1); i++)
      check(std::isfinite(actual[i]) && fabsf(actual[i] - gold.v[i]) <= 0.000002f, "part bounds and lattice", gold.model, gold.type);
  }
  for (const auto& gold : buildGolden::sweeps) {
    const auto states = hullStateList(buildGolden::kPacked[gold.model], gold.inside != 0, gold.mesh != 0);
    check(int(states.size()) == gold.count, "hull-sweep count", gold.model);
    check(stateHash(states) == gold.hash, "ordered hull-sweep states", gold.model);
  }
  // Unknown packed models still choose the legacy fleet shader fallback and emit no parts.
  float custom[96]; memcpy(custom, buildGolden::kPacked[0], sizeof custom); custom[2] = 7.f;
  PartInst unused[kMaxPartInst];
  check(familyOf(custom) == Family::Fleet && partList(custom, false, unused) == 0 && partList(custom, true, unused) == 0,
        "unknown-engine compatibility", -1);
  printf("aircraft_build_contract: %d checks across 15 aircraft, %s\n", checks, failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
