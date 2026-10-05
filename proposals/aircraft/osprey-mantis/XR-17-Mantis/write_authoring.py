import pathlib,json
D=pathlib.Path(__file__).parent
feeds={'cockpit': 'sealed opaque pressure pod, no transparent canopy', 'eye_body': [0, 0.65, -3.35], 'panes': [{'slot': 0, 'center_body': [0, 0.41, -4.123], 'center_eye': [0, -0.24, -0.773], 'normal': [0, 0, 1], 'up': [0, 1, 0], 'half_size': [0.41, 0.177], 'camera_mount_body': [0, 0.2, -8.05], 'nose': True, 'solid_center_body': [0, 0.41, -4.135], 'solid_half_depth': 0.012, 'registration': 'center_body is the flat visible inner surface, not slab center; rounded corners have 6 mm inset.'}, {'slot': 1, 'center_body': [-0.547, 0.365, -3.51], 'center_eye': [-0.547, -0.285, -0.16], 'normal': [1, 0, 0], 'up': [0, 1, 0], 'half_size': [0.37, 0.172], 'camera_mount_body': [-1.05, 0.5, -3.48], 'nose': False, 'solid_center_body': [-0.559, 0.365, -3.51], 'solid_half_depth': 0.012, 'registration': 'center_body is the flat visible inner surface, not slab center; rounded corners have 6 mm inset.'}, {'slot': 2, 'center_body': [0.547, 0.365, -3.51], 'center_eye': [0.547, -0.285, -0.16], 'normal': [-1, 0, 0], 'up': [0, 1, 0], 'half_size': [0.37, 0.172], 'camera_mount_body': [1.05, 0.5, -3.48], 'nose': False, 'solid_center_body': [0.559, 0.365, -3.51], 'solid_half_depth': 0.012, 'registration': 'center_body is the flat visible inner surface, not slab center; rounded corners have 6 mm inset.'}], 'integration': 'feedRigOf(special=0) returns0 today: explicit per-aircraft registry dispatch required; do not reuse XR9 or XR11 special values', 'pane_material': 112, 'preview': 'Static amber calibration markings on opaque teal panes, clearly marked STATIC; reference instrument bars are illustrative only, not live telemetry.'}
(D/'feed_cameras.json').write_text(json.dumps(feeds,indent=2))
sockets={'coordinate_system':'body-space +x right,+y up,+z aft, metres','store_definition':'fictional low-detail game object visuals only; no real weapon engineering','slots':[{'id':'port_dart','part':34,'parent_rig':34,'rest_attachment':[ -1.08,-.72,-1.85],'forward':[0,0,-1],'up':[0,1,0],'deploy_channel':1,'occupied_channel':4,'release_rule':'deploy==1; copy current world transform to external projectile, then occupancy=0 atomically'},{'id':'starboard_dart','part':35,'parent_rig':35,'rest_attachment':[1.08,-.72,-1.85],'forward':[0,0,-1],'up':[0,1,0],'deploy_channel':2,'occupied_channel':5,'release_rule':'deploy==1; copy current world transform to external projectile, then occupancy=0 atomically'},{'id':'internal_store','part':36,'parent_rig':36,'rest_attachment':[0,-.70,1.6],'forward':[0,0,-1],'up':[0,1,0],'eject_channel':3,'occupied_channel':6,'release_rule':'require bay channel0==1; interpolate eject channel3 to1; copy world transform then occupancy=0 atomically'}],'transform_contract':'world=T_aircraft * forwardRig(part,state) * T_rest_attachment. Hinge forward: pivot+R(axis,angle)*(p-pivot). Slide forward: p+axis*distance. No projectile motion stays in aircraft SDF after release.','physics':'No guidance, damage, explosions, ammunition, inventory, release input or weapon mass model implemented.'}
(D/'store_sockets.json').write_text(json.dumps(sockets,indent=2))
mat=[{'id': 110, 'name': 'exposed titanium', 'albedo': [0.34, 0.39, 0.43], 'roughness': 0.35, 'metalness': 0.8, 'emission': 0, 'pattern': 'fine longitudinal brushed lines, shading only'}, {'id': 111, 'name': 'amber test datum', 'albedo': [0.95, 0.43, 0.055], 'roughness': 0.48, 'metalness': 0, 'emission': 0, 'pattern': 'solid amber, no animation'}, {'id': 112, 'name': 'camera pane', 'albedo': [0.035, 0.17, 0.2], 'roughness': 0.22, 'metalness': 0, 'emission': 0.35, 'pattern': 'Opaque camera placeholder with STATIC label and calibration reticle; no live camera or telemetry until renderer integration'}, {'id': 113, 'name': 'graphite instrument structure', 'albedo': [0.035, 0.045, 0.054], 'roughness': 0.48, 'metalness': 0, 'emission': 0, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 114, 'name': 'charcoal technical upholstery', 'albedo': [0.11, 0.125, 0.135], 'roughness': 0.48, 'metalness': 0, 'emission': 0, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 115, 'name': 'amber task-light diffuser', 'albedo': [1, 0.48, 0.08], 'roughness': 0.48, 'metalness': 0, 'emission': 0.6, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 116, 'name': 'ochre harness webbing', 'albedo': [0.41, 0.29, 0.13], 'roughness': 0.48, 'metalness': 0, 'emission': 0, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 117, 'name': 'static illustrative reference instruments', 'albedo': [0.015, 0.035, 0.042], 'roughness': 0.48, 'metalness': 0, 'emission': 0.25, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 118, 'name': 'vent louver graphite', 'albedo': [0.009, 0.014, 0.017], 'roughness': 0.48, 'metalness': 0, 'emission': 0, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}, {'id': 119, 'name': 'bezel brushed aluminum', 'albedo': [0.25, 0.29, 0.31], 'roughness': 0.48, 'metalness': 0.5, 'emission': 0, 'pattern': 'See materials_preview.glsl for static-only material shading. No animation.'}]
(D/'materials.json').write_text(json.dumps(mat,indent=2))
c=json.loads((D/'self_checks.json').read_text())
text=f'''### XR-17 Mantis — Forward swept systems research demonstrator

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
{(D/'rows.cpp.inc').read_text()}
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
'''
text += '\n## Geometry review\n\nSee FIXES.md for the corrected gaps, mirrored motion and regression coverage.\n'
text += '\n' + (D/'INTERIOR.md').read_text()
(D/'AUTHORING.md').write_text(text)
