# Additional aircraft — authoring contract

Prepared against `db08f7aa334cf3e7c04ed99c6f5caa5bdca63ca6` and the supplied Aircraft Authoring Brief. Both are Tier A: the four rows below work with the unmodified generic aircraft renderer, ordinary fuel use and shared flight physics. The separately requested fleet gear and XR-9 canopy patch is independent.

### Swift S6 — Four seat retractable piston tourer

Design brief: Swift combines the compact proportions of an Arrow-style touring piston with a softly rounded cabin and tapered cantilever wing. Its 8.60 m fuselage and 11.40 m span provide four seats, including the pilot, in cream and deep teal. It fills a PPL touring slot above the Wren through cruise speed, range and retractable-gear management, while retaining familiar analog instruments. At 68,000 / 600 rent, 140 km design range and a 600 m nominal paved runway, it leaves inexpensive training and rough-strip work to the existing aircraft.

Self-check:

| Check | Formula / required | Result |
|---|---|---|
| Max weight | empty + fuel + cargo + 90×pax + pilot | 980 + 180 + 420 + 270 + 90 = **1940 kg** |
| Wing loading | m×9.81 / S | 1112.95 N/m²; within 400–1,200 tourer |
| Stall, clean | sqrt(2×m×9.81 / (1.225×S×CLmax)) | 33.185 m/s |
| Stall, full flap | sqrt(2×m×9.81 / (1.225×S×(CLmax+flapCL))) | 27.345 m/s |
| Rotate | vr / vs0, required 1.10–1.15 | 31 m/s = 1.134×vs0 |
| Approach | vref ≈ 1.3×vs0; above rotate | 36 m/s = 1.317×vs0; above 31 |
| Cruise | cruise ≥ 1.6×vref | 76 m/s ≥ 57.6 m/s |
| Power loading | 5–9 kg/kW | 7.462 kg/kW (1940 / 260) |
| Runway | nominal runwayM; flight liftoff < 0.9×runwayM | 600 m; liftoff limit 540 m (measured flights in validation report) |
| Inertia | 0.12×m×(b/2)²; 0.18×m×(L/2)²; sum, ±40% | Reference 7563.7 / 6456.7 / 14020.4; set 7600 / 6500 / 14100 kg m² |
| Geometry | station length; max halfH; 2×half span; trapezoidal S | 8.60 m; 0.66 m; 11.40 m; 5.70×(1.98+1.02)=17.10 m²; all exact |
| Prop clearance | gearHeight + nose centreY − propR | 0.323 m ≥ 0.25 m |
| Career fit | licence, price/rent, game-scale range | LIC_PPL; 68000 / 600; 140 km within 40–300 |
| Control power | elevator .38–.45; aileron .045–.07; rudder .05–.07 | 0.420 / 0.065 / 0.068 |
| Cabin / wing | roof clearance ≥ 0.08 m; floor above root wing top | Checked with the native monotone station spline and conservative root half-thickness; see validation report |
| Gear wells | wing present across the full opening at derived track | Both ends of each main bay lie between the local leading and trailing edges; see validation report |

Integration notes: insert at **index 7**, before `xr9`, in both arrays. For the set, Swift is 7, Nightjar is 8, `kResearchJet = 9`, `kWraith = 10`, and automatic `kNumAircraft` becomes 9. Keep the original XR-9 / XR-11 rows last, and keep IDs `swift_s6` stable. The laboratory appends candidates at 9 / 10 solely to leave production indices unchanged during testing; do not copy that laboratory count override into the game. See the source-verified coordinate correction below.

```cpp
// aircraft.cpp — insert before xr9; Tier A, all conventional shared flight behaviour.
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"swift_s6", "Swift S6", "Retractable low-wing tourer", ENG_PISTON, 1, 6, 3, 700.0f, 2700.0f,
 980.0f, 180.0f, 420.0f, 3, 17.10f, 11.40f, 1.552f,
 0.28f, 4.9f, 1.65f, 0.78f, 0.025f, 0.010f, 0.052f, 0.80f,
 260000.0f, 23.0f, 31.0f, 36.0f, 76.0f, 140.0f, 600.0f, false, false, true,
 7600.0f, 6500.0f, 14100.0f, 0.42f, 0.065f, 0.068f, LIC_PPL, 68000, 600,
 8.60f, 0.66f, -1.121212f, -0.155f, 0, 0,
 vec3(0.91f, 0.88f, 0.80f), vec3(0.13f, 0.26f, 0.32f), 0},
```

