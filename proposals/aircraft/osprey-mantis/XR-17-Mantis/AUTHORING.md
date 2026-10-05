### XR-17 Mantis — Forward swept systems research demonstrator

Design brief: Mantis is a 16.0 m-long, 13.2 m-span, single-pilot twin-jet systems demonstrator with forward-swept wings and independently moving foreplanes. Its broad inspiration is the X-29/Su-47 family of forward-sweep experiments, but its short almond-shaped wings, titanium canards, single dorsal fin and sealed camera cockpit form an original silhouette. Graphite pressure skin, amber survey datums and exposed titanium control surfaces distinguish it from the Specter, Wraith and Nightjar. Its conventional thrust and paved-runway envelope suit medium/large island airports, while the recessed fictional stores and an internal bay provide game-animation integration points. It is an authoring candidate for the research selection, not a finished flight controller or a currently installed aircraft.

## Self-check (maximum design weight)

Mass = 6100 + 1800 + 0 + 90×0 + 90 = 7990 kg. g=9.81, rho=1.225.

| Check | Calculation/result | Status |
|---|---|---|
| Wing loading | 2612.73 N/m² | Below 4000 jet limit |
| Clean stall | 52.46 m/s | Analytical estimate |
| Full-flap stall | 44.54 m/s | Analytical estimate |
| Rotate | 50.0 m/s =1.123×vs0 | Pass analytical target |
| Approach | 58.0 m/s =1.302×vs0 | Pass analytical target |
| Cruise | 190 m/s =3.276×vref | Isolated max-weight harness223.8m/s achieved |
| Jet thrust/weight | 30000/78381.9 =0.3827 | Within 0.25–0.40 reference |
| Runway | 1350 m paved | Isolated max-weight liftoff666m; final in-game learning pending |
| Roll inertia | 42000 vs41765 kg m² reference | +0.56% |
| Pitch inertia | 92000 vs92045 kg m² reference | −0.05% |
| Yaw inertia | 134000 vs133810 kg m² reference | +0.14% |
| Fuselage | station end difference16m; max half-height1.05m | Matches spec |
| Span | 2×6.6=13.2m; rounded visual wings ~13.06m | Within10% |
| Wing area | proxy6.6×(3.15+1.35)=29.70m²; visual zero-plane sampled28.71m² | Within10% of30m²; visual sample is not lift prediction |
| Ground clearance | game gearHeight2.115m, deployed tyre bottom−2.115m | Matches exactly |
| Props | None | N/A |
| Career fit | ATP/480000/no rent; game range210km | Research presentation only; prevent accidental career exposure |

## Integration notes

Baseline inspected read-only: Solace-Express a298771. The existing code does NOT implement PART, partOn or rig helpers. This pack implements the provisional authoring contract and supplies a standalone software-GL adapter for preview only. It is not a two-row drop-in. The integrator must add model dispatch, part extraction/baking, rig runtime, materials, camera-feed dispatch, research selection and store state/lifecycle bindings.

Keep special=0. Never claim Wraith/Specter controllers or VTOL support. No new special enum is invented. Engine7 is next unused custom shape code in the inspected baseline; reserve a different free7+ value if other packs were integrated first. It must still use ordinary ENG_JET twin-engine propulsion; verify engine-code branches do not accidentally choose nose-engine sound/geometry.

Suggested combined-set ordering: existing career indices0–6, new conventional aircraft7, existing XR9 at8 and XR11 at9, Mantis at10. Set kResearchJet=8 and kWraith=9 after one career insertion, preserve their behavior. Current sizeof(kAircraft)-2 would incorrectly count a third research row. The integrator must explicitly separate career/research counts (career count8 in that ordering) and selection metadata. Otherwise leave <i> symbolic; do not make it a purchasable career craft just to avoid registry work.

### AircraftSpec and ModelDef rows

