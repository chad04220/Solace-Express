# Larkspur L4 and Atlas A180 flight decks

Stable aircraft indices 13 and 14 extend the existing fleet. Existing aircraft indices 0–12 are preserved. Both interiors use the production renderer, existing live instrument atlases and the same flight-input bindings as the rest of the fleet.

## Larkspur L4

- Authored four-seat olive/ivory utility-tourer cabin with staggered analog instrument clusters, an offset engine/fuel tower and shared live heading/flap/gear/fuel status.
- Compact linked floor sticks have 0.24 rad of pitch/roll travel; their tops stay below the instrument sightlines. Four moving rudder/toe-brake pedals sit ahead of the controls.
- A narrow pedestal supports the visible throttle/mixture bank behind the status display. Pushing throttle forward moves its knob toward the panel. The two rear seats have separate pans, backs, headrests, lap belts and floor-supported rails.
- The fifteen actual instrument areas are included in gaze-based focus zoom.

## Atlas A180

- Two-crew charcoal/blueglass flight deck with inward-canted primary/navigation bridges, a shared engine display and status glass, a supported inclined navigation tablet, and a live overhead systems/status bank.
- Floor-supported linked yokes, four animated pedals and paired thrust levers retain ordinary aircraft controls. The thrust pedestal lies between empty knee wells; the side consoles are backed by floor-to-wall cabinets.
- Transport windshield and side-pane openings match the exterior glazing. A fitted rear partition closes the flight deck, with a separate central door.
- Eight live display regions, including the inclined navigation and overhead faces, have matching focus-zoom targets.

## Automated checks

- `cockpit_layout_test`: 1,288 layout, deterministic fit, stable index, display-facing and focus checks across all 15 flyable aircraft.
- `conventional_cockpit_layout_test.py`: 3,080 conservative display/skin and forward-sightline checks. New primary faces have at least 78 mm (Larkspur) / 479 mm (Atlas) outer-skin separation.
- `new_aircraft_cockpit_contract_test.py`: 354,433 sampled geometric checks. Each crew station is tested at 81 combined pitch/roll states against its live instrument sightlines. Minimum sampled control/ray clearance is 199 mm for Larkspur and 64 mm for Atlas. The narrow shared status glass is beside the engine bank, clear of the navigation tablet from both pilots by 153 mm. The Atlas thrust sweep now also clears the inclined navigation housing by at least 58 mm for shafts and 46 mm for handle bounds, with 13 mm minimum sampled live-face-ray clearance. The Atlas navigation glass clears its pedestal by 73 mm; the Larkspur power-control sightline clears its status housing by 128 mm through full throttle travel.
- `atlas_windshield_contract_test.py`: 144,000 sampled shell checks from both crew seats, yaw 0/±8°, level through 4° down plus the actual normal-view pitch of −0.13 rad. The shared exterior/interior mask has no blocked sampled ray (minimum 13.8 mm shell clearance).
- Full assembled/pruned GLSL coverage and per-aircraft isolation are tracked by integration validation. Focused native captures compile and exercise every changed cockpit shader path.

The sampled contracts are conservative geometry checks, not a substitute for visual inspection, human-factors certification or hardware performance evidence. Native 1920×1080 Larkspur normal/down/right/overhead/aft and full input/throttle endpoints have been inspected. The raised wing root clears the former forward obstruction, and the relocated power bank remains visible throughout travel. Revised Atlas and broader fleet visual review are tracked separately with the aircraft preview evidence.

See [skin/face-fit evidence](validation/new-aircraft-cockpit-skin-fit.json) and [control/display-ray evidence](validation/new-aircraft-cockpit-control-scan.json), and [shared front-glazing evidence](validation/atlas-windshield-contract.json).

## Full-fleet visual review

The frozen initial sweep includes 136 cockpit captures across all 15 aircraft. Each type was inspected for irregular edges/shading, gaps and clipping in normal, down, side, overhead and control-endpoint views; Larkspur also has an aft-cabin view. All 15 first-postrepair groups and focused follow-up comparisons were inspected. All 28 confirmed findings now have native before/after acceptance. Original full-size frames and hashes are retained.

Accepted corrections:

- Atlas thrust pivot moves 95 mm aft and the existing side consoles support both vents. Exact primary-bezel normals are scoped to Atlas/Meridian; Atlas's inclined tablet also has an exact side/back-bevel normal without changing its housing.
- Conventional front shoulder webbing follows the unchanged padded backs with a 7 mm field offset, retaining its 44 mm bands and existing lap belts/buckles.
- Larkspur's aft oval has a matching physical aperture and rounded trim. Its external glass formula and existing main-window pillar are preserved.
- Atlas deck and Pelican firewall rounding remove their verified jagged highlights. Kestrel/Bush interior wing-root junctions no longer expose shards through the upper glazing.
- Kestrel/Wren/Bush decks and the five verified Kestrel/Wren/Bush/Swift/Nightjar firewall contacts overlap the existing 60 mm shell by 20 mm, closing sub-lattice gaps while retaining the fixture tops and window masks. Kestrel's pilot brow has a narrow outboard return; its inboard half, height and depth are unchanged.
- Specter's exact inner-support normal removes repeated highlight facets. Its 27 unchanged 9 mm overhead capsules use a tightly bounded local mesh refinement, leaving the rest of the cabin at its existing resolution. Native crowns, roots and the overlap perimeter are continuous.
- Conservative static-cockpit winding repair changes only single-material triangles whose three coherent field normals strongly oppose the geometric face. Exact coincident opposite-winding pairs are excluded. Research 10/12, exterior slot 0 and moving parts are outside its scope. The first rebuilt pass repairs 2,242 triangles; both Starling opposite-face pairs survive with exactly the same vertex positions/materials. Native inner-trim checks pass, and renderer exterior controls are pixel-exact to the frozen initial frames.

`cockpit_surface_contact_test.py` passes 25,317 source-bound contact, aperture, return-shape and normal-gradient checks. The conventional cockpit contract passes 3,080 checks, including both Bushmaster low-brow dispatch branches. `mesh_orientation_test` covers valid back faces, explicit two-sided pairs, mixed-material/ambiguous exclusions, invalid input, unchanged attributes/index partitions and the original Swift winding witness; it also passes with fast-math enabled.

The Specter refinement uses 56 core plus 170 overlap cells and 960,081 local samples. It adds 4,022 vertices and 6,452 triangles (206,128 bytes of final geometry), with 39,024,536 bytes of peak tracked CPU working vectors. That counter excludes output geometry and GPU staging and is not total process RSS.

Minor rendering details remain explicit: Bush has a small dark, continuous contact crease from mesh/raster faceting; Wren retains an isolated verified vent highlight; Mantis has 2–3 pixel continuous display-bevel raster/specular steps. Independent review found no remaining hole, clipping or malformed contact in those details, so no further shape change was made. Wraith's reviewed cockpit and hover-control gate remained clear.

See [the full-fleet visual audit](validation/fleet-cockpit-visual-review.json), [first-postrepair native hashes/counts](validation/fleet-cockpit-postfix-review.json), [final focused acceptance and residual details](validation/fleet-cockpit-round2-review.json), [surface-contact evidence](validation/cockpit-surface-contact.json) and [initial winding scope/counts](validation/fleet-cockpit-winding-baseline.json). Flight and hardware-performance gates remain separately tracked.

## Final integrated visual signoff — 2026-10-10

All 15 aircraft and all 136 standard final cockpit frames were reviewed after the shared CPU build-interface refactor. Normal, down, side, overhead and full control/throttle endpoints retain the accepted fixes; Larkspur also retains its inspected aft cabin. No new confirmed cockpit defect was found. The three minor rendering details above remain disclosed. All nine Wraith cockpit frames are pixel-exact to the frozen initial set; Specter’s overhead is pixel-exact to its accepted local-refinement capture. Other comparisons distinguish exact repeats from visual retention against older, pre-contact-fix captures.

The final source-keyed cockpit caches contain 1,849 conservative winding repairs, with both Starling opposite-face pairs preserved and dedicated Specter/Wraith cabins excluded. This differs from the historical first-postrepair total above because the later contact fixes changed the extracted mesh. [Per-aircraft counts and actual bake provenance](validation/fleet-cockpit-final-winding.json) distinguish fresh bakes from reused caches.

The frozen visual binary is `a408baffd161bb610d729b6b6de254e1efc70faf64c8a632ef9bd59830ff0b41`. See [all final cockpit frames and acceptance](validation/fleet-cockpit-integrated-review.json), [the exact visual-snapshot source hashes](validation/fleet-cockpit-final-source.json), and [the separate eight-fixture CPU-refactor parity](validation/aircraft-build-refactor/native-parity.json). The full renderer sweep contains 283 native cockpit/exterior images; that coverage is distinct from this 136-frame cockpit signoff.

The later guidance-only binary, `5ed3edea36ad257b27ea9f4901ffef2cd55bb2845db6b504d9e269d8733f9ca3`, passes a separate 3/3 fixed-state native parity bridge: Atlas loaded front, Atlas normal cockpit and Wraith weapon-bay underside. Pixels, geometry/topology, camera metadata and the applicable special-state frame metadata are exact. Only `src/aircraft.cpp` and `src/aircraft.h` differ from the visual snapshot; all 286 final live build inputs match the rebuilt capture provenance. See [the exact bridge result](validation/fleet-cockpit-guidance-bridge.json), [final source hashes](validation/fleet-cockpit-guidance-source.json) and [clean header-consumer rebuild evidence](validation/fleet-cockpit-guidance-rebuild.json). The 15-aircraft sweep was not rerendered on this later binary. These software-rendered captures demonstrate geometry/material retention, not native GPU performance.
