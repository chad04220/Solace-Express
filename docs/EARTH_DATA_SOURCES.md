# Solace Express Phase 0: source and redistribution review

Checked **2026-10-11 UTC**. This is a source/terms review, not a legal opinion. Evidence and raw samples are outside the repository. No raw global dataset or road archive was downloaded or committed. No source was published. Local numeric sources were fetched through official read-only HTTP/OPeNDAP routes.

## Recommended working defaults

- Elevation: **NOAA ETOPO 2022 v1, surface**, EGM2008 metres, with 60″ and 30″ source samples. Preserve CC0 text, NOAA citation, download URL, exact bytes/hash, query window and processing steps.
- Airports: **OurAirports**, pinned commit below; public domain / repository Unlicense. Preserve stable upstream IDs and record feet-to-metres conversions. The supplied sample is TXKF and its runway, not a claim of complete country coverage.
- Roads: owner chose to proceed under a **CC BY 4.0 working assumption for GRIP4**, with attribution. Preserve the publisher discrepancy and upstream-source uncertainty; this choice does not establish legal clearance. No GRIP geometry is included in this research sample. Synthetic topology tests must stay clearly identified.
- Natural Earth is a lower-detail public-domain road alternative. Direct OSM requires separate ODbL compliance and should not be silently mixed into a permissively labelled pack.
- Never present published raw-source bytes or a regional compression result as a measured full-world pack. Full 60″ and 30″ pack sizes remain **not measured** until those packs are actually built.

## 1. NOAA ETOPO 2022

Official sources:

- Product: https://www.ncei.noaa.gov/products/etopo-global-relief-model
- DOI: https://doi.org/10.25921/fd45-gt74
- Product metadata: https://data.noaa.gov/metaview/page?xml=NOAA/NESDIS/NGDC/MGG/DEM//iso/xml/etopo_2022.xml&view=getDataView&header=none
- User guide, revision 1.2 (13 October 2022): https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/docs/1.2%20ETOPO%202022%20User%20Guide.pdf
- CC0 legal text: https://creativecommons.org/publicdomain/zero/1.0/legalcode.en

The metadata identifies **CC0-1.0 worldwide**, in addition to U.S. public-domain status. The guide permits private, academic and commercial uses. Retain the accuracy/liability disclaimer and navigation warning. Horizontal coordinates are WGS84 longitude/latitude (EPSG:4326); heights are metres relative to **EGM2008** (EPSG:3855), not WGS84 ellipsoidal heights. The 30″ and 60″ products are downsampled from 15″; only 15″ has accompanying source-ID grids. This is a terrain/bathymetry source, not navigation data.

Suggested credit: NOAA National Centers for Environmental Information (2022), ETOPO 2022 15 Arc-Second Global Relief Model, https://doi.org/10.25921/fd45-gt74, accessed 2026-10-11. Regional surface subset and game-tile transformations by the Solace Express project. CC0-1.0. Not for navigation.

### Verified small-subset route

NOAA's official interactive alternative is https://www.ncei.noaa.gov/maps/grid-extract/ (GeoTIFF output). For reproducibility, the following **OPeNDAP ASCII requests were actually downloaded**:

60″:
https://www.ngdc.noaa.gov/thredds/dodsC/global/ETOPO2022/60s/60s_surface_elev_netcdf/ETOPO_2022_v1_60s_N90W180_surface.nc.ascii?z[7176:1:7499][6720:1:7103]

30″:
https://www.ngdc.noaa.gov/thredds/dodsC/global/ETOPO2022/30s/30s_surface_elev_netcdf/ETOPO_2022_v1_30s_N90W180_surface.nc.ascii?z[14352:1:14999][13440:1:14207]

Both have cell-edge bounds **west −68°, south 29.6°, east −61.6°, north 35°**, approximately 600 × 600 km around Bermuda. Indices are inclusive. The NetCDF/OPeNDAP **latitude axis increases south to north**; longitude increases west to east. Do not infer row order from the north-up GeoTIFF filename. The response contains actual float32 elevation values, `z.lat` and `z.lon` coordinate arrays.

| Source | Rows × columns | First / last latitude centre | First / last longitude centre | Actual ASCII bytes |
|---|---:|---|---|---:|
| 60″ | 324 × 384 | 29.608333333333334 / 34.99166666666666 | −67.99166666666667 / −61.608333333333334 | 1,406,779 |
| 30″ | 648 × 768 | 29.60416666666667 / 34.99583333333334 | −67.99583333333334 / −61.60416666666667 | 4,915,126 |

Observed regional heights: 60″ **−5,746.25 to +24.292978 m**, 25 positive samples; 30″ **−5,898.25 to +42.91061 m**, 96 positive samples. Maximum coordinate residual from uniform 1/60° and 1/120° steps was 1.4211e−14°. These extrema and counts were calculated from downloaded samples, not the global model.

