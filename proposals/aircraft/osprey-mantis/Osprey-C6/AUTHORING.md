### Osprey C6 — Six-seat coastal charter piston twin
Design brief: The Osprey C6 is an original six-seat coastal charter aircraft inspired by the general layout of the Piper PA-34 and Beechcraft Baron, rather than a replica of either. A 9.8 m rounded cabin, 12.4 m low cantilever wing, twin three-blade piston engines, conventional swept fin and retractable tricycle gear give it a distinct silhouette alongside the fleet’s high-wing singles and utility Islander. Warm ivory paint with a copper-orange stripe, two broad passenger windows per side and an analog twin cockpit emphasize a small charter operator’s working aircraft. At CPL / 68,000 purchase / 650 rental, it fills the five-passenger paved-airport gap between touring singles and the Islander, with 110 km game-scale range and a conservative 550 m initial runway rating.

Self-check: Uses g=9.81 m/s², rho=1.225 kg/m³ and the brief’s conservative mass m=1420+220+270+90×5+90=2450 kg. Results come from validate.py parsing the delivered C++ rows, not a separate parameter copy.

| Check | Calculation and result | Requirement / result |
|---|---|---|
| Wing loading | 2450×9.81/22.5 = 1068.20 N/m² | 400–1200: PASS |
| Stall speed, clean | sqrt(2×2450×9.81/(1.225×22.5×1.65)) = 32.511 m/s | Reported |
| Stall speed, full flap | sqrt(2×2450×9.81/(1.225×22.5×2.45)) = 26.680 m/s | Reported |
| Rotate | 30.2 m/s = 1.132×Vs0 | 1.10–1.15×Vs0 and >Vs0: PASS |
| Approach | 35.0 m/s = 1.312×Vs0 | Approximately1.3×Vs0 and >Vr: PASS |
| Cruise | Design74.0 m/s / 35.0 = 2.114 | ≥1.6×Vref: PASS; measured cruise is separately reported |
| Power loading | 2450/(180×2) = 6.806 kg/kW | 5–9: PASS;180kW is per engine |
| Thrust/weight | Not applicable; piston props | Not a jet |
| Runway | Initial550m paved; no rough-field authorization | Max-weight simulation takeoff must be<495m; see verification |
| Inertia | Reference Ixx11301.36, Iyy10588.41, Izz21889.77; supplied9000/10800/19800 kg m² | Errors−20.36%/+2.00%/−9.55%; all within±40% |
| Geometry | Length4.90−(−4.90)=9.80m; max half-height0.90m; span2×6.20=12.40m | Exact matches |
| Wing area | Full-span trapezoid6.20×(2.25+1.35)=22.32m²; spec22.50m² | 0.8% difference, within10%; centreline-spanning trapezoid already includes carry-through, so it is not counted twice |
| Prop clearance | Gear height0.90×1.3+0.75=1.920m;1.920−0.540−1.050=0.330m | ≥0.25m: PASS; fuselage lateral disc gap0.53m |
| Career fit | CPL,68000/650 between Bushmaster40000/450 and Islander85000/900;110km range | PASS;5 passengers excludes pilot |

Integration notes: Insert both rows at index7 immediately before xr9 on baseline a298771. Update kResearchJet from7 to8 and kWraith from8 to9. Keep the existing −2 career-count rule because the last two rows remain the existing hidden research craft. If integrating additional aircraft simultaneously, recalculate all indices and research counts for the combined roster rather than applying these offsets blindly. The stable save ID is osprey_c6. No structs, enums, shaders, original rows, materials or game assets need changing.

The aggregate stores special=0, engine=2, engLayout=1, conventional tail=0 and gear=4. Gear3 is intentionally avoided because the authoring contract reserves it for engine3. The main gear x=1.612,z=0.392 lies within the low wing’s local LE−0.657 / TE1.359 interval. Spec wingY/wingZ exactly match the model root values. Mean chord1.82 agrees with22.5/12.4=1.815m.

