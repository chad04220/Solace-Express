# Environmental asset construction

## Current four-tier redesign, 2026-10-10

The user's rejection of the earlier house/car appearance superseded the original three-tier checkpoint. Current meshes use four authored ranges: slot 0 near, slot 1 middle, slot 2 far, and slot 3 close inspection. See [CLOSE_DETAIL.md](CLOSE_DETAIL.md) for distance/fade policy and [QA.md](QA.md) for the final integrated test and render checkpoints.

The existing 52 entity kinds, 40-byte vertex format, 32-byte instance format, instancing and material interface remain. The mesh builder is split into `entity_mesh_nature.inc`, `entity_mesh_buildings.inc` and `entity_mesh_airport.inc`. The two immutable runway-light/PAPI kinds deliberately use their original near geometry for the close slot; their original three ranges and fixture placements remain exact. Parked scenery aircraft were redesigned; flyable-aircraft meshes, physics and autopilot are outside this mesh pass.

Close detail is selected only within bounded kind/quality-dependent distances. It is not submitted for every visible island object. Shadow and auxiliary-camera tiers retain cheaper meshes; the near/middle costs can still affect them and must be included in workload measurements. CPU counts and software-render captures do not establish native-1080p 60 FPS on an RTX 3070.

### Current construction

- Natural assets now have intentional close species/geology forms, with cheaper distant silhouettes. Very-close foliage remains procedural geometry and alpha coverage, rather than scanned botanical assets.
- Buildings now have close construction and fitted opening detail. Human-scale doorway/floor proportions and placement are documented separately in [SCALE_AUDIT.md](SCALE_AUDIT.md) and [COMMUNITIES.md](COMMUNITIES.md).
- All 20 airport/vehicle kinds below have intentional close geometry. The scenery layouts and clearance guarantees are in [AIRPORTS.md](AIRPORTS.md).
- Sedan slots 0/1/3 share the same monotone, continuously tangent body loft and fitted cabin proportions. Hardware is simplified at distance. Slots 0/1/3 also share the bowser's domed tank-head profile; its distant slot remains an economical silhouette.
- Road wheels retain their material-slot, radius and rest-pivot ABI. Close wheels have curved rubber shoulders, recessed discs, physical spokes and rim depth. Close lamps have actual cavity/reflector/lens depth rather than flat white decals.

## Current measured mesh inventory

Counts below are authored triangle-list vertices, not visible instances or frame submissions. The production-linked measurement after the final pump correction gives:

| Scope | Near 0 | Middle 1 | Far 2 | Close 3 | Total |
|---|---:|---:|---:|---:|---:|
| All 52 kinds | 46,365 | 17,961 | 7,026 | 442,110 | 513,462 |
| Airport/vehicles, kinds 32–51 | 17,025 | 9,492 | 3,741 | 197,544 | 227,802 |

The full atlas has a raw vertex payload of 20,538,480 bytes before allocator/renderer overhead. Close geometry is resident but is distance-gated for submission. [ENTITY_SUBMISSIONS.md](performance/ENTITY_SUBMISSIONS.md) records actual CPU submissions; neither that ledger nor atlas size is a GPU timing result.

The persisted [airport inventory CSV](performance/airport-asset-inventory.csv) contains every airport kind/slot count, measured axis-aligned bounds and degenerate-triangle count. Its associated [source hashes](performance/airport-asset-inventory-sources.sha256) identify the measured source snapshot.

| Kind | Asset | Slot 0 | Slot 1 | Slot 2 | Slot 3 | Close construction |
|---:|---|---:|---:|---:|---:|---|
| 32 | Hangar | 312 | 114 | 78 | 5,916 | Standing seams, roof vents, sliding-door guides/rollers and fitted clerestory panes |
| 33 | Arch hangar | 504 | 228 | 132 | 5,868 | Curved shell/ribs, segmented door and real framed end-wall opening |
| 34 | T-hangars | 264 | 210 | 90 | 1,344 | Roof seams, sectional door joints/guides and handles |
| 35 | Terminal | 846 | 594 | 96 | 16,782 | Individually framed curtain-wall panes, entrance canopy and rooftop ventilation |
| 36 | Control tower | 726 | 462 | 318 | 4,950 | Fitted outward-raked cab panes, mullions, balcony rails and shaft joints |
| 37 | Flight centre | 288 | 132 | 90 | 1,800 | Framed storefront, drains and rooftop equipment |
| 38 | Fuel tank | 636 | 288 | 96 | 5,088 | Smooth shell, weld bands, ladder/cage, pipe and valve |
| 39 | Fuel pump | 324 | 228 | 222 | 2,478 | Rounded painted-metal dispenser, exposed fitted display, hose/nozzle and tank straps |
| 40 | Windsock | 360 | 228 | 228 | 4,428 | Sagged tapered fabric, open mouth and rim/bearing frame |
| 41 | Beacon | 468 | 216 | 180 | 2,478 | Lattice/bracing, ladder, lens housing and collar |
| 42 | Parked aircraft | 906 | 702 | 294 | 18,129 | Curved fuselage, fitted glazing, aerofoil sections, gear/wheels and propeller |
| 43 | Parked airliner | 3,408 | 2,220 | 852 | 62,838 | Curved fuselage, aerofoils, rolled intakes, physical fan blades, fitted panes and landing gear |
| 44 | Jet bridge | 276 | 240 | 144 | 4,956 | Framed panes, telescoping collars, bellows and drive wheels |
| 45 | Car | 3,279 | 1,623 | 96 | 25,569 | Curved shared body loft, open arches, fitted cabin, real tires/rims and recessed headlamps |
| 46 | Truck | 1,476 | 792 | 168 | 24,918 | Rounded/raked cab, shared domed tank ends, sealed tips, pipework and physical wheels/lamps |
| 47 | Fence | 90 | 66 | 42 | 300 | Posts, tension wires/ties and material-cutout chain link |
| 48 | Localizer | 960 | 498 | 72 | 1,716 | Feed cables and antenna-element detail |
| 49 | Radar | 1,572 | 372 | 372 | 5,220 | Smooth dome, cross-braced lattice and mounting collar |
| 50 | Mast | 210 | 183 | 75 | 894 | Equipment enclosure/vents, coax and brackets |
| 51 | Floodlight mast | 120 | 96 | 96 | 1,872 | Four separate rounded, finned lamp heads and control enclosure |

