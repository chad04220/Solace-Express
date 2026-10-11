# Hive military prototype review

Local branch: `codex/hive-military-review-20261011`  
Base: released v3.46.0, `72d0af0b23defb8067dd697f87d94384b39a412b`  
Status: local review candidate validated; not published or accepted for release.

## Requested scope

- Four supplied Cinder Concord/Hive craft are enemy-only: Needle interceptor, Bastion bomber, Cantor relay/support, Archon command ship. They do not occupy playable aircraft, career ownership, hangar, or research-selection entries.
- Military career supplies its own outfitted loaner. Reconnaissance uses an ordinary aircraft; attack and defense use XR-40.
- Research XR-40 free flight is a practice range, with forward-weapon/bomb selection and bounded enemy-wave spawning. Practice does not settle career rewards or alter civilian finances/progression.
- Three forward sets and three payloads begin on XR-40. Weapon slots on the other XR aircraft remain a future expansion.
- Detailed weapon hardware, readable projectiles, particle/emissive effects, and credible finite performance/thermal/ammunition limits are required. Gameplay values are authored game balance, not real-world weapon specifications.

## Trying the prototype

- Open the career hub's Military tab, then choose the reconnaissance loaner or outfitted XR-40 defense/strike mission. The mission HUD names the current site or target and extraction objective.
- Recon scanning is automatic while the site is within the forward sensor cone, visible and within 1.5 km. Each of the three sites requires five accumulated valid seconds. Complete the scans and extraction; merely landing does not finish the mission.
- In Research, select XR-40 and Free Roam. Choose forward weapon, payload and mixed-wave leader. After launch, pause to Spawn wave or Clear / rearm. Changing equipment resets the practice encounter, as the panel states. Practice is isolated from career rewards.
- Keyboard Up/Down and controller focus can reach the equipment controls; Left/Right retain research site selection. Controller Start launches once without also changing the focused equipment. Stable widget IDs preserve focus through repeated changes.
- The hub's existing global Escape/Back behavior returns to the main menu. The Civilian work button returns from Military to the civilian hub list.

## Architecture

`hive_combat.*` is a renderer/career-independent deterministic fixed-step simulation with stable actor IDs and bounded actor/projectile/event arrays. `hive_loadout.*` defines equipment and firing state. `enemy_fleet.*`, `enemy_mesh.cpp` and separate enemy shaders preserve the supplied procedural design in native raster meshes. Game flow connects missions and practice to these systems.

The four ships retain their supplied original geometry. Their package contained a visual demonstration rather than flight/combat statistics; role performance, AI, weapons, mission logic and damage were newly authored. The historical overlay patch was not applied wholesale because it targeted an older shader layout.

Enemy geometry has its own cache and registry. Aircraft-family shader boundaries remain separate. XR-40 equipment variants require a valid mesh/cache revision. Merge this branch with the prebuilt-aircraft loader only deliberately, then regenerate the full canonical player-geometry bundle from the combined source. Never copy an old digest into a changed build to force asset acceptance.

## Career/save compatibility

This branch writes save format 5. Readers accept versions 1–5. The military record is separate from civilian jobs, ownership, loans, reputation and finances. Service credits, rank, sorties and intelligence are tracked independently. Attempt IDs prevent duplicate settlement across save/load.

Back up careers before testing: older releases cannot read a version-5 save. An interrupted military attempt is cleared safely; there is no mid-combat encounter persistence or resume in this prototype. Completion requires the actual mission objective and extraction, rather than landing alone.

## Collision and simulation limits

- Authored body-local collision proxies follow enemy orientation and preserve major openings. They are not exact per-triangle collision with every bevel.
- Player collision boxes include bank and major body/wing/pod parts. Relative translational sweeps reduce moving-target tunneling; orientation is held during each 1/60-second tick.
- Scenery sweeps use existing authored entity proxies expanded for ordnance/body radius. Terrain uses distance-based sampling with conservative height-grid rejection; the near-ground interval is one metre with contact refinement. This is not an exact analytic solution of the procedural height field.
- Integrated blast tests cover surface and wall impact origins, shielding, falloff and seam-aware sweeps. Query origins are offset from the surface so the impact itself does not incorrectly occlude the blast; visual impact and crater positions remain at contact.
- Bound simulation capacities and visual capacities must agree: no damaging projectile may be hidden because player effects filled another draw list.

## Validation checkpoint (2026-10-11)

The focused 21-test run passed. Actual Game lifecycle and UI/input coverage passed 155 checks; military career passed 323 in both strict and fast-math builds; combat passed 422 in each mode; world collision passed 50 in each mode. Weapon selection and clearance contracts passed 6,550 checks. Invalid floating-point input checks use bit-based validation because production fast-math can optimize away ordinary finite tests.

