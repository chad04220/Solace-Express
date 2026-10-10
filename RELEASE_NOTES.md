## What's new

### XR-40 cloak
- Engaging the cloak no longer drops the frame rate (on an RTX 3070 laptop it fell to 59 fps). To show the world through the cloaked part of the craft, the game traced the XR-40's whole shape for every pixel it covered; it now draws that part from the craft's own mesh, which tells it the same surface at a fraction of the cost. The cloak looks the same.

### Faster aircraft builds
- Building the aircraft (the loading screen's step after an update that changes them) takes less than half the time it did. On the test machine the XR-40's exterior went from 22 s to 9 s, its cockpit from 20 s to 3 s, and the Kestrel's cockpit from 13 s to 8 s. The bodies that come out are the same; only the work behind them changed:
  - The first search for where a body's surface can be now also passes over the space deep inside it. The XR-40's cabin is solid all round, and that search sent 90 million points to the finer one to find a few thousand cells.
  - The surface is found with each cell's own lookup tables instead of one large map of every point. That map was most of the time.
  - The moving parts (control surfaces, fans, gear, pods) are sampled only where their surface is, following it from block to block, instead of over their whole box. That is 19 million points instead of 85 million on the XR-40.
  - The simplifier that trims each body to its final triangles weighs its candidates on several processor cores at once.
- Bodies already built are kept: this update builds nothing again.
- `compile.log` now has a line for each body built, giving its time and how it splits between the GPU, the frames shown while it ran, and the processor. The next diagnostics will show where the time goes on your machine.

### Fixes
- An error log left by a launch that failed is cleared once the game starts normally again, and `diagnostics.bat` keeps any it finds apart (`error_before.log`), so the error log it collects is from its own runs. The v3.42.0 diagnostics still showed the NVIDIA shader compiler failure fixed in v3.36.x, from a log the game had never cleared.
