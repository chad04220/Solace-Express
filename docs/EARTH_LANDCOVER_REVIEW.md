# Solace Express: Esri / Impact Observatory land-cover source review

Verified: 2026-10-11 UTC. Scope: read-only source, metadata, access and licensing research. No implementation, imagery downloads, accounts, terms acceptance, service subscriptions or external publishing.

## Decision

Record this as a strong Phase 0 candidate for Phase 5 land-cover masks. It can guide forest, crop, water and built-up scenery placement in an offline game. It does not supply individual tree positions/species, building footprints/heights or terrain elevation. Acquisition for a selected route and conversion into game assets remain deferred.

## Product and current coverage

- Product: **Sentinel-2 10m Land Use/Land Cover Time Series**, also described as **Impact Observatory Maps for Good / 10m Annual Land Use Land Cover (9-class)**.
- Produced by Impact Observatory with Esri and Microsoft, from ESA Sentinel-2 imagery. This is classified land-use/land-cover data, not a photographic basemap.
- Current Esri item metadata lists annual maps for **2017–2025**, global coverage, **10 m** cells, and **Version 003**. Each year is a representative composite of classifications over that year, not a single-date observation.
- Native source coordinate system: UTM WGS84. The ArcGIS image service uses Web Mercator EPSG:3857. Do not assume every delivery channel has the same CRS.
- The official item notes fewer source observations in 2017 and potentially lower classification accuracy for that year.

Authoritative sources:

- Landing page: https://livingatlas.arcgis.com/landcover/
- Official dataset item: https://www.arcgis.com/home/item.html?id=cfcb7609de5f478eb7666240902d4d3d
- Exact official item metadata read successfully via Python urllib: https://www.arcgis.com/sharing/rest/content/items/cfcb7609de5f478eb7666240902d4d3d?f=json
- Live service metadata: https://ic.imagery1.arcgis.com/arcgis/rest/services/Sentinel2_10m_LandCover/ImageServer?f=pjson
- Esri's 2025 release announcement: https://www.esri.com/about/newsroom/arcnews/latest-land-cover-data-release-shows-more-change-over-time

The release article contains an apparent count typo (2017–2025 spans nine years). Use the explicitly listed years in the dataset metadata.

## Class schema and suitability

| Stored value | Class | Potential game use |
| --- | --- | --- |
| 0 | NoData | Unknown; preserve separately |
| 1 | Water | Broad water mask; validate small waterways separately |
| 2 | Trees | Broad wooded-area/procedural-tree mask |
| 4 | Flooded vegetation | Wetland or inundated vegetation treatment |
| 5 | Crops | Agricultural-field surface/placement mask |
| 7 | Built area | Broad settlement/impervious-surface mask |
| 8 | Bare ground | Sparse vegetation, soil, rock/desert treatment |
| 9 | Snow/ice | Persistent snow/ice surface treatment |
| 10 | Clouds | Unknown because of persistent cloud cover |
| 11 | Rangeland | Grass/shrub/open-vegetation treatment |

These IDs are categorical, not continuous measurements. Historical grass/scrub values 3 and 6 are not current classes. The provider merged those into rangeland.

The built-area category includes major roads, rail and other impervious surfaces. Official Esri metadata warns that its land-use emphasis can classify urban yards, parks and groves as built area. Trees denotes clustered canopy, not a tree inventory. Crops does not identify crop species or guarantee exact field boundaries. Annual compositing, class confusion and mixed pixels limit fine scenery precision; 10 m resolution does not mean 10 m positional accuracy or perfect classification.

Source: https://docs.impactobservatory.com/lulc-maps/maps-for-good.html and the official ArcGIS item above.

## Download and subset routes

1. **Esri Land Cover Explorer** exposes GeoTIFF downloads. Esri documents downloads of individual tiles by year or complete years. Prefer only tiles intersecting the eventual route/AOI.
   - https://livingatlas.arcgis.com/landcoverexplorer/
   - https://www.esri.com/arcgis-blog/products/arcgis-living-atlas/imagery/global-land-cover-updates
2. **AWS public source bucket**: `s3://io-10m-annual-lulc/`, region `us-west-2`. The AWS registry documents anonymous access, with no AWS account required. Impact Observatory hosts a STAC API for spatial/year queries.
   - https://registry.opendata.aws/io-lulc/
   - https://api.impactobservatory.com/stac-aws/collections
   - https://docs.impactobservatory.com/tutorials/aws-open-data-exchange/aws-open-data-exchange.html
3. **Microsoft Planetary Computer**: STAC collection `io-lulc-annual-v02`, with Cloud Optimized GeoTIFF data assets. Query by AOI/year, then use the returned asset references according to the platform's access workflow. No credentials or access grants were created in this review.
   - https://planetarycomputer.microsoft.com/dataset/io-lulc-annual-v02
   - https://planetarycomputer.microsoft.com/api/stac/v1/collections/io-lulc-annual-v02

COGs support later windowed/range reading and clipping, avoiding global downloads. Before conversion, inspect the actual asset CRS, geotransform, class schema, NoData, bounds, year and release. Preserve a reproducible manifest with source URL, acquisition time, asset ID, license evidence and a cryptographic checksum after acquisition. The name alone does not prove immutability.

