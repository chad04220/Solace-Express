# The cube contract

`cube.py` is what remains of the Phase 0 Earth tooling (October 2026): the tangent-adjusted cube map whose face order,
tile addressing and exact-edge tie rules `src/geo.h` reproduces for the generated regions' addresses. `tests/geo_cube_parity_test.py`
(the `earth_cube_parity` test) checks the C++ against it.

The Earth packs the game reads are built by `tools/earth2` (`docs/REAL_EARTH_PLAN.md`); the Phase 0 builders, sampler,
verifier and USGS handoff were removed in v4.4.2, with their tests, workflow and studies.
