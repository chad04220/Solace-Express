# Rejected adaptive-shadow submission experiment

**Diagnostic evidence only. The production rollout was rejected and fully reverted.**
Fixed 96 m near coverage improved close detail but lost acceptable house/pump
shadows farther away. A 160 m compromise still weakened the 180 m house case;
256 m retained coverage but left close eave-edge artifacts. The accepted build
retains its original quality-base cascade radii and still has residual fine
close-edge shadow artifacts. Arithmetic and sanitizer passes did not establish
a visual win.

This isolated CPU audit exercised the rejected controller with each frozen
Capital, Port Verde and airport camera held stationary for 600 frames at 60 Hz.
The controller itself selected the committed radius; no radius was forced.
All settled cases had zero speed, Stable phase and full fade. The recorded
AGL uses the actual world's ground/sea-level convention. Medium's base is
420 m and High's base is 520 m. No new camera coordinates were generated.

| Camera | Quality | AGL (m) | Settled radius (m) | Fixed near triangles | Rejected near triangles | Unchanged far triangles | Both-dirty shadow change |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| capital_overview | Medium | 244.318 | 420 | 116,903 | 116,903 | 1,649,994 | +0.00% |
| capital_street | Medium | 4.219 | 96 | 90,860 | 9,526 | 1,608,305 | -4.79% |
| capital_tower_close | Medium | 12.745 | 96 | 87,842 | 11,476 | 1,601,845 | -4.52% |
| port_verde_overview | Medium | 220.933 | 420 | 99,779 | 99,779 | 908,112 | +0.00% |
| port_verde_street | Medium | 2.647 | 96 | 85,070 | 10,036 | 1,203,252 | -5.82% |
| port_verde_tower_close | Medium | 7.080 | 96 | 85,848 | 10,956 | 1,159,403 | -6.01% |
| airport_capital | Medium | 190.000 | 420 | 1,020 | 1,020 | 636,404 | +0.00% |
| airport_ramp_close | Medium | 8.600 | 96 | 594,875 | 57,279 | 1,177,910 | -30.32% |
| capital_overview | High | 244.318 | 520 | 181,323 | 181,323 | 2,409,710 | +0.00% |
| capital_street | High | 4.219 | 96 | 131,205 | 9,526 | 2,333,674 | -4.94% |
| capital_tower_close | High | 12.745 | 96 | 130,934 | 11,476 | 2,383,574 | -4.75% |
| port_verde_overview | High | 220.933 | 520 | 158,985 | 158,985 | 1,234,612 | +0.00% |
| port_verde_street | High | 2.647 | 96 | 121,924 | 10,036 | 1,542,340 | -6.72% |
| port_verde_tower_close | High | 7.080 | 96 | 125,802 | 10,956 | 1,504,910 | -7.04% |
| airport_capital | High | 190.000 | 520 | 2,312 | 2,312 | 967,356 | +0.00% |
| airport_ramp_close | High | 8.600 | 96 | 821,849 | 57,279 | 1,750,684 | -29.72% |

Low-view near refreshes alone submit 86.9–90.4% fewer triangles on Medium and
91.2–93.0% fewer on High. Because far geometry is unchanged, the combined
both-dirty reduction is only 4.5–7.0% in city close views and roughly 30% on
the airport ramp. Every view, hero and far-shadow count matches the fixed-policy
CSV exactly. Overview cameras remain at their quality base and change no counts.

These are **per-refresh submitted triangles**, not frame timings or steady-state
cost. The existing movement threshold is 0.12R: 11.52 m at 96 m versus 50.4 m
at Medium's 420 m and 62.4 m at High's 520 m. Smaller coverage can refresh more
often. Sun, camera direction, streaming and vehicle changes also affect cadence.
No RTX 3070/FPS claim, accepted optimization, or inferred aggregate GPU saving
follows from this rejected experiment.

## Reproducibility

- [Full rejected CSV](entity-submissions-rejected-adaptive96.csv), including groups, draws, fade bounds and AGL.
- [Experimental source snapshot hashes](entity-submissions-rejected-adaptive96-sources.sha256).
- [Unchanged frozen cameras](entity-submission-views.txt).
- [Accepted fixed-policy results](ENTITY_SUBMISSIONS.md).
- [Disabled prototype and reason for rejection](../../../tools/validation/experiments/adaptive-near-shadow/README.md).

The numeric run includes the finite instantaneous-speed hardening; controller
SHA-256 is `122e736d3ff58d2a15d4c16a1fcba1949af6f2b6fa98a9777012073f2bca90b6`.
Its full CPU/source-contract test separately passed 184,023 ASan/UBSan checks
with leak detection disabled because LeakSanitizer cannot run under this
environment's ptrace. Those are rejected-prototype checks, not additional
accepted-game CTest coverage.

The read-only probe's optional `settled` argument enables this experiment.
Compile with the disabled prototype directory as an explicit include path to
make the controller available. Without that header, the optional mode fails
closed. The default probe path retains fixed coverage and was verified to
reproduce the existing fixed-policy CSV byte-for-byte. Neither world cache
was regenerated or modified.
