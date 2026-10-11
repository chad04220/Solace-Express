# Offline Earth packs (Phase 0)

These tools build and verify **data only**. They do not change the game's globe,
physics, terrain shaders, Solace placement, airports, career or gameplay. No raw
source data, generated real-world pack or rendered preview belongs in Git.

The source/terms review, exact upstream URLs and measured source checksums are in
[`docs/EARTH_DATA_SOURCES.md`](../../docs/EARTH_DATA_SOURCES.md). Runtime loading is
future work; the Python verifier is an offline format/security contract.

## Quick start and dependencies

From the repository root, Python 3.10 or later:

```sh
python3 tests/earth_pack_test.py
python3 -m tools.earth build --config tests/fixtures/earth/config.json \
  --output /tmp/earth-fixture
python3 -m tools.earth verify /tmp/earth-fixture
```

The checked-in 7,620-byte fixture is deliberately synthetic and uses **zlib** so
ordinary tests need only Python's standard library. It is not GRIP/ETOPO data.
Production/default compression is **zstd level 9**, single-threaded, with content
size and frame checksum. Install the optional `zstandard==0.25.0` package in the
chosen Python environment to reproduce the measured sample (libzstd 1.5.7).
No install or download happens implicitly. A build override is explicit:

```sh
python3 -m tools.earth build --config CONFIG.json --output NEW_PACK --codec zstd
```

ENVI sampling uses standard-library mmap; conversion from local ETOPO GeoTIFFs
needs the separately installed official GDAL `gdal_translate` utility. The road
adapter accepts GeoJSON exported from GRIP with GDAL's `ogr2ogr`. Neither is
required for fixture tests or the small NOAA ASCII subset route.

## Cube/sample contract

- Sphere radius 6,371,000 m; ECEF X at longitude 0, Y at 90°E, Z north.
- Face order `px,nx,py,ny,pz,nz`. Each has a `2^L × 2^L` quadtree level, L=0..16.
- A tile is **256 × 256 cell-centred samples**, plus one sample on all sides:
  **258 × 258** stored values. Interior indices are 0..255; apron indices -1,256.
- For full-face integer index `i` and face width `N=256*2^L`,
  `u=2*(i+0.5)/N-1` (likewise v). Direction is the normalized vector
  `normal + tan(pi*u/4)*u_axis + tan(pi*v/4)*v_axis`.

| Face | normal | u axis | v axis |
|---|---|---|---|
| px | +X | +Y | +Z |
| nx | -X | -Y | +Z |
| py | +Y | -X | +Z |
| ny | -Y | +X | +Z |
| pz | +Z | +Y | -X |
| nz | -Z | +Y | +X |

Internal tile aprons reuse the same full-face integer address. Across a cube
edge, `sample_location` rotates/reflects the integer address onto the adjacent
face's **core** sample. It does not normalize an extrapolated coordinate and hope
that it matches. At a double-outside corner, all three incident faces use the
corner core sample owned by the first incident face in the documented face order.
This corner alias is an explicit format choice. Tests independently cover all 24
oriented face edges, every sample along them, and all eight three-face corners.

The geographic terrain datum is recorded in source metadata. ETOPO surface heights
are **EGM2008 metres**, not ellipsoidal heights. No geoid-to-ellipsoid conversion or
runtime geodesy is introduced here.

## Elevation resampling and binary format

`Grid` accepts a small regular JSON point grid, with south-to-north rows,
west-to-east columns and positive `lat_step`/`lon_step`. `periodic:true` requires
an exactly repeated endpoint column spanning 360°. Missing/nonfinite data,
ragged grids and regional samples outside source-centre coverage are rejected.

`EnviGrid` accepts north-up geographic WGS-84 Float32, single-band BSQ files plus
their `.hdr`. Byte order is explicit. It validates exact binary length, offset,
spacing, dimensions and nodata. Global centre-aligned rasters wrap through a
virtual longitude endpoint; **only complete 360° × 180° global rasters** extend
the nearest latitude centre over the half-cell polar cap. Regional data never
silently extrapolates.

