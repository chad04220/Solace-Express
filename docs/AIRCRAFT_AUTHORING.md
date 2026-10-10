# Solace Express — Aircraft Authoring Guide

How to add an aircraft to Solace Express, from its numbers to its shape, materials, moving parts, cockpit, weapons,
sound, career role and tests. It is written for whoever adds the next type, a person or an AI assistant, and reflects
the code at v3.33.0. Where the code and this guide disagree, the code wins: fix the guide in the same change.

Older material, kept for its history: `docs/design/additional-aircraft/` (Codex's proposals for the Swift S6, the
Osprey C6 and the XR-10 / XR-20).

---

## 0. In one page

An aircraft is **data first**. Nothing is modelled in a 3D package, and the only image files are the shared surface
textures (§7.2), never one made for an aircraft:

| Part of the aircraft | Where it comes from |
|---|---|
| Physics, career and livery | one row in `kAircraft[]`, `src/aircraft.cpp` (`AircraftSpec`, `src/aircraft.h`) |
| 3D shape | one row in `kModels[]`, `src/models.cpp` (`ModelDef`, `src/models.h`): a parametric shape the GLSL distance field builds (`src/shaders/plane_sdf.glsl`) |
| Triangle meshes | baked from that distance field at launch, then cached on disk (`src/aircraft_mesh.cpp`) |
| Materials and textures | GLSL material code per material id (`src/shaders/plane_material.glsl`) on a 30-layer texture array: CC0 photo scans in `assets/materials` (`tools/pack_materials.py`), procedural where a layer has none (`src/materials.cpp`) |
| Moving parts | rigid parts, posed every frame from the controls (`src/shaders/plane_parts.glsl`) |
| Cockpit | generated from the model row: seats, panel, yokes, pedals, visors, compass, windows (one of three layouts) |
| Engine sound | synthesized from the engine type, cylinder count and blade count (`src/audio.cpp`) |
| Flight performance the autopilot flies to | learned at launch by flying the type (`src/aircraft_perf.cpp`), cached |

Two tiers:

- **Tier A, the normal way.** One `AircraftSpec` row and one `ModelDef` row. No renderer code. Every career aircraft and
  two of the four research craft (the XR-10 Nightjar and the XR-20 Mantis) are Tier A.
- **Tier B, only when the shape can't be described by a model row.** A hand-written GLSL distance field with its own
  moving parts, like the XR-30 Specter (`mapJet`, `plane_sdf.glsl`) and the XR-40 Wraith (`wraith_sdf.glsl`,
  `wraith_cockpit_sdf.glsl`). Weeks of work, a heavier shader for every aircraft pixel, and many places to wire up
  (§6, §17). A hand-built XR-20 once cost four times the frame in the research terminal and was put back to Tier A.

The checklist for a new type is §17. The fill-in template is §18.

---

## 1. Conventions

- **Body axes:** +x right, +y up, **+z aft** (towards the tail). The origin is the centre of gravity. The nose is at
  negative z.
- **Units:** metres, kilograms, seconds. Speeds in `AircraftSpec` are **m/s** (the UI shows knots). Prop power is
  **watts per engine**; jet thrust is **newtons per engine**. Angles in the model rows are **degrees** where the
  field says so; the shaders work in radians.
- **The world** is an 80 × 80 km archipelago, so ranges are game scale (45–300 km for career types). Airports run
  from 480 m strips (Gull Rock) to 2.8 km runways; one gravel strip is at 1650 m elevation.
- **C++ rows** are aggregate initialisers: field order is law. Floats carry `f`, colours are `vec3(r, g, b)` in
  linear 0–1. Avoid pure white and pure black in liveries: the sun blows them out or crushes them.

---

## 2. The roster: indices, limits and what keys on them

`kAircraft[]` and `kModels[]` are **index-aligned**: row *i* of one is row *i* of the other (`kModels[spec - kAircraft]`).
Today:

| Index | id | Name | Notes |
|---|---|---|---|
| 0 | `kestrel` | Kestrel T2 | the lessons' aircraft (`Contract::forceAircraft = 0`) |
| 1 | `wren` | Wren 180 | |
| 2 | `bush` | Bushmaster STOL | taildragger |
| 3 | `islander` | Islander Twin | |
| 4 | `pelican` | Pelican Caravan | |
| 5 | `meridian` | Meridian Q400 | |
| 6 | `starling` | Starling 500 Jet | |
| 7 | `swift_s6` | Swift S6 | |
| 8 | `osprey_c6` | Osprey C6 | `kOsprey`; its own cabin trim, keyed by index in GLSL (`kOspreyModel = 8`) |
| 9 | `xr10_nightjar` | XR-10 Nightjar | `kNightjar`; research, Tier A |
| 10 | `xr30_specter` | XR-30 Specter | `kResearchJet`; research, Tier B (`special = 1`, engine code 5) |
| 11 | `xr20_mantis` | XR-20 Mantis | `kMantis`; research, Tier A |
| 12 | `xr40_wraith` | XR-40 Wraith | `kWraith`; research, Tier B (`special = 2`, engine code 6), the only armed type |

Rules and limits:

1. **Career types come first, the research craft last.** `kNumAircraft = rows - 4`: the last four rows are the
   research craft, hidden from the career. A new **career** type goes **before** `xr10_nightjar`, at the same index in
   both tables. Then every research constant shifts up by one: `kNightjar`, `kResearchJet`, `kMantis`, `kWraith` in
   `aircraft.h`. `flight_test` checks those constants against the ids and fails if one is wrong. A new **research**
   type goes at the end, and `kNumAircraft`'s `- 4` becomes `- 5`.
2. **Sixteen types at most.** The learned-performance tables are fixed arrays of 16, indexed by row
   (`aircraft_perf.cpp: s_perf[16]`; the `perf.bin` cache also rejects more). A type past 16 gets an empty
   performance model and its autopilot fails silently. There are 13 today. Grow the arrays before adding a 17th.
3. **Saves are keyed by `id`, not index.** The fleet, a loan and open jobs are written as `id` strings
   (`career.cpp`), so inserting rows doesn't break players' saves. An `id` is permanent: lowercase ASCII, unique, never
   renamed. It also seeds the registration ("SX-" plus three letters, `registrationOf`), which is painted on the
   fuselage and used as the call sign.