## Geometry and pixel checks

`environment_asset_mesh_test` independently enforces finite values, unit normals, nondegenerate triangles, material IDs, explicit per-kind/slot ceilings, old vehicle/construction envelopes, wheel metadata and immutable runway fixtures. The focused `airport_construction_detail_test` adds:

- Curved body/pane/arch coverage and physical wheel/lens construction.
- Numerical left/right hood tangent continuity and monotone station bounds.
- Bidirectional spatial coverage of the sedan shell-to-nose join, plus hood/collar tangent agreement.
- 256 rays through the sedan lamp apertures that must not encounter painted bumper geometry.
- Outward-facing reflector cavities and sealed bowser tank-tip centres.
- 117 rays per slot through the arch-hangar doorway, rejecting an opaque wall behind the door.
- 45 rays requiring the fuel-dispenser display itself to be the exposed front surface, and explicit metal-cabinet/concrete-support material labels.
- 1,960 triangle-centre/near-edge rays requiring the GA glass to clear the actual painted fuselage mesh; the fitted panes have a bounded 6 mm outward seating adjustment.

Production-render inspection caught errors that numerical vertex validation alone missed: broken sedan lamp/bumper joins, loft highlight bands, tiny open tank tips, an arch-hangar wall behind its door, an old fuel-pump sign hiding the fitted display, and millimetre-scale GA fuselage intrusion along the windshield. Their fixes have targeted regressions. Pixel acceptance of the newest grouped corrections is recorded in the final render checkpoint rather than inferred from these tests.

### Confirmed production-render corrections, 2026-10-10 10:44 UTC

The author independently inspected the actual native-render results, not just mesh statistics:

- `hero-lamps-v5`: all four sedan lamp/fascia angles show the smoother hood/collar transition, seated lamp boundaries and no painted obstruction over the optics.
- `hero-inspection-v5`: all four views each of arch hangar 33, fuel pump 39 and truck 46 show the clear door face, exposed dispenser display/correct cabinet and hose finishes, and smoothly closed bowser ends.
- `hero-final-mesh-v6/kind_42_front_oblique.png`: the same native camera as the earlier GA capture shows the windshield's white intrusion teeth removed; accompanying side/rear views remain coherent.

These are unit-scale staged asset inspections through the production renderer. They validate the named construction corrections, not every placement combination in the world, target-GPU frame time or photorealism. No further airport geometry edits are queued after this review. The held airport source is `8a9981627a8af6978f4ad6eeb0e567d4469b0409a3739cbf1c543db4d845611e`; the focused test source is `1800c70f5284e2bf4e2ba52126ff958dc3bf5302da7efd638c29e870fd8513c9`.

## Remaining visual limitations

These are bounded procedural assets, not photogrammetry or a certified photorealistic result. Generic unbranded airport equipment, relatively simple terminal/tower side walls, opaque fitted-glass interior cues and coarse far silhouettes remain visible limitations. Most airport equipment is stationary scenery; richer geometry does not add workers, service simulation or animated airport traffic. The final close screenshots should be used to assess quality, and target-GPU profiling is still needed for the requested frame-rate goal.

## Historical checkpoint: original three-tier pass, 2026-10-10

This subsection preserves the earlier checkpoint as history only. It is superseded by the four-tier redesign above and is not a description of current counts or accepted visual quality.

That first pass added conifer support/branch variation, broadleaf branching, jointed rock forms, roof fascia, porch/door accents, airport construction strips and more shaped road vehicles. Actual closeups caught an unsupported-looking conifer leader and tanker straps intersecting the barrel; supporting stems and barrel-matched straps corrected those defects. Very-close foliage still read as stylized, and the user subsequently rejected the house/car quality and requested the deeper redesign.

| Historical atlas | Before | Original checkpoint | Change |
|---|---:|---:|---:|
| Near | 44,706 | 46,326 | +3.62% |
| Middle | 15,774 | 15,810 | +0.23% |
| Far | 7,026 | 7,002 | −0.34% |
| All | 67,506 | 69,138 | +2.42% |

The historical increase was 1,632 vertices / 65,280 bytes. At that checkpoint the car had 486 / 330 / 96 vertices and the truck 1,044 / 600 / 168. Earlier foliage tests included 911,400 leaf-coverage comparisons, and the old building suite had 5,736 checks. Those historical numbers must not be used as the current four-tier workload or as proof of current visual acceptance.