Actual Game tests cover loaner settlement/civilian isolation, practice swaps/waves, equipment mass/recoil, forward-bolt and bomb contact, blast shielding and released-store orientation. Collision preparation is performed during loading without advancing the combat clock; the tested scene costs were 34.312 ms for recon, 16.250 ms for defense and 13.999 ms for strike. Ongoing look-ahead uses the existing bounded scenery request queue. These measurements do not establish native GPU performance.

The combined CPU regression run has passed all 96 selected suites, including reruns of three corrected compatibility/golden fixtures. The two GL suites were excluded from that CPU run and are not included in the claim. Windows cross-builds link successfully; the final projectile-visibility changes also compile and link in the Windows cross-build.

The initial Needle day/night renders revealed aliasing in the supplied unfiltered manufactured-grain material. A shadow-disabled diagnostic retained the lines, supporting the material diagnosis. Screen-derivative filtering corrected it. A separate hangar-floor clipping artifact was fixed by retaining conventional vertex clip depth and the existing fragment logarithmic depth.

Actual full-resolution weapon inspection found and corrected a disconnected plasma cage, circular-band normal artifacts and planar hardware shading. These changes preserve detailed meshes rather than reducing quality. Released payloads reuse the actual bay mesh buffers. A hidden-aircraft effects path was incorrectly selecting the all-aircraft shader; retaining the XR-40 family identity fixed the excessive software-renderer compilation cost. The failed process ended with signal 9, which alone is not proof of a kernel out-of-memory kill.

Actual combat captures exercise all four roles and the full 96-projectile capacity. A floor-occlusion pair hides the lower 48 while retaining all 96 simulation packets. Translated-world comparison captures differ by only 0.025–0.038 average RGB levels out of 255; pixels differing by more than 20 levels occupy at most 0.0064%. Final corrections cap a new projectile's visual trail by its actual age and keep a luminous core visible when its ribbon points directly at the camera. All 16 final combat rerenders passed at native 1920×1080 with no GL errors. Eight final Needle views also passed, including the corrected floor.

Focused AddressSanitizer/UndefinedBehaviorSanitizer checks passed combat (422), loadout, enemy-cache codec and world collision (50), including instrumented terrain/scenery dependencies. This is not a full-fleet or GPU sanitizer claim. After the shared lifetime-helper refactor, combat (422) and loadout passed refreshed ASan/UBSan runs. The final projectile changes also passed all nine affected CPU suites, including the 155-check Game integration test.

Do not infer native RTX 3070 performance from software-renderer timings. The target remains native 1920×1080 with a minimum 60 FPS; dense combat, smoke, shadows and terrain streaming require hardware frame-time validation.

## Integration cautions

The upstream bridge work postdates this base. Preserve it when merging. Windows compiles a direct production source list separately from Linux object libraries; every newly required production source must be included on both paths. Re-run save, gameplay and shader tests after the combined merge.

The CI test selector also includes the cross-drive Windows path fix verified on the prebuilt review branch: external tools on C: are not treated as source paths under a D: checkout. Eight drive/share cases and the local 98-test selection catalog pass.

No push or release publication has been performed for this branch. The source is frozen at this local review checkpoint. Evidence is linked below.


## Review evidence

Evidence directory: [validation/hive-military](validation/hive-military/). The full CPU log initially records three fixture failures; [affected reruns](validation/hive-military/affected-final-ctest.log) close those failures. [Final projectile regressions](validation/hive-military/projectile-trail-ctest.log) cover subsequent scoped edits. This is an aggregate 96-suite checkpoint, not a claim that the first invocation was entirely green.

- [Windows cross-build](validation/hive-military/windows-cross-build.log)
- [Focused sanitizers](validation/hive-military/sanitizers.log) and [final projectile sanitizers](validation/hive-military/projectile-sanitizers.log)
- [Final actual combat renders/events](validation/hive-military/combat-render.log)
- [World collision benchmark](validation/hive-military/world-benchmark.txt)
- [Kinetic weapon](validation/hive-military/XR40-Kinetic-Weapon.jpg), [charged weapon](validation/hive-military/XR40-Charged-Weapon.jpg), [EMP payload](validation/hive-military/XR40-EMP-Payload.jpg)
- [Military menu](validation/hive-military/Military-Mission-Menu.jpg): actual UI over a plain readability-check background, not a complete gameplay frame.

Recon is an unarmed site survey; enemy patrol/detection mechanics are not implemented for that mission. Practice equipment choices are session-local. The renderer and combat harness sources are included for reproducing the inspection views. Native Windows gameplay, controller hardware and RTX 3070 frame-time tests remain necessary before release.

Weapon coverage: 48 improved captures comprise 32 penetrator/EMP views with final planar/round normals and 16 plasma views. Six plasma bay/released views were refreshed after the final planar pass; the other ten precede only that final pass. Dedicated camera-feed and 8 km visual captures were not run. Existing XR-40 airframe edge artifacts remain outside this hardware-only correction. Hardware contracts passed 7,843 checks, including fast-math.
