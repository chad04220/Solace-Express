## What's new

### Fixes
- Another try at starting on NVIDIA graphics. v3.36.0 still stopped at "Graphics initialisation failed", this time naming the effects shader. NVIDIA's shader compiler fails internally ("C9999: Unhandled expr op ... in CreateDag") on two of the game's programs (the aircraft shadows and the effects), and the v3.36.0 change aimed at it was not the cause. Three things now:
  - Two constructs that only the new landing gear code used are written another way.
  - A program NVIDIA's compiler fails on is built again with that compiler's own options (other drivers ignore them), one set after another, until one builds. The shader cache remembers what failed and what built, so only the first launch takes longer.
  - If none of them builds, the game starts on a build of that program with less in it rather than not at all: the aircraft shadows without the traffic's marched shadows or the airframe's shadows in the landing lights; the effects without the XR-40 cloak's view through its cloaked part or the research jets' exhaust flames.
  - startup.log (beside your saves) lists any program the driver rejected, the driver's own message, and what was built instead. If anything is listed there, please send it: it says exactly which part of which program the compiler can't take.
- The start-up error dialog names the program that failed. v3.36.0's showed the shadow program's failure under the effects program's name.
