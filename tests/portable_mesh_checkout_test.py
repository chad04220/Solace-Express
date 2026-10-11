"""Portable source stamps must survive a Windows-style Git checkout unchanged.

Uses an isolated index, never commits or changes the real checkout configuration.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def git(root, *args):
    return subprocess.run(
        ["git", "-c", "core.autocrlf=true", "-C", str(root), *args],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def main():
    checks = 0
    attributes = ROOT / ".gitattributes"
    assert attributes.is_file(), "Cross-platform geometry hashes require an LF checkout contract"
    # Representative current inputs and future files covered by the source globs.
    paths = [
        "src/aircraft_mesh.cpp", "src/aircraft_mesh_asset.h", "src/shaders/plane_sdf.glsl",
        "src/future/nested_model.h", "tools/aircraft_geometry_inputs.cmake",
        "tools/generate_aircraft_geometry_source.cmake", "tools/mesh_assets/export_aircraft_meshes.cpp",
        "tools/mesh_assets/future_producer.cpp", "CMakeLists.txt",
    ]
    with tempfile.TemporaryDirectory(prefix="solace-portable-checkout-") as name:
        root = Path(name)
        git(root, "init", "--quiet")
        shutil.copyfile(attributes, root / ".gitattributes")
        # Start with CRLF too: text=eol=lf must normalize both the Git index and
        # checkout, rather than only preserving files already written with LF.
        expected = b"first line\nsecond line\n"
        for i, relative in enumerate(paths):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(expected if i % 2 else expected.replace(b"\n", b"\r\n"))
        (root / "unrelated.txt").write_bytes(expected)
        git(root, "add", ".")
        for relative in paths + ["unrelated.txt"]:
            (root / relative).unlink()
        git(root, "checkout-index", "--all", "--force")
        for relative in paths:
            actual = (root / relative).read_bytes()
            assert actual == expected, f"Portable source changed bytes under autocrlf=true: {relative}"
            checks += 1
            indexed = git(root, "show", f":{relative}")
            assert indexed == expected, f"Portable source index was not canonical LF: {relative}"
            checks += 1
        # The fixture genuinely exercised autocrlf; attributes remain narrowly
        # scoped instead of overriding unrelated user documents globally.
        assert (root / "unrelated.txt").read_bytes() == expected.replace(b"\n", b"\r\n")
        checks += 1
    print(f"portable mesh checkout: {checks} checks passed")


if __name__ == "__main__":
    main()