| Local file | Actual bytes | SHA-256 |
|---|---:|---|
| `bermuda60.ascii` | 1,406,779 | `5ec0303542bffbe4f80837767b4ad35f34e345d04794873a19b0ab0e2ce1a644` |
| `bermuda30.ascii` | 4,915,126 | `6506652edfa3a835c9e6db5dc801c4f0b9fda9c627b719d38580c0416905ee74` |
| `bermuda60.grid.json` | 1,268,544 | `1d60fac492fb583591b8ba5e5f28083032cb3adbf3e0f06b1686b5b95f17f25f` |
| `bermuda30.grid.json` | 4,389,589 | `231cdfa1db76e1ee7f9a6dad71151c6a6e5d6ef4480dbdc3151fc3235500ead2` |

Grid JSONs are derived inputs, not world packs: version 1, centre-based `lon_min` / `lat_min`, positive `lon_step` / `lat_step`, `periodic:false`, and unrounded numeric `heights` in south-to-north rows. JSON serialization used compact separators and one trailing newline. No synthetic values were added. A tile including an apron must fit within the sample-centre interpolation coverage; the 600 km source window is not automatically a complete set of cube tiles.

### Published or HTTP-advertised global sizes, not full-pack measurements

| ETOPO surface file | Bytes | Evidence type |
|---|---:|---|
| 60″ NetCDF | 478,290,125 | NOAA THREDDS catalog `dataSize` |
| 30″ NetCDF | 1,642,335,281 | NOAA THREDDS catalog `dataSize` |
| 60″ GeoTIFF | 465,969,062 | HTTP HEAD Content-Length |
| 30″ GeoTIFF | 1,585,813,987 | HTTP HEAD Content-Length |

Catalogs:
- https://www.ngdc.noaa.gov/thredds/catalog/global/ETOPO2022/60s/60s_surface_elev_netcdf/catalog.html?dataset=globalDatasetScan/ETOPO2022/60s/60s_surface_elev_netcdf/ETOPO_2022_v1_60s_N90W180_surface.nc
- https://www.ngdc.noaa.gov/thredds/catalog/global/ETOPO2022/30s/30s_surface_elev_netcdf/catalog.html?dataset=globalDatasetScan/ETOPO2022/30s/30s_surface_elev_netcdf/ETOPO_2022_v1_30s_N90W180_surface.nc

Exact GeoTIFF URLs:
- https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif
- https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/30s/30s_surface_elev_gtif/ETOPO_2022_v1_30s_N90W180_surface.tif

Caution: the THREDDS catalogs display a stale generic ETOPO1 description/citation despite the ETOPO2022 filename. Use the current product DOI/metadata above for attribution. No global-file SHA-256 was measured.

## 2. GLOBIO GRIP4 roads

- Official product: https://www.globio.info/download-grip-dataset
- Version: **Version 4 – 2018**, published 23 May 2018; Zenodo record created 7 April 2022.
- Dataset DOI: https://doi.org/10.5281/zenodo.6420961
- Record: https://zenodo.org/records/6420961
- Machine-readable metadata: https://zenodo.org/api/records/6420961
- Paper: https://doi.org/10.1088/1748-9326/aabd42
- Official download portal default attribution terms: https://dataportaal.pbl.nl/
- Working license: https://creativecommons.org/licenses/by/4.0/
- Exact legal text: https://creativecommons.org/licenses/by/4.0/legalcode.txt

**Unresolved discrepancy:** GLOBIO and Zenodo's description say CC0; the same Zenodo record's `metadata.license.id` is `cc-by-4.0`. PBL's portal defaults to CC BY 4.0 attribution unless otherwise stated. Both GLOBIO and Zenodo acknowledge mixed upstream sources including OpenStreetMap. A publisher's CC0/BY notice cannot establish that every upstream right is cleared. The owner's chosen CC BY 4.0 assumption preserves this uncertainty. GRIP is explicitly unsuitable for navigation and does not constitute a complete, current record of every real road.

Under the working CC BY 4.0 approach, retain authors, source, license/disclaimer notices, indicate modifications, avoid implying endorsement, and do not add restrictions preventing permitted reuse of the licensed data. CC BY is not ShareAlike; this does not resolve possible upstream ODbL issues.

### Attribution text for packs that actually contain GRIP

GRIP4 (Version 4 – 2018), J. R. Meijer, M. A. J. Huijbregts, C. G. J. Schotten and A. M. Schipper. Global patterns of current and future road infrastructure, Environmental Research Letters 13, 064006 (2018), https://doi.org/10.1088/1748-9326/aabd42. Dataset: https://doi.org/10.5281/zenodo.6420961. Used under the project's CC BY 4.0 working assumption: https://creativecommons.org/licenses/by/4.0/. Supplied without warranties and not suitable for navigation. No endorsement implied.

