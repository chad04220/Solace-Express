#!/usr/bin/env python3
"""Create an isolated, pinned-source laboratory. Never modifies the production checkout."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = "db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6"
EXPECTED = {'src/aircraft.cpp': 'f96d7e4dfaedacc0b7b643692d54900c5e84b757', 'src/models.cpp': 'd67f5bca6e325e37f8ab379acd9fcfa09d28225a', 'src/shaders/plane_sdf.glsl': '6dc7d51ab1ff7f583ae5fe86a85a4db1ee85e0c6', 'src/shaders/plane_material.glsl': '242a67f7c914996dbff41d45fa1a43d4c4773401', 'src/shaders/wraith_sdf.glsl': 'bf389fccc4c6decdb18b056b29ebcb82ff766b40'}


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Pinned source anchor must occur once: {old[:90]}")
    return text.replace(old, new, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=HERE.parents[1])
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--career-insertion", action="store_true", help="Rehearse the guide's production indices and automatic career count.")
    args = ap.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise SystemExit("Use an isolated output outside the production source tree.")
    for name, expected in EXPECTED.items():
        data = (source / name).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != expected:
            raise SystemExit(f"Source identity differs from {BASE}: {name}; rebase deliberately.")
    if output.exists():
        raise SystemExit("Choose a fresh output directory; existing directories are not overwritten.")
    output.mkdir(parents=True)
    for name in ("src", "tests", "tools"):
        shutil.copytree(source / name, output / name)
    for name in ("CMakeLists.txt",):
        shutil.copyfile(source / name, output / name)
    if (source / "assets").exists():
        # Tests may read existing voice assets; this is read-only and does not duplicate them.
        (output / "assets").symlink_to(source / "assets", target_is_directory=True)
    else:
        (output / "assets").mkdir()
    shutil.copytree(HERE, output / "prototypes/aircraft")
    for name, additions in (
        ("src/aircraft.cpp", "#include \"../prototypes/aircraft/swift_spec.inc\"\n#include \"../prototypes/aircraft/nightjar_spec.inc\"\n"),
        ("src/models.cpp", "#include \"../prototypes/aircraft/swift_model.inc\"\n#include \"../prototypes/aircraft/nightjar_model.inc\"\n"),
    ):
        p = output / name
        if args.career_insertion:
            anchor = '  {"xr9",' if name.endswith("aircraft.cpp") else "  // ---------------------------------------------------------------- XR-9 Specter"
            text = once(p.read_text(), anchor, additions + anchor)
        else:
            text = once(p.read_text(), "};\n// clang-format on", additions + "};\n// clang-format on")
        if name.endswith("aircraft.cpp") and not args.career_insertion:
            text = once(text,
                        "const int kNumAircraft = sizeof(kAircraft) / sizeof(kAircraft[0]) - 2;",
                        "const int kNumAircraft = 7; // laboratory candidates are not in career or save files")
        p.write_text(text)
    if args.career_insertion:
        p = output / "src/aircraft.h"
        text = once(p.read_text(), "kResearchJet = 7", "kResearchJet = 9")
        text = once(text, "kWraith = 8", "kWraith = 10")
        p.write_text(text)
    cmake = output / "CMakeLists.txt"
    cmake.write_text(cmake.read_text() + "\n" + (HERE / "targets.cmake").read_text())
    if args.career_insertion:
        with cmake.open("a") as f:
            f.write("\ntarget_compile_definitions(aircraft_candidate_test PRIVATE CANDIDATE_CAREER_INSERTION)\n")
            f.write("if(TARGET aircraft_candidate_render)\n  target_compile_definitions(aircraft_candidate_render PRIVATE CANDIDATE_CAREER_INSERTION)\nendif()\n")
    manifest = {"base": BASE, "source": str(source), "stage": str(output),
                "candidate_slots": {"swift_s6": 7 if args.career_insertion else 9, "xr14_nightjar": 8 if args.career_insertion else 10},
                "career_insertion_rehearsal": args.career_insertion,
                "production_career_count": 9 if args.career_insertion else 7, "source_checkout_modified_by_preparer": False,
                "staged_changes": ["insert two specs before xr9" if args.career_insertion else "append two specs",
                                   "insert two models before xr9" if args.career_insertion else "append two models",
                                   "bump research indices; automatic career count 9" if args.career_insertion else "keep career count 7",
                                   "use the separate fleet-gear and XR-9 canopy changes", "add optional laboratory targets"]}
    (output / "candidate-stage.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
