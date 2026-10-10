## What's new

### Performance
- Every cockpit draws faster. Each cockpit pixel started by copying the aircraft's shape parameters into memory before anything else, because one fuselage lookup picked its entry with a loop counter, which forces the GPU to keep the whole copy in slow per-thread memory. The lookup now picks its entry directly, so the parameters stay in fast storage. The XR-30 and XR-40 cockpits, which never draw traffic, read them straight from where the game supplies them. Nothing looks different.

### Diagnostics
- The analysis's cockpit-shading check now uses a separate build of the cockpit shader, made only when the analysis runs, so the game's own shaders carry nothing extra for it.