Each query is bilinear in the source grid. `area_samples=N` uses N×N deterministic
midpoint quadrature over the target pixel footprint, weighting samples by the
spherical tangent-cube Jacobian
`(1+a²)(1+b²)/(1+a²+b²)^(3/2)`, where a=tan(pi*u/4), b=tan(pi*v/4).
N is explicitly bounded 1..16; default **2**. N=1 is centre/bilinear sampling only.
Increase N when downsampling many source pixels into one output cell and check
convergence. This is an area-weighted quadrature approximation, **not an exact
source-cell area integral** or an automatic accuracy guarantee. Aprons use the
same canonical footprint and weights as their neighbor's core.

Heights are rounded to **whole metres, half away from zero**, with supported
terrestrial range -12,000..+10,000 m. Every tile uses the same global quantization
unit, preventing independent per-tile scale drift at seams. Each tile stores an
int32 metre offset, int16 residuals, then uint16 modular row-deltas:
`delta=(residual-previous)&65535`, resetting previous=0 at each row. The transform
is lossless after quantization; no saturating arithmetic is used.

The little-endian header is `struct.Struct('<8sHBBIIHHiBBI32s')`:

1. magic `SEARTH01`, format version 1
2. face index, level, uint32 tile x/y
3. uint16 width/height (258 each), int32 metre offset
4. codec byte (1=zstd, 2=zlib), unit byte (1 metre)
5. exact uncompressed length **133,128 bytes**, SHA-256 of the delta bytes
6. exactly one compressed frame, with no trailing data

The manifest additionally hashes each entire compressed file. The decoder bounds
file size, output size and zstd window before allocation/decompression; checks all
header fields, complete frame consumption, decoded checksum and height range.

## Roads

Normalized input is GeoJSON FeatureCollection with LineStrings, [lon,lat] points
and properties `id`, `class`, optional `node_ids`. Classes are highway, primary,
secondary, tertiary and local. Coordinate-equal source vertices share generated
IDs; explicit node IDs must have consistent positions. Antimeridian aliases,
poles and signed zero are canonicalized.

Segments are shortest great-circle arcs represented as 3D chords. Clip parameters
come from intersections with the four tangent-cube tile planes, plus the forward
face hemisphere. Antipodal segments are rejected. Shared cut nodes use a stable
ID derived from the original segment endpoint IDs and its global direction;
neighboring tiles therefore keep the same connectivity IDs. A crossing without
a source vertex is **not** automatically a junction, preserving bridges rather
than inventing intersections.

Douglas–Peucker simplification defaults to 20 m, preserving shared source nodes
and tile-boundary nodes. Output points are uint16 0..65535 in tile u/v. Each road
fragment retains the source class, generated/shared node IDs and available source
provenance. The verifier rejects repeated node IDs at conflicting tile positions.
This is input-network preservation, not road routing, surface grading or an
assertion that GRIP is a complete present-day navigable network.

For GRIP4, the verified fields are `gp_rtp` (1 highway,2 primary,3 secondary,
4 tertiary,5 local; 0 unspecified) and `gp_rex` (3 closed,4 construction/repair).
The adapter omits closed/construction features and requires an explicit class
choice for unspecified roads. It preserves `gp_rsi`, `gp_rsy`, `gp_rcy`,
`gp_gripreg`, `gp_rex`, `gp_rav` when supplied, plus input hash/source feature ID.
If a stable ID field is absent, IDs are explicitly generated from the input hash
and feature order; they are not claimed to be stable across GRIP re-exports.