```cpp
// models.cpp — insert at the Swift AircraftSpec index; +x right, +y up, +z aft.
{ // Eight closed nose-to-tail fuselage stations: z, half width, half height, centre y.
  {{-4.35f,.10f,.09f,-.055f},{-4.10f,.35f,.31f,-.060f},
   {-2.95f,.49f,.41f,-.035f},{-1.90f,.62f,.66f,.075f},
   {-.25f,.63f,.65f,.105f},{1.55f,.43f,.43f,.160f},
   {3.70f,.14f,.18f,.270f},{4.25f,.055f,.075f,.315f}}, .94f, // elliptical cabin
  {5.70f,1.98f,1.02f,.72f,-.74f,-1.13f,5.0f,.125f}, // low tapered wing; spar below floor
  0,0.0f,.34f,.57f,0,0,                               // no struts; winglets; 57% flaps
  {1.90f,1.08f,.62f,.40f,.28f,3.06f,0.0f},0,           // conventional horizontal tail
  {1.55f,1.55f,.55f,.76f,.23f,2.70f},                  // fin and rudder
  0,0.0f,0.0f,0.0f,0.0f,0.0f,.13f,1.03f,             // nose piston; spinner and prop
  4,.28f,0,                                         // wing/body retracts; no cargo pod
  1,.35f,1.45f,.24f,.28f,.205f,                       // one passenger window per side
  vec3(-.32f,.52f,-1.40f),0,-2.80f,-2.14f,.31f,.50f }, // left-seat analog; windshield ahead of panel
```

Harness views to look at: `gav_7_120_10_0`, `gav_7_210_5_0`, `gav_7_60_35_0`, `ckv7_0_-8_11`, `ckv7_-60_-20_11`. Regenerate the README performance row with `flight_test --table` after insertion; nominal speeds are design values, not the learned table.

### XR-14 Nightjar — Civil twin jet research demonstrator

Design brief: Nightjar combines an X-29-inspired tapered wing silhouette with the slender body and aft nacelles of a small business jet. It is a fictional, conventional aerodynamic test aircraft: its leading edge is nearly straight, while its quarter-chord line sweeps mildly forward. The 16.30 m body, 16.60 m span, graphite/teal paint, large fin and raised conventional tailplane distinguish it from the fleet without a custom distance function. Its ATP licence, 340,000 purchase price, 240 km design range and 1,250 m nominal runway place it above the Starling as an optional equipment-carrying test platform, with no passenger seats. The offset glass cockpit uses ordinary windows and instruments; the aircraft has normal fuel, control and structural limits.

Self-check:

| Check | Formula / required | Result |
|---|---|---|
| Max weight | empty + fuel + cargo + 90×pax + pilot | 5700 + 1300 + 100 + 0 + 90 = **7190 kg** |
| Wing loading | m×9.81 / S | 1847.40 N/m²; within 400–4,000 jet |
| Stall, clean | sqrt(2×m×9.81 / (1.225×S×CLmax)) | 44.113 m/s |
| Stall, full flap | sqrt(2×m×9.81 / (1.225×S×(CLmax+flapCL))) | 37.898 m/s |
| Rotate | vr / vs0, required 1.10–1.15 | 43 m/s = 1.135×vs0 |
| Approach | vref ≈ 1.3×vs0; above rotate | 50 m/s = 1.319×vs0; above 43 |
| Cruise | cruise ≥ 1.6×vref | 150 m/s ≥ 80.0 m/s |
| Thrust / weight | 0.25–0.40 civil | 0.354 (25000 N / 70533.9 N) |
| Runway | nominal runwayM; flight liftoff < 0.9×runwayM | 1250 m; liftoff limit 1125 m (measured flights in validation report) |
| Inertia | 0.12×m×(b/2)²; 0.18×m×(L/2)²; sum, ±40% | Reference 59438.3 / 85964.0 / 145402.3; set 60000 / 86000 / 146000 kg m² |
| Geometry | station length; max halfH; 2×half span; trapezoidal S | 16.30 m; 0.80 m; 16.60 m; 8.30×(3.50+1.10)=38.18 m²; all exact |
| Prop clearance | gearHeight + nose centreY − propR | Not applicable: two aft jets, no props |
| Career fit | licence, price/rent, game-scale range | LIC_ATP; 340000 / 0; 240 km within 40–300 |
| Control power | elevator .38–.45; aileron .045–.07; rudder .05–.07 | 0.420 / 0.060 / 0.065 |
| Cabin / wing | roof clearance ≥ 0.08 m; floor above root wing top | Checked with the native monotone station spline and conservative root half-thickness; see validation report |
| Gear wells | wing present across the full opening at derived track | Both ends of each main bay lie between the local leading and trailing edges; see validation report |

