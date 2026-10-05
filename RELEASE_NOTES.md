## What's new

**Renderer (preview)**
- The rasterizer (Settings → *Renderer* → *Rasterized (preview)*) now draws everything the ray tracer draws: propeller discs, the research jets' plumes and vapour cone, the weapons, the hologram and the XR-11's cloak run as an effects pass over the lit frame. `benchmark.bat --raster` and `analyze.bat --raster` measured the ray tracer by mistake in v3.12.0; fixed