```cpp
// aircraft.cpp — authoring row only; research registry insertion is an integration task.
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"xr17", "XR-17 Mantis", "Forward-swept experimental systems test aircraft", ENG_JET, 2, 0, 0, 0.0f, 0.0f,
 6100.0f, 1800.0f, 0.0f, 0, 30.0f, 13.2f, 2.27f,
 0.25f, 5.0f, 1.55f, 0.60f, 0.026f, 0.012f, 0.070f, 0.78f,
 15000.0f, 0.0f, 50.0f, 58.0f, 190.0f, 210.0f, 1350.0f, false, false, true,
 42000.0f, 92000.0f, 134000.0f, 0.42f, 0.055f, 0.060f, LIC_ATP, 480000, 0,
 16.0f, 1.05f, -0.62f, 0.65f, 2, 0, vec3(0.065f,0.085f,0.105f), vec3(0.95f,0.43f,0.055f), 0},

// models.cpp — same index; engine 7 is next free at baseline a298771; resolve collisions at integration.
{ // Eight hull proxy stations, nose to tail. Actual custom hull is independent.
 {{-8.0f,.07f,.07f,.05f},{-6.3f,.44f,.44f,.05f},{-4.8f,.79f,.79f,.05f},{-3.9f,.98f,.98f,.05f},
  {-.3f,1.02f,1.02f,.04f},{2.65f,1.05f,1.05f,.04f},{6.1f,.40f,.40f,.20f},{8.0f,.07f,.07f,.28f}}, 1.0f,
 {6.6f,3.15f,1.35f,-2.48f,-.62f,.65f,0.0f,.10f}, // forward-swept wing aerodynamic proxy
 0,0.0f,0.0f,.52f,0,0, // no struts, winglets, slats, boots
 {2.75f,1.18f,1.18f,0.0f,-.15f,-4.19f,0.0f},0, // canard proxy: physics support requires validation
 {2.3f,1.84f,1.10f,.80f,.20f,4.30f},
 7,1.55f,.05f,.48f,1.73f,3.07f,0.0f,0.0f, // custom shape dispatch; conventional twin-jet physics
 4,.26f,0, // custom gear visual travel; no cargo pod
 0,0.0f,0.0f,0.0f,0.0f,0.0f, // no passenger windows
 vec3(0.0f,.65f,-3.35f),2, // sealed cockpit eye, glass instruments
 -4.95f,-4.45f,.45f,-2.7f }, // inert windshield proxy; do not cut generic windows in sealed custom hull

```

## Exact rig contract

See parts.json for machine-readable data and PARTS.txt for human-readable declarations. All coordinates are body-space metres; angles radians; every moving primitive uses only its matching inverse rig helper. STATIC parts0,1,2,40 have no geometry-state dependency. Rest evaluation keeps state=0, except visibility channels4–6=1. Individual bake selection ignores occupancy so every store is baked once. Runtime occupancy hides only the detached store draw, never re-bakes or changes stored geometry. For matching authored geometry, helpers return body-space REST coordinates rather than pivot-local coordinates.

Hinge forward transform: p'=o+R(normalize(axis),k·state+c)(p−o). Its inverse is the same rotation with negative angle. Slide forward: p'=p+normalize(axis)(k·state+c). Inverse subtracts that offset. STRETCH26–28 maps rest length0.25m to0.25+1.295·gear about each origin along−Y. Its inverse compresses only axial displacement by length/restLength. Scale is at least1, so evaluating the rest SDF under that inverse remains a conservative distance bound. Do not replace it by a >1 distance multiplier. Lower gear forks/wheels slide by1.295m, while telescope tops stay attached to the airframe.

Inputs: gPS.x gear, gPS.y flaps; gCtl.xyz pitch/roll/yaw, gCtl.w throttle. Standard ranges apply. gPS.z steering is intentionally not visually bound: the current contract has no specified parent hierarchy for a steering hub inside a sliding gear assembly. Tyres and fan faces are static within their carrier parts; no misleading unregistered spin animation is used. No variable nozzles, speed brake, opening pressure canopy or flexing wing is promised.

Eight custom channels are declared in parts.json. Channel0 opens bay doors,1/2 deploy independent recessed cradles,3 ejects the internal store;4/5/6 are occupied flags interpreted solely by runtime part visibility;7 reserved zero. The aircraft's SDF never contains a flying projectile: transition from baked store to a detached game object uses store_sockets.json. Prevent door closure while the internal store is in the opening; coordinate deployment before release. These are game interface rules, not implemented weapon logic.

## Geometry and materials

Geometry is time independent. Exact sdRoundCone/sdBox/sdRoundBox/sdCapsule/sdCylX/sdTorus primitives, min unions and max subtractions form finite closed volumes. Constant flattened wing/fin/canard coordinates multiply returned distance by the minimum singular value (0.12,0.09,0.16), preserving the lower-bound/Lipschitz property. A constant mirrored rigid wing frame uses a normalized direction to float precision. No non-distance plane or variable hand-written animation is returned. Part boundary tangencies/CSG edges need normal bake tolerances as with any SDF.