Integration notes: insert at **index 8**, before `xr9`, in both arrays. For the set, Swift is 7, Nightjar is 8, `kResearchJet = 9`, `kWraith = 10`, and automatic `kNumAircraft` becomes 9. Keep the original XR-9 / XR-11 rows last, and keep IDs `xr14_nightjar` stable. The laboratory appends candidates at 9 / 10 solely to leave production indices unchanged during testing; do not copy that laboratory count override into the game. See the source-verified coordinate correction below.

```cpp
// aircraft.cpp — insert before xr9, after Swift. Tier A civil research demonstrator; special=0.
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"xr14_nightjar", "XR-14 Nightjar", "Tapered-wing research demonstrator", ENG_JET, 2, 0, 0, 0.0f, 0.0f,
 5700.0f, 1300.0f, 100.0f, 0, 38.18f, 16.60f, 2.508f,
 0.15f, 4.6f, 1.55f, 0.55f, 0.020f, 0.010f, 0.045f, 0.80f,
 12500.0f, 0.0f, 43.0f, 50.0f, 150.0f, 240.0f, 1250.0f, false, false, true,
 60000.0f, 86000.0f, 146000.0f, 0.42f, 0.060f, 0.065f, LIC_ATP, 340000, 0,
 16.30f, 0.80f, -0.9375f, -1.145f, 2, 0,
 vec3(0.14f, 0.18f, 0.20f), vec3(0.20f, 0.78f, 0.70f), 0},
```

```cpp
// models.cpp — same index as Nightjar AircraftSpec; existing engine 4 and gear 4 only.
{ // Eight closed stations; enough roof height for the offset glass-cockpit eye.
  {{-8.30f,.035f,.035f,-.035f},{-6.65f,.28f,.22f,-.030f},
   {-5.05f,.56f,.49f,-.010f},{-3.55f,.77f,.79f,.045f},
   {-.30f,.93f,.80f,.075f},{3.10f,.78f,.72f,.075f},
   {6.75f,.28f,.33f,.155f},{8.00f,.065f,.090f,.225f}}, .77f,
  {8.30f,3.50f,1.10f,.08f,-.75f,-1.50f,2.0f,.11f},   // near-straight LE, mildly forward quarter-chord; 11% thick
  0,0.0f,.42f,.58f,0,0,                              // winglets; 58% flaps; no special surfaces
  {2.65f,1.55f,.80f,.48f,.83f,5.72f,0.0f},0,          // conventional raised horizontal tail
  {2.22f,2.28f,.77f,1.40f,.55f,4.85f},                // tall swept fin
  4,1.63f,.16f,.45f,2.54f,3.37f,0.0f,0.0f,            // ordinary aft twin jets and generated pylons
  4,.35f,0,                                        // wing/body retracts; no cargo pod
  0,0.0f,0.0f,0.0f,0.0f,0.0f,                       // no passenger windows
  vec3(-.42f,.59f,-3.30f),2,-5.45f,-4.23f,.30f,-2.20f }, // glass cockpit; avoid centre-display overlap using data
```

Harness views to look at: `gav_8_120_10_0`, `gav_8_210_5_0`, `gav_8_60_35_0`, `ckv8_0_-8_11`, `ckv8_-60_-20_11`. Regenerate the README performance row with `flight_test --table` after insertion; nominal speeds are design values, not the learned table.

## Source-verified corrections to the brief

The supplied brief calls `AircraftSpec.wingY` and `wingZ` the root position in metres. In this source, coarse physics contacts instead use `wingY * fusRad` and `wingZ` for the wingtip contact. The rows therefore use the native convention: Swift `wingY=-0.74/0.66`, Nightjar `wingY=-0.75/0.80`; their `wingZ` values are the tip quarter-chord z. Actual root coordinates remain in `ModelDef.wing[4..5]`, in metres. Using the brief's literal spec coordinates here would put the physics contacts at the wrong heights. This is a documentation discrepancy, not a new aircraft-specific physics rule.

Retracting nose gear is also placed at `max(-0.36*L, st[3].z+0.35)` in `packModel`, rather than always at `-0.36*L`. The models and screenshot probe use that derived station. The pre-existing coarse physics nose contact still uses `-0.36*L`; changing that mismatch is a separate physics/renderer integration decision.

Nightjar's first prototype had negative leading-edge sweep and a central glass-cockpit eye. Those required a bound correction and an engine-display relocation in the current renderer. Neither shader hook is part of these final Tier A rows: the near-straight leading edge fits the original bounds, and the offset eye fits the existing cockpit layout.