Append a truthful transformation notice matching the actual output, for example: “Changes by Solace Express: selected regional features, clipped lines to cube tiles, simplified geometry, quantized tile coordinates, retained source road classes, and generated deterministic tile/node identifiers.” Do not claim unperformed transformations. Keep the preceding uncertainty note in provenance. If no GRIP geometry is present, say so rather than suggesting synthetic tests derive from GRIP.

### Official archive sizes/checksums (not downloaded)

| Archive | Published bytes | Published MD5 |
|---|---:|---|
| `GRIP4_global_vector_fgdb.zip` | 2,334,355,294 | `526db5eb478f4c990e27fcbc471abf4b` |
| `GRIP4_Region1_vector_fgdb.zip` | 525,425,082 | `18cb99e5e42ee224b6d6a761427c3fc1` |
| `GRIP4_Region1_vector_shp.zip` | 909,111,290 | `24680b88e7f58b973b267ecbc8a02618` |

Direct regional sources:
- https://dataportaal.pbl.nl/downloads/GRIP4/GRIP4_Region1_vector_shp.zip
- https://dataportaal.pbl.nl/downloads/GRIP4/GRIP4_Region1_vector_fgdb.zip
- https://zenodo.org/records/6420961/files/GRIP4_Region1_vector_shp.zip?download=1

The metadata response is saved as `grip_metadata.json`, 13,438 bytes, SHA-256 `0bff395a3ffbb8e9a44bbb4fc8f02a6266ea6777caafdb2ce71eae228f816035`. No publisher-verified small geometric extract route was established; the official regional archives exceed the download budget. An unrelated ArcGIS mirror is not a substitute for verified upstream provenance.

### Verified importer fields

Official workbook: https://zenodo.org/records/6420961/files/GRIP4_AttributeDescription.xlsx?download=1

Downloaded as `GRIP4_AttributeDescription.xlsx`: **23,947 bytes**, SHA-256 `38ced93e91000179bd398df7e2d919b234d37c178532a21c12651ecbeb901a51`.

- `gp_rtp`: 0 unspecified; 1 highway; 2 primary; 3 secondary; 4 tertiary; 5 local. Exporters may uppercase names.
- `gp_rex`: 0 unspecified; 1 open; 2 restricted; 3 closed; 4 under construction/repair.
- `gp_rav`: 0 unspecified; 1 seasonal; 2 all-year.
- `gp_rsi`: upstream source ID; 54 identifies OSM, 57 Google MapMaker export, 66 U.S. Census. Preserve this provenance.
- `gp_rsy`: source year, zero unspecified. `gp_rcy`: numeric country code (Bermuda 60). `gp_gripreg`: archive region, 1–7.
- The workbook documents no universal stable unique feature ID. Preserve a source FID plus pinned archive hash if used; generated IDs must be labelled generated.

## 3. OurAirports

- Official terms/data: https://ourairports.com/data/
- Official repository: https://github.com/davidmegginson/ourairports-data
- Pinned source commit: **ecead39624006017b34acf27a4b098c9cc8a4635**, 2026-10-11T01:53:12Z.
- Commit URL: https://github.com/davidmegginson/ourairports-data/commit/ecead39624006017b34acf27a4b098c9cc8a4635
- Airport source: https://raw.githubusercontent.com/davidmegginson/ourairports-data/ecead39624006017b34acf27a4b098c9cc8a4635/airports.csv
- Runway source: https://raw.githubusercontent.com/davidmegginson/ourairports-data/ecead39624006017b34acf27a4b098c9cc8a4635/runways.csv
- License: https://raw.githubusercontent.com/davidmegginson/ourairports-data/ecead39624006017b34acf27a4b098c9cc8a4635/LICENSE

The official data page releases the data into the public domain, permits competing/reused datasets, requests but does not require credit, and disclaims accuracy/fitness. The official repository's LICENSE is the Unlicense. Suggested credit: “Airport and runway data: OurAirports contributors, public domain; source snapshot ecead39624006017b34acf27a4b098c9cc8a4635, retrieved 2026-10-11; regional selection and unit conversion by Solace Express.”

The country CSV endpoint repeatedly returned HTTP 502. A small, reproducible alternative was verified: HTTP Range requests to the pinned `airports.csv` (response 206), bytes **0–1023** for the header and **10000000–10065535** for a segment containing TXKF. The exact matching complete row was selected. This is an actual TXKF sample, not evidence of all airports in Bermuda. The full pinned runway CSV was small enough to fetch; selected `airport_ident=TXKF`.

