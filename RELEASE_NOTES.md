## What's new
- Night and storm flights: the aircraft shadow pass now takes the fast path at night too. The aircraft's beacon now gets a small downward shadow map. Before, it was the one light without a map and it forced the slow path on every pixel. On the test renderer the night shadow pass went from 24 to 8 ms, with the picture unchanged.
- The UFO encounter: when only the UFO (or debris) needs tracing, the objects pass uses a build with no aircraft code in it. Your v3.29.1 diagnostic showed the UFO pass at 4.9 ms against 2.0 ms in v3.27.
