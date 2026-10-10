## What's new

### XR-40 cloak
- Engaging the cloak no longer drops the frame rate (on an RTX 3070 laptop it fell to 59 fps). To show the world through the cloaked part of the craft, the game traced the XR-40's whole shape for every pixel it covered; it now draws that part from the craft's own mesh, which tells it the same surface at a fraction of the cost. The cloak looks the same.

### Fixes
- An error log left by a launch that failed is cleared once the game starts normally again, and `diagnostics.bat` keeps any it finds apart (`error_before.log`), so the error log it collects is from its own runs. The v3.42.0 diagnostics still showed the NVIDIA shader compiler failure fixed in v3.36.x, from a log the game had never cleared.
