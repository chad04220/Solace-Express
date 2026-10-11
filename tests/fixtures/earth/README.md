# Synthetic Earth pipeline fixture

This is original fabricated test data, not a reduced copy of any real dataset.
It is deliberately geographically near Bermuda for coverage tests, but its relief,
roads, airport identifiers and runway information have no real-world meaning.
Do not use it for navigation. No raw NOAA, GRIP or OurAirports data is included.

- `elevation.json`: 9 by 7 south-to-north samples of the affine function
  `-4500 + 23 * (latitude - 32) + 17 * (longitude + 65)` metres.
  The grid covers latitude 25 to 40, longitude -75 to -55, at 2.5 degrees.
  Exact affine interpolation is an independent elevation oracle.
- `roads.geojson`: invented polylines with one shared junction and a crossing
  of the `ny/6/49/56` and `ny/6/50/56` tile boundary.
- `airports.csv`, `runways.csv`: OurAirports-shaped invented rows, including
  excluded airport types, an airport without runways and a closed runway.
- `config.json`: pinned SHA-256 input hashes and two adjacent cube tiles.
  Zlib is explicitly requested for dependency-free fixture tests; production
  packs use zstd. `example.test` provenance URLs are labels, not downloads.
- `LICENSE.txt`: CC0 dedication for the synthetic data.

Run from the repository root:

```sh
python3 tests/earth_pack_test.py
```

All generated packs live in temporary directories. Tests do not use the network,
change the game world, or download full-resolution source data.
