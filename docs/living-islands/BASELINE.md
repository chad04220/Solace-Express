# Living islands review baseline

- Repository: chad04220/Solace-Express
- Default branch at task start: claude/compassionate-davinci-4cfo20
- Exact upstream commit: 52317bb1f74a5ee9e66e063e2f17d5be786955a0
- Latest published release at task start: v3.41.0; this proposal uses the newer repository head requested by the user.
- Integrated newer upstream commit: b46ed9050197076e2cb7a3d8e0e2a0cadb8f0450 (component breakup), merged without environment conflicts.
- Integrated current upstream commit: 994dbaf (per-aircraft/per-scenery-class shader builds, cloud wake and G-force lens).
- Working branch: codex/living-islands-review-20261010

## Non-negotiable invariant

Every existing runway retains its position, heading, elevation, length, width,
surface type, threshold positions and physical ground. The summit of Mount Kaleo
is the only terrain-shape exception: a bounded crater within a 600 m audit disk,
well clear of runway and approach protections. Environment work must not
alter aircraft behavior, career progression or the separately developed autopilot.

## Hardware target

Native 1920×1080 on RTX 3070, targeting at least 60 FPS. CPU/geometry budgets and
software-renderer checks are useful evidence but do not certify that hardware
frame-rate target. Exact native-GPU testing remains required.

## Review state

Work in progress. No final validation claims or remote publication yet.

## Subsequent upstream integration

On 2026-10-10 the asset checkpoint a13d079 merged upstream 9dde57ebcde738acb73bd5f310b00e1dd56c3440 cleanly as6952bd7. Its aircraft breakup, gameplay impact crater shape/ejecta and wreck rendering are preserved; the immutable runway preservation baseline remains52317bb. Actual v4 asset images were frozen before this merge and retain exact binary/source manifests. Final merged-source checks are separate.

## Final upstream integration

Local asset checkpoint `131d2db` merged latest verified upstream `18f617869a207f0085e3efc5b4c7d0e94defbc17` cleanly as `4f2ed6565b7f02b545075f970b52484557c1f3f4`. This contains the published v3.42.0 commit `e34f71dfe1cfac425896f0f171b5206c090ad2c8`, plus subsequent Windows stale-log cleanup and research screenshot timing fixes. No terrain, placement, mesh or shader source changed in that upstream range. The immutable preservation baseline remains `52317bb1f74a5ee9e66e063e2f17d5be786955a0`.

The experimental adaptive near-shadow controller was reverted byte-for-byte from production after matched renders exposed lost intermediate-distance house/pump shadows. Its disabled proposal is retained only for reproducibility; original cascade coverage remains enabled.
