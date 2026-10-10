# Photographic environment materials

## Source and scope

Seven environment-only surface sets now use natural photographic colour,
roughness and tangent-space normal detail. The original 30-layer material array
and every existing material JPEG are unchanged, including the concrete used by
parked-aircraft trim. The grass first used Poly Haven's Leafy Grass, which is mostly
fallen leaves over soil: it turned every meadow on the islands khaki. By the owner's
choice the grass is ambientCG Grass004 at 2K instead (the legacy layer's source, which
ambientCG's page identifies as procedural rather than photographed), its mean colour and
roughness matched to the legacy layer the islands were tuned with
(`tools/pack_environment_scans.py`, `PALETTE_MATCH`): the islands' green, with real
blade detail up close. It is the one layer whose colour is matched.

| Environment layer | Original material ID | CC0 source | Physical source tile |
|---|---:|---|---:|
| Grass | 0 | [Grass004](https://ambientcg.com/view?id=Grass004), ambientCG (colour and roughness matched) | 2 m |
| Asphalt | 5 | [Asphalt 04](https://polyhaven.com/a/asphalt_04), Sergej Majboroda / Jenelle van Heerden | 4.04 m |
| Concrete | 8 | [Concrete Floor Worn 001](https://polyhaven.com/a/concrete_floor_worn_001), Dimitrios Savva / Rico Cilliers | 3 m |
| Brick | 12 | [Red Brick](https://polyhaven.com/a/red_brick), Rob Tuytel | 1.4 m |
| Plaster | 11 | [Painted Plaster Wall](https://polyhaven.com/a/painted_plaster_wall), Amal Kumar | 2 m |
| Roof tile | 9 | [Clay Roof Tiles 02](https://polyhaven.com/a/clay_roof_tiles_02), Amal Kumar | 2.5 m |
| Bark | 25 | [Bark Brown 02](https://polyhaven.com/a/bark_brown_02), Rob Tuytel | 1 m |

All assets are CC0 1.0 ([Poly Haven](https://polyhaven.com/license), [ambientCG](https://ambientcg.com/license)). Official asset pages and
API metadata were checked on 2026-10-10. The provider's
[texture requirements](https://docs.polyhaven.com/en/technical-standards/textures)
specify photo-based sources, photogrammetry or photometric stereo, with calibrated
real-world dimensions. The brick, plaster and roof pages also provide reference
photographs. Each selected source was visually inspected; original artist credits
and API metadata are preserved with hashes. The plaster is naturally neutral and
lightly weathered. The brick and roof retain individual fired-clay colour variation;
bark retains real furrows and small moss patches. These are material inputs, not a
claim that every rendered environmental object has passed photorealistic acceptance. The exact source URLs, provider MD5,
downloaded-source SHA-256, artist credits, dimensions, packing description, output
file sizes and packed-file SHA-256 are in
`assets/materials/environment/manifest.json`. Every downloaded map matched the
provider's published size and MD5 before packing. Runtime rendering is offline;
the game does not call the provider API.

## Packing and runtime

- `tools/pack_environment_scans.py` downloads actual 2048 × 2048 PNG maps from the
  official host and writes 42 compact JPEG runtime maps: 21 at 512 px in
  `assets/materials/environment/` and 21 at 2048 px in `assets/materials/high/`.
  Large raw-source PNGs stay outside the repository. The delivered set is about
  60.40 MiB; the audit enforces an 80 MiB ceiling.
- The albedo is converted from source sRGB to linear light, filtered in linear
  light for the 512 px fallback, then stored using the game's square-root encoding.
  It is not recoloured to the previous procedural mean. Source roughness/AO and
  DirectX normal orientation are retained. Filtered normals are renormalized.
- Existing `c/n/m` packing remains compatible: albedo RGB; normal XY plus height;
  roughness R and AO G. Maps use full-colour JPEG sampling, without chroma
  subsampling. The unused material B channel is ignored by the renderer.
- Separate seven-layer GPU arrays use approximately 298.7 MiB for 2K plus 18.7 MiB
  for the 512 px fallback, including all mip levels: 317.3 MiB total, 181.3 MiB more
  than the previous three-layer set. This avoids expanding every aircraft and
  cockpit material to 2K. CPU staging for the high array is 224 MiB, plus up to
  48 MiB of decoded layer maps; the old 60 MiB base-array CPU staging is released
  before optional environment decoding. Driver/transient GPU allocations are not
  included in these texture-size estimates. Low selects the smaller set but both
  sets are prepared at startup when capabilities and memory allow.
- Medium/High use the 2K environment set; Low uses natural-colour 512 px. If a 2K
  map is missing, allocation fails or dimensions are unsupported, the whole set
  falls back together. If both optional sets are unavailable, the original base
  materials remain usable.
- Textures are uploaded only during initialization. Trilinear mipmaps are
  generated once; anisotropy is enabled only when its capability query succeeds
  and is capped at the smaller of 8× and the driver limit.
- The sampler binding code checks maximum combined texture units, array-layer
  capacity and texture dimensions. Environment units 29 and 31 do not collide
  with the separate aircraft part-pose program's unit-31 2D sampler. Programs
  without environment uniforms are skipped. No extra per-sample texture fetch is
  introduced: each environment lookup selects one complete material set.
- Each low/high layer decode and each upload stage reports through the existing
  renderer startup callback. Progress intervals are normalized by the layer count
  so adding layers cannot move the bar backwards or exceed the material phase.
  The intro's current progress/rendering flow is reused.

The original runway geometry, elevation, headings, footprints, collision ground,
thresholds and surface types remain untouched. Environmental material/lighting
work changes how surfaces appear; it does not move or reshape them.

## Validation and reproduction

Repack using Python, NumPy and Pillow, with an external download cache:

```sh
python tools/pack_environment_scans.py --cache /path/outside/repository
```

Standard-library-only CI audit (no downloads and no dependency installation):

```sh
python tests/environment_scan_assets_test.py --source-only
```

This checks source/packing provenance, all output hashes, JPEG SOF dimensions,
resolutions, physical dimensions, CPU upload order, file budget and recursive Windows packaging.
The standard-library audit passes 387 checks. The JPEG header parser supports baseline and progressive SOF records.

The full pixel audit additionally requires NumPy and Pillow:

```sh
python tests/environment_scan_assets_test.py
```

It verifies preserved mean source albedo/roughness and usable normal vectors.
Optional full source-cache verification checks all original PNG dimensions,
provider MD5 and SHA-256, exact API records, artist credits, physical dimensions,
and source-derived colour/roughness statistics without accessing the network:

```sh
python tests/environment_scan_assets_test.py --source-cache /path/outside/repository
```

Use `--source-only --source-cache ...` for a standard-library source/hash audit
without the pixel checks. Statistics use float64 accumulation to avoid biased
means from summing millions of similar plaster texels in float32.
The full source-cache and pixel audit passes 660 checks. The previous three-layer
loader cross-compiled with LLVM-MinGW Clang 23.1.3; the seven-layer expansion awaits
the coordinated production freeze for its Windows build. Rendering still requires
independent production-image checks; neither asset packing nor cross-compilation establishes native RTX 3070
performance or guarantees that every scene is photorealistic.

`Renderer::matEnvScanned == 7` and `matEnvFallbackScanned == 7` confirm both sets
were loaded; capture metadata should record these values to distinguish actual
2K from a fallback. Before/after source swatches are in
`previews/material-sources-before-after.png`. These are colour-management-accurate
ground-texture swatches from the first three-layer pass, not in-engine screenshots; production captures remain the
visual acceptance evidence.
