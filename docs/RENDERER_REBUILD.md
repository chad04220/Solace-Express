# Renderer rebuild: from one ray-tracing shader to a deferred rasterizer

Working notes for the rebuild. The flight model, career, ATC, audio, UI and the `FrameParams` interface between the game and the renderer stay as they are; only `Renderer::renderScene` and the shaders behind it change. Each milestone ships behind a switch, is compared against the ray tracer in the test harness, and is measured by the owner on the real GPU before the next one starts.

## 1. Why

Owner's measurements, v3.10.0, RTX 3070 Laptop, 1920×1080, quality 2 (`analyze.bat`):

| scene | frame | ray trace | of frame | note |
|---|---|---|---|---|
| menu | 16.2 ms | 12.9 ms | 48 % | scenery + shadows 12.5 ms GPU, 9.7 ms CPU gather |
| air | 36.6 | 34.0 | 79 % | per-pixel bound (100 % costs 1.82× 67 %) |
| storm | 29.1 | 27.5 | 91 % | |
| night | 48.9 | 44.9 | 88 % | |
| cockpit | 212.4 | 207.1 | 95 % | 28 airframe distance samples per pixel |
| rjetc | 68.4 | 62.5 | 88 % | fixed cost: the display cameras re-trace the scene |
| wr_8 | 100.0 | 96.4 | 92 % | same |

Switching features off saves little each (clouds 2 ms, aircraft shadow 2.5 ms in `air`): the cost is the shader itself. One fragment program holds the terrain march, the airframe distance field, the clouds, the lights with their shadow marches and every material. It needs so many registers that the GPU runs few pixels at once, and it pays for the most complex path on every pixel. Patching it item by item (work plan A1/A3/A4/A8/A9) was estimated at 1.5–2×; the rebuild targets a steady 60+ fps at 1080p in every scene.

## 1b. Owner's measurements on the rasterizer, v3.18.0 (RTX 3070 Laptop, 1920x1061, quality 2, `diagnostics.bat`)

| scene | frame (raster) | frame (ray tracer, v3.15) | first pass | airframe shadow off saves |
|---|---|---|---|---|
| menu | 19.2 ms (52 fps) | 26.0 | 19.2 ms (9.5 ms CPU gathering 10,039 instances from 987 chunks) | 1.9 ms |
| air | 33.2 (30) | 42.4 | 30.7 | 7.2 ms (29%) |
| storm | 22.6 (44) | 31.0 | 20.2 | 4.1 ms (20%) |
| night | 81.1 (12) | 49.6 | 78.4 | 55.4 ms (69%) |
| cockpit | 160.4 (6) | 220.9 | 158.8 | 36.3 ms (23%) |
| rjet | 31.4 (32) | 44.2 | 22.8 | 5.0 ms (23%) |

Resolution scaling: 100% costs 1.5-2.0x of 67% (2.2x the pixels), so the first pass is per-pixel bound with a fixed cost besides.
The first pass lumped the world, the display atlases and feeds, the objects (airframe) pass and the airframe shadow proxy; it is
split into those four from v3.18.1 on. The one number that stands out across the table is the airframe shadow: the per-pixel
march of the aircraft's field toward the sun and the three brightest lights (the shadow proxy). At night the apron's lights make
it the whole frame. First-run shader compile: 150.8 s with an empty cache, 11.5 s warm.

Done in response (v3.18.1): `Renderer::rasterShadowMaps` renders the player's baked static mesh from the sun (orthographic about
the airframe's bound, 1024², layer 0) and from the three brightest shadow-casting lights (perspective along each beam, layers
1-3) into a depth array, and the moving hull into a mask; the shadow proxy reads the depth (3x3 PCF) and marches the field only
under the mask (the gear, the surfaces, the props) and for the traffic. `SHMAPOFF=1` restores the old march for an A/B.

## 2. Target architecture

Rasterize everything that has a surface; march only volumes (clouds, flames, blasts). Light once, in a small full-screen pass.

