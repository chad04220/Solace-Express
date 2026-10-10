# Atlas exterior final review

Final native 1920×1080 production-renderer evidence passed on 2026-10-10. The main bays are swept beneath the wing and have a smooth 1.75 m forward ramp. The connected trailing-link mains preserve the deployed contact stations. The asymmetric doors, two-axis fold, fixed trunnions and sealed aft closure were checked across the complete motion.

- Independent geometry regression: 21,923,754 checks; dense moving-surface sweep: 401 poses × 163,200 samples, no fixed-material intrusion.
- Door clearance: tyre minimum 88.6 mm; trailing-link minimum 98.1 mm. Stowed aft margin 44.9 mm, roof 51 mm.
- Actual baked closed-door vertices remain at least 95.13 mm ahead of the wing trailing edge. Reconstructed poses match native GPU readback bounds within 0.00051 mm. The clipped fairing boundary stays within 0.30 mm extraction tolerance of the exact planform.
- Native 0–100% gear sequence was captured before the final leading-ramp refinement; final closed/half/full and wide views rechecked the refined outer shell. The cavity and mechanism did not change.
- The only aerodynamic geometry change is Atlas flap root 3.85→4.05 m, synchronized in shader, part bounds and force strips. Flight reruns are recorded separately.

## Material and density correction

A cancellation-sensitive comparison against the smooth-union result falsely classified distant nacelle/exhaust vertices as wing paint. The final local primitive-ownership predicate removes that error. The regression covers 74,537 cases including actual identical-position native vertex witnesses. The native false-paint count fell from 39,855 to zero, and static-body triangles fell from 743,436 to 444,102.

Final exterior cache: 18,986,904 bytes. Final submitted triangles: 885,508. Compared with the pre-underwing, already depth-corrected model, cache grows 3.01% and triangles 3.56%. Compared with the original pre-depth-fix model, cache grows 6.47% and triangles 14.70%. These are different baselines; the targeted 0.20 m Atlas door edge cap remains enabled.

## Remaining rendering caveat

The small bright door spot is confirmed as the existing shared environment-reflection approximation, not missing geometry, wrong-facing depth or emission. `shadeSurface` in `src/shaders/light_common.glsl` mirrors a below-horizon reflection with `abs(r.y)` before `skyColor`; the sun disc in `src/shaders/common.glsl` then creates an unrealistic underside highlight. Native G-buffer normals, material, emission and bright/dark pixel values match that calculation. Lighting was intentionally left unchanged.

The native captures use llvmpipe and establish appearance/geometry correctness, not RTX 3070 frame time. Hardware profiling remains necessary before making a GPU performance claim.

Machine-readable projection, cost baselines and spot-lighting results are alongside this report. Native image/cache paths in them refer to the separate review artifact bundle.