Cabin checks use the same monotone-cubic stations as the shader. The eye at(−0.34,0.74,−1.90) has0.100926m conservative cabinRoof clearance at its actual x, after the0.035m shell inset; the0.25m centreline margin is not used as the eye clearance. Analog panel z=−2.58, half-width0.643686m; windshield ends at−2.62, ahead of that panel. Floor y=−0.34 is0.0475m above even the conservative wing-upper bound−0.3875, which uses the full thickness ratio rather than half. Passenger-window top stays inside80% of the local half-height. The cabin generator deliberately suppresses headrests where its shoulder/roof rule cannot fit them; this is supported Tier A behavior, not a1m physical clearance.

The baseline mass convention differs from the brief: career payload uses85kg per passenger plus85kg pilot, so full fuel/cargo/people gives2420kg,30kg below the2450kg brief check. Plane::mass itself adds empty+fuel+payload and does not add a pilot automatically. Performance learning’s stall reference uses60% fuel plus150kg payload at1200m; takeoff/landing learning uses the game’s cargo-only convention. The learned market runway is therefore not a guarantee at brief maximum load.

```cpp
// aircraft.cpp — insert before the xr9 row (index 7 at a298771)
// id, name, role, eng, n, cyl, blades, idle, max, empty, fuel, cargo, pax, S, b, c, CL0, CLa, CLmax, flapCL, CD0, gearCD, flapCD, e,
// power, v0, vr, vref, cruise, range, runway, rough, tail, retract, Ixx, Iyy, Izz, elev, ail, rud, lic, price, rent,
// fusLen, fusRad, wingY, wingZ, engLayout, tail, colBase, colStripe
{"osprey_c6", "Osprey C6", "Six-seat coastal charter twin", ENG_PISTON, 2, 6, 3, 700.0f, 2700.0f, 1420.0f, 220.0f, 270.0f, 5, 22.5f, 12.4f, 1.82f,
 0.30f, 4.9f, 1.65f, 0.80f, 0.028f, 0.006f, 0.060f, 0.78f, 180000.0f, 24.0f, 30.2f, 35.0f, 74.0f, 110.0f, 550.0f, false, false, true,
 9000.0f, 10800.0f, 19800.0f, 0.41f, 0.056f, 0.062f, LIC_CPL, 68000, 650,
 9.8f, 0.90f, -0.68f, -0.80f, 1, 0, vec3(0.91f, 0.88f, 0.79f), vec3(0.82f, 0.24f, 0.06f), 0},
```

```cpp
// models.cpp — insert at the same index as AircraftSpec
{ // Fuselage stations: pointed luggage nose, six-seat cabin, tapered tailcone.
  {{-4.90f,0.06f,0.06f,-0.06f},{-4.45f,0.37f,0.35f,-0.04f},{-3.10f,0.63f,0.67f,0.03f},{-2.10f,0.72f,0.90f,0.09f},
   {0.55f,0.72f,0.90f,0.09f},{2.15f,0.49f,0.58f,0.16f},{4.25f,0.16f,0.24f,0.30f},{4.90f,0.06f,0.08f,0.34f}}, 0.72f,
  // Low cantilever wing: span 12.4 m, modest sweep, dihedral; wing-body gear wells.
  {6.20f,2.25f,1.35f,0.55f,-0.68f,-0.80f,4.0f,0.13f},
  0,0.0f,0.0f,0.59f,0,1, // no struts/winglets/slats; flaps to 59%; de-ice boots
  {2.15f,1.35f,0.85f,0.28f,0.34f,3.15f,0.0f},0, // conventional horizontal tail
  {1.85f,1.65f,0.65f,0.85f,0.32f,2.80f}, // tapered swept fin
  2,2.30f,-0.54f,0.40f,-1.50f,2.60f,0.16f,1.05f, // twin three-blade piston nacelles
  4,0.29f,0, // retract into low wing/body; no cargo pod
  2,-0.55f,1.45f,0.25f,0.27f,0.22f, // two large cabin windows per side
  vec3(-0.34f,0.74f,-1.90f),1, // left pilot eye; analog twin cockpit
  -3.05f,-2.62f,0.40f,-0.65f }, // windshield and pilot side windows
```

