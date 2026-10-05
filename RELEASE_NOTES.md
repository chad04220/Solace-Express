## What's new

**Landings**
- The autopilot adds the usual gust allowance to its approach speed (half the gust factor, more in turbulence), so gusty landings have the energy to flare through a gust
- Fast jets no longer float metres above the runway and then drop in: the flight-path integrator is held through the flare (in an XR-9 it wound up to 0.4 g while rounding out and held the jet level 10 m up), and the flare looks a pitch-response time ahead, so slow-answering aircraft arrive at the sink rate they were asked for
- The glidepath aims short of the touchdown point by the distance the flare floats, so the wheels meet the runway near the markers instead of hundreds of metres past them
- A long float firms up the longer it lasts, and turbulence eases off in the last wingspan above the ground (the air can't move through the surface)
- The XR-11 starts slowing to its hover where it can stop from its actual ground speed, tailwind included (in a strong tailwind it used to overshoot the pad by more than a kilometre)

**Shadows**
- Distant mountains shade the ground at sunrise and sunset again: the baked terrain shadow looked only 8 km toward the sun, so ranges 11-23 km away (seen from Capital, Cedar Ridge and Port Virel) cast nothing. It now looks the full 30 km, skipping terrain that can't rise high enough, and bakes slightly faster than before
- Tall buildings cast their shadows from further away: the shadow maps picked whole blocks of scenery with a fixed margin, which could leave out a skyscraper whose long low-sun shadow reached the area
- Aircraft now sit in the shadows of hangars, buildings and trees like the ground around them (they only received terrain and cloud shadows before)
- Fence shadows no longer crawl: the shadow maps used the camera-dependent, frame-dithered see-through pattern of the chain link; they now use a fixed pattern of the same density
- Scenery shadow edges are filtered by the GPU's comparison sampler (one lookup instead of four, same result)

**Performance**
- The aircraft and air traffic are traced only as far as the nearest tree, building or ground already found on that pixel
