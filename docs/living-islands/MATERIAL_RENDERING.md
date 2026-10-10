# Living-island surface rendering

## Renderer paths actually changed

- `terrain_fs.glsl` already supplies world-space derivatives before its sea-floor discard. `terrain_material.glsl` uses those footprints for local community street paint, off-runway airport paint, fine crop rows and volcanic cracks.
- Instanced scenery uses `ent_fs1.glsl` + `ent_fs2.glsl`, not the aircraft `material_common.glsl` triplanar function. The entity material path captures local derivatives before every alpha/dissolve discard and uses explicit texture gradients in projection branches.
- `water_fs.glsl` writes prelit water into the deferred G-buffer. The lighting pass subsequently adds aerial perspective. Clear shallows now include a refracted seabed contribution in that existing pass; this is not framebuffer alpha blending or an extra scene render.

## Surface changes

### Selective photographic material arrays

Seven photographic 2048² layers live in environment-only albedo/normal arrays: grass 0, asphalt 5, concrete 8, brick 12, plaster 11, clay roof tiles 9 and bark 25 map to array layers 0–6. `uEnvMaterials` selects either the complete 2k set or the separate natural-colour 512² fallback. Each lookup replaces its corresponding legacy lookup; it adds no fetch. Seven-layer 2k RGBA8 albedo/normal arrays cost approximately 299 MiB with mipmaps, plus 19 MiB for the 512² fallback. Aircraft arrays/assets stay unchanged; the 30-layer legacy array is not upscaled. Physical mapping is grass 2 m, asphalt 4 m, concrete 3 m, brick 1.4 m, plaster 2 m, clay tiles 2.5 m and bark 1 m. Provenance, natural albedo/roughness packing and all-or-nothing fallback are recorded by the material pipeline.

### Communities and airports

Community data occupies the existing scene-data texture: info `[320,351)`, plans `[352,383)`, count at `383`. The current 18 settlements are selected by minimum distance/radius below one. Streets use the CPU's town-centred rotation and block dimensions. Asphalt extends 3.5 m from each centreline, sidewalks to 5 m, and downtown forecourts only through the 21 m frontage band. The positive-X/positive-Z central block remains green. Paint stops at junctions.

Taxiway centre/edge/holding lines, apron stands, tie-downs, and car-park stripes use interval/periodic coverage instead of hard subpixel thresholds. The 16 m terminal-lot entrance aisle is kept free of stall stripes and extends to the scenery fence gate. Runway coordinates, dimensions, elevation, physical surface class, threshold positions, navigation fixtures and paint layout are unchanged. The four frozen runway helper function bodies retain their baseline source, apart from passing the analytic footprint into the off-runway helper. Their environmental asphalt/concrete texture inputs and scene-wide diffuse illumination are refined, so runway albedo and lighting are intentionally not visually identical to the baseline. No special G-buffer layout is used.

### Foliage, rocks and buildings

Environment triplanar sampling omits projections below 2%, renormalizes the survivors, and projects the sampled normal perturbation onto the local tangent plane. Unmirrored UVs no longer reverse bump direction on negative faces. Position, normal and explicit gradients share the same frame for corrugated cladding and roof orientations.

Pine cards use asymmetric forward-leaning needle shoots instead of 46-spoke radial fans. Fir/spruce cards hold four staggered pairs of short branchlets, so fine needles grow around narrow shoots rather than spanning the whole card like giant fern ribs. The nearest pair is selected analytically, with no per-needle loop or texture. Closest leaf cards use their actual two-sided spray normals; modelled palm leaflets bypass the coarse frond cutout. Middle/far backing envelopes retain only one faded shade-noise evaluation instead of seven lumpy-normal evaluations. Fine rock strata fade with actual pixel footprint. Snow, metal and porous surfaces receive bounded, exposure-sensitive wet roughness. Parked car lenses have neutral front glass and dark red rear lenses without emitting light. Flyable-aircraft shared material helpers and parked-aircraft triplanar sampling remain unchanged.

### Fitted glazing, clearcoat and facade continuity

Real high-detail facade walls carry `P_WALL aux.u=1` so no procedural window is stamped over their openings or gables. Fitted panes use `P_GLASS aux.u=1..2, aux.v=0..1`. Pane centres are reconstructed from position/UV derivatives for stable per-window variation. A single analytic room-box intersection supplies side/back/floor/curtain cues, fading to a mean before panes become subpixel or exceed 180 m. It adds no texture read, ray-march or per-pane loop. Room colours are shaded by indirect daylight; only selected occupied rooms emit at night. This is an opaque-depth interior approximation, not transparent rendering of arbitrary interior meshes.

Lower-detail facades use nominal mesh-space opening rectangles matched to the real high-detail declarations, including custom house fronts, townhouses, balconies, stepped towers, church arches and lighthouse slots. An instance's scale changes opening dimensions rather than adding windows. Filtered frame/sash coverage and matching pane IDs limit visible changes during the complementary slot 0/3 fade. Fitted airport panes suppress their former painted clerestories as well.

