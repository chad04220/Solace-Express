# Solace Express — Aircraft Authoring Brief (for an AI assistant)

You are an aircraft designer for **Solace Express**, a C++17 / OpenGL 3.3 flight simulator with a procedural world. Your job is to produce new aircraft types that drop into the game **as data**, with no renderer or engine work. This document is the complete contract. Follow it exactly; the integrator (another AI, Claude, working in the repository) will paste your output into the source, build it and run the test suite. Anything outside this contract will be sent back.

---

## 0. TL;DR for the model

- Every aircraft is **two C++ table rows**: an `AircraftSpec` (flight model, career data, livery) and a `ModelDef` (3D shape parameters). There are no mesh files, no textures, no external assets. Geometry is a parametric signed-distance model evaluated by the renderer; shading is procedural.
- The renderer is being rebuilt from a per-pixel ray tracer into a rasterizer. **The rebuilt renderer bakes triangle meshes from the same parametric model at load time**, so a correctly written table row works in both the current and the new renderer with zero changes. That is what "easily implemented" means here: stay inside the parametric model (Tier A below).
- Hand-written distance-field airframes (Tier B, how the two research jets are built) are only for shapes the parametric model cannot express. They are accepted only if they follow the part/rig rules in §6, because the mesh bake needs them.
- Output format is fixed (§7). Always include the self-check table (§5). Never invent fields, never reorder fields, never change existing rows.

---

## 1. Coordinate system, units, conventions

- Body coordinates: **+x right, +y up, +z aft** (towards the tail). Origin at the aircraft's centre of gravity. The nose is at negative z.
- Units: metres, kilograms, seconds, radians inside the renderer; **degrees where the field says deg**; speeds in **m/s** in `AircraftSpec` (the UI converts to knots); power in **watts per engine** for props, **newtons of thrust per engine** for jets; prices in game currency.
- The world is a 80 km × 80 km archipelago, so "range" is **game-scale** (45–300 km), not real-world.
- The table rows are C++ aggregate initialisers. Field order is law. Floats carry an `f` suffix (`0.62f`), ints are plain, booleans are `true`/`false`, enums by name, colours as `vec3(r, g, b)` in linear 0–1.

---

## 2. Where aircraft live in the code

| What | File | Notes |
|---|---|---|
| Flight model + career + livery table | `src/aircraft.cpp` → `const AircraftSpec kAircraft[]` | Struct in `src/aircraft.h` |
| 3D model table | `src/models.cpp` → `const ModelDef kModels[]` | Struct in `src/models.h` |
| Row count rule | `kNumAircraft = sizeof(kAircraft)/sizeof(kAircraft[0]) - 2` | The **last two rows are the hidden research craft** (`xr9`, `xr11`). New career aircraft must be inserted **before** the `xr9` row in **both** tables, in the **same position**, because the tables are index-aligned (`kModels[spec - kAircraft]`). |
| Research craft indices | `src/aircraft.h` → `kResearchJet = 7`, `kWraith = 8` | Inserting N career rows before them means these two constants must be increased by N. Say so explicitly in your output. |
| Save files | by `id` string | `id` must be unique, lowercase ASCII, stable forever. |

Everything else is derived automatically from the two rows: engine sound (type, cylinders, blades), drag build-up (`aero.cpp` from fuselage/wing/tail geometry), autopilot performance learning (`Plane::perf` flies each type at start-up), AI traffic (new types appear as traffic), ATC call signs (registration from the `id` hash), hangar/market UI, README performance table (`flight_test --table`), hull and mesh baking.

---

## 3. `AircraftSpec` — field by field

Struct (from `src/aircraft.h`), in order:

```cpp
struct AircraftSpec {
  const char* id; const char* name; const char* role;
  int engineType, engines, cylinders, blades;   // ENG_PISTON / ENG_TURBOPROP / ENG_JET; engines 1..4; cylinders (piston only, else 0); prop blades (0 for jets)
  float idleRpm, maxRpm;       // piston: prop rpm; turboprop: N1 % scale (e.g. 1100, 1900); jet: 0, 0
  float emptyMass, maxFuel, cargoKg; int pax;   // kg, kg, kg, seats (excluding the pilot)
  float wingArea, span, chord; // m^2, m, m (mean chord)
  float CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, oswald;   // CLa, CD0, gearCD and oswald are REFERENCE values only: aero.cpp derives its own from the geometry. CL0, CLmax, flapCL, flapCD are used.
  float power;                 // W per engine (prop) or N thrust per engine (jet)
  float v0;                    // static-thrust knee speed for props (m/s); 0 for jets
  float vr, vref, cruise;      // rotate, approach reference, cruise speeds (m/s)
  float rangeKm, runwayM;      // game-scale design range; runway needed at sea level (m) — used until the type's learned take-off/landing distances replace it
  bool roughOK, taildragger, retract;
  float Ixx, Iyy, Izz;         // roll, pitch, yaw inertia (kg m^2)
  float elevPow, ailPow, rudPow;   // control power scalars; stay within 0.38–0.45 / 0.045–0.07 / 0.05–0.07 for conventional aircraft
  int license; int price; int rentFee;   // LIC_STUDENT / LIC_PPL / LIC_CPL / LIC_ATP; rentFee 0 = not rentable
  // visual / aero geometry
  float fusLen, fusRad, wingY, wingZ; int engLayout, tail;   // fuselage length and max half-height (m); wing root height above CG and root LE z (m); engLayout 0 nose, 1 wing nacelles, 2 aft fuselage; tail 0 conventional, 1 T-tail
  vec3 colBase, colStripe;     // livery: base paint and accent (cheat line, wingtips, fin flash)
  int special = 0;             // 0 for every career aircraft. 1 and 2 are the research jets. Never use.
};
```

Column header used in the table (keep it as a comment above your row):

```
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
```

Reference rows (exact, from the game):

```cpp
{"kestrel", "Kestrel T2", "Two-seat trainer", ENG_PISTON, 1, 4, 2, 750, 2600, 530, 70, 120, 1, 14.9f, 10.1f, 1.5f,
 0.30f, 4.8f, 1.45f, 0.55f, 0.030f, 0.004f, 0.045f, 0.75f, 110000, 22, 25, 30, 50, 45, 400, false, false, false,
 900, 1300, 1900, 0.40f, 0.060f, 0.060f, LIC_STUDENT, 18000, 120,
 7.3f, 0.62f, 1.19f, -0.92f, 0, 0, vec3(0.92f, 0.92f, 0.95f), vec3(0.85f, 0.12f, 0.10f)},
{"pelican", "Pelican Caravan", "Single turboprop hauler", ENG_TURBOPROP, 1, 0, 3, 1100, 1900, 2150, 420, 1400, 12, 25.9f, 15.9f, 1.95f,
 0.32f, 5.0f, 1.60f, 0.90f, 0.030f, 0.005f, 0.060f, 0.78f, 540000, 30, 31, 38, 85, 130, 550, true, false, false,
 12000, 14000, 24000, 0.40f, 0.052f, 0.060f, LIC_CPL, 120000, 1600,
 11.5f, 0.92f, 1.09f, -1.83f, 0, 0, vec3(0.95f, 0.95f, 0.95f), vec3(0.85f, 0.45f, 0.05f)},
{"starling", "Starling 500 Jet", "Light business jet", ENG_JET, 2, 0, 0, 0, 0, 4600, 1100, 700, 7, 30.0f, 15.9f, 2.0f,
 0.25f, 5.0f, 1.40f, 0.75f, 0.022f, 0.012f, 0.070f, 0.80f, 15000, 0, 55, 62, 200, 260, 1250, false, false, true,
 30000, 60000, 85000, 0.42f, 0.050f, 0.055f, LIC_ATP, 260000, 0,
 14.0f, 0.95f, -0.58f, 1.0f, 2, 1, vec3(0.97f, 0.97f, 0.97f), vec3(0.55f, 0.08f, 0.12f)},
```