4. **Things keyed by index**, to check whenever the roster changes:
   - `kOspreyModel = 8` in `plane_sdf.glsl` and `gModelId == 8` in `plane_material.glsl` (the Osprey's cabin trim);
   - AI traffic picks types by hard-coded index (`traffic.cpp`: 0–4 at small fields, 5–6 at big ones, 0–6 for
     cruisers). **New types don't appear as traffic until you add them there**;
   - the main menu's tour (`kMenuShots`, `game.cpp`) names craft by index;
   - the loading pictures are named by index (`assets/loading/air_<index>.jpg`), and `loadshot_air_<n>` picks the
     scenery spot from `kSpot[kWraith + 1]` (`game.cpp`). Inserting a career type renames every later picture:
     re-render them (§16);
   - loops that run "to the last type" use `kWraith`: the launch prewarm (`game.cpp`, every type's meshes),
     `--loadshots` (`platform_win32.cpp`), `aircraft_visual_test` and `autoland_sweep`. A new last row needs those
     moved to it. A `kNumTypes` constant would be cleaner; add one if you touch them all.

---

## 3. `AircraftSpec`: physics, career and livery

```cpp
struct AircraftSpec {
  const char* id; const char* name; const char* role;
  int engineType, engines, cylinders, blades;  // ENG_PISTON / ENG_TURBOPROP / ENG_JET; 1..4; cylinders (piston, else 0); prop blades (0 for jets)
  float idleRpm, maxRpm;       // piston: prop rpm; turboprop: N1 % scale (e.g. 1100, 1900); jet: 0, 0
  float emptyMass, maxFuel, cargoKg; int pax;   // kg, kg, kg, seats besides the pilot
  float wingArea, span, chord; // m^2, m, mean chord m
  float CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, oswald;   // CLa, CD0, gearCD, oswald: reference only (aero.cpp derives its own from the shape)
  float power;                 // W per engine (prop) or N per engine (jet)
  float v0;                    // props: static-thrust knee speed (m/s); jets 0
  float vr, vref, cruise;      // rotate, approach reference, cruise (m/s)
  float rangeKm, runwayM;      // game-scale range (sets the fuel flow); runway at sea level until the learned distances replace it
  bool roughOK, taildragger, retract;
  float Ixx, Iyy, Izz;         // roll, pitch, yaw inertia (kg m^2)
  float elevPow, ailPow, rudPow;   // control power: ~0.38–0.45 / 0.045–0.07 / 0.05–0.07 for conventional types
  int license; int price; int rentFee;   // LIC_STUDENT..LIC_ATP; rentFee 0 = not rentable
  float fusLen, fusRad, wingY, wingZ; int engLayout, tail;   // fuselage length, max half height; wing root height and LE z; 0 nose / 1 wing nacelles / 2 aft; 0 conventional / 1 T-tail
  vec3 colBase, colStripe;     // livery: base paint; cheat line, wingtips, fin flash
  int special = 0;             // 0 for every type but the XR-30 (1) and XR-40 (2): they switch on whole code paths (§5)
  float designMach = 0, gPos = 0, gNeg = 0;   // research tiers: < 1 held under the barrier by its drag rise, > 1 reheat
                                              // and the research drag rise; structural limits (0: 5.8 / -3 g)
};
```

What the game derives from these by itself: the drag build-up, lift slope and span efficiency (`aero.cpp`, from the
geometry), the fuel flow (from `rangeKm` and `cruise`), the maximum take-off mass (`maxMass()`), the runway it needs
(`runwayNeeded`, from the learned take-off and landing at full weight, +15%, longer at altitude), the engine sound, the
registration and call sign, the hangar and market cards, and the README's performance table (`flight_test --table`).

The column header to keep above a row:

```
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe[, special, designMach, gPos, gNeg]
```

The physics must close: `flight_test` flies every career type off the Capital's runway and fails it if it doesn't lift
off within **0.9 × `runwayM`**, climb, hold altitude on the autopilot, and respond the right way to each control. Use
the self-check table (§19) before building.

---

## 4. `ModelDef`: the shape (Tier A)

```cpp
struct ModelDef {
  float st[8][4];        // fuselage stations nose -> tail: {z, half width, half height, centre y}; z strictly increasing; tiny at both ends
  float roundness;       // section: 1 ellipse, 0 rounded box (0.3–0.65 reads as a cabin with flat sides)
  float wing[8];         // half span, root chord, tip chord, LE sweep at the tip (m aft), root y, root LE z, dihedral (deg), thickness ratio
  int strut; float strutX, winglet, flapFrac; int slats, deice;   // lift struts and their x; winglet height; flaps end at this fraction of the half span (ailerons from there to 0.94); slats; de-ice boots
  float ht[7]; int ttail; // tailplane: half span, root chord, tip chord, LE sweep, y, LE z, dihedral (deg); T-tail flag (y and z then come from the fin)
  float vt[6];           // fin: height, root chord, tip chord, LE sweep, base y, root LE z
  int engine; float nacX, nacY, nacR, nacZ0, nacLen, spinnerR, propR;   // 0 nose piston, 1 nose turboprop, 2 wing piston nacelles, 3 wing turboprop nacelles, 4 aft jets; 5, 6 the XR-30 / XR-40 fields (reserved)
  int gear; float wheelR; // 0 fixed tricycle with spats, 1 fixed tricycle, 2 taildragger tundra, 3 retracts into the nacelles (needs engine 3), 4 retracts into wing/body
  int cargoPod;
  int winCount; float winZ0, winZ1, winY, winW, winH;   // passenger windows: count per side, first/last z, height above the section centre, half width, half height
  vec3 eye; int cockpit; // pilot's eye (left seat); 0 analog single cluster, 1 analog twin, 2 glass (PFDs, centre display, leather)
  float wsZ0, wsZ1, wsY, sideZ1;   // windscreen from z0 to z1 above height wsY; side windows from wsZ1 aft to sideZ1
};
```

An annotated row (the Wren 180):

```cpp
{ {{-4.15f,0.100f,0.090f,-0.020f},{-3.92f,0.380f,0.350f,-0.050f},{-2.80f,0.520f,0.445f,-0.035f},{-1.80f,0.600f,0.645f,0.095f},
   {-0.20f,0.620f,0.645f,0.115f},{1.60f,0.450f,0.420f,0.160f},{3.40f,0.150f,0.190f,0.290f},{4.15f,0.060f,0.090f,0.350f}}, 0.86f,
  {5.50f,1.62f,1.00f,.55f,.80f,-1.85f,1.7f,.13f},   // wing: 11 m span, chords 1.62 / 1.00, tip sweep 0.55, root y .80 z -1.85, 1.7 deg dihedral, 13% thick
  0,0,0,.55f, 0,0,                                   // no struts or winglets, flaps to 55% span, no slats or de-ice
  {1.75f,1.05f,.72f,.18f,.32f,3.00f,0}, 0,           // tailplane; conventional
  {1.62f,1.45f,.50f,1.02f,.27f,2.60f},               // fin
  0, 0,0,0,0,0, .14f,.95f,                           // nose piston, spinner r .14, prop r .95
  1, .26f, 0,                                        // fixed tricycle, wheel r .26, no pod
  1, .50f,1.72f,.22f,.27f,.20f,                      // one cabin window a side
  vec3(-.30f,.54f,-1.30f), 0, -2.60f,-1.75f,.36f,0.45f },   // eye, analog single cockpit, windscreen, side windows
```

### 4.1 Rules the shape generator relies on

1. **Stations.** Exactly 8, z strictly increasing; a monotone cubic runs through them, so a bulge needs a station.
   Keep the half height at least half the half width in the cabin; make `st[0]` and `st[7]` tiny (≤ 0.1 m).
   `fusLen ≈ st[7].z − st[0].z`, `fusRad ≈` the largest half height.
2. **Wing.** Thickness 0.10–0.16. The fillet is automatic. Root height sets high or low wing. For a low wing keep the
   cabin floor (`eye.y − 1.08`) above the wing's upper surface at the root. A high wing's flaps start over the roof:
   from the cockpit they are clipped to the outside of the fuselage, as the wing is.
3. **Tail.** T-tail: `ttail = 1`. Conventional: the tailplane at or a little above the fin base, its LE near the fin's.
4. **Engines and props.** `spinnerR ≈ st[1] half width × 0.35`; `propR` so the disc clears the ground by 0.25 m at the
   gear height (`fusRad × 1.3 + 0.55` singles, `+ 0.75` twins and jets, `fusRad + 0.45` taildraggers). Wing nacelles
   slightly below the wing at `nacX`, starting about `0.8 × nacR` ahead of the LE. Aft jets: `nacX ≈ st[5] half width +
   nacR + 0.4`; the pylons are generated.
5. **Gear.** You choose the kind and the wheel radius; the track (`max(1.2, 0.13 × span)`), the wheel stations and the
   wells are derived. `gear 3` needs `engine 3`. `gear 4` mains fold sideways into a fairing under the wing root, so the
   wing must be there at the track.
6. **Cabin.** The eye at least 8 cm under the roof. The panel goes at `eye.z − 0.68` (`− 0.85` glass), its half width
   `0.93 ×` the section's. The windscreen must sit ahead of the panel and under the roof. Seats, yokes, pedals,
   pedestal, overhead console, visors, compass, trim and the window openings' rounded lips are all fitted from these.
7. **Windows.** `winY` is relative to the section's centre line; keep `winW`/`winH` inside `0.8 ×` the half height.

### 4.2 The packed model (what the shaders see)

`packModel` (`models.cpp`) packs the two rows into 24 `vec4`, `uM[]` / `gM[]` in GLSL. Shader code reads these slots:

| Slot | x | y | z | w |
|---|---|---|---|---|
| 0 | fusLen | gear kind | engine code | fusRad |
| 1–8 | station z | half width | half height | centre y |
| 9 | wing half span | root chord | tip chord | tip sweep |
| 10 | wing root y | root LE z | tan(dihedral) | thickness |
| 11 | strut | strutX | winglet | flapFrac |
| 12 | tail half span | root chord | tip chord | sweep |
| 13 | tail y | tail LE z | tan(dihedral) | T-tail |
| 14 | fin height | root chord | tip chord | sweep |
| 15 | fin base y | fin LE z | roundness | slats |
| 16 | nacX | nacY | nacR | nacZ0 |
| 17 | nacLen | spinnerR | propR | cargoPod |
| 18 | gear track | wheel radius | mains z | nose z |
| 19 | gear height | 0.45 L | taildragger | de-ice |
| 20 | window count | winZ0 | winZ1 | winY |
| 21 | winW | winH | cockpit layout | panel z |
| 22 | eye x | eye y | eye z | panel half width |
| 23 | wsZ0 | wsZ1 | wsY | sideZ1 |

The cockpit view also derives the cabin fit from these (`loadCabinFit`, `plane_common.glsl`: `gCab0`, `gCab1` for seat
width, headrest, dome light, armrests, visor height and slope, overhead, vents).

---

## 5. Flight model and autopilot

- **Aerodynamics** come from the shape (`aero.cpp`): part-by-part drag from wetted areas and form factors, Reynolds-
  dependent skin friction, the lift slope and span efficiency from the aspect ratio, the wave drag from the section's
  critical Mach. Cruise speed and climb come out of it; nothing is tuned. A wrong number shows up as a wrong cruise in
  `flight_test`, so fix the shape or the power, not the drag.
- **`special`** switches whole paths in `Plane::substep` (`aircraft.cpp`): 1, the XR-30 (fly-by-wire, pitch thrust
  vectoring, no fuel burn, the research drag rise, a 50 g structure); 2, the XR-40 (four tilting thruster pods,
  vertical flight, a 90 g structure). A new Tier B type with new physics needs a new value and a new path. A
  conventional research type uses `special = 0` with `designMach`, `gPos`, `gNeg` (the XR-10 and XR-20).
- **Learned performance.** At launch, `Plane::perf` flies short test sorties per type (stall speeds, best climb, idle
  sink, roll rate, pitch response, g per stick, take-off and landing distances) at the test weight (60% fuel, 150 kg
  aboard, 1200 m) and caches them in `shadercache/perf.bin`, stamped with the build.
- **One autopilot, no per-type code.** Every step it is on, `Plane::apSense()` reads what the aircraft can do *now* into
  `Plane::apEnv` (`ApEnvelope`, `aircraft.h`): the learned envelope corrected for the weight (stall and approach speeds
  with √weight, landing distance with weight, a heavier airframe pulling fewer g per stick), the air (density), ice (up
  to 30% of the lift), engine health (thrust from `thrustAt`, so an engine out halves a twin's and cuts its climb by
  more), and what it is built with (flaps, rate-command controls, thrust it can turn downward). Every decision reads
  that and nothing else: the speeds it flies, how hard it turns and climbs, the steepest descent (its idle sink in
  landing trim) and glidepath (its idle glide; shallow for engines slow to spool), whether it can go around at all (an
  aircraft that can't climb lands from what it has), when it flares (the slower of the pitch's lag and the wing's in
  building lift, `apPathLag`), whether it sheds speed with the belly-up (an airframe that takes 8 g or more) and
  whether it comes down vertically (thrust it can direct downward above its weight). **A new type therefore needs no
  autopilot code or tuning**: give it honest physics and it is flown to them. Don't branch on an index, `special` or
  the engine type in the autopilot; add a reading to `ApEnvelope` instead.
- **The autopilot's two laws.** It flies to the airframe's limits (hard turns, high g) unless the job carries
  passengers or a fragile load (`Contract::gentle()` sets `Plane::apComfort`: 25° of bank, 1.25 g, soft climbs and
  descents, a stabilized approach no more than ~500 fpm beyond the glidepath's own descent, and no belly-up).
- **Autoland** plans each runway end (`apPlan`). It refuses a field the career wouldn't send the type to (the same
  `surfaceOK` and `runwayNeeded` rule as dispatch: grass and sand only with `runwayM` under 900, gravel and snow only
  with `roughOK`), a runway it can't stop on (the aim point plus 0.20 × the touchdown ground speed squared, 0.27 for a
  taildragger, more on a rough surface: the approach speed at the weight it has now, in the field's air, plus any
  tailwind), terrain that keeps it too high, high ground where it would turn in, or a turn-in that would leave the
  chart. So `runwayM` and `roughOK` must be honest for a type whose distances aren't learned (`special`). An airframe built for it
  (`apEnv.highAlpha`) flies the final fast and does the belly-up (`APS_BLEED`, `apBellyUp`) before landing; one that
  can hold itself up on its thrust (`apEnv.hover`) then hovers.
- **The test:** `autoland_sweep --craft <i>` (every airport, three winds, two starts), then `--comfort` for a career
  type, and `--all` to check it refuses fields it can't use. Every case must land or be refused with a reason.

---

## 6. Hand-built shapes (Tier B)

Read §7–§10 first: a Tier B shape must obey the material, part and bake rules as well.

**Dispatch.** `mapPlaneBody` (`plane_sdf.glsl`) routes on the engine code in `gM[0].z`: 5 → `mapJet`, 6 →
`mapWraith`, everything else → the packed model. A new Tier B type takes the next code (7) and a new branch. Research
code sits behind `RESEARCH_ON`, which the `AF_LIGHT` builds compile out, so the light aircraft don't pay for it, and its
call sites behind `#if HAS_JET` / `HAS_WRAITH` (a new one gets its own), so no other aircraft's program holds it (§8).

**Field rules.** These are what the mesh bake needs; break them and it makes holes, slivers or ragged edges:

1. **A lower bound of the true distance everywhere** (Lipschitz ≤ 1). Compose with `min`, `max`, negation, `opU`,
   `smin` (blend ≤ 0.35 m) and the primitives in `plane_common.glsl` (`sdBox`, `sdRoundBox`, `sdCapsule`,
   `sdEllipsoid`, `sdTorus`, `sdRoundCone`, `sdCylX`, `sdRoundCylX`, `sdPanel`, `sdSurface`). Never scale a distance up;
   never return something that isn't a distance.
2. **`sdRoundBox(p, b, r)`'s half sizes `b` include the rounding.** Its flat face is at `b`, not `b + r`. (A compass
   card tested at `b + r` never matched in v3.32.0.)
