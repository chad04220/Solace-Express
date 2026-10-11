"""Exercise portable mesh source identities without modifying the checkout."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
cmake = sys.argv[1] if len(sys.argv) > 1 else "cmake"
checks = 0

def check(value, message):
    global checks
    assert value, message
    checks += 1

def generate(source, destination):
    header = destination / "aircraft_geometry_source.h"
    subprocess.run([cmake, f"-DSRC_DIR={source}", f"-DOUT={header}", "-P",
                    str(source / "tools/generate_aircraft_geometry_source.cmake")], check=True)
    data = json.loads((destination / "aircraft_geometry_source.json").read_text())
    cpu = "solace-aircraft-cpu-source-v1\n"
    full = "solace-aircraft-build-source-v1\n"
    for item in data["files"]:
        actual = hashlib.sha256((source / item["path"]).read_bytes()).hexdigest()
        check(actual == item["sha256"], "source hash mismatch")
        record = f'{item["path"]}:{actual}\n'
        if item["kind"] in ("cpu", "producer"):
            cpu += record
        full += record
    check(hashlib.sha256(cpu.encode()).hexdigest() == data["geometry_digest"], "CPU digest")
    check(hashlib.sha256(full.encode()).hexdigest() == data["digest"], "build digest")
    check(data["digest"] in header.read_text(), "compiled build digest")
    check(data["geometry_digest"] in header.read_text(), "compiled CPU digest")
    return data

with tempfile.TemporaryDirectory(prefix="solace-geometry-source-") as temp:
    temp = Path(temp)
    source = temp / "source"
    (source / "tools").mkdir(parents=True)
    shutil.copytree(root / "src", source / "src")
    for name in ["aircraft_geometry_inputs.cmake", "generate_aircraft_geometry_source.cmake"]:
        shutil.copy2(root / "tools" / name, source / "tools" / name)
    if (root / "tools/mesh_assets").exists():
        shutil.copytree(root / "tools/mesh_assets", source / "tools/mesh_assets")
    first = generate(source, temp / "one")
    second = generate(source, temp / "two")
    check(first == second, "build-directory independent")
    (source / "README.md").write_text("unrelated documentation\n")
    check(first == generate(source, temp / "one"), "unrelated documentation stable")
    shader = source / "src/shaders/ui_vs.glsl"
    if not shader.exists():
        shader = next((source / "src/shaders").glob("*.glsl"))
    with shader.open("a") as out:
        out.write("\n// test source provenance edit\n")
    changed_shader = generate(source, temp / "one")
    check(first["geometry_digest"] == changed_shader["geometry_digest"], "raw shader excluded from CPU key")
    check(first["digest"] != changed_shader["digest"], "raw shader changes producer provenance")
    with (source / "src/common.h").open("a") as out:
        out.write("\n// test CPU provenance edit\n")
    changed_cpu = generate(source, temp / "one")
    check(changed_cpu["geometry_digest"] != changed_shader["geometry_digest"], "CPU mutation invalidates")
    check(changed_cpu["digest"] != changed_shader["digest"], "CPU mutation changes build provenance")
    producer = source / "tools/mesh_assets/source_test_fixture.cpp"
    producer.parent.mkdir(exist_ok=True)
    producer.write_text("// producer implementation\n")
    changed_producer = generate(source, temp / "one")
    check(changed_producer["geometry_digest"] != changed_cpu["geometry_digest"], "canonical producer invalidates runtime key")
    check(changed_producer["digest"] != changed_cpu["digest"], "producer mutation invalidates provenance")
    (source / "src/aircraft_mesh_source_test_fixture.h").write_text("// future geometry dependency\n")
    changed_header = generate(source, temp / "one")
    check(changed_header["geometry_digest"] != changed_producer["geometry_digest"], "new geometry header discovered")
print(f"aircraft geometry source: {checks} checks passed")