Physical consistency the tests enforce (see §5): the aircraft must take off within `0.9 × runwayM`, climb, hold altitude and heading on autopilot within its g limits, respond in the correct sense to each control, and complete a comfortable autopilot route to a landing. Unflyable numbers fail CI.

---

## 4. `ModelDef` — the 3D shape

Struct (from `src/models.h`), in order:

```cpp
struct ModelDef {
  float st[8][4];        // fuselage stations nose -> tail: {z, half width, half height, centre y}; z strictly increasing; st[0] is the nose tip, st[7] the tail tip (both small radii)
  float roundness;       // cross-section: 1 = ellipse, 0 = rounded box (0.3–0.65 reads as a cabin with flat sides)
  float wing[8];         // half span, root chord, tip chord, LE sweep at the tip (m aft), root y, root LE z, dihedral (deg), thickness ratio
  int strut; float strutX, winglet, flapFrac; int slats, deice;   // lift struts (0/1) and their spanwise attach x; winglet height (0 none); flaps end at this fraction of the half span (ailerons run from there to 0.94); leading-edge slats (0/1); dark de-ice boots on the leading edges (0/1)
  float ht[7]; int ttail; // horizontal tail: half span, root chord, tip chord, LE sweep, y, LE z, dihedral (deg); ttail 1 mounts it on the fin top (y/z then ignored)
  float vt[6];           // fin: height, root chord, tip chord, LE sweep, base y, root LE z
  int engine; float nacX, nacY, nacR, nacZ0, nacLen, spinnerR, propR;   // engine 0 nose piston, 1 nose turboprop, 2 wing piston nacelles, 3 wing turboprop nacelles, 4 aft fuselage jets (5 and 6 are reserved for the research craft). nacX/nacY: nacelle axis (mirrored), nacR radius, nacZ0 front z, nacLen length; spinnerR, propR for props (0 for jets)
  int gear; float wheelR; // gear 0 fixed tricycle with spats, 1 fixed tricycle, 2 taildragger with tundra tyres, 3 retracts into the nacelles (needs engine 3), 4 retracts into wing/body; main wheel radius (m)
  int cargoPod;          // belly cargo pod (0/1)
  int winCount; float winZ0, winZ1, winY, winW, winH;   // passenger windows: count, first/last z, height above the section centre line, half width, half height (all 0 for none)
  vec3 eye; int cockpit; // pilot eye position (body coords); cockpit 0 analog single cluster, 1 analog twin (pilot + copilot clusters), 2 glass (PFDs, centre display, leather)
  float wsZ0, wsZ1, wsY, sideZ1;   // windshield from z0 to z1 above height wsY; side windows run from wsZ1 aft to sideZ1
};
```

Reference row (the Wren 180, annotated):

```cpp
{ // stations: z, halfW, halfH, centreY — a 4-seat tourer, 8.3 m long
  {{-4.15f,0.100f,0.090f,-0.020f},{-3.92f,0.380f,0.350f,-0.050f},{-2.80f,0.520f,0.445f,-0.035f},{-1.80f,0.600f,0.645f,0.095f},
   {-0.20f,0.620f,0.645f,0.115f},{1.60f,0.450f,0.420f,0.160f},{3.40f,0.150f,0.190f,0.290f},{4.15f,0.060f,0.090f,0.350f}}, 0.86f,
  {5.50f,1.62f,1.00f,.55f,.80f,-1.85f,1.7f,.13f},   // wing: 11 m span, 1.62/1.00 m chords, 0.55 m tip sweep, root at y .80 z -1.85, 1.7 deg dihedral, 13% thick
  0,0,0,.55f, 0,0,                                   // no struts, no winglets, flaps to 55% span, no slats, no de-ice
  {1.75f,1.05f,.72f,.18f,.32f,3.00f,0}, 0,           // horizontal tail; conventional
  {1.62f,1.45f,.50f,1.02f,.27f,2.60f},               // fin
  0, 0,0,0,0,0, .14f,.95f,                           // nose piston engine, spinner r .14, prop r .95
  1, .26f, 0,                                        // fixed tricycle gear, wheel r .26, no cargo pod
  1, .50f,1.72f,.22f,.27f,.20f,                      // one cabin window per side between z .50 and 1.72
  vec3(-.30f,.54f,-1.30f), 0, -2.60f,-1.75f,.36f,0.45f },   // eye (left seat), analog single cockpit, windshield z -2.60..-1.75 above y .36, side windows to z 0.45
```

