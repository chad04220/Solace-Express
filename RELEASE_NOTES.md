## What's new

### Fixes
- An error log left by a launch that failed is cleared once the game starts normally again, and `diagnostics.bat` keeps any it finds apart (`error_before.log`), so the error log it collects is from its own runs. The v3.42.0 diagnostics still showed the NVIDIA shader compiler failure fixed in v3.36.x, from a log the game had never cleared.