There are no zero-thickness decorative sheets. Smallest exterior slab is16mm total; interior details are18mm or thicker; minimum round-over is4mm; light capsules radius24mm. Some smooth wing edges converge naturally at the boundary, as all closed curved solids do. Bay and cockpit are real subtractions from the static pressure hull. Main wells cut both wing and fairing so the union does not refill them. Bay doors close against an integral flat-bottom fairing; gear uses matching flat fairings. The exact-shader cabin audit samples 6,000 boundary rays with a minimum 70.1 mm outer-shell margin; this is not a global proof. Control inserts and recesses share their parent solids and rounded hinge-pocket masks, with 4 mm intentional reveal. Gear and bay covers overlap their complete closed apertures. Main shutters mirror outward; the centre shutter slides aft, all retained on fixed runners.

Bounding sphere radius=max(16,13.2)×.55+1.5=10.3m. Conservative per-assembly enclosures: fuselage/nose sensors radius8.2m; wing/controls radius8.0m even at extremal hinge angles; rear fin/rudder radius7.2m; gear assemblies radius6.5m; doors/stores radius4.4m; cockpit radius4.8m. Maximum occupied store ejection stays inside these bounds. Detached stores are no longer aircraft parts. This is an analytic conservative envelope; a complete new renderer's per-part bounding-box extraction remains an integration test.

Existing materials use ids1,2,3,5,6,8,10–14,17,18,21,60,61,64,67. New ids are documented in materials.json:110 brushed titanium,111 amber markings,112 camera panes,113–119 bespoke research-cockpit trim. Material IDs110–119 also need collision checks at integration. All procedural patterning belongs in shading. Local preview shading is illustrative and simpler than the game's materials.

## Sealed cockpit and cameras

ModelDef.eye=(0,.65,−3.35). The cavity roof is.82m, giving17cm above the eye; pressure roof is higher. Proxy fuselage roof at that station ~1.03m. The generic glass-panel proxy at eye.z−.85 is−4.20m; the windshield proxy ends−4.45m ahead of it. No real windshield openings should be generated for this custom sealed aircraft. The forward wing upper root ~−.45m stays below eye.y−1.08=−.43m. Canards are external to the pilot footwell.

feed_cameras.json supplies three panes with centre, normal, up, half-size and explicit camera mounts. FeedMount screen centres are eye-relative, while all shape coordinates are body-relative. The shader material112 must sample the matching camera atlas after integration. Existing feedRigOf special0 resolves to no feed: add explicit aircraft registry selection without changing special. Local previews show opaque camera placeholders marked CAM/STATIC and illustrative REF instrument bars, not live camera scenery or telemetry. Cockpit panel/seat/stick/throttle/pedals are closed geometry; UI data/renderers are integration work.

## Test matrix / harness

After integrating at<i>: gav_<i>_120_10_0, gav_<i>_210_5_0, gav_<i>_60_35_0; ckv<i>_0_-8_11, ckv<i>_-60_-20_11, ckv<i>_0_-45_11. Use GAVOUT=1 for exterior. Harness names alone do not set all state variables: add a deterministic per-shot state injector.

Render each moving part alone at rest and extrema; all-parts views at both signs of pitch/roll/yaw, throttle0/1; gear0/1; flaps0/1; bay0/1; each rail0/1; store ejection0/1 with occupied1 then0. Inspect all three views from below for door/tyre clearance. Hinge/canard/aileron signs were reviewed against the adapter; final flight control response still requires game tests. Do not treat a still image or analytic self-check as CI success.

Repro local: python build_pack.py; python validate_pack.py; python render_preview.py --audit. The validator also runs audit_geometry.py against actual GLSL via float readback; geometry_audit.json documents its coverage. Uses Python/NumPy/Pillow, system libEGL/libGL and Mesa software; no binary or texture assets for the game. Preview adapter is generated from parts.json and this shader plus existing baseline primitives. Preview-only primitive compatibility functions are included in preview_primitives.glsl, copied from the inspected baseline. Isolated conventional dynamics verification was run in a temporary copy with this row inserted at index7: standard flight suite0 failures, plus pilot-added maximum7990kg supplement0 failures. At maximum weight measured liftoff666m, climb13.8m/s, cruise223.8m/s, stall51.2m/s and fuelRange165km. Comfortable route landed349s; autoland343s with1.0m/s touchdown. The210km range row is a design target, not achieved in this harness; use165km observed until the integrator learns/retunes it. No weapons mass, drag, CG, guidance or release physics was included; loadout mass must be assigned during integration. These physics checks do not validate custom rendering, bay/rig motion, cameras or research registry. CPU/GPU rendering and screenshots from the actual game remain pending. Run cmake --build build && ctest --test-dir build after integration, then the owner's GPU harness at1920×1080. The aerodynamic effect of a foreplane encoded in the horizontal-tail proxy must be validated; no stable forward-sweep flight-controller claim is made.