3. **No knife edges and no plates thinner than ~2.5 lattice cells.** The cockpit lattice is 1.56 cm (§10). Where a
   flat cut meets a curved surface at a shallow angle it leaves a wedge thinner than that, and the bake serrates it.
   Round every cut with a smooth boolean (`-smin(-a, b, k)`): v3.32.0's window openings got a 3 cm lip
   (`shell = -smin(-shell, winHole, 0.03)`) and their jagged frames were gone. Minimum feature thickness is 8 mm
   (4 mm radius for antennas and wicks).
4. **A part that should hug a curved surface must follow it.** Don't rotate a flat plate to the average slope. Build it
   as an even layer of the surface's own field, bounded in the other two axes. The v3.32.0 sun visors are
   `abs(f + 0.075) - 0.012` under the headliner: the old tilted plates came apart where the cabin trimmed them, and one
   hung a loose black plate at the windscreen.
5. **Everything inside the bounding radius** `max(fusLen, span) × 0.55 + 1.5` m, in every state.
6. **Geometry depends on state, never on time.** State is `gPS` (gear, flaps, steer, inside the cockpit), `gCtl`
   (pitch, roll, yaw, throttle), `gFlame` (spool, reheat, nozzle, Mach), the prop angle and the type's own channels
   (`gWr[7]` for the XR-40). `uTime` is for shading only.