### Verified year-specific source path (metadata and HEAD only)

The IO STAC query below returned item **60W-2024**, with 2024 start/end dates, 10 m spacing, UTM zone 60N (`EPSG:32660`), and the class schema above:

https://api.impactobservatory.com/stac-aws/search?collections=io-10m-annual-lulc&datetime=2024-01-01T00%3A00%3A00Z%2F2024-12-31T23%3A59%3A59Z&limit=1

Exact returned COG asset URL:

https://io-10m-annual-lulc.s3.us-west-2.amazonaws.com/60W_2024.tif

An HTTP HEAD request returned 200, `Content-Length: 79851388`, `Accept-Ranges: bytes`, `Last-Modified: Mon, 23 Jun 2025 20:03:06 GMT`, and ETag `c7ca8830334945d77ef2f4c10a3a6101-10`. The multipart ETag is not a verified content checksum. No raster bytes were downloaded. This is an availability example, **not a chosen Solace Express region**.

## Release and metadata discrepancies

- Esri's current item and Explorer show **2017–2025** and identify **Version 003**.
- Microsoft's live `io-lulc-annual-v02` collection metadata describes **2017–2023**, with temporal end at 2024-01-01.
- IO's live AWS STAC collection and AWS registry describe **2017–2024**, with temporal end at 2025-01-01. A 2024 query returned a real asset as recorded above. An equivalent 2025 query returned zero features on 2026-10-11; this does not contradict 2025 being available through Esri.
- The IO documentation still refers to earlier coverage and release labels. Do not assume Microsoft `V2`, Esri `Version 003` and IO `v1.x` are interchangeable identifiers or that mirrors update together.
- The IO STAC collection has a machine-readable **`license: proprietary`** field, while that same collection's description explicitly states CC BY 4.0 for all years. The AWS registry, IO legal documentation and Esri source-data terms independently specify CC BY 4.0. Treat this as a real metadata inconsistency, preserve the evidence and resolve any conflicting asset-specific licensing before redistribution; do not silently relabel an unknown asset.

## Licensing and attribution

The official ArcGIS item explicitly distinguishes the **Esri service/work**, under the Esri Master License Agreement, from the **source LULC data**, under **CC BY 4.0**. Access to the app or an image service alone is not permission to redistribute unrelated Esri basemap imagery or service tiles. For offline game assets, use the explicitly licensed annual source data and retain its provenance.

Impact Observatory independently confirms annual Maps for Good data as CC BY 4.0 and requests credit to **Impact Observatory, Esri and Microsoft**. This excludes IO Monitor/custom commercial products, which have separate terms.

CC BY 4.0 permits copying, redistribution and adaptations, including commercial uses. Obligations include appropriate credit, a license link, identification of changes, preservation of supplied notices and no additional legal or technological restrictions on the licensed material. Do not imply endorsement. Before game distribution, ensure the dataset-derived portions retain the required rights and attribution rather than being swept into an incompatible blanket asset restriction.

Suggested attribution template, to complete with the selected year/release:

> Land-cover data derived from Impact Observatory, Esri and Microsoft, 10m Annual Land Use Land Cover, [year/release], licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Clipped, reprojected and reclassified for Solace Express. Source: [actual source URL].

Authoritative license URLs:

- https://docs.impactobservatory.com/legal.html
- https://docs.impactobservatory.com/lulc-maps/maps-for-good.html
- https://www.arcgis.com/home/item.html?id=cfcb7609de5f478eb7666240902d4d3d
- https://creativecommons.org/licenses/by/4.0/
- https://creativecommons.org/licenses/by/4.0/legalcode

## Deferred Phase 5 implementation recommendation

These are recommendations inferred from the product specifications, not completed work:

1. Select a route corridor/AOI, actual available year and named source release. Retrieve only intersecting tiles/windows and pin the acquired inputs.
2. Reproject categorical class IDs with **nearest-neighbor**. For coarser LODs, choose documented mode/majority aggregation or calculate per-class area coverage. Do not bilinearly/cubically interpolate class IDs, because that invents or blends category values.
3. Preserve clouds and NoData as unknowns, with explicit fallbacks. If using continuous per-class weights for visual blending, derive them after decoding categories; do not treat raw numeric IDs as weights.
4. Align masks with terrain and route coordinates; inspect seams, rivers, urban greenery and crop/forest boundaries against local reference data. Keep geometry/elevation/tree-detail sources separate.
5. Record regional quality checks, source manifest, license evidence and final shipped attribution.

Categorical resampling guidance:

- https://doc.esri.com/en/arcgis-pro/latest/tool-reference/environment-settings/resampling-method.html
- https://pro.arcgis.com/en/pro-app/3.4/tool-reference/data-management/resample.htm

## Review activity boundary

Only public pages, JSON metadata and one asset HTTP HEAD request were used. No source rasters, rendered imagery or game assets were downloaded. No legal agreement was accepted, no account was created, no game code or content was changed, and no Git operations were performed. This evidence document is the sole generated file from this review.