## Included and excluded

Included: exact authoring rows, Tier B GLSL,31-part registry, contract-readable declarations, material/feed/socket JSON, analytical checker, reproducible local renderer, eight presentation geometry previews, nine matched exterior audit views, five dedicated cockpit views, exact-shader regression evidence. Excluded: private full checkout, compiled binaries, shader cache, fabricated game test passes, repository changes, commits, pushes, real-world weapons design or advanced research flight controller.

## Geometry review

See FIXES.md for the corrected gaps, mirrored motion and regression coverage.

# Mantis cockpit integration

The cockpit is an original compact angular graphite/amber research station, inspired by the baseline XR-11's angular fighter-seat shell, bolsters, harness, framed instrument blade and fixture lighting, and the XR-9's framed side-camera bays and control banks. References inspected read-only: src/shaders/raytrace_wraith_cockpit.glsl lines 85–170 and src/shaders/raytrace_fs.glsl lines255–340 in Solace-Express a298771.

## Exact integration

This release already combines the reviewed exterior and cabin. build_interior.py regenerates only the complete partOn(40) block from its component definitions; part40_replacement.glsl is the matching standalone snippet. It preserves ids41–44, all rig definitions and the sealed hull cavity. All115 cabin components remain STATIC under id40; the six front switch stems, console keys and annunciators are fixed illustrative controls, not promised moving switches. No geometry uses time or undeclared channels.

Use feed_cameras.json and materials.json from this deliverable. Ids110–111 are unchanged;112 stays a camera-pane material with explicit static preview marking;113–119 add graphite, upholstery, amber diffusers, webbing, illustrative reference instruments, vent graphite and brushed aluminum. The shader function mantisCabinMaterial(m,p) is body-space material shading and is included by the standalone renderer; it must be explicitly registered in the game renderer if these material patterns are wanted there. This package does not install live camera or avionics runtime support.

Three visible feed surfaces, with body-space centers and half-sizes:
- front: (0,.410,-4.123), (.410,.177), inward normal+Z
- port: (-.547,.365,-3.51), (.370,.172), inward normal+X
- starboard: (.547,.365,-3.51), (.370,.172), inward normal-X

The JSON also records solid slab centers, 12mm half-depth and 6mm corner radius. Registration is on the inner front surface, not center of the24mm-thick slab. Eye-relative centers are derived from eye(0,.65,-3.35). Existing external mounts remain(0,.2,-8.05),(-1.05,.5,-3.48),(1.05,.5,-3.48). Final exterior integrator measured these mounts against exact full exterior and hull: nose122.094mm clearance, each side158.072mm. Feed atlas hookup and feedRigOf dispatch remain pending. Panes show an amber calibration reticle and CAM/STATIC labels; other screens show REF plus illustrative bars. These do not represent live telemetry or simulated camera scenery.

## Verification

cavity_clearance.json: all115 primitives are wholly inside the exact original rounded-box cavity. Its convexity means containing every primitive AABB corner proves containment for the entire primitive, including all capsule segments. Minimum conservative clearance is10.918071mm. Smallest modeled thickness is18mm (all details exceed8mm). Static joined subcomponents intentionally overlap to avoid seams.

control_sweep.json: retained controls evaluated at81 positions per declared state interval, with7,360 surface samples for each lever and13,126 for each pedal. Minimum unintended static clearance: pitch12mm, throttle14mm, each pedal12mm. Minimum sampled cavity clearance remains≥179.55mm. Matching fixed pivot sockets and pedal slider tracks intentionally overlap their moving assemblies at their mechanical bearing interfaces and are the only exemptions. This dense sample is not a proof of continuous swept collision clearance. Pitch and throttle use their actual declared axes and angles; pedals use actual declared travel. Controls otherwise unchanged.

All five final views rendered through actual GLSL, Mesa software EGL: cockpit_front.png, cockpit_left.png, cockpit_right.png, cockpit_down.png, cockpit_aft.png. Images were visually checked for frames, labels, seat/harness, consoles and control clearance. They are standalone geometry previews, not screenshots of the game. Render with python render_preview.py. Reproduce static checks with python build_interior.py and OPENBLAS_NUM_THREADS=1 python probe_control_sweep.py.

Regenerate with python build_pack.py; python build_interior.py; python validate_pack.py; python write_authoring.py; python render_preview.py --audit. The authoring generator retains the new feed registration and material definitions, and final previews include both exterior and cabin views.
