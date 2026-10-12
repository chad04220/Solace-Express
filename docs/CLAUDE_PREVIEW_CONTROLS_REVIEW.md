# Research equipment and showroom preview controls

This local follow-up on `codex/military-career-review-20261012` builds on the ordered military progression commit `2f05a0a`. It implements the owner's subsequent research loadout and preview-control requests. No push, merge, or release is authorized by this work.

## Research terminal

- XR-40 Free Roam equipment now occupies the existing violet right rail, with separate forward-weapon, payload and mixed-wave selectors. Full specifications and handling notes scroll below it; no equipment card covers the aircraft.
- Stable focus IDs survive cycling and scrolling. Invisible or partially clipped selectors cannot activate. Focusing a hidden selector scrolls it fully into view before a later activation.
- All research aircraft have Rotate left/right, Zoom out/in and Reset in a dedicated strip below the aircraft stage. The native camera uses the same shortened stage layout, keeping the aircraft above these controls.
- Keyboard Up/Down walks every registered control in order; controller D-pad remains spatial. Keyboard Left/Right walks the focused preview strip, and keeps its original insertion-site shortcut elsewhere. Controller Start launches independently of focused-control activation; A/Enter activates the selected control.
- Preview rotation, zoom and equipment changes never launch, save, mutate career records or affect active gameplay. Existing aircraft, test-card, site, weather and time selections keep their behavior.
- The native XR-40 model now uses the selected forward/payload hardware in the terminal. Free Roam opens the bay and deploys the forward hardware for visual inspection while weapons remain safe. Test-card/warm-up previews stay closed. Ammunition, enemy behavior and kit capabilities are unchanged.
- At compact/high-scale layouts, the four airframes use a two-by-two tile register. This fixes an existing 1280×720/140% layout that left no test-card rows visible, preserving actual access to every card instead of relying on hidden state or offscreen hit targets.

## Career Hangar and Free Flight showroom

Both use one native-style preview strip: Rotate left/right, Zoom out/in and Reset. Existing role or cruise/range information remains in the header. Viewing controls are separate from purchases and departure actions.

The showroom rotates the parked aircraft about world-up after its existing parking pitch, preserving tyre contact including taildraggers. Zoom changes the lens at a fixed safe camera location rather than moving through the room. Each selected airframe/context gets a sensible default view; military/research/flight camera state remains separate. Classified aircraft continue using the existing material-free silhouette pass.

## Validation contract

- `tests/military_gameplay_test.cpp` exercises the complete research terminal at 1920×1080/100%, 1280×720/140%, 960×540/100% and 800×600/140%. Coverage includes genuine keyboard reachability from no focus, D-pad graph reachability, all selectors and preview controls across input devices, clipping/scrolling, all aircraft/test-card transitions, preview hardware identity and exact career-save isolation.
- `tests/hangar_preview_controls_test.cpp` exercises career and Free Flight controls, every catalog aircraft, ten viewport/scale combinations, 24 yaw angles and three zoom settings. It checks real room-triangle sightlines, projected model landmarks, tyre contact, classified privacy and unchanged serialized career data.
- `tests/ui_review_harness.cpp` renders the unmodified production scene. New fixtures include `research40_kinetic_emp_underside`, `research40_heavy_penetrator_underside`, `research40_testcard`, and optional `_focus`/`_scroll` variants. `hangarN_rotated`, `hangarN_zoomed` and equivalent Free Flight suffixes demonstrate native viewer state.

Native previews, final logs and their checksums are retained in `docs/preview-controls-review/`. Human aiming, physical-controller feel and native Windows/GPU performance still require owner testing. Software-rendered screenshots demonstrate layout and real renderer behavior, not hardware performance.

## Verified results (2026-10-12)

- All 121 configured CTest suites pass, including the new showroom-control suite and the optional GPU/GLSL suites. The aggregate uses the repository's `FLIGHT_QUICK=1` representative flight-model coverage.
- Research/military gameplay: 622 assertions, 0 failures. Showroom controls: 6,245 assertions, 0 failures.
- The final compact callout text-fit refinement was rebuilt and both affected Game-layer suites rerun: identical 622/6,245 passing counts.
- ASan/UBSan: both affected suites and their world fixture pass. Both Game-layer suites also reran against the final text-fit revision without sanitizer reports.
- Final Windows x86-64 cross-build linked successfully; SHA-256: `6f5da0f30944d5d93891faeaf36772df0e0741f51fa1d751afe2fd1015583886`. The executable remains outside source control; it was not run on native Windows.
- Full production renderer captures at 1280×720/100% and 140% were visually inspected. The normal-scale final text-refinement recapture is byte-identical to the delivered research preview. Compact callouts occupy separate half-stage lanes, all four aircraft tiles and all five XR-40 test-card rows remain visible, and the focused equipment control stays in view.
- Independent source and pixel audit found no remaining blocker after the keyboard-reachability, same-frame content, compact-card access and compact-callout fixes.

No new weapon capability, combat balance, aircraft ownership, save record, or research progress behavior was introduced by this UI follow-up.