Harness views to look at: gav_7_120_10_0, gav_7_210_5_0, gav_7_60_35_0, ckv7_0_-8_11, ckv7_-60_-20_11, ckv7_0_-45_11. Set GAVOUT=1 for exterior views; replace index7 if integration order changes. These Windows/full-game scenes remain an integrator check; the included previews use the Linux production-SDF inspection harness instead.

Verification completed on the final rows:

- All23 analytic validator checks pass. The rows compile against the unchanged actual AircraftSpec/ModelDef structs.
- Release-mode Linux CMake build and CTest:10/10 tests pass. This covers flight model, progression, shader keywords, saves, airport layouts, terrain envelope, hull, entity raycast, gameplay loop and terrain-shadow bake. No source-test assertion was weakened for this run.
- Supplemental Osprey sorties initialized at2450kg pass with0 failures after tuning Ixx from11500 to9000kg m² and matching Izz to19800, both within the brief’s±40% envelope. Takeoff347m, less than0.9×550=495m; climb5.7m/s; comfortable route landed after582s with25.8° maximum en-route bank and0.84–1.25g. Crosswind autoland landed after481s, touchdown0.7m/s, lateral stop error2.9m, no go-arounds. These sorties burn fuel normally after initial maximum load.
- Baseline takeoff case initializes1856kg (1420+220+0.8×270), not2450kg:153m takeoff,8.3m/s climb. This lighter test must not be presented as maximum-load performance.
- Linux llvmpipe rendered17 geometric inspection views plus6 prop-composited exterior hero views. The Osprey-specific actual-GLSL endcap probe measured0.000202m maximum seam jump, below0.001m. The cabin-fit headrest test returned0 intersections because headrests are omitted by its fit logic; its printed1m sentinel does not describe physical clearance.
- Actual Windows executable GAV/CKV scenes, instrument-texture rendering, a physical GPU, and the future rasterizer were not tested. Previews are honest production-geometry inspections with simplified materials, not full-game screenshots.

The optional learned-performance README row is in verification/learned_performance.md. Regenerate it after integration; at this baseline the learner reports about163kt cruise and435m paved runway under its own lighter loading convention. Keep the conservative550m supplied field and max-load evidence distinct from that dynamically learned UI number.


## Geometry audit revision (2026-10-05)

The matched wing-root z value is now −0.80 m in both rows (formerly −1.35 m). This moves the fixed wing under the entire main-gear bay rather than leaving the bay across the flap hinge. The 0.55 m shift does not change wing area, span, section, station geometry, cockpit fit, mass, engine placement or flight coefficients. Full bay-to-hinge clearance is now 0.06966 m at the worst outboard edge, versus −0.48034 m before. The analytic validator now checks the complete bay envelope and matching spec/model roots. Refer to FIXES.md and verification/fix-* logs for fresh checks on this revision.

New ortho-front, ortho-rear, ortho-top and ortho-underside previews are truly axis-aligned orthographic views, unlike the historic artistically angled views named front/rear. Inspection lighting has no lateral component. The independent silhouette images omit materials and composited prop blades to isolate production SDF geometry. A single left-wing pitot, a left pilot position, red/green navigation colors in the game, and arbitrary propeller rotation phases are intentional asymmetries. Control gaps are generated hinge/sliding clearances and must not be filled by stretching the fixed wing.

The baseline gearBay operation preserves a 0.03 m skin around its cut, making the well an enclosed cavity; the new rows do not claim to repair this shared-engine behavior. Gear doors, wheels and legs are still visible and mirrored. No shared production shader was modified.
