## What's new
- Parked and taxiing traffic now casts its sun shadow from a small shadow map of its own instead of a per-pixel trace, which was the shadow pass's largest cost at busy airports.
- The light aircraft's cockpit view runs many times faster. On an RTX 3070 the cockpit took 140 ms a frame (7 fps): the cabin's thin parts (sun visors, window frames, grab handles) were found per pixel by tracing the whole aircraft's shape up to 17 times. They are now built into the cabin's mesh at a fine resolution instead.
- Aircraft bodies are rebuilt after an update only when the aircraft's shape changed. Before, any shader change in an update (terrain, lighting, menus) threw away every body, so the first start after an update spent about six minutes rebuilding them.
