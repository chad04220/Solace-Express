## What's new
- Fixes the big slowdown in v3.30.0, worst in the cockpit (99 ms a frame in the light aircraft's, 82 ms in the Mantis's; the UFO scene's objects pass went from 0.9 to 31 ms). The per-pixel edge tracing added in v3.30.0 is far too heavy on real GPUs, so it is removed and aircraft drawing is back to v3.29.2's.
- Kept from v3.30.0, all cheap:
  - The yokes turn the right way.
  - Twin panels without the overlapping radio box.
  - Larger, sharper readouts.
  - The LED strip trimmed to the dash.
  - The cockpit's own fine sun shadow map: no speckle on the window posts, for about 0.3 ms.
- The cockpit's jagged edges are back for now. They will be fixed in the mesh build instead, at no cost while flying.
- The first launch rebuilds every aircraft's mesh once.
