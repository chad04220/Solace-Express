## What's new

- **Much faster launches after the first.** Built aircraft bodies were saved to the cache but never loaded back: a
  size check on the saved file was off by one value, so every launch threw the file away and built all the bodies
  again (about 6.5 minutes for the full set on an RTX 3070 Laptop). They now load from the cache. The game's own
  start-up and every step of `diagnostics.bat` after the first two (which time a cold start on purpose) skip the
  rebuild.
- **Cockpit view faster.** Inside the cabin, the sun's shadow of the airframe on itself now comes from its shadow map
  everywhere. Before, the shadow edges and window frames were worked out ray by ray, which cost about 19 ms a frame
  on an RTX 3070 Laptop (the v3.25.0 analysis, "aircraft shadow"). On the test renderer the cockpit's aircraft pass
  drops 18% and the picture is unchanged apart from a few frame edges.
- **Traffic in the air casts its shadow again.** An aircraft's shadow on ground farther below it than its shadow map
  reaches was dropped (found by the v3.24.0 review). The ground under it now looks up the map like any other point.
