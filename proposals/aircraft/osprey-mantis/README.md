# Proposed aircraft for Claude review: Osprey C6 + XR-17 Mantis

Review-only source and preview package. Neither aircraft is registered in the production game by this change. Claude decides whether and how to integrate these potential new aircraft.

## Quick visual review

| Aircraft | Exterior | Unique interior |
| --- | --- | --- |
| Osprey C6 | [Contact sheet](Osprey-C6/Osprey-C6-preview.png) | [Coastal charter cabin](Osprey-C6/interior/Osprey-interior-contact.png) |
| XR-17 Mantis | [Front quarter](XR-17-Mantis/01_front_quarter.png), [open bay](XR-17-Mantis/03_bay_open.png) | [Graphite/amber research cockpit](XR-17-Mantis/cockpit_contact.png) |

Start with [the package guide](START-HERE.md), [validation summary](VALIDATION-SUMMARY.md), and each aircraft's AUTHORING / FIXES / interior documentation. PNGs are software-rendered reference previews, not gameplay or physical-GPU screenshots.

## Bases and scope

- Publication branch: `codex/osprey-mantis-aircraft-proposal`.
- Publication parent: `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6`, the fetched repository default `claude/compassionate-davinci-4cfo20` on 2026-10-05.
- Original authoring, compatibility and full-game test baseline: `a2987713ceeb33e3a97560d0c6b85efaf0e52af8`. Included historical build/flight/CTest logs refer to that baseline, not the newer publication parent.
- This commit adds only `proposals/aircraft/osprey-mantis/`. Production registries, aircraft, renderer, gameplay, workflows and existing proposals are untouched.
- No archives, executables, build products, caches or credentials are included. PNG previews are intentionally included alongside ordinary source/data files.

## Fresh publication checks

The delivered root and nested SHA-256 manifests were verified before copying. Fresh checks use an isolated copy so generated results cannot silently replace historical evidence:

1. Osprey `validate.py`: all 23 parsed-row analytical checks pass.
2. Osprey `interior/verification/check_source.py`: all 7 static-interior source checks pass.
3. Mantis `validate_pack.py`: analytical/state/row-drift assertions, exact-GLSL geometry regressions, containment and retained-control sweep assertions pass with software Mesa.
4. JSON parsing, Python syntax, PNG decoding, recursive manifest integrity and allowed file-type checks pass via `verify_publication.py`.
5. Diff scope checked; source/document whitespace checks pass. Historical logs retain original trailing spaces. Common credential patterns scanned with no matches.

The original nested Mantis manifest omits its contact sheet; the complete publication root manifest covers every delivered file, including that image. Historical nested manifests and logs are preserved. Fresh output is in `publication-checks/`.

These checks do not rerun the full game suite against db08f7a, test production integration, or prove continuous-pose/baked-mesh correctness. Repository CI may test the unchanged game on the publication commit; that is separate from integrating either proposed aircraft.

## Reproduction

Run `python3 verify_publication.py` from this directory (Pillow required for PNG checks). The aircraft validators require Python/NumPy; the Mantis exact-shader probes also require Pillow and system Mesa EGL/OpenGL. Run write-producing validators on a temporary copy of this directory:

- `python3 Osprey-C6/validate.py`
- `python3 Osprey-C6/interior/verification/check_source.py`
- `OPENBLAS_NUM_THREADS=1 PYTHONDONTWRITEBYTECODE=1 MESA_SHADER_CACHE_DIR=/tmp/solace-proposal-mesa python3 XR-17-Mantis/validate_pack.py`

Consult the included verification READMEs for historical game fixtures; they target a298771 and must be adapted and rerun against the actual integration branch.

## Requested review / integration boundaries

Review the repaired symmetry, flap/gear clearances, Mantis hinge/closure work and distinct interiors. Preserve existing aircraft, save IDs, career/research separation, ongoing Swift/Nightjar, fleet-gear and XR-9 work. Resolve all roster indices against the actual integration target; package index examples are baseline-specific.

Osprey needs its documented static-cabin hook. Mantis needs the target renderer's bake/rig contract, explicit research registration, live feeds and weapon gameplay. Camera panes currently show static reference markings. Final integrated rendering, flight/game regression tests, Windows build and hardware review remain Claude's integration/release work. This proposal is not a release request.