### 4.1 Geometry rules the shape generator relies on

1. **Stations.** Exactly 8. `z` strictly increasing. The spline through them is monotone-cubic, so a bulge between two stations needs a station there. Keep `half height` ≥ `half width × 0.5` in the cabin, and make `st[0]`/`st[7]` tiny (≤ 0.1 m) so the nose and tail close. `fusLen` in the spec ≈ `st[7].z − st[0].z`; `fusRad` in the spec ≈ the largest half height.
2. **Wing.** Thickness ratio 0.10–0.16. The fuselage/wing fillet is automatic. `wing[4]` (root y) sets high/low wing: above the cabin centre line for a high wing (≈ `centreY + halfH`), below it for a low wing (≈ `centreY − halfH × 0.7`). For a low wing the spar passes under the cabin floor; keep `eye.y − 1.08` (the floor) above the wing's upper surface at the root.
3. **Tail.** T-tail: set `ttail = 1`, `ht[4..5]` are recomputed. Conventional: place `ht` at `vt` base height or slightly above, LE z near the fin root LE.
4. **Engines.** Nose engines take the spinner from `st[0]`; set `spinnerR` ≈ `st[1].halfW × 0.35` and `propR` so the disc clears the ground by ≥ 0.25 m at the computed gear height (`gearHeight = fusRad×1.3 + 0.55` for singles, `+0.75` for twins/jets, `fusRad + 0.45` for taildraggers). Wing nacelles: `nacY` slightly below the wing surface at `nacX`, `nacZ0` ahead of the wing LE by ~0.8 × `nacR` so the spinner sits in front. Aft jets (`engine 4`): nacelles alongside the rear fuselage with pylons generated automatically; set `nacX ≈ st[5].halfW + nacR + 0.4`.
5. **Gear.** You only choose the kind and `wheelR`. Track, wheel stations and the retraction wells are derived (`track = max(1.2, 0.13 × span)`; mains at `0.04 L` behind the CG, nose at `−0.36 L`, or `−0.10 L` for a taildragger's mains). `gear 3` requires `engine 3`; `gear 4` wells are cut in the wing underside at the track, so the wing must be there (low wing, or a wing root wide enough).
6. **Cabin.** The pilot's eye must be inside the fuselage with ≥ 8 cm clearance to the roof (`cabinRoof` at the eye z) and the instrument panel is placed automatically at `eye.z − 0.68` (`− 0.85` for glass cockpits) with half width `0.93 × halfWidth(panelZ)`. The windshield (`wsZ0..wsZ1`, base `wsY`) must sit ahead of the panel and below the roof; side windows `wsZ1..sideZ1`. Seats, yoke, pedals, pedestal, overhead console, visors and trim are generated from these numbers; they are fitted to the section automatically.
7. **Windows.** `winY` is relative to the section centre line at that z; `winW`/`winH` are half sizes; keep them inside `halfH × 0.8`.
8. **Livery.** `colBase` paints fuselage, wings, tail; `colStripe` paints the cheat line, wingtips and fin flash. Registration letters, panel seams, rivets, door outlines, fuel caps, antennas, pitot and static wicks are added automatically. Avoid pure white (`vec3(1)`) and pure black; the sun will blow them out.

### 4.2 What the new renderer does with this

At load the renderer evaluates the parametric distance field of each type and extracts triangle meshes per moving part (control surfaces, flaps, gear, doors, yoke, pedals, levers), with per-vertex normals and ambient occlusion baked from the same field. Materials stay procedural. Nothing in this section asks you for mesh data and nothing you write is view-dependent; keep it that way.

---

## 5. Self-check table (mandatory in every output)

Compute and print these before the rows. Use `g = 9.81`, `ρ = 1.225`, mass at **max weight** `m = emptyMass + maxFuel + cargoKg + 90·pax + 90`.

| Check | Formula | Required |
|---|---|---|
| Wing loading | `m·g / wingArea` (N/m²) | 400–1200 trainers/tourers, up to 4000 airliners/jets |
| Stall speed, clean | `vs1 = sqrt(2·m·g / (ρ·S·CLmax))` | report |
| Stall speed, full flap | `vs0 = sqrt(2·m·g / (ρ·S·(CLmax+flapCL)))` | report |
| Rotate | `vr ≈ 1.10–1.15 × vs0` | `vr > vs0` |
| Approach | `vref ≈ 1.3 × vs0` | `vref > vr` |
| Cruise | realistic for the power; `cruise ≥ 1.6 × vref` | `cruise > vref` |
| Power loading (props) | `m / (power/1000 · engines)` kg/kW | 5–9 light aircraft, 4–6 turboprops |
| Thrust/weight (jets) | `power·engines / (m·g)` | 0.25–0.40 civil |
| Runway | `runwayM` consistent with wing loading and power (trainer ≈ 400, bush ≈ 220, twin ≈ 450, turboprop ≈ 550, regional ≈ 1100, bizjet ≈ 1250) | report |
| Inertia | `Ixx ≈ 0.12·m·(span/2)²`, `Iyy ≈ 0.18·m·(fusLen/2)²`, `Izz ≈ Ixx + Iyy` (± 40 %) | within range |
| Geometry | `fusLen ≈ st[7].z − st[0].z`, `fusRad ≈ max halfH`, `span ≈ 2·wing[0]`, `wingArea ≈ 2·wing[0]·(wing[1]+wing[2])/2 + fuselage carry-through` | within 10 % |
| Prop clearance | nose prop: `gearHeight + st[0].centreY − propR`; nacelle props: `gearHeight + nacY − propR` (the CG sits `gearHeight` above the ground, §4.1 item 4) | ≥ 0.25 m |
| Career fit | `license`/`price`/`rentFee` between neighbours of similar size; `rangeKm` 40–300 | sensible |

The integrator runs: `cmake --build build && ctest --test-dir build` (flight model, progression, saves, gameplay loop, airport layouts, envelope, hull), then renders the type with the harness scenes `gav_<index>_<yaw>_<pitch>_<dist>` (parked on the runway, orbit view: e.g. `gav_3_120_10_0`, `gav_3_210_5_0`, `gav_3_60_35_0`, with `GAVOUT=1` for the outside model) and `ckv<index>_<yaw>_<pitch>_<hour>` (cockpit view: `ckv3_0_-8_11`, `ckv3_-60_-20_11`, `ckv3_0_-45_11`), and on the owner's GPU `SolaceExpress.exe --shots gav_3_120_10_0,ckv3_0_-8_11 --size 1920x1080`. Design for those views.

---

## 6. Tier B — hand-built airframes (only when Tier A cannot express the shape)

The two research jets (`mapJet`, `mapWraith` in `src/shaders/raytrace_fs.glsl` and `raytrace_wraith*.glsl`) are hand-written GLSL signed-distance functions. A Tier B aircraft is a new such function. It costs integration work (dispatch, material ids, part table), so propose it only with a reason. The rules below exist because the rebuilt renderer **bakes meshes from the function at load**; break them and the bake produces holes, slivers or frozen animation.

The `PART(...)`, `partOn(id)` and `rig*(p, id)` helpers in rule 5 are the renderer rebuild's contract, not yet functions in the tree: write to the contract, and the integrator maps it onto whichever shader version is current. Everything else in this section (primitives, globals, material ids) exists today.

1. **Signature.** `vec2 mapCustom_<Name>(vec3 p)` returning `(signed distance, material id)`. Negative inside. The function must be a **lower bound of the true distance** everywhere (Lipschitz constant ≤ 1): compose only with `min`, `smin` (blend radius ≤ 0.35 m), `max` with a negated primitive (subtraction), `opU`, and the primitive library (`sdBox`, `sdRoundBox`, `sdCapsule`, `sdEllipsoid`, `sdTorus`, `sdRoundCone`, `sdCylX`, `sdRoundCylX`, `sdPanel`, `sdSurface`). Never scale a distance by a factor > 1, never return a non-distance (a plane equation, a product, a texture lookup).
2. **Bounding sphere.** Everything must lie within radius `planeBound() = max(fusLen, span)·0.55 + 1.5` of the origin, in every state.
3. **Closed solids.** Every part is a watertight solid with a minimum feature thickness of **8 mm** (antennas, wicks, LED strips: radius ≥ 4 mm). No infinitely thin sheets, no zero-thickness `max(abs(d) − 0, …)` shells: use `abs(d + t) − t`.
4. **State, not time.** Geometry may depend only on the state vector: `gPS = (gear 0..1, flaps 0..1, steer rad, insideCockpit 0/1)`, `gCtl = (pitch, roll, yaw −1..1, throttle 0..1)`, `gFlame = (spool, reheat, nozzle angle, mach)`, prop angle `uPr.x`, and up to 8 type-specific channels you declare (`uCustom[8]`). **`uTime` may be used in shading only, never in geometry.**
5. **Parts and rigs.** Every piece that moves is one **part**, declared in a table you supply:
   `PART(id, "name", kind, params)` with `kind` ∈ { `STATIC`, `HINGE(pivot, axis, angle = k·state + c)`, `SLIDE(dir, dist = k·state + c)`, `SURFACE(span, rootChord, tipChord, sweep, hingeFrac, s0, s1, deflection = k·state, slide = k·state)` (exactly the `sdSurface` parameters), `SPIN(pivot, axis, angle = state)`, `STRETCH(origin, axis, length = k·state + c)` }.
   Inside the function, wrap each part's primitives in `if (partOn(id)) { ... }` and express its motion **only** through the matching helper (`rigHinge(p, id)`, `rigSlide(p, id)`, …) that transforms `p` into the part's rest frame. The bake evaluates each part alone in its rest pose and animates it with the same table; anything moved by hand-written math is baked frozen.
   Cut-outs that open with a part (gear wells, bay cavities, hatches) belong to the **static body** and are always cut; their doors are parts that close flush at rest. The body must look right with every door closed and every well cut.
6. **Material ids.** Use the shared table: 1 fuselage paint, 2 wing paint, 3 tail paint, 5 nacelle, 6 rubber/tyre, 8 bare metal (legs, antennas), 16 spinner, 17 exhaust, 18 nav lights (red x<0 / green x>0), 19 beacon, 21 fan face, 94 light-fixture housing, 95–100 lenses. Cockpit: 10 panel (instruments are drawn on it), 11 shell/floor, 12 seats, 13 controls, 14 glareshield, 60 brushed metal, 61 rubber, 63 trim, 64 lenses, 65 radio stack, 66 satin black, 67 centre display, 68 red knobs, 69 harness. New looks need a new id in 110–127 **with a one-line material description** (albedo, roughness, metalness, emission, pattern); the integrator writes the shading.
7. **Cockpit.** A sealed cockpit (no transparent canopy) shows the outside on display panes fed by cameras you place (`feed_cameras.h` rig: pane centre, normal, half size); an open cockpit uses real window openings cut in the shell. Say which. The eye position comes from `ModelDef.eye`.
8. **Deliver with it.** The Tier A `AircraftSpec` row (it still drives physics), a `ModelDef` row with `engine` set to the next free code (7+) and stations that approximate the hull (the hull mesh and the camera feeds use them), the part table, the material notes, and a list of harness views that exercise every part at both ends of its travel.

---

## 7. Output format (exactly this, per aircraft)

```
### <Name> — <role in five words>
Design brief: 3–6 sentences. Real-world analogue(s), dimensions, why it fits the fleet (gap it fills in licence / price / runway / range).

Self-check: the table from §5 with your numbers.

Integration notes: index to insert at (before `xr9`), new values of kResearchJet/kWraith, anything the integrator must know.

```cpp
// aircraft.cpp — insert before the xr9 row
{ ...AircraftSpec row... },
```

```cpp
// models.cpp — insert at the same index
{ ...ModelDef row with inline comments per group... },
```

Harness views to look at: gav_<i>_120_10_0, gav_<i>_210_5_0, gav_<i>_60_35_0, ckv<i>_0_-8_11, ckv<i>_-60_-20_11
Optional: a README table row placeholder (the integrator regenerates the table from the learned performance).
```

Use `<i>` for the new row's index. Produce one aircraft per message unless asked for a set; for a set, keep the fleet coherent (no two aircraft in the same licence/price/runway slot).

---

## 8. Do not

- Do not modify existing rows, enums, structs, shaders or tests.
- Do not add fields, assets, textures, meshes, or references to external files.
- Do not use `special` ≠ 0, `engine` 5/6, or any value outside the documented ranges.
- Do not propose physics that the §5 checks cannot justify.
- Do not place the eye, panel or windshield where §4.1 rules would be violated; the cabin generator cannot recover from that.
- Do not write Tier B code unless a Tier A shape is genuinely impossible, and then follow §6 to the letter.

---

## 9. Fleet today (for gap analysis)

| Aircraft | Type | Seats / Cargo | Range | Cruise | Runway | Licence | Price / Rent |
|---|---|---|---|---|---|---|---|
| Kestrel T2 | Two-seat trainer | 1 / 120 kg | 45 km | 116 kt | 401 m paved | Student | 18 000 / 120 |
| Wren 180 | Four-seat tourer | 3 / 320 kg | 70 km | 128 kt | 437 m paved | PPL | 30 000 / 250 |
| Bushmaster STOL | Backcountry taildragger | 4 / 480 kg | 72 km | 131 kt | 257 m, gravel/snow | CPL | 40 000 / 450 |
| Islander Twin | Nine-seat utility twin | 9 / 900 kg | 90 km | 129 kt | 458 m, gravel/snow | CPL | 85 000 / 900 |
| Pelican Caravan | Single turboprop hauler | 12 / 1400 kg | 130 km | 149 kt | 553 m, gravel/snow | CPL | 120 000 / 1600 |
| Meridian Q400 | Regional turboprop airliner | 40 / 4500 kg | 170 km | 266 kt | 1129 m paved | ATP | 600 000 / 8000 |
| Starling 500 | Light business jet | 7 / 700 kg | 260 km | ~390 kt | 1250 m paved | ATP | 260 000 / — |

Obvious gaps: a PPL-level four-seat retractable (fast tourer), a six-seat piston twin, a float/amphibian is **not** possible (no water physics), a light sport two-seater below the Kestrel, a 19-seat turboprop commuter, a medium bizjet or small regional jet, a vintage biplane is **not** possible (single wing only), an agricultural/utility taildragger, a glider is **not** possible (engine required).

Airports range from 600 m grass/gravel strips to 2800 m international runways; the world has one 1650 m-elevation gravel strip (thin air). Design runway needs accordingly.
