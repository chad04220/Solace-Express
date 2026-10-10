## What's new

### Living islands (the first step)
Codex's environment work, merged in:
- **Towns and cities.** All 18 settlements were rebuilt round streets that follow the road coming in: houses face the street, with gardens, courtyards and parked cars. The two cities have dense cores of towers and skyscrapers, and the villages are smaller and lower. At night, high-rise windows light floor by floor, in warm and cool tones, with some floors dark.
- **Buildings, vehicles, trees and rocks** remodelled in all four levels of detail: window openings, roof detail, fitted car glazing and lamps, connected tree branches, and close-up sprays of leaves and needles.
- **Airports** have service yards, structures, vehicles and clear gate access.
- **Photographic materials** for asphalt, concrete, brick, plaster, roof tiles and bark: 2K scans up close, with a 512 px set on Low quality.
- **Shallow water** shows the sand and rock beneath it, fading with depth; deep water costs less to draw.
- **Mount Kaleo** has a real summit crater, with glowing fissures, smoke drifting with the wind, embers and a lava glow at night.

### Grass
- The grass keeps the islands' green and gains real blade detail up close: a 2K scan, matched to the colour and finish the islands were tuned with. The merged set's first grass scan was mostly fallen leaves over soil, and it turned every meadow khaki.

### Good to know
- **The first launch after updating takes longer, once.** The islands are regenerated (the world format changed for the volcano's crater). Every aircraft body is rebuilt, which takes about two minutes on an RTX 3070 laptop. The new textures load as well.
- **Size:** the download is about 60 MB larger, and the new textures use about 180 MB more GPU memory.

### Next
The plan for the rest of the islands is in `docs/LIVING_ISLANDS_PLAN.md`:
- a real road network with highways, bridges and graded roads,
- farms, villages, towns and cities that connect to it,
- landmarks built by hand,
- traffic and lights,
- a pass over the whole picture.

Please run `diagnostics.bat` on this version. It's the first measurement of the new environment on a real GPU.