```sh
# Input archive must first be acquired and safely unpacked outside Git.
# Choose a bounded regional extent before GeoJSON export (64 MiB input limit).
ogr2ogr -f GeoJSON /data/earth/grip-region.geojson /data/earth/GRIP.shp \
  -t_srs EPSG:4326 -spat WEST SOUTH EAST NORTH
python3 -m tools.earth prepare-grip --source /data/earth/grip-region.geojson \
  --output /data/earth/roads.json
# Only after choosing a policy for gp_rtp=0:
# ... --unknown-class local
```

Read the GRIP licensing discrepancy in the source review. The owner chose a
CC BY 4.0 working assumption; no real GRIP geometry was downloaded or packed in
this Phase 0 sample. Synthetic road tests do not establish GRIP archive coverage.

## Airports

OurAirports parsing joins stable airport/runway IDs, keeps small/medium/large
fields with at least one open runway, and preserves ident, ICAO, GPS and IATA
codes, name, position, elevation and runway ends/headings/dimensions/surface.
Closed airports, heliports, runway-less fields and closed runways are excluded.
Seaplane bases are not enabled in v1. Feet convert exactly with factor 0.3048;
missing optional fields stay **null**, never fabricated zero coordinates.
Source CSV values and stored JSON types have separate strict validation.

## Configuration and provenance

A source specification uses paths relative to the configuration file (absolute
local paths also work). `sha256` is an optional expected input hash; it is checked
when supplied. The actual hash and byte count are **always** recorded. Pin it for
published/reviewed builds. Each source requires its own URL, version and license
notice file. Optional `resolution_arcsec` and `description` preserve per-layer
resolution, datum, coverage and transformation context.

```json
{
  "version": 1,
  "name": "bermuda-etopo-60s-measured",
  "coverage": "Two adjacent representative Bermuda tiles; regional sample only",
  "codec": "zstd",
  "area_samples": 2,
  "tiles": [
    {"face":"ny","level":6,"x":49,"y":56},
    {"face":"ny","level":6,"x":50,"y":56}
  ],
  "elevation": {
    "path": "bermuda60.grid.json",
    "format": "grid-json",
    "url": "https://www.ngdc.noaa.gov/thredds/dodsC/global/ETOPO2022/60s/60s_surface_elev_netcdf/ETOPO_2022_v1_60s_N90W180_surface.nc.ascii?z[7176:1:7499][6720:1:7103]",
    "version": "ETOPO2022-v1-surface-60arcsec",
    "resolution_arcsec": 60,
    "description": "EPSG:4326; EGM2008 metres; regional surface subset",
    "sha256": "1d60fac492fb583591b8ba5e5f28083032cb3adbf3e0f06b1686b5b95f17f25f",
    "license_id": "CC0-1.0",
    "license_path": "NOAA-CC0-notice.txt"
  }
}
```

Optional `roads` is another source specification (`format:geojson`); optional
`airports` has `airports` and `runways` CSV source specifications. Omit missing
layers instead of claiming empty files are complete global data. Each layer is
separately described and replaceable at build time. Elevation `format:envi` also
records the header as a separate hashed input. For a reviewed ENVI source, also
pin `elevation.header` to an object containing exactly `sha256` (the expected
64-character lowercase digest) and `bytes` (an integer from 1 through 65536).
The builder verifies both before opening the raster reader. The header is
`INPUT.hdr`, falling back to `INPUT.bin.hdr` only if the first is absent; a
symlink is never accepted. Legacy configs without this optional record remain
valid. The pin is build-input validation only: it does not change the emitted
v1 source records or pack format. Keep the binary and header immutable during
the build, in an isolated single-writer input directory.

For a transformed input, include optional `origin` with exact `url`, `sha256`,
`bytes`, `transformation` fields describing the original downloaded bytes. The
build hashes the actual consumed input separately. Source URLs are metadata;
the tools do not fetch them.

Pack layout is `manifest.json`, `elevation/FACE/L/X_Y.elv`, optional matching
`roads/FACE/L/X_Y.json`, optional `airports.json`, and `licenses/LICENSE_ID.txt`.
License notices may contain the verified credit, disclaimer and official legal
link; label them clearly if they are **not the full legal text**. The measured
sample contains a NOAA credit/link notice and the full OurAirports Unlicense.

