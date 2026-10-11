"""Compile actual portable identity bodies under strict and fast floating-point modes.

The two functions are extracted without modification from production sources and
linked to the production asset codec, shader pruner, assembled shaders, generated
source contract and roster IDs. Only Renderer storage is replaced by a tiny shim,
so this regression needs no GL context or full game build. It does not assert
cross-compiler or cross-driver equality of freshly generated geometry.

Usage: python portable_mesh_identity_test.py CXX_COMPILER CMAKE_EXECUTABLE
Linux GCC/Clang flags are deliberate; register this test only on those hosts.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def production_function(relative, signature):
    source = (ROOT / relative).read_text(encoding="utf-8")
    begin = source.index(signature)
    # Both top-level functions have their closing brace at column zero. Nested
    # scopes are indented. A future layout change should fail this extraction
    # or the C++ compilation, rather than silently exercise a copied algorithm.
    end = source.index("\n}\n", begin) + 3
    return source[begin:end]


def fixture_source():
    aircraft = (ROOT / "src/aircraft.cpp").read_text(encoding="utf-8")
    table = aircraft.split("const AircraftSpec kAircraft[] = {", 1)[1].split("// clang-format on", 1)[0]
    ids = re.findall(r'\{"([^"\n]+)",\s*"([^"\n]+)"', table)
    header = (ROOT / "src/aircraft.h").read_text(encoding="utf-8")
    count = int(re.search(r"constexpr int kAircraftCount\s*=\s*(\d+)", header).group(1))
    assert len(ids) == count and len(set(item[0] for item in ids)) == count, "Incomplete production roster extraction"
    roster = ",".join("{" + json.dumps(item[0]) + "}" for item in ids)
    source = """
#include "aircraft_mesh_asset.h"
#include "mesh_validation.h"
#include "shader_prune.h"
#include "shaders.h"
#include "aircraft_geometry_source.h"
#include <cstdio>
struct Spec { const char* id; };
"""
    source += f"static const Spec kAircraft[] = {{{roster}}};\n"
    source += f"""
class Renderer {{ public:
  static constexpr int kAfModels = {count};
  struct Own {{ aircraftAsset::Identity portableIdentity[2]; }};
  Own afOwn[kAfModels];
  aircraftAsset::Identity prebuiltAircraftIdentity(int model, int slot);
}};
"""
    source += production_function("src/renderer.cpp", "std::string meshGeometryStamp(const std::string& defines)")
    source += production_function("src/aircraft_mesh.cpp", "aircraftAsset::Identity Renderer::prebuiltAircraftIdentity(int model, int slot)")
    source += """
int main() {
  Renderer renderer;
  for (int model = 0; model < Renderer::kAfModels; ++model) for (int slot = 0; slot < 2; ++slot) {
    const auto first = renderer.prebuiltAircraftIdentity(model, slot);
    const auto cached = renderer.prebuiltAircraftIdentity(model, slot);
    if (first.geometry != cached.geometry || first.source != cached.source) return 1;
    const auto name = aircraftAsset::filename(first);
    if (name.empty()) return 2;
    std::puts(name.c_str());
  }
  if (!aircraftAsset::filename(renderer.prebuiltAircraftIdentity(-1, 0)).empty()) return 3;
  if (!aircraftAsset::filename(renderer.prebuiltAircraftIdentity(Renderer::kAfModels, 0)).empty()) return 4;
  if (!aircraftAsset::filename(renderer.prebuiltAircraftIdentity(0, -1)).empty()) return 5;
  if (!aircraftAsset::filename(renderer.prebuiltAircraftIdentity(0, 2)).empty()) return 6;
}
"""
    return count, source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler")
    parser.add_argument("cmake")
    args = parser.parse_args()
    count, source = fixture_source()
    with tempfile.TemporaryDirectory(prefix="solace-portable-identity-") as temporary:
        directory = Path(temporary)
        generated = directory / "gen"
        generated.mkdir()
        for source_dir, output, script in [
            (ROOT / "src/shaders", "shaders_gen.h", "embed_shaders.cmake"),
            (ROOT, "aircraft_geometry_source.h", "generate_aircraft_geometry_source.cmake"),
        ]:
            subprocess.run([args.cmake, f"-DSRC_DIR={source_dir}", f"-DOUT={generated / output}",
                            "-P", str(ROOT / "tools" / script)], check=True)
        fixture = directory / "identity.cpp"
        fixture.write_text(source, encoding="utf-8")
        results = []
        for label, extra in [("strict", []), ("fast", ["-ffast-math"])]:
            executable = directory / label
            subprocess.run([args.compiler, "-std=c++17", "-O2", *extra, "-I", str(ROOT / "src"),
                            "-I", str(generated), str(fixture), str(ROOT / "src/aircraft_mesh_asset.cpp"),
                            "-o", str(executable)], check=True)
            results.append(subprocess.check_output([str(executable)]).splitlines())
        assert len(results[0]) == count * 2 and len(set(results[0])) == count * 2, "Each aircraft/view must have a unique identity"
        assert results[0] == results[1], "Portable filenames differ under fast math; do not hash host-evaluated model constants"
    print(f"portable mesh identity: {count * 2} exact production identities match strict/fast builds; cache and invalid-ID guards passed")


if __name__ == "__main__":
    main()
