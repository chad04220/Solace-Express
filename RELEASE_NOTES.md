## What's new

### Fixes
- Start-up on NVIDIA graphics, found this time: the nose gear's position was worked out by dividing a rotation matrix by a number, a form added with the folding gear in v3.35.0 that NVIDIA's shader compiler can't build ("C9999: Unhandled expr op assign/ in CreateDag"). It took the aircraft shadows, the effects and the aircraft mesh builder down with it, since all three contain the aircraft's shape. It is now worked out the way v3.34.0 did (the vector is divided instead), with the same result, and a test rejects a divided matrix from now on. The XR-10 and XR-20 changes were not the cause.
- If the driver's compiler still fails on one of those shaders, the game builds it once more without the retractable gear in the aircraft's shape and starts with that, rather than stopping. startup.log then says which shader it was. The compiler-option retries from v3.36.1 and v3.36.2, which hung on the aircraft mesh builder, are gone.
- compile.log, in the shadercache folder next to the game, still lists each shader as it is built, with its time and any error.

### Loading screen
- The bar shows how much of the launch is actually done: each shader program built or loaded, the islands, your career, the renderer, the menu and each aircraft's mesh count as one step each, and the percentage is the steps finished out of all of them. It no longer runs on a timer, so it stands still while one long step (a large shader, the first build of an aircraft's mesh) is under way, and reaches 100% only when everything is.