## Reproduce the verified NOAA subsets

Keep data outside the checkout. These URLs and source bytes were verified on
2026-10-11; curl's `--globoff` is essential for literal DAP brackets.

```sh
mkdir -p /data/earth
curl --fail --globoff -L \
 'https://www.ngdc.noaa.gov/thredds/dodsC/global/ETOPO2022/60s/60s_surface_elev_netcdf/ETOPO_2022_v1_60s_N90W180_surface.nc.ascii?z[7176:1:7499][6720:1:7103]' \
 -o /data/earth/bermuda60.ascii
curl --fail --globoff -L \
 'https://www.ngdc.noaa.gov/thredds/dodsC/global/ETOPO2022/30s/30s_surface_elev_netcdf/ETOPO_2022_v1_30s_N90W180_surface.nc.ascii?z[14352:1:14999][13440:1:14207]' \
 -o /data/earth/bermuda30.ascii
sha256sum /data/earth/bermuda{60,30}.ascii
python3 -m tools.earth prepare-opendap --source /data/earth/bermuda60.ascii \
 --output /data/earth/bermuda60.grid.json
python3 -m tools.earth prepare-opendap --source /data/earth/bermuda30.ascii \
 --output /data/earth/bermuda30.grid.json
# Create the config and notices beside those files using the template above.
python3 -m tools.earth build --config /data/earth/sample-60s.config.json \
 --output /data/earth/pack-60s
python3 -m tools.earth verify /data/earth/pack-60s
```

| Source | Actual bytes | SHA-256 |
|---|---:|---|
| 60″ ASCII | 1,406,779 | 5ec0303542bffbe4f80837767b4ad35f34e345d04794873a19b0ab0e2ce1a644 |
| 30″ ASCII | 4,915,126 | 6506652edfa3a835c9e6db5dc801c4f0b9fda9c627b719d38580c0416905ee74 |
| 60″ adapter JSON | 1,268,544 | 1d60fac492fb583591b8ba5e5f28083032cb3adbf3e0f06b1686b5b95f17f25f |
| 30″ adapter JSON | 4,389,589 | 231cdfa1db76e1ee7f9a6dad71151c6a6e5d6ef4480dbdc3151fc3235500ead2 |

The ASCII adapter validates dimensions, ascending regular axes and every value.
When spacing is an integer arcsecond within floating-point tolerance, it records
that nominal spacing. It copies elevation values without rounding/resampling.
The hashes above were reproduced through this adapter, not hand-entered data.

For a full locally downloaded ETOPO GeoTIFF, use:

```sh
python3 -m tools.earth prepare-raster --source /data/earth/ETOPO.tif \
 --output /data/earth/ETOPO.bin
# Point the configuration's elevation source to ETOPO.bin, format "envi".
```

Only ordinary local TIFF/BigTIFF files with the GTiff driver are accepted; VRT and
other reference-bearing formats are rejected. GDAL writes into isolated staging,
existing output/sidecars are preserved, and path-dependent header descriptions
are canonicalized. The source TIFF and generated ENVI files can be several GB.
Budget disk/RAM/runtime before acquiring or converting global products.

## Measured results, not global claims

Measured 2026-10-11 with Python/zstandard 0.25.0, libzstd 1.5.7, tool 0.1.0.
Both real packs contain the **same two level-6 tiles**, one TXKF airport and
runway from OurAirports commit `ecead39624006017b34acf27a4b098c9cc8a4635`, and no
roads. The ~600 × 600 km source window is **larger than the packed footprint**.
Core bounds: 66.09375°W to 63.28125°W, 30.83016353°N to 32.77583071°N.
Numerical spherical core area: **39,899.59 km²**, a representative bounded Bermuda
sample selected to test neighboring tiles, bathymetry, shoreline and airports.
The footprint is a curved cube-grid region, not its rectangular bounding box.