```
shadow cascades (sun; entities, later terrain + aircraft)          existing, extended
G-buffer pass  : terrain mesh | water | entities | aircraft parts | debris | UFO
lighting pass  : sky | sun with cascades + terrain-shadow bake + cloud shadow | ambient | moon
                 | point lights (shadow maps for the flagged ones) | emission | interior fixtures | fog
clouds         : quarter-resolution march along texDepth + composite              existing
effects pass   : the cloaked XR-40 refracts the lit frame (screen space), prop discs, vapour cone, plumes, weapons, hologram
                 (effects_fs, into the free TAA history texture and copied back)
TAA, sprites, bloom, light shafts, post                                         existing
```

The lighting pass writes exactly what the ray tracer wrote (`texRaw` colour + TAA class, `texDepth` distance, `texCloudMask`), so everything downstream is untouched. Camera feeds run the same passes on their own targets (`swapView`).

### G-buffer (render resolution, allocated at window size like today)

| target | format | contents |
|---|---|---|
| GB0 | RGBA32F | view distance, octahedral normal (2), class + flags |
| GB1 | RGBA8 | sqrt(albedo), roughness |
| GB2 | RGBA16F | emission (HDR), metalness |
| GB3 | RGBA8 | ambient occlusion, the surface's own sun shadow, the terrain's sun shadow at it (airframes: from the objects pass), flags: glint / display pixel / TAA moving / TAA rigid |
| depth | D32F | logarithmic depth (the entity shader's `uLogC` encoding), shared by every raster pass |

Classes: 0 sky, 1 terrain, 2 water (prelit), 3 entity, 4 foliage, 5 aircraft exterior, 6 cabin interior seen from outside (prelit: sun through the windows + fixture lights, then fog), 7 the aircraft in a cockpit view (prelit, no fog, no clouds), 8 display (prelit), 9 debris, 10 UFO, 11 wreck, 12 traffic. The cockpit classes are lit in the objects pass by the same `planeLight` the ray tracer uses, because their light comes from the cabin fixtures and the airframe's own field, not from anything the lighting pass can see; everything else is lit deferred. TAA class per pixel from the flags (1 world, 0.5 rigid with the aircraft, 0.2 moving, 0.55 display, 0 none).

### Terrain

Quadtree of 32×32-quad chunks (the envelope mesh's structure) extended below the 39 m heightmap texel: levels from 0.6 m near the camera up to the whole world. The vertex shader evaluates `terrainH` (the same function physics uses) with octave count by level, computes the vertex normal from four more samples, and morphs towards the coarser level near chunk boundaries (CDLOD) so there are no cracks and no skirts. The fragment shader is `terrainMaterial` and friends, unchanged, writing the G-buffer. Water is a dense grid at y = 0 shaded as today (waves, Fresnel sky, cloud reflection, foam) and written prelit.

### Aircraft

Milestone R1 keeps the distance-field march for the aircraft, but in its own small program: a full-screen pass that starts from the rasterized hull and writes the G-buffer with depth, so it competes fairly with terrain and scenery and never runs the terrain or cloud code.

Milestone R2 (`aircraft_mesh.cpp`) takes the march off every pixel where the airframe never moves, without a rig table: the field is sampled, with the hull bake's program, in every gear / flap / steering / control state the hull bake sweeps, and a 6.25 cm cell whose distance changes between states is *moving* (a flap, an aileron, the elevator, the rudder, a gear leg and its doors, the yoke, the pedals, the levers); the rest of the surface band is *static* and becomes a mesh: surface nets on a 1.56 cm lattice, one vertex per lattice cube the surface crosses, pulled onto the surface with the field's normal, material id and (in the cabin) ambient occlusion. The moving cells, grown by one 0.25 m voxel, make a hull that the objects pass starts its march on, exactly as the full hull did for the whole airframe; everywhere else the mesh is the airframe and no pixel marches (in the cockpit the near march stays only while the eye sits inside the moving hull). The mesh pass (`plane_mesh_vs/fs`) draws it with a depth pre-pass and then the march's own materials and lighting classes (`plane_gb.glsl`); the field is asked only on the few triangles that straddle two materials. Traffic draw their model's mesh with that aircraft's data loaded, and their hull pass draws the moving hull too. Meshes are cached on disk by model, view and shader fingerprint; `MESHOFF` marches the whole airframe as before. Not meshed: the research jets (their fields animate through other uniforms) and wreck pieces.

Shadows from the aircraft (on the ground, on itself, in the landing-light beam) become shadow maps once the meshes exist: a third sun cascade centred on the aircraft and a small map per flagged spot light. Until then (R1) a proxy pass (`shadow_proxy_fs`) runs the distance-field shadow march over the G-buffer's surfaces - each march starts with a bounding-sphere test, so pixels away from the aircraft cost almost nothing - into one RGBA8 texture: the sun's shadow of the player's aircraft and the traffic on the ground, and the airframe in the beams of the three brightest shadow-casting lights (`gbShadowSlot`), which the lighting pass reads in `lightShadow`.

### Lighting pass

`shadeSurface` without the geometry: PBR sun with the three shadow terms, ambient, moonlight, sky reflection, the point-light loop, emission, lightning, fog. Interior classes reconstruct the body-space position from depth and apply `cabinLight` / `podLight` / `wraithPodLight` exactly as now. Later (R3) ambient and reflections come from a per-frame low-resolution sky probe that includes the clouds.

## 3. Milestones and gates

| id | content | gate |
|---|---|---|
| R0 | Split the uber shader into modules; extract `planeMaterial`, `waterShade`, terrain/entity/debris shading into functions. No behaviour change. | **done** (db08f7a): bit-identical on `multi:air,cockpit,night,storm,rjetc,wr_8_0_0_0_1` |
| R1a | Deferred world: terrain mesh, water, sky, entities, lighting pass, behind `Renderer::mode` (Settings → Renderer; harness `RASTER=1`; tools `--raster`) | harness: air/mountain/storm/night/sunset match the ray tracer by eye (`RASTER=1 ./render_harness multi:...`, PSNR 27–35 dB with the aircraft absent); owner `benchmark.bat --raster` pending |
| R1b | Objects pass (aircraft, traffic, wreck, debris, UFO) into the G-buffer; interior lighting; camera feeds; aircraft shadow proxy | **done** on the harness: cockpit, rjetc, wr_8, trf25_0, ufo13_0, night match the ray tracer by eye; ctest; owner cockpit / rjetc fps pending |
| R1c | Effects pass (prop discs, vapour cone, plumes, weapons, hologram) and the cloak over the lit frame; timing stamps and tooling (F3, analyze, bench, profile) on the new path | **done** on the harness: wr_3/wr_4 (cloak), wr_6 (lasers, plumes), wr_7 (plasma dome), wr_9, rjetc, air, night match the ray tracer by eye (one intended difference: the ray tracer skipped the plume segment between the camera and a cloaked skin, the raster path shows it); owner `analyze.bat --raster` pending |
| R2 | Aircraft meshes (static / moving split from the field) for the player and the traffic; shadow maps deferred (the proxy marches stay) | harness: air, cockpit match the ray tracer by eye; owner cockpit fps pending |
| R3 | Sky probe, cloud reprojection, entity draw caching, terrain in the near cascade; delete the ray tracer | owner benchmark ≥ 60 fps in air/night/cockpit; sign-off |

Owner checks are posted as exact `.bat` runs and shot lists; the harness runs on llvmpipe in CI.

## 4. What is kept, what goes

Kept unchanged: `FrameParams`, the game, the display atlases (`renderDisplays`), the terrain-shadow bake, the entity meshes and cascades, clouds, TAA, sprites, bloom, light shafts, post, the UI, the GPS map, the hull bake machinery (reused to sample the fields for mesh extraction).

Removed at the end of R3, after the owner signs off: the uber ray tracer and its per-pixel terrain march, the terrain envelope pass, the hull-start pass, `traceBoxes` (already dead: airport buildings are scenery entities).

## 5. Risks

- Terrain: a mesh approximates the heightfield between vertices. Near the aircraft the spacing is 0.6 m, so wheels sit within a few centimetres of the drawn ground; far away the interpolation smooths what the per-pixel march also simplified (fewer octaves). Checked by eye on taxi and approach shots.
- Mesh extraction from fields that are bounds rather than exact distances (`smin`, `max`): dual contouring with gradient normals handles it; thin parts get finer grids. Checked part by part in the `gav_` / `ckv` views.
- Non-rigid motions in the fields (Fowler flap slide, yoke shaft, iris petals, actuator rods): each gets a rig function in the vertex shader that mirrors the field's formula exactly, or is built as a procedural primitive placed by the rig.
- Compile time: many small programs instead of one giant one; the shader cache stays.
