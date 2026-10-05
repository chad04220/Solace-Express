## What's new

**Career**
- Runway requirements, cruise speeds and deadlines now come from how each aircraft actually flies: its take-off roll, landing distance and level cruise speed are measured in the flight model (see the table in the README). Most types need about what they did; the Bushmaster needs 257 m on its steep STOL approach and the Starling 621 m

**Controls**
- A key or button held when the game changes what it's listening to (starting a flight, leaving the pause menu, closing a menu) does nothing until it's released: no A held through the loading screen braking, no Resume press firing the XR-11's lasers. The GPS map keeps its own buttons while open but you keep flying the aircraft
- Losing the controller in flight pauses the game; reconnecting it doesn't resume on its own


**Performance**
- The game is no longer locked to 60 fps. Settings → *Frame rate*: *Display* (default) follows your monitor's refresh rate (144 Hz monitor → up to 144 fps), or pick a cap of 30 / 60 / 90 / 120 / 144 / 240. The automatic resolution scaler targets whatever rate is in effect
- Resolution changes from the automatic scaler no longer cause a brief hitch (render buffers are kept at full size and the scaler just uses part of them)
- Temporal anti-aliasing blends by elapsed time, so the image looks the same at any frame rate
- F3 shows the worst frame of the last second and the target frame rate

**Renderer (preview)**
- Settings → *Renderer* can switch to the new rasterizer. It is being built in stages (docs/RENDERER_REBUILD.md): this release draws the terrain, the sea, the sky and the scenery with it and lights the frame in one pass; the aircraft, cockpits, traffic and effects are not drawn yet, so it is for comparing the world and its frame times (F3, `analyze.bat --raster`, `benchmark.bat --raster`). The ray tracer stays the default and is unchanged