| Local file | Actual bytes | SHA-256 |
|---|---:|---|
| `ourairports_airport_header.range` | 1,024 | `7ebd62b0fb7e01f484d4120de67ab1bfa7ee33e8fff606944fa0d3171148e20a` |
| `ourairports_airport_probe.range` | 65,536 | `86c334ed8415566682faa7003a674ec92a249d1a75b45531360cbf505cd86f82` |
| `ourairports_runways.csv` | 3,969,671 | `bcc84c682d36c38e585f116855ae7f72bb590647efba354a6ae05caa2c66d533` |
| `bermuda_airports.csv` | 564 | `c97f7466d8b5dc34e53e7d978e078bbab80571ebfa83c827841d3c9120e7e6ae` |
| `bermuda_runways.csv` | 367 | `8d82704dc4515b2eddd11944ff1ac305a7d8773e530832aa9d1d2a0a61ce3a36` |
| `ourairports_LICENSE` | 1,211 | `6b0382b16279f26ff69014300541967a356a666eb0b91b422f6862f6b7dad17e` |

Stable IDs: airport **6416 / TXKF / BDA**, runway **233616**. Current airport header includes both `icao_code` and `gps_code`. Airport location 32.363802, −64.67824; elevation 12 ft. Open asphalt runway 12/30, 9,705 × 150 ft; endpoints 32.366699, −64.694099 and 32.361401, −64.6633; true headings 117°/297°; end elevations 18 ft. Do not replace missing threshold values with claims of measured zero.

For scale context only, the official data page listed the previous day's airports CSV as 12,743,789 bytes and runways CSV as 3,969,607 bytes. Those are published 10 October figures, not the fetched 11 October runway size, and no complete airport-file SHA-256 was measured.

## 4. Road alternatives

### Natural Earth

Official terms: https://www.naturalearthdata.com/about/terms-of-use/

Roads product: https://www.naturalearthdata.com/downloads/10m-cultural-vectors/roads/

The road theme is **5.0.0** (the overall Natural Earth collection has other version numbers). Published size: **8.66 MB**; verified HTTP HEAD on https://naciscdn.org/naturalearth/10m/cultural/ne_10m_roads.zip advertised **9,075,919 bytes**, consistent with binary MB rounding. Body not downloaded; SHA-256 not measured. Last-Modified was 8 December 2021. The optional North American supplement is theme 4.0.0, published 45.57 MB and was not fetched.

The terms explicitly allow commercial use/modification/distribution and require no permission or attribution. Suggested voluntary credit: “Made with Natural Earth.” Preserve the no-warranty notice. This is cartographic road data at 1:10 million presentation scale, not an all-roads navigation network. The detailed roads page contains old historical GRIP licensing text; use current GLOBIO/Zenodo evidence for GRIP rather than treating this historical cross-link as authoritative.

### Direct OpenStreetMap

- Terms: https://www.openstreetmap.org/copyright
- ODbL 1.0 text: https://opendatacommons.org/licenses/odbl/1-0/
- OSMF attribution guidance, including games: https://osmfoundation.org/wiki/Licence/Attribution_Guidelines

Direct OSM road extraction is permitted commercially with ODbL obligations. Include “© OpenStreetMap contributors” and a link to the copyright page; retain license notices in the database/documentation. Public use/distribution of a substantial transformed road database generally requires ODbL share-alike and a machine-readable database or alteration offer. A rendered game is not automatically wholly ODbL, but rendering does not remove obligations on its derived database. Keep the road database separable and review distribution packaging. OSMF game guidance allows visible attribution on a startup screen, credits/menu, gameplay, or another suitable location, with detailed licensing accessible. No OSM data was downloaded.

CC0 and CC BY legal text was inspected through the web research tool. Direct attempts to save the official plain-text license URLs returned HTTP 403, so this evidence directory does not contain local copies of those two license texts. Do not describe them as bundled until the pack actually includes them.

## 5. Scientific preview and download accounting

`bermuda-etopo-preview.png` is a 1980 × 1290 PNG generated from the actual 30″ JSON source, with a TXKF point from the pinned OurAirports sample. It includes geographic labels, the observed vertical range, EGM2008 units, source credit, access date, a navigation warning and an explicit **scientific tool preview, not an in-game render** label. Visually inspected after correcting header/footer/colorbar spacing and annotation contrast. Generator: `make_bermuda_preview.py`.

Fetched numeric payloads plus the GRIP attribute workbook total **10,382,083 bytes** (two ETOPO subsets, two airport ranges, runway CSV, workbook), plus small pages/metadata/licenses and one preliminary 4,879-byte ETOPO probe. No download approached the 20 MB cap; no global elevation or GRIP archive was fetched. Derived JSON/CSV/PNG sizes are local output sizes and do not count as upstream downloads or finished world-pack measurements.
