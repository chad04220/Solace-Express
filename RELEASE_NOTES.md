## What's new

**Loading screens**
- New pre-rendered loading pictures for all 16 airports, and an in-flight picture for every aircraft (the Kestrel, Wren, Bushmaster, Islander and Pelican now have their own too)
- `render_menu.bat` renders the main menu's montage on your GPU into `menu.mp4` (the whole 128 s loop, 1920x1080, 30 fps, every frame with its scenery complete). With that video next to the exe the main menu plays it instead of ray tracing the montage live, so the menu runs at full frame rate; without it the menu works as before

**Fixes**
- The climb-out from a field you'll land back at (the traffic-pattern lesson) no longer counts as a go-around in the debrief
- The research terminal's sortie bar scales to narrow windows (at 1024x768 the INITIATE button overlapped the weather buttons)