7. **Everything that moves is a rigid part** (§9). Wells and cavities that open are cut in the static body; their doors
   are parts that close flush.
8. **Bound expensive sub-fields** by box: `if (sdBox(q, halfSize) < res.x) { ... }`. Every pixel of the objects pass
   and every bake sample evaluates the whole field.
9. **Cockpit-only shapes** check `gPS.w > 0.5` (the cockpit view). `mapPlaneBody` keeps the window openings' distance
   in `winHole` for cabin fittings to stay clear of the glass.
10. **A sealed cockpit** (no canopy) shows the outside on display panes fed by cameras on the airframe
    (`feed_cameras.h`: up to 13 feeds; the pane geometry must match the shader's `feedScreen`). A cockpit with glass
    uses real window openings cut in the shell.

---

## 7. Materials and textures

An aircraft has **no textures of its own and no UV mapping.** Its look is GLSL: a material function per material id
(§7.1) that paints the livery, seams, rivets and placards procedurally and takes its surface detail from one shared
array of 30 texture sets (§7.2), sampled triplanar in body space (§7.3). Adding an aircraft never adds an image file.

A material fills a `Mat` (`alb`, `rough`, `metal`, `nrm`, `emit`) for a material id, usually from a triplanar sample:

```glsl
tx = triSample(lp, ln, M_METAL, 0.25, nT);   // body-space position and normal, set, scale (m), out: tangent-space normal
m.alb = tx.rgb*vec3(0.62, 0.63, 0.65); m.metal = 0.9; m.rough = clamp(tx.a*0.6, 0.15, 0.5); m.nrm = nT;
```

### 7.1 Material ids

Material ids (`plane_material.glsl`, `planeMaterialN`), as the field returns them in `.y`:

| Ids | What | Handled where |
|---|---|---|
| 1, 2, 3 | fuselage, wing, tail paint: livery, cheat line, registration, seams, rivets, de-ice | the first chain |
| 5, 6, 8 | nacelle, rubber, bare metal | the first chain |
| 10–14 | cockpit: panel (instruments drawn on it), shell and floor, seats, controls, glareshield | the first chain (10–14 are lit as interior) |
| 16, 17, 18, 19, 21 | spinner, exhaust, nav lights, beacon, fan face | the first chain |
| 30–59 | the XR-30's airframe and the research cockpits | `RESEARCH_ON && mid >= 30 && mid < 60` |
| 60–69, 78 | light-aircraft cockpit: brushed metal, rubber, trim, lenses, radio stack, satin black, centre display, red knobs, harness; the compass card | `mid >= 60` |
| 61–79 on the XR-40 | its cockpit | `shadeWraithCockpit`, when engine code is 6 |
| 80–93 | the XR-40's airframe | `shadeWraith` |
| 94, 95–100, 101–104 | light fixture housings, own lenses, traffic lenses | the second chain |
| 120–124 | the Osprey's cabin trim (`gModelId == 8`) | its own block |

Rules learned the hard way:

- **An id lands in exactly one branch.** If you add id 59 under `mid >= 60`, it never runs (v3.32.0's compass card did
  exactly this). Check the ranges above before choosing.
- **Interior or exterior is decided by id:** `interior = (10..14) || (40..93)`. Interior pixels get the cabin's own
  lighting (sun through the windows, its bounce, the fixtures); exterior pixels get the sky and the airframe's sun
  shadow. A cockpit material outside those ranges is lit as if it were outside.
- **Free ids today:** for a light-aircraft cockpit, 70–77 and 79 (those are the XR-40's only when the engine code is 6);
  for the exterior, 4, 7, 9, 15, 20 and 22–29, each needing a new branch in the first chain. For a new Tier B type,
  claim a range and gate it with `RESEARCH_ON` if it is research-only.
- **Livery:** `gColBase` and `gColStripe` come from `colBase`/`colStripe`. Paint, the cheat line, wingtips and fin
  flash, panel seams, rivets, door outlines, fuel caps, antennas, the pitot, static wicks and the registration are
  automatic for Tier A.
- **Night:** scale lamp emission by `uNight`. Keep cockpit glows dim: a lit compass at full strength was the brightest
  thing in the cabin.

### 7.2 The texture array

`Renderer::genMaterials` (`renderer.cpp`) builds two `GL_TEXTURE_2D_ARRAY`s of 30 layers, 512² each, mipmapped, 8×
anisotropic, repeating: **`uAlb`** (rgb the albedo stored as the square root of linear - the shaders square it - and
alpha the roughness) and **`uNrm`** (rg the tangent-space normal's x and y, DirectX's convention: green is a slope
*down* the image; b the height; a the ambient occlusion). About 84 MB with the mips, loaded once at launch.

Each layer comes from **`assets/materials`** (shipped beside the exe as `materials/`) when its three files are there:
`NN_name_c.jpg` (albedo), `NN_name_n.jpg` (normal x, y and height), `NN_name_m.jpg` (roughness in red, AO in green),
all exactly 512². Otherwise - a file missing, unreadable or the wrong size - the layer is made by the **procedural
generator**, `materialProcLayer` in `src/materials.cpp`, silently. So a game without the folder still runs, and
the render harness can show the old look with `MATDIR=""`.

| # | `M_` | Source | Shader tile (m) | Used by |
|---|---|---|---|---|
| 0 | `M_GRASS` | ambientCG Grass004 | 4-6 | terrain, airfield grass |
| 1 | `M_FOREST` | procedural (crowns from above) | 26 | terrain forest |
| 2 | `M_ROCK` | Poly Haven rock_face_03, stretched | 18 | terrain cliffs, rocks |
| 3 | `M_SAND` | Poly Haven coast_sand_01, at 0.58× the generator's brightness | 6 | beaches |
| 4 | `M_SNOW` | ambientCG Snow006 | 8 | snow |
| 5 | `M_ASPHALT` | Poly Haven asphalt_04 | 6-7 | runways, taxiways, roads |
| 6 | `M_GRAVEL` | ambientCG Gravel022 | 2-6 | gravel strips, aprons |
| 7 | `M_DIRT` | Poly Haven dry_ground_rocks | 4-7 | soil, tracks |
| 8 | `M_CONCRETE` | Poly Haven concrete_floor_worn_001 | 1.6-5 | aprons, walls, towers |
| 9 | `M_TILES` | Poly Haven clay_roof_tiles_02 | 3 | roofs |
| 10 | `M_SLATE` | Poly Haven roof_slates_03 | 3 | roofs, the church |
| 11 | `M_PLASTER` | ambientCG PaintedPlaster017 | 2.5 | walls |
| 12 | `M_BRICK` | Poly Haven red_brick | 1.4 | walls |
| 13 | `M_LEAVES` | procedural | 0.75-3 | broadleaf trees |
| 14 | `M_NEEDLES` | procedural | 0.9 | conifers |
| 15 | `M_PAINT` | procedural (white, orange peel) | 0.6-0.9 | aircraft paint |
| 16 | `M_METAL` | ambientCG Metal009 (brushed) | 0.25-2 | aircraft, hangars, fixtures |
| 17 | `M_RUBBER` | procedural (tyre tread grooves) | 0.08-0.35 | tyres, seals |
| 18 | `M_PLASTIC` | ambientCG Plastic012A | 0.2-0.4 | cockpit panels, trim |
| 19 | `M_FABRIC` | ambientCG Fabric030 | 0.08-0.3 (4 once) | seats, headliner |
| 20 | `M_CARPET` | ambientCG Carpet012 | 0.15 | cabin floors |
| 21 | `M_LEATHER` | ambientCG Leather030 | 0.25-0.3 | seats, yoke grips |
| 22 | `M_CORRUGATED` | Poly Haven corrugated_iron | 1-4 | hangars, warehouses |
| 23 | `M_CROP` | procedural (rows) | 4 | fields |
| 24 | `M_WHEAT` | procedural | 4 | fields |
| 25 | `M_BARK` | Poly Haven bark_brown_02 | 0.8-1 | tree trunks |
| 26 | `M_PLANKS` | Poly Haven weathered_brown_planks | 2 | docks, sheds |
| 27 | `M_LITTER` | Poly Haven forest_floor | 4 | forest floor |
| 28 | `M_SHINGLES` | Poly Haven grey_roof_tiles_02 | 3 | roofs |
| 29 | `M_SIDING` | ambientCG WoodSiding008, flipped | 3 | house walls |

The constants live in `scene_uniforms.glsl` (and `ent_fs1.glsl` for the scenery). Every scan is CC0, credited in
`THIRD_PARTY_NOTICES.md`; `tools/pack_materials.py`'s manifest is the record of what each layer was made from.

### 7.3 Using a layer in a material

- **Samplers.** `triSample(p, n, layer, scale, out nTS)` (`material_common.glsl`) blends three planar projections by
  the normal and, in each, the tile at `scale` metres with a second at 4.7× mixed 35% (it breaks the repeat). Terrain
  uses `groundSample` (two rotated lookups blended by noise and height, a macro layer, the AO); the scenery its own
  `triS` (`ent_fs2.glsl`). The sample's albedo comes back linear.
- **`scale` is the tile size in metres** and should match the "shader tile" column: the scans were fitted to it.
  Twice the scale is twice-size planks.
- **Colour is a tint.** Each scanned layer keeps the average colour of the procedural layer it replaced (§7.4), so a
  material's `tx.rgb * tint` gives the colour it always had. Pick the tint for the colour you want; don't fight the
  layer's mean.
- **Relief:** `m.nrm = nT` at full strength, or scaled (`nT*0.7`) for a softer surface. Under a low sun, strong relief
  on fine detail reads as grain (§7.4, "speckle").
- **Roughness** comes in `tx.a`; most materials clamp or scale it to their own range.
- **Metal** is the material's call (`m.metal`), not the layer's.

### 7.4 Replacing a layer with a scan

1. **Choose the scan.** CC0 only (Poly Haven, ambientCG). It needs a colour map and a DirectX normal map; roughness,
   AO and displacement are used when there. Prefer even, unremarkable surfaces: one striking feature (a long crack, a
   stain) repeats visibly every tile. Note its real width (Poly Haven lists it; ambientCG doesn't, so judge it from a
   feature of known size).
2. **Dump the procedural layers:** `cmake --build build --target material_dump && build/material_dump /tmp/old`. The
   packer matches each scan to these.
3. **Add a manifest row** in `tools/pack_materials.py`: `layer: ("ph:id" or "acg:Id", real width m, shader tile m,
   options)`. The packer repeats the scan a whole number of times to fill the tile (or, with `stretch`, fits it
   whole: a 2.6 m rock face read as an 18 m cliff), box-filters it to 512² (seamless), and then matches the procedural
   layer: the per-channel mean colour, the mean roughness, and the mean slope (held to 0.5-2× the scan's own).
   Options: `colour` (how much of the scan's own colour variation to keep), `contrast` (scale its variation about
   the mean), `bump` (relief against the procedural layer's), `brightness` (scale the matched mean: the generator's
   sand was twice a real beach's and blew out to a textureless white), `flip` (upside down, for a wall layer with an
   up).
4. **Pack:** `tools/pack_materials.py --old /tmp/old --only N` (downloads are cached in `~/.cache/solace-materials`).
   It prints the repeat, the content's scale against its real size, the colours matched and the relief gain.
5. **Look at it** next to the old layer (the `_c.jpg` against the dump's `_alb.ppm`), then in the game: the render
   harness reads `assets/materials` (`MATDIR=""` for the procedural look), so render the same scenes both ways -
   an `apv_` airport view, `ckv` cockpits, `mountain`, `loadshot_` - and compare.
6. **Commit the three JPEGs** with the manifest change. Nothing else ships or needs rebuilding: no cache keys on the
   textures.

Learned the hard way:

- **Speckle is usually relief, not shadows.** The generated fabric and plastic had strong, coarse bumps; under a
  grazing sun the Osprey's window arches looked like shadow acne. The scans and a soft `nT` fixed it.
- **A flat scan raised to the generator's relief amplifies its grain** (asphalt by 8×, concrete by 23× before the 2×
  clamp): its noise becomes speckle. The clamp is there for that.
- **A dark scan matched to a light layer keeps its contrast**: white siding from dark boards came out striped black
  and white. Use `contrast` 0.5-0.7.
- **Scale and repeat show on runways and roofs first.** A prominent crack every 3 m down a runway, roof tiles the
  size of paving slabs: check the content scale the packer prints and the shader's real tile.
- **Walls run v up the wall**, so the image's top is at the bottom of the wall: `flip` a layer whose look has an up.
- **The normal map must be DirectX's** (green down). A GL map lights every bump from the wrong side.
- **JPEGs are 4:4:4** (`subsampling=0`); chroma subsampling smears the normal map's x and y.

### 7.5 Adding a layer

A new set is a new layer for every GPU (about 2.8 MB with mips), so first try an existing one with a new tint, scale
or pattern. If it must be new: bump `kMatLayers` (`materials.h`), add its name to `materials.cpp`'s table and a
`case` to the generator (the fallback, and the reference a scan is matched to), add an `M_*` constant in
`scene_uniforms.glsl` (and `ent_fs1.glsl` if the scenery uses it) and its name to `NAMES` in the packer, then give it
a scan (§7.4) if one fits.

### 7.6 Other generated textures

Built at launch in `renderer.cpp`, none tied to an aircraft: the cloud coverage map and 3D noise (`genCloudNoise`), the
minimap (`genMinimap`), and the sea's wave bands (`genWaves`): three tileable maps of slope and height, 256², from a
Phillips spectrum by inverse FFT (wavelengths 16-128 m, 2-16 m and 0.25-2 m). `water.glsl` scrolls each at its waves'
phase speed down the actual wind (`uWindV`), twice at crossing angles, scales the slope to the wind (Cox and Munk) and
puts whitecaps on the crests as the wind rises. A seaplane or a ditching reads the sea from there.

---

## 8. Shaders: how they're built and what they cost

- **Embedding.** Every `src/shaders/*.glsl` starts with `//! kName` and a description. `tools/embed_shaders.cmake` turns
  each into a C++ string constant in `build/gen/shaders_gen.h`, and `shaders.h` assembles programs from them
  (`objectsFSAssembly`, `planeMeshFSAssembly`, `effectsFSAssembly`, …). A new file is picked up by the glob; add it
  to the assemblies that need it.
- **Program families.** The full-screen airframe programs (every aircraft in the frame in one pass) are built twice:
  every aircraft, and `AF_LIGHT` (no research code). `OBJ_NO_AF` is the objects pass with no airframe at all (UFO and
  debris), and `PROXY_MAPS_ONLY` is the shadow proxy that only reads shadow maps. The mesh bake's program adds
  `PART_BAKE`.
- **Each aircraft's own build.** The aircraft mesh pass draws every aircraft (the player's and each traffic aircraft)
  with its type's own build, and the bake builds each body with its own pair: `aircraftDefines` (`shaders.h`) gives
  `AF_MODEL` (the roster index), the family's switch and the packed model as the constant `gM`, so every branch on the
  shape is settled as the program compiles. Code only some types run goes behind the preprocessor switches in
  `plane_common.glsl` - `HAS_FLEET`, `HAS_JET`, `HAS_WRAITH`, `HAS_RESEARCH`, `HAS_FLEET_CABIN` and one per type with its
  own code (`HAS_SWIFT`, `HAS_BUSHMASTER`, `HAS_OSPREY`, `HAS_MANTIS`...) - and a test of the player's type is
  `MODEL_IS(n)`, never `gModelId == n`. The pruner (`shader_prune.h`) settles those conditionals before it drops what
  nothing calls, so another type's code is not in the program at all: wrap the **call site** (an `if (...) {...} else`
  with `#if HAS_X` ... `#endif` round it keeps the chain whole), and the functions only it calls go too.
  `aircraft_specialization_test.py` checks each type's own programs hold its own functions and no other type's: add a
  new type's own functions to its table. Each own build is made the first time its aircraft is drawn or baked (the
  launch's prewarm draws every one), from the binary cache after the first launch; `AF_ALL=1` draws and bakes
  everything with the shared builds, for an A/B.
- **The scenery.** The trees, the rocks and the buildings (with the airport's fittings and the vehicles) each have their
  own pair of programs (`ENT_CLASS`, `ent_common.glsl`: `ENT_TREES`, `ENT_ROCKS`, `ENT_BUILDINGS`); each draw takes its
  kind's class's (`entities.h entClass`).
- **Caches.** Compiled programs are cached by source in `shadercache/`; the first launch of a new build compiles them.
  Meshes are cached by a stamp of each aircraft's own bake programs as the driver gets them (`Renderer::meshStamp`:
  the type's pruned bake pair, plus the GPU driver). So material and lighting edits rebake nothing, and a shape edit
  rebakes only the aircraft whose programs it changes (an edit to shared shape code, every aircraft once). Bump `kMeshMagic` (`aircraft_mesh.cpp`) when you
  change what the bake makes of the field, not just the field.
- **Portability.** OpenGL 3.3 core, and it must compile on NVIDIA, AMD, Intel and Mesa:
  - no GLSL keywords as identifiers (`flat`, `sample`, `patch`, `input`, `output`, …; `shader_keyword_test` checks);
  - nothing that inlines a huge loop body many times (some NVIDIA drivers refuse oversized shaders: the display atlas
    evaluates each page once for that reason);
  - `shader_check` validates every assembled program.
- **Cost.** The owner plays at 60 fps on a mid-range GPU and prefers performance to fancy graphics. Baseline from the
  owner's v3.29.2 diagnostic: cockpit 7.4 ms, Mantis cockpit 10.1 ms, night 7.7 ms. Every line in the field or the
  material code runs for every aircraft pixel of several passes, so bound sub-fields, keep loops short, and never trace
  the field per pixel in the mesh pass (v3.30.0 did, and the cockpit went from 7.4 to 98.9 ms). The gate
  (`docs/WORK_PLAN.md`): the owner's `diagnostics.bat` before and after; the targeted pass improves and nothing else
  regresses by more than 0.2 ms.

---

## 9. Moving parts

Everything that moves is a **rigid part** (`plane_parts.glsl`): a solid with its own shape in its own frame
(`partField(k, l)`) and a pose from the state (`partPose`: `body = R·local + T`, where `R` is a rotation, a mirror for
the other side, a hinged surface's deflection about its swept and tapered hinge, or a stretch along an axis).

- **Ids 0–45 are taken**: the cockpit controls 0–10, the light aircraft's surfaces 11–14, the XR-40's 15–29 and 45,
  the XR-30's 30–32 and 38–44, the packed model's gear 33–37. A new part takes 46 or above.
- **`partList`** (`aircraft_mesh.cpp`) lists each type's instances (type, side, which one) for the outside and the
  cockpit. Tier A types get the flaps, ailerons, elevators, rudder, gear and doors outside, and the yokes, pedals and
  throttle (knob or levers) and flap lever inside, automatically. A new engine code needs its own branch. Limits: 128
  part instances per mesh (`kMaxPartInst`), 512 posed instances per frame (`kMaxPoseInst`).
- **A bake box per part.** Surfaces use `surfaceBox`, the gear `gearPartBox`, the XR-40 `wraithPartBox` (box plus lattice
  step, 3–8 mm). Other parts are found by a 1 cm survey of their field.
- **In the airframe's field**, place each part through its pose (`partAt(res, PT_X, vec2(side, which), p)`) and leave it out when
  `gPartMode == -2` (the static mesh's bake). The part's own bake calls `partField` directly. Motion written as
  hand-made math in the airframe field is baked frozen.
- **Control directions are tested.** Roll +1 raises the right aileron and rolls right; pitch +1 raises the elevator's
  trailing edge and pitches up; yaw +1 swings the rudder's trailing edge right. Yokes turn the way the stick goes
  (`flight_test`'s control-direction check, and the owner looks).

---

## 10. Baking

At launch every aircraft's meshes are built or read from the cache, outside and cockpit, research craft included.
That's the "Building the <name> cockpit's mesh" step, one of the loading bar's counted steps (`load_pacer.h`). In `bakePlaneMesh`:

1. **States.** The field is sampled on the GPU in every gear, flap, steering and control state the hull sweep lists
   (`hullStateList`).
2. **Moving cells.** A 6.25 cm cell whose distance changes by more than 8 mm between states is "moving"; the rest of
   the surface band is static.
3. **Thin cells** (cockpit only). A plate or rod under about 2.5 cells through is re-meshed on a lattice twice as fine
   (0.78 cm), as a patch over the first mesh; the first mesh sinks its ring 3 mm under it.
4. **Surface nets** on the 1.56 cm lattice: one vertex per lattice cube the surface crosses. Inside, sharp edges use
   dual contouring (the vertex where the face planes meet). Each vertex is pulled onto the surface, and normals,
   material ids and the cabin's ambient occlusion are baked per vertex.
5. **Simplified** to within 1 mm outside and 0.4 mm in the cockpit, without moving a material boundary.
6. **Parts** are baked each alone in its own frame (§9). The **moving cells**, grown by one 0.25 m voxel, make a hull
   that the objects pass marches inside; anything not a part is marched there every frame, so keep it rare.
7. **Cached** as `shadercache/mesh_<key>_<stamp>.bin`, validated on read (counts must add up to the file).

`HULLDBG=1` prints each bake: states, how far vertices sit off the surface, and every part's lattice, vertex and
triangle counts. A slow or huge bake usually means a thin feature, a knife edge or a part box far bigger than its part.

---

## 11. Cockpits

- **Three generic layouts** (`ModelDef.cockpit`): 0 analog single cluster, 1 analog twin (pilot and copilot clusters),
  2 glass (PFDs, a centre engine display, leather, an overhead plate). The instruments are drawn once a frame into a
  display atlas (`disp_main.glsl`, `drawInstruments(q, layout)`) and sampled by the panel material (id 10). A new
  layout is a new case there and in the panel's material.
- **Generated fittings:** seats with headrests (dropped where the roof is too low), the yokes and pedals (parts), the
  pedestal with its throttle, fuel selector and trim wheel, the switch row, the radio stack, side trim with armrests,
  the overhead console with dome and map lights, the visors, the compass and the glareshield light strip.
- **One type's own fittings** go in the field gated by `gModelId` (the Osprey's `mapOspreyCabinTrim`) with their own
  material ids. Remember to move the index constant if rows shift (§2).
- **Lighting** inside: the sun through the windows with the cabin's own sun map (`texShCab`, 2048², hardware-filtered),
  the sun's bounce off the cabin, the sky through the windows, the fixtures at night, and baked ambient occlusion.
- **Check views** with the harness (§16): `ckv<i>_<yaw>_<pitch>_<hour>[_<roll>[_<flaps>]]`, at 9:00 for long shadows,
  15:00, 19:30 and 22:00, looking forward, left and right, and up at the roof and visors.

---

## 12. Effects and lights

- **Propellers** are not geometry: the effects pass draws each prop as a motion-blurred disc (`effects_fs.glsl`) at
  the hubs `modelProps` derives from the model row, with the spec's blade count. The blades show solid below 250 rpm (an engine stopped or
  starting) and blur fully by 700 rpm.
- **Jet plumes** exist for the research codes 5 and 6 (`jetPlumes`, `wraithPlumes`). The transonic vapour cone shows
  on any type between Mach 0.9 and 1.07 in humid low air, sized by its span. A new plume shape is effects-pass code behind `RESEARCH_ON`.
- **Lights:** the nav lights, strobes and beacon are placed from the model (`modelWingTip`, `modelFinTop`,
  `modelTailTip`) for Tier A. The player's own lamps are up to six lenses (`uLensP/D`). Their light on the ground and
  airframe comes from the light list in `FrameParams`.

---

## 13. Weapons

Only the XR-40 is armed, and its systems are written for it (`game_wraith.cpp`, gated by `special == 2`). The XR-15's
belly Gatling was tried and reverted. To arm another type, generalise rather than copy: a per-type weapon table, with
`WraithState`'s parts split into a weapons state. Every piece a weapon touches:

| Piece | Where | Notes |
|---|---|---|
| Controls | `ACT_WEAPONS`, `ACT_FIRE`, `ACT_BOMB`, `ACT_CLOAK` (`game.h`); `wraithControls` | rebindable; voices never name a rebound control |
| State | `WraithState` (`game.h`): armed, turrets and bay travel, bolts, bombs, blasts, craters, scorch marks, counters | |
| Firing | `fireLaser`: muzzle points in body space (`kLaserLens`), convergence 650 m ahead, 2000 m/s plus the craft's own velocity, a 0.12 s cooldown | the bolt leaves after the physics step, from where the turret is now |
| Hits | `updateBolts`: each segment swept against the ground and sea (`groundHit`), traffic (`traffic.rayHit`), scenery (`g_scenery.raycast`) and the UFO | `laserImpact` downs aircraft, damages or destroys scenery (`g_scenery.damage`) and scorches the ground |
| Bombs | `updateWraith`: the bay opens, the bomb drops from the cradle (`kBayBomb`), falls with gravity and a little drag, and goes off on contact or near an aircraft | `detonate`: a blast, a crater, everything within 230 m, a shock on the player |
| Visuals | `wraithVisual` fills `FrameParams::fx`: up to 16 beams, 8 bombs, 6 blasts, the impact pip; `effects_fs.glsl` `weaponsFx` draws them | the craters and scorch marks join the wreck's crater list (24 at most) |
| Moving hardware | the turrets, hatches, arms and bay doors are rigid parts posed from `pv.wr[]` | their travel is state, never time |
| Cockpit | the bomb-impact pip on the floor displays; the bomb camera on feed slot 12 | |
| Sound | `SFX_LASER`, `SFX_PLASMA`, `SFX_BOOM`, `SFX_CLOAK`, `SFX_GEAR_CLUNK` | |
| Rules | weapons are research-only, never in the career | `traffic.destroyNear` and `traffic.rayHit` serve any weapon |

---

## 14. Sound

Engine sound is synthesized per engine (`audio.cpp`): a piston's firing pulses from its cylinder count and rpm, the
prop's blade-pass tone from its blades, a turboprop's or jet's N1 whine and roar, a sick engine skipping firings, and
cockpit muffling inside. A new type gets its sound from `engineType`, `engines`, `cylinders`, `blades`, `idleRpm` and
`maxRpm`. A special engine (the XR-40's pods) is its own code path.

---

## 15. Career and world

- **Market and hangar:** every type below `kNumAircraft` appears, by `license`, `price` and `rentFee` (0: buy only).
  `canFly`/`runwayOK` keep the career from sending it where `runwayNeeded` is longer than the runway, or to a rough
  field without `roughOK`.
- **Contracts** offer cargo up to `cargoKg` and seats up to `pax`; passengers or a fragile load make the autopilot
  fly gently. Lessons force type 0.
- **Traffic, menu tour, loading pictures:** by index, by hand (§2).
- **README:** the aircraft table comes from `flight_test --table` (the learned performance); paste it in.
- **A research type** also needs its terminal entry (`kResCraft[]`, `game_research_ui.cpp`: designation, accent colour,
  velocity / agility / signature bars, the spec rows, the envelope chart, handling notes), its preview call-outs, its
  test cards (`kResCards`), and a scenery spot in `kSpot` for its loading picture.

---

## 16. Testing and looking at it

1. **Build and test:** `cmake --build build && ctest --test-dir build`. For any C++ change, also check it compiles on
   MSVC: `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -Ibuild/gen -Isrc <file>` (no variable-length arrays, no GCC
   builtins).
2. **`flight_test`:** every career type takes off within 0.9 × `runwayM`, climbs, holds altitude, flies its controls
   the right way; the index constants match the ids. `flight_test --table` prints the README's table.
3. **`aircraft_visual_test OUT [W H] [first last]`** (Linux, Mesa/EGL): the production field from fixed views, plus
   numeric probes for headrest clearance, seam jumps and the XR-40's touchpads. Use `AVT_VIEWS=cockpit,front` to pick
   views, `AVT_DIR=x,y,z` to aim the cockpit camera, and `AVT_IDS=1` to write material ids instead of colours (find what a
   stray shape is).
4. **`autoland_sweep`**, both laws (§5).
5. **The render server** (`tests/render_harness.cpp serve W H`, client `tools/render_client.sh`) keeps everything
   loaded between shots. Start it without `PREWARM` so it bakes only the aircraft a shot needs. Useful scenes:
   - `gav_<i>_<yaw>_<pitch>_<dist>[_<gear>]`: parked on the runway, orbit view;
   - `ckv<i>_<yaw>_<pitch>_<hour>[_<roll>[_<flaps>]]`: the cockpit (`settle=30` lets the anti-aliasing settle);
   - `loadshot_<CODE>`, `loadshot_air_<i>`: the loading pictures (`settle=40`, 1920×1080);
   - `research` (the XR-30 selected), `research10`, `research20`, `research40`: the research terminal.
   - `apv_<CODE>_<view>_<hour>`, `mountain`, `sunset`, `storm`: the world, for textures and the sea;
   - `wcam_<x>_<z>_<height>_<yaw>_<pitch>[_<hour>]`: a free camera anywhere (metres above the ground, yaw 0 north). The harness loads
     the scanned textures from `assets/materials` (`MATDIR=""` for the procedural ones, §7.4).
6. **On the owner's GPU:** `SolaceExpress.exe --shots gav_<i>_120_10_0,ckv<i>_0_-8_11 --size 1920x1080`, then
   `diagnostics.bat` for the frame-time gate (§8). `SolaceExpress.exe --loadshots` re-renders every loading picture.

---

## 17. Checklist: adding a type

**Tier A, career:**

1. Design it against the fleet (§20) and fill in the self-check (§19).
2. Add the `AircraftSpec` row and the `ModelDef` row at the same index, before `xr10_nightjar`.
3. Bump `kNightjar`, `kResearchJet`, `kMantis`, `kWraith` in `aircraft.h` (and `kOsprey`/`kOspreyModel`/`MODEL_IS(8)`
   if inserting before the Osprey), the indices in `plane_common.glsl`'s `HAS_<type>` switches and `MODEL_IS` tests,
   and `Renderer::kAfModels` (§8).
4. Check the roster stays at 16 types or fewer (§2).
5. Build, run `ctest`, `flight_test --table`, `aircraft_visual_test`, `autoland_sweep --craft <i>` and `--comfort`.
6. Look at it: `gav_` from four sides with the gear down and up, `ckv_` forward, left, right and up, morning and night.
7. Add it to traffic (`traffic.cpp`) if it should fly about, and to the menu tour if wanted.
8. Re-render the loading pictures (every `air_<n>` from the new index on has moved) and add its own.
9. Update the README's aircraft table and features, `RELEASE_NOTES.md`, and this guide's roster (§2, §20).

**Tier A, research:** the same, appended at the end with `kNumAircraft`'s `- 4` made `- 5`. Move the "to the last type"
loops (§2) to it, and add its terminal entry, test cards and scenery spot (§15).

**Tier B:** all of the above, plus a new engine code and its dispatch (§6), its field behind `RESEARCH_ON`, its parts
and their `partList` branch and bake boxes (§9), its material range (§7.1), its cockpit (generic, or sealed with
camera feeds), its physics path if it needs a new `special` (§5), its effects (§12), and a measured frame time on the
owner's GPU (§8).

---

## 18. Template

```cpp
// aircraft.cpp, kAircraft[]: before the xr10_nightjar row
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"<id>", "<Name>", "<Role in a few words>", ENG_<PISTON|TURBOPROP|JET>, <engines>, <cyl>, <blades>, <idle>f, <max>f,
 <empty>f, <fuel>f, <cargo>f, <pax>, <S>f, <b>f, <c>f,
 <CL0>f, <CLa>f, <CLmax>f, <flapCL>f, <CD0>f, <gearCD>f, <flapCD>f, <e>f,
 <power>f, <v0>f, <vr>f, <vref>f, <cruise>f, <rangeKm>f, <runwayM>f, <roughOK>, <taildragger>, <retract>,
 <Ixx>f, <Iyy>f, <Izz>f, <elevPow>f, <ailPow>f, <rudPow>f, LIC_<STUDENT|PPL|CPL|ATP>, <price>, <rentFee>,
 <fusLen>f, <fusRad>f, <wingY>f, <wingZ>f, <engLayout>, <tail>, vec3(<base r, g, b>), vec3(<stripe r, g, b>)},
```

```cpp
// models.cpp, kModels[]: at the same index
{ // stations: z, half width, half height, centre y (nose to tail; tiny at both ends)
  {{<z0>f,<w>f,<h>f,<y>f},{...},{...},{...},{...},{...},{...},{<z7>f,<w>f,<h>f,<y>f}}, <roundness>f,
  {<halfSpan>f,<rootChord>f,<tipChord>f,<tipSweep>f,<rootY>f,<rootLEz>f,<dihedralDeg>f,<thickness>f},   // wing
  <strut>,<strutX>f,<winglet>f,<flapFrac>f, <slats>,<deice>,
  {<htHalfSpan>f,<htRoot>f,<htTip>f,<htSweep>f,<htY>f,<htLEz>f,<htDihedralDeg>f}, <ttail>,       // tailplane
  {<finHeight>f,<finRoot>f,<finTip>f,<finSweep>f,<finBaseY>f,<finLEz>f},                           // fin
  <engine>, <nacX>f,<nacY>f,<nacR>f,<nacZ0>f,<nacLen>f, <spinnerR>f,<propR>f,                       // engines and props
  <gear>, <wheelR>f, <cargoPod>,                                                                     // gear
  <winCount>, <winZ0>f,<winZ1>f,<winY>f,<winW>f,<winH>f,                                             // cabin windows
  vec3(<eyeX>f,<eyeY>f,<eyeZ>f), <cockpit>, <wsZ0>f,<wsZ1>f,<wsY>f,<sideZ1>f },                     // eye, layout, windscreen, side windows
```

Start from the nearest existing type (a row of the same size and engine layout) and change it a step at a time,
rendering as you go: it is much faster than starting from zero.

---

## 19. Self-check (before the first build)

With `g = 9.81`, `ρ = 1.225` and the mass at maximum `m = emptyMass + maxFuel + cargoKg + 85·pax + 85`:

| Check | Formula | Required |
|---|---|---|
| Wing loading | `m·g / S` (N/m²) | 400–1200 trainers and tourers, up to 4000 airliners and jets |
| Stall, clean | `vs1 = sqrt(2·m·g / (ρ·S·CLmax))` | report |
| Stall, full flap | `vs0 = sqrt(2·m·g / (ρ·S·(CLmax + flapCL)))` | report |
| Rotate | `vr ≈ 1.10–1.15 × vs0` | `vr > vs0` |
| Approach | `vref ≈ 1.3 × vs0` | `vref > vr` |
| Cruise | consistent with the power; `cruise ≥ 1.6 × vref` | `cruise > vref` |
| Power loading (props) | `m / (power/1000 · engines)` kg/kW | 5–9 light aircraft, 4–6 turboprops |
| Thrust/weight (jets) | `power·engines / (m·g)` | 0.25–0.40 civil |
| Runway | `runwayM` consistent with the wing loading and power | trainer ≈ 400, bush ≈ 220, twin ≈ 450, turboprop ≈ 550, regional ≈ 1100, bizjet ≈ 1250 |
| Inertia | `Ixx ≈ 0.12·m·(b/2)²`, `Iyy ≈ 0.18·m·(L/2)²`, `Izz ≈ Ixx + Iyy` | within ±40 % |
| Geometry | `fusLen ≈ st[7].z − st[0].z`, `fusRad ≈ max half height`, `b ≈ 2·wing[0]`, `S ≈ 2·wing[0]·(root + tip)/2` plus carry-through | within 10 % |
| Prop clearance | nose: `gearHeight + st[0].y − propR`; nacelles: `gearHeight + nacY − propR` | ≥ 0.25 m |
| Career fit | licence, price and rent between neighbours of similar size; `rangeKm` 40–300 | sensible |

---

## 20. The fleet today

Spec values (the learned numbers are in the README's table):

| Aircraft | Type | Seats / cargo | Range | Cruise | Runway | Licence | Price / rent |
|---|---|---|---|---|---|---|---|
| Kestrel T2 | Two-seat trainer | 1 / 120 kg | 45 km | 97 kt | 400 m | Student | 18 000 / 120 |
| Wren 180 | Four-seat tourer | 3 / 320 kg | 70 km | 117 kt | 450 m | PPL | 30 000 / 250 |
| Bushmaster STOL | Backcountry taildragger | 4 / 480 kg | 72 km | 107 kt | 220 m, rough | CPL | 40 000 / 450 |
| Islander Twin | Nine-seat utility twin | 9 / 900 kg | 90 km | 126 kt | 420 m, rough | CPL | 85 000 / 900 |
| Pelican Caravan | Single turboprop hauler | 12 / 1400 kg | 130 km | 165 kt | 550 m, rough | CPL | 120 000 / 1600 |
| Meridian Q400 | Regional turboprop airliner | 40 / 4500 kg | 170 km | 272 kt | 1100 m | ATP | 600 000 / 8000 |
| Starling 500 Jet | Light business jet | 7 / 700 kg | 260 km | 389 kt | 1250 m | ATP | 260 000 / — |
| Swift S6 | Retractable low-wing tourer | 3 / 420 kg | 140 km | 148 kt | 600 m | PPL | 68 000 / 600 |
| Osprey C6 | Six-seat coastal charter twin | 5 / 270 kg | 110 km | 144 kt | 550 m | CPL | 68 000 / 650 |

Research craft (the research terminal only):

| Craft | Tier | What it is | Top Mach | Structure |
|---|---|---|---|---|
| XR-10 Nightjar | A | conventional twin-jet demonstrator | 0.96 | +9 / −4 g |
| XR-20 Mantis | A | forward-swept systems demonstrator, canards | 1.9 | +14 / −6 g |
| XR-30 Specter | B | fly-by-wire, pitch thrust vectoring, sealed cockpit | 2.7 | +40 / −20 g |
| XR-40 Wraith | B | four tilting thruster pods, vertical flight, cloak, lasers, plasma bombs | 4.3 | +90 / −45 g |

Gaps worth filling: a light-sport two-seater below the Kestrel, a 19-seat turboprop commuter, a medium business jet or
small regional jet, an agricultural or utility taildragger. Not possible without new physics: floats and amphibians
(no water handling), biplanes (one wing only), gliders (an engine is assumed).

## Authored cockpit interiors

See [Cockpit interiors](COCKPIT_INTERIORS.md) for the bounded layout records, CPU/shader fit contract, rigid-control families, live display mapping and representative-crew verification. Preserve the latest exterior and gear definitions when changing cabin data.