Environment-only G-buffer flags 16/32 identify dielectric glazing and vehicle clearcoat. Glazing uses an effective thin-pane Fresnel response, reflects sky above and lit ground below the horizon, and transmits the room/cabin approximation. Car paint stays dielectric beneath a separate restrained clearcoat lobe; rims/reflectors use metal, tyres remain rough rubber, and brake discs are dark steel. Metallic ambient diffuse is removed rather than making chrome an illuminated white coating. Only environment lighting reads these flags; aircraft programs never set them. `P_TRIM aux.u=2` distinguishes weathered foundations/steps from painted joinery.

### Architectural night windows

Office and skyscraper curtain walls retain the same nominal pane grids and masks across LODs, but use building-seeded floor occupancy and three-bay office zones instead of independent white checks. Time/camera/LOD do not seed the pattern. The expected occupied fraction is 22–34%, with shared zone color/intensity and small pane variation. Full-night lit luminance is bounded to 0.05915–0.28 linear units, versus the previous uniform 1.3. Warm/cool office colors are normalized to equal luminance. At exposure 1.8, the old value maps to about 246–247/255 through ACES+gamma; the new brightest warm/cool channels are about 180–215 before reflection, bloom and Fresnel. The matched native Port Verde v7 image was reviewed and accepted for the white-window defect. In the same 700-row city crop, pixels with all RGB > 240 dropped from 39,319 to 0; that whole-crop metric also includes unchanged non-office lights and is not a material-isolated luminance measurement. Close-office and broader scene checks remain useful; this is not a photorealism or FPS certification.

This changes only office/skyscraper curtain-wall emission, not fitted lobby rooms, other domestic glazing, aircraft, navigation, runway or beacon lights. It adds two hashes (three total) only on those panes, no texture reads, loops, passes or time dependency. `tools/validation/audit_night_emission.py` checks 3,727,360 float32 pane samples against a frozen production-block hash: 27.94% lit and measured luminance 0.059234–0.279848, with correlated neighboring occupancy. Reproduction and source-difference records are in `performance/night-emission/`.

### Close contact shadows

The existing environment cascade comparison now uses texel- and surface-angle-derived offsets within 180 m, with full correction through 90 m and a smooth return to the legacy policy. Normal offset is bounded 2–20 cm; comparison depth bias is 1.5–10 cm, divided by the existing 6000 m light-space span. A geometric receiver plane is reconstructed from position derivatives captured before divergent exits. At mixed close shadow edges, the four neighbouring texel centres are compared using their individual receiver-plane depths, then bilinearly combined. This avoids hardware 2×2 PCF comparing one reference depth against four positions on a sloped wall. It adds no broad blur or metre-scale uniform bias. The reference-depth change itself is bounded to 4 m per texel at extreme grazing angles; this follows the receiver plane rather than displacing it.

Most lit/shaded interiors retain one hardware comparison per active cascade. Close, sun-facing entity walls with more than 8 cm receiver-depth change across a shadow texel use four exact comparisons directly, including initially fully lit/shadowed samples; the redundant initial lookup is skipped. Other mixed close edges use five total (the initial test plus four exact comparisons). Horizontal receivers and terrain do not enter the four-read wall path. Sky pixels, distant surfaces, and aircraft receiver sampling do not enter this shadow helper; two position derivatives are captured before the sky exit to keep receiver reconstruction well-defined. Exact-view house/apartment shadow-on/off diagnostics isolate the original wall bands to the cascades; v6 still fails visual acceptance because the map undersamples close caster edges. Actual GPU readback and exact mesh raster reproduction isolate this limitation; see [SHADOW_READBACK.md](SHADOW_READBACK.md). The final runtime keeps the original Low/Medium/High near radii 300/420/520 m, far radii 1,600/2,600/3,600 m and map resolutions 2,048/2,048/4,096. Fixed 96/160/256 m and adaptive-radius experiments are rejected production policies: the smaller near maps improve close sampling but sacrifice readable middle-distance shadows. The eave/balcony aliasing remains a known limitation.

### Water and the volcano

Clear shallow water uses Snell refraction (air/water ratio 0.7501875), one bounded heightfield-intersection correction, existing sand/rock albedo layers with decorrelated two-view anti-tiling, and RGB Beer–Lambert attenuation. Red attenuates fastest; storm/cold conditions shorten visibility. The contribution fades smoothly between 24 and 32 m depth. Existing Fresnel sky/cloud reflection and foam remain, so glancing water still reflects the sky. Unresolved wave slope bands now actually fade into the already-computed specular roughness.

This approximates the natural seabed from the game's heightfield and material layers. It does not render submerged entities or sample the active G-buffer; above-water geometry cannot appear as an underwater refraction artifact. The original sea-floor raster discard remains to prevent ocean/terrain depth fighting.

