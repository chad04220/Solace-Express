# Earth pack v2 test fixture

Real data over Martha's Vineyard and Nantucket, 41-42° N and 72-69° W, in the format `tools/earth2` builds and
`src/earth_data.cpp` reads. `tools/earth2/fixture.py` made it:
- the elevation (ETOPO 2022 30″) and land cover (CGLS-LC100) tiles;
- two GLO-90 tiles: N41W071 cropped to 41.20-41.68° N, and N41W070 whole;
- the airports, the places and the main roads of the three tiles;
- the local roads of Martha's Vineyard alone.

It also holds:
- `expect.txt`: what the Python reference reads from the fixture, which `tests/earth_data_test.cpp` compares the C++
  reader against;
- `inflate.bin`: zlib test vectors.

The sources and their licences are in `licenses/ATTRIBUTION.txt`: ETOPO 2022 (CC0), Copernicus DEM GLO-90
(Copernicus DEM licence, attribution), CGLS-LC100 (CC BY 4.0), OurAirports (public domain), GeoNames (CC BY 4.0) and
GRIP4 (CC BY 4.0, working assumption).
