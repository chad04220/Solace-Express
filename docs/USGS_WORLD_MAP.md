# Global elevation source handoff

The complete ETOPO 2022 v1 60-arc-second surface baseline is prepared and audited.
All **233,280,000 Float32 pixels** match the original download bit-for-bit.
A bounded 20-tile sample passes the existing Earth-pack verifier, repeats every
file hash, and matches all tested neighboring aprons. This is offline data for
the future Earth loader; no runtime integration is included.

Interface: [`tools.earth` at `ebcbba8c`](https://github.com/chad04220/Solace-Express/tree/ebcbba8c4c5f839894cd2f326c4818454c29fa2f/tools/earth),
format `solace-earth-pack` v1. The source wrapper delegates conversion to its
`prepare_raster` adapter and leaves the pack writer, sampler and verifier unchanged.
The assignment and source discussion are in [issue #2](https://github.com/chad04220/Solace-Express/issues/2#issuecomment-6105477286).

## Verified downloads and prepared raster

| Input | Version | Measured bytes | SHA-256 |
|---|---|---:|---|
| [NOAA surface GeoTIFF](https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif) | ETOPO 2022 v1, 60 arc-seconds | 465,969,062 | `9d27d4b8ea8e76977e2988bca667d7c8fa68b927355feffcddd6b4875a7fd08e` |
| [USGS mean ArcGrid ZIP](https://edcintl.cr.usgs.gov/downloads/sciweb1/shared/topo/downloads/GMTED/Grid_ZipFiles/mn30_grd.zip) | GMTED2010, 30 arc-seconds | 277,229,566 | `dfd0d6c6486f4da22109be6107c93a70ef9917a5b6f954cd82d87cf8b920149d` |
| Prepared ENVI binary | Same ETOPO Float32 cells | 933,120,000 | `522c6329ed7a8c08c14718e164b04ba203d23c51212371146b789baa419c9895` |
| Prepared ENVI header | GDAL 3.8.4; canonical description | 658 | `fffb4f6c3051d382557744a7e32775664220338255921f5b867fc7c7f947a86f` |

The original and prepared data, generated packs and diagnostic PNG stay outside
Git. The downloadable source bundle contains the ENVI binary/header, GDAL
sidecar, source audit/manifest, exact sample configuration, credit notice and
labeled elevation preview. It also contains the separately verified sample pack
and measurement records. Raw downloads are reproducible from the URLs above.

The delivery ZIP is **675,246,138 bytes**, SHA-256
`e2c73659133353f43fbe55326b2f94bb781ca680ad1b00eb1bc781f46b7f13da`.
It is delivered as two numbered byte parts because the single-file save failed.
Download both `.zip.001` and `.zip.002` into one directory, then reassemble:

```sh
python3 tools/usgs_world_map/join_downloads.py --parts-dir /path/to/downloads \
  --output /data/earth/Solace-Express-Global-Elevation.zip
```

The helper verifies both part sizes/SHA-256 and the complete ZIP digest before
publishing a new output file. This is transport splitting only; the ENVI input
and v1 pack contract are unchanged. Keep the extracted source and sample outside
Git. The full archive passes ZIP CRC checks and all 37 payload hashes.

Prepared geometry is north-up, west-to-east, single-band Float32 BSQ,
little-endian, zero header offset: **21,600 × 10,800** cells. Nominal geographic
bounds are `[-180,-90,180,90]`; affine `(a,b,c,d,e,f)` is
`(1/60,0,-180,0,-1/60,90)` degrees. GDAL's header writes the pixel size as
`0.0166666666666667`; the corresponding bounds differ from nominal by less than
`1e-12` degrees. Nodata is `-99999`, with **zero nodata or nonfinite cells** in
the complete source. The first/last latitude centres are approximately
`±89.9916666667°`.

The original and ENVI CRS are EPSG:9518, combining WGS84 geographic coordinates
with **EGM2008 orthometric metres**. The header preserves the vertical CRS.
No resampling, quantization or vertical transformation occurs in preparation.
The existing `EnviGrid` wraps longitude and extends the nearest latitude centre
across the half-cell polar cap for a complete global raster. Ten antimeridian
latitude checks and ten polar-cap checks pass. The measured extrema are
`-10752.08203125 m` and `8157.3564453125 m`; their cell locations and regional
probes are in `source-audit.json`.

GMTED is retained as a **separate comparison source**. Its ZIP passes CRC checks.
Its native ArcGrid has 43,200 × 20,880 cells, a small geographic grid offset, and
ends near 84°N. It supplies no ocean bathymetry. The [USGS source report](https://pubs.usgs.gov/of/2011/1073/pdf/of2011-1073.pdf)
describes predominantly EGM96 heights with other source datums in some areas.
It is not relabeled EGM2008 or blended into this single-source v1 pack.

## Reproduction

Tested on Linux with Python 3.12.14, GDAL CLI 3.8.4, Rasterio 1.5.2/GDAL 3.12.2,
NumPy 2.3.5, Pillow 12.3.0 and zstandard 0.25.0/libzstd 1.5.7. Install the
official `gdal_translate` utility separately. Dependencies and downloads are
explicit; keep all data outside the checkout. From the repo root:

```sh
python3 -m pip install -r tools/usgs_world_map/requirements.txt
mkdir -p /data/earth
curl --fail -L 'https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif' \
  -o /data/earth/ETOPO_2022_v1_60s_N90W180_surface.tif
python3 -m tools.usgs_world_map.prepare_sources prepare \
  --etopo /data/earth/ETOPO_2022_v1_60s_N90W180_surface.tif \
  --output /data/earth/source-prep --preview
# Optional: add --gmted-zip /data/earth/mn30_grd.zip to verify the separate archive.
python3 -m tools.usgs_world_map.prepare_sources make-sample \
  --source-dir /data/earth/source-prep
python3 -m tools.earth build \
  --config /data/earth/source-prep/representative-L4.config.json \
  --output /data/earth/sample-A
python3 -m tools.earth build \
  --config /data/earth/source-prep/representative-L4.config.json \
  --output /data/earth/sample-B
python3 -m tools.earth verify /data/earth/sample-A
python3 -m tools.usgs_world_map.prepare_sources check-sample \
  --pack /data/earth/sample-A --repeat /data/earth/sample-B \
  --output /data/earth/sample-validation.json
python3 -m tools.usgs_world_map.prepare_sources convergence \
  --pack /data/earth/sample-A \
  --prepared /data/earth/source-prep/etopo2022_surface_60s.bin \
  --output /data/earth/convergence-probe.json
python3 tests/earth_pack_test.py
```

The conversion itself is the published command
`python3 -m tools.earth prepare-raster --source INPUT.tif --output OUTPUT.bin`.
The wrapper adds pinned-download verification, resource preflight and the full
pixel/geometry audit. Its Linux resource measurements use `resource.getrusage`.
Output directories must be new and have one writer.

## Measured sample and resource use

Exact [configuration](../tools/usgs_world_map/representative-L4.config.json):
SHA-256 `021831eccf7956222e2f202ad404d5ca03c952f09c294fd055d7b861e622b195`.
Twenty L4 tiles cover Himalayan land, California coast, Atlantic ocean,
Greenland, Mariana vicinity, both polar centres, the antimeridian and a
`px`/`py` face edge. This is a bounded sample, not a full global tile inventory.
Each tile uses 256² cell-centred samples plus apron, whole-metre half-away-from-zero
quantization, `area_samples=2`, zstd level 9 and the existing v1 header/codec.

| Measurement | Observed result |
|---|---:|
| GeoTIFF → ENVI conversion | 4.4491 s |
| Complete pixel/wrap/pole audit | 3.0246 s |
| Preparation process peak RSS, including preview | 265.60 MiB |
| Sample A / B build, including built-in verification | 36.5312 / 36.4994 s |
| Sample A / B child peak RSS | 275.74 / 275.84 MiB |
| 20 elevation files | 1,396,943 bytes |
| Whole sample including manifest and credit | 1,403,930 bytes |
| Manifest SHA-256 | `cddc55d760f1397d29cedcff34facbb19d132dc97cc3da493dd670ad7102f4b4` |
| Exact apron/core comparisons | 6,144 same-face; 512 cross-face; 8 corner |
| Repeated build | Every filename/byte count/hash matches |
| Existing Earth contract tests | 55 passed, no skips; 8.835 s |

Full records, source geometry/terms, tile inventory and codec versions are in
[measured-handoff.json](../tools/usgs_world_map/measured-handoff.json). Timings
come from a shared host. Only same-environment repeatability was tested.

The 1,280 stride-32 convergence probes found N2→N8 quadrature differences up to
38.3563 m (mean absolute 0.9138 m), N4→N8 up to 6.2803 m (mean 0.1800 m), and
N8→N16 up to 1.3733 m (mean 0.0397 m). Maximum integer-quantization error on
these probes is 0.499979 m. These compare sampling choices on the same source;
they do not measure terrain truth or establish a global error bound. **Review
the quadrature choice before a production global build.** N8 is the proposed
starting point for a fuller convergence review; the measured sample remains N2.

## Proposed full-global build, not yet performed

Start with all six faces at **L4** using the same ETOPO baseline. Globally
averaged L4 spacing is about 2.25 km; the source's north–south spacing is about
1.85 km. Higher output levels interpolate this source without creating 30 m
terrain detail. Consider a 0..4 pyramid only after the loader and sampling policy
are reviewed; the current manifest records one quadrature N for an entire pack.

| Standalone global level | Exact tiles | Exact raw int16 payload | Header bytes | Linear N2 time projection |
|---|---:|---:|---:|---:|
| L4 | 1,536 | 204,484,608 | 101,376 | ~47 min |
| L5 | 6,144 | 817,938,432 | 405,504 | ~3.1 h |
| L6 | 24,576 | 3,271,753,728 | 1,622,016 | ~12.5 h |

A 0..4 inventory has 2,046 tiles and 272,379,888 raw payload bytes. No full-world
compressed size or runtime is measured. The table is geometry plus a linear
projection of the 20-tile N2 observation; source hash/setup, cache effects and
terrain differences prevent treating it as a guarantee. Quadrature work scales
roughly with N²: an N8 L4 build could take around 12.5 hours on this Python
implementation, but that is an unmeasured projection, not a benchmark.

For initial L4 planning, reserve at least 3 GB incremental working disk alongside
the downloaded source and prepared raster, and 2 GiB available RAM. The prepared
raster alone is 933 MB. A full mmap traversal may make more pages resident than
the representative sample; its measured 276 MiB RSS is not a global bound.
Compressed storage remains to be measured, without extrapolating this sample's
compression ratio to all terrain. The host had 26.25 GB free disk and 9.72 GB
available RAM at preparation preflight. No large global pack was attempted.

## Terms and owner-supplied later sources

[NOAA's official metadata](https://data.noaa.gov/metaview/page?header=none&view=getDataView&xml=NOAA/NESDIS/NGDC/MGG/DEM//iso/xml/etopo_2022.xml)
specifies CC0-1.0 worldwide. The [credit/link notice](../tools/usgs_world_map/NOAA-CC0-notice.txt)
preserves the DOI citation and source limitations. USGS-produced GMTED is public
domain with requested credit under [USGS policy](https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits).

The owner supplied two additional sources during this handoff:

| Source | Actual meaning/resolution | Suggested later role |
|---|---|---|
| [Natural Earth 1:10m rasters](https://www.naturalearthdata.com/downloads/10m-raster-data/) | **1:10 million map scale**, not 10-metre pixels; largest raster 21,600 × 10,800 | Separate coarse world-color reference; [Natural Earth I](https://www.naturalearthdata.com/downloads/10m-raster-data/10m-natural-earth-1/) offers land-cover colors without baked relief |
| [ASTER GDEM V003](https://www.earthdata.nasa.gov/data/catalog/lpcloud-astgtm-003) | 1 arc-second, approximately 30 m at the equator; land between 83°N and 83°S | Candidate regional detail around airports/flying regions, keeping the global ocean/polar baseline |

[Natural Earth's terms](https://www.naturalearthdata.com/about/terms-of-use/) make
its raster/vector data public domain; optional credit is “Made with Natural Earth.”
RGB map colors are not categorical biome labels. ASTER's [V3 guide](https://lpdaac.usgs.gov/documents/434/ASTGTM_User_Guide_V3.pdf)
describes 22,912 one-degree tiles with 3,601² DEM cells plus QA/NUM companions.
Its [NASA catalog](https://doi.org/10.5067/ASTER/ASTGTM.003) specifies WGS84/EGM96.
The [project site](https://www.jspacesystems.or.jp/ersdac/GDEM/E/) identifies
the GDEM public-domain policy. USGS [V3 validation](https://www.usgs.gov/publications/validation-aster-global-digital-elevation-model-version-3-over-conterminous-united)
also shows that the grid can include canopy/building effects: 30 m sampling
does not imply 30 m or bare-earth accuracy.

Neither candidate was downloaded, converted, or added to the v1 pack in this
assignment. Regional ASTER integration needs exact tile hashes, QA checks,
EGM96→EGM2008 conversion and reviewed boundary blending. Imagery/biome atlases,
runtime streaming, Solace placement and airport changes remain separate work.