Mount Kaleo materials follow the new bounded world bowl: `(25000, -9000)`, 1780 m floor, 145 m floor core and wall transition through 340–520 m. Incandescence requires an almost-horizontal normal and floor height below 1795 m. Most of the floor stays cooled basalt, with narrow hot fissures and a smaller central vent. A noisy basalt/scoria margin avoids the prior dark circular outline on snow. Crater walls use actual scanned rock albedo/normal/roughness in a slope-aware, explicit-gradient triplanar frame; fractures come from the scan rather than oversized procedural contour lines, and basalt tint retains the scan detail. Fine molten floor fissures fade to their mean coverage. These shaders do not displace terrain; the separately tested world change creates the actual bowl without modifying airport terrain.

### Environment diffuse light

The deferred environment path uses a two-direction zenith/horizon quadrature approximation for diffuse sky irradiance. Horizontal surfaces weight zenith more than walls, and the old saturated zenith-only ×2.2 fill becomes integrated ×1.5 fill. Shared aircraft/cockpit lighting remains unchanged. Opaque environment direct sun, point-light, moon and fog formulas retain their prior response; metallic diffuse energy and the reflected lower hemisphere are corrected only in the environment helper. Fitted glazing has its own dielectric response. The shared `ambientLight` and `shadeSurface` functions are byte-frozen by the source test; only terrain/scenery/foliage call the separate environment helper. This adds one sky-color evaluation, not a scene pass or texture read.

Photographic grass retains 94% of scan chroma and just 35% of the legacy biome tint, instead of being recolored back to the procedural palette. Town lawns reuse the already-computed surrounding biome's grass tint, moisture/slope variation and low-frequency breakup, with mild per-block maintenance color and filtered park/courtyard paths. They no longer replace the ground with uniformly bright raw green.

## Cost and verification

Static budgets, not measured GPU timings:

- Flat entity face: **2** albedo/normal-array reads instead of **6**. Two surviving projections cost 4, a diagonal 6. The selective high-resolution array branch substitutes each lookup; it does not add a fetch. Parked aircraft retain the original 6-read function.
- Airport anti-aliasing: arithmetic replacement, no added texture reads or loops. Runway geometry, navigation and paint layout are preserved; environmental texture inputs and illumination are refined.
- Foliage: backing-envelope shade detail drops seven noise evaluations to one, fading away with footprint/distance. Close real leaf cards do not run that envelope branch.
- Fitted glass: one analytic room intersection on resolved panes, no textures or marching; the dielectric deferred path replaces the opaque path and retains one bounded point-light loop. Vehicle clearcoat adds one sky-color evaluation and one sun BRDF only on flagged paint pixels.
- Close environment shadows: one comparison per active cascade in other interiors, four on significantly sloped close entity walls, five at other mixed close edges, using existing maps (up to two active cascades in their blend region). No new pass, map allocation, sampler or loop.
- Office/skyscraper night panes: three deterministic hashes instead of one; zero added texture reads, loops or passes.
- Town grid: one bounded loop over the uploaded town count (currently 18), only inside settlement-mask pixels, plus one winning plan read. This is the explicit cost of matching rotated CPU town geometry. It requires GPU review in dense-city scenes.
- Water: existing wave/reflection budget unchanged; shallow-water pixels add one `groundH(...,5)` correction, four existing albedo-array fetches, two analytic low-frequency noise evaluations for UV warping and one broad rock-patch noise evaluation. Two of those reads are the seabed anti-tiling addition on all three quality presets: rotated, offset and slightly rescaled scan views, blended in linear space by a continuous 19/31 m analytic domain warp and bounded patch blend, with the exact warp Jacobian and rotation applied to texture gradients. Deep water skips all three material operations. No new framebuffer, texture allocation, sampler, march loop or draw call.
- Volcano: two low-frequency value-noise samples within the small crater material branch. Scanned rock detail adds 4/8/12 existing-array reads for one/two/three surviving triplanar projections, only where crater rubble contributes. The shader adds no terrain mesh, texture, sampler or loop.

The updated `terrain_material_test.py` / `terrain_material_math_test.cpp` execute production-extracted GLSL arithmetic, including town paint, negative coordinates, normal projection, sample-count contracts, and monotonically bounded water transmission. The release aircraft helper hashes and original parked-aircraft triplanar function are checked. `shader_check` validates the actual assembled and pruned programs; aircraft specialization checks remain required.

These tests establish shader compilation and arithmetic/source contracts. They do **not** establish native 1920×1080 RTX 3070 60+ FPS or photorealistic visual quality. Production scene captures and hardware GPU/pass timings remain necessary, especially shallow coasts, dense towns, wet night airports and the volcano.

## Final shader identity

The handoff hashes are recorded in `performance/environment-shaders-final.sha256`.
The accepted night change is in `ent_fs2.glsl`; the retained receiver-plane path
is in `light_fs.glsl`. These exact shader sources passed the post-18f6178 Release
suite (54/54) and the optional night arithmetic audit. Subsequent full-build/ASan
results are owned by the aggregate QA record. Interior glazing remains an opaque
approximation, water transmits the heightfield seabed rather than arbitrary
submerged meshes, and target-GPU 60+ FPS remains unmeasured.
