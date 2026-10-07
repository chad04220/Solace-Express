## What's new
- More of the work now happens at launch, and the results are cached, so play from the menu onward isn't held up building things:
  - The research jets' meshes (XR-10 to XR-40, outside and cockpit) are now built at launch with the other aircraft. Opening the research screen or flying one no longer builds anything.
  - The islands (the terrain, roads, land use and terrain bounds) are generated once and saved next to the shader cache (about 130 MB). Later launches read them in a fraction of a second instead of generating them again.
  - Each aircraft's learned flight performance is saved too, so later launches don't fly the test sorties again.
  - Everything cached is kept per game build: a new version rebuilds it once.
- The loading bar moves at a steady pace. Each launch step's real duration is measured and remembered, and the next launch weights the bar by those times. Building from scratch and loading from the cache are remembered separately, so the first launch of a new version is paced right too.
- The loading text says exactly what is happening, for example "Loading the shaders from the cache: aircraft shadows (light aircraft build) 9 of 37", "Generating the islands (once)", "Building the Pelican cockpit's mesh (once: kept for the next launch) 14 of 26", or "Loading the menu's scenery and its terrain shadow".
- startup.log records how long each launch step took.
