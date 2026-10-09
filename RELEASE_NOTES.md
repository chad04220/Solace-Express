## What's new

### Fixes
- The first launch no longer sits on "Compiling the shaders: the aircraft mesh builder 17 of 19". v3.36.1 retried a shader NVIDIA's compiler fails on with each of that compiler's own option sets, and on the largest program, the aircraft mesh builder, those retries could each take minutes, or not finish at all. Now:
  - The retries stop after two and a half minutes in all, or after one that takes longer than a minute, and there are four of them instead of six.
  - A retry that an earlier launch started and never finished is not tried again.
  - The loading screen says when it is retrying ("the driver's compiler failed, retrying with its options, 2 of 4").
  - If the aircraft mesh builder can't be built at all, the game still starts. It then draws the aircraft the slower way, without their baked meshes.
- compile.log, in the shadercache folder next to the game, now gets a line as each shader starts and finishes building, with the time it took and the driver's error if it failed. If a launch ever hangs, its last line names the shader it was stuck on. If the first launch reports anything in startup.log, or takes unusually long, please send compile.log and startup.log.