| Same footprint, source resolution | Elevation bytes (2 tiles) | Other payload | Manifest | Total pack |
|---|---:|---:|---:|---:|
| 60″ | 105,681 | 2,443 | 3,680 | **111,804** |
| 30″ | 116,312 | 2,443 | 3,688 | **122,443** |
| Full-world 60″ | not measured | not measured | not measured | **not measured** |
| Full-world 30″ | not measured | not measured | not measured | **not measured** |

Each complete sample was built twice; all filenames and hashes matched exactly.
Both adjacent aprons match their neighbors' quantized core samples exactly.
Maximum quantization errors versus the unrounded 2×2 quadrature values were
**0.4999988 m (60″)** and **0.4999996 m (30″)**. This is not a claim of sub-metre
source accuracy: interpolation, source error and quadrature are separate.
On 512 stride-16 probe pixels, 2×2 versus 8×8 quadrature differed by at most
1.8594 m / 4.8351 m (mean absolute 0.0691 m / 0.1352 m), respectively. That is a
sampled convergence diagnostic, not a global bound or ground-truth comparison.

60″ source spacing is about 1.85 km north–south; 30″ about 0.93 km. The level-6
output's globally averaged spacing is about 0.563 km, so these sample outputs
**oversample** the sources and do not create finer real-world detail.

These exact geometric counts may help planning, without guessing compression:

| Global output level | Tiles | Uncompressed 258² int16 bytes, one level | Mean spacing |
|---|---:|---:|---:|
| 4 | 1,536 | 204,484,608 | ~2.25 km |
| 5 | 6,144 | 817,938,432 | ~1.13 km |
| 6 | 24,576 | 3,271,753,728 | ~0.56 km |

Counts exclude headers, manifests, roads, airports, licenses and other levels.
Aprons add **1.5686%** to sample storage. A world pack needs explicit output levels
and inventory, followed by an actual complete build before stating a compressed
size. Bermuda compression must not be presented as representative global terrain.

## Safety, determinism and remaining work

- Verification rejects absolute/traversal/Windows-device paths, duplicate or
  case-colliding inventory entries, links, unexpected directories/files, unsupported
  schemas/versions/codecs, invalid JSON numbers/types and corrupt payloads.
- Limits: 64 MiB manifest, 200,000 files, 64 GiB total payload, 2 MiB elevation
  file, exactly 133,128 decompressed elevation bytes; 64 MiB roads/airport file,
  200,000 road vertices per tile, 1 MiB license notice. Road input is bounded at
  64 MiB/1,000,000 vertices; partition large GRIP inputs into reviewed regional
  jobs. v1 is not a high-throughput global vector spatial index.
- Construction verifies the staged pack before final rename; failed builds do
  not publish partial packs. Use a new output and **one writer**. Portable rename
  is atomic but not an atomic no-replace against a racing creator of an empty
  directory; concurrent publication/mutation is unsupported.
- SHA-256 detects corruption and inconsistent manifests, **not authenticity**.
  A malicious author can replace both payload and manifest. Use trusted release
  provenance and a separately verified manifest digest; no signing system exists.
- Same-environment byte repeatability is tested. Cross-platform libm rounding,
  Python/GDAL and compression-library differences are not yet proven byte-identical.
  Pin input hashes, tool version, parameters and recorded codec versions. Decoder
  format portability is distinct from producer cross-platform identity.
- No global NOAA raster or GRIP archive was downloaded in this run, no complete
  global pack was measured, and no real GRIP geometry was tested. A full 60″/30″
  size table remains an explicit Phase 0 deliverable pending those builds.
- Source credentials, download links, datum and licensing are separately recorded
  per layer. No land-cover, higher-resolution regional DEM or game integration is
  added in this phase.
