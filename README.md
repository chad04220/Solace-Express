# Air Xpress

A pilot-career flight game for Windows with a real-time **GPU ray-traced** world. You start as a student with a permit, earn your licences, rent small planes to haul cargo, rent bigger ones to fly passengers, then buy your own aircraft for longer, harder contracts: mountain strips, glaciers, volcano fields, night storms and finally your own airline.

![Sunset over Port Verde](docs/sunset.jpg)

| | |
|---|---|
| ![Main menu](docs/menu.jpg) | ![Mountains](docs/mountain.jpg) |
| ![Storm run](docs/storm.jpg) | ![Business jet over Kaleo](docs/jet.jpg) |
| ![Bushmaster STOL](docs/bushmaster.jpg) | ![Islander cockpit](docs/cockpit.jpg) |
| ![Meridian Q400](docs/meridian.jpg) | ![Starling 500](docs/starling.jpg) |
| ![Port Verde city](docs/city.jpg) | ![Farmland and forest](docs/farmland.jpg) |
| ![Kaleo coast with palms and a sea stack](docs/kaleo_coast.jpg) | |

## Features

- **Ray-traced renderer.** Every pixel is traced on the GPU: an eroded heightfield over 80 × 80 km, an ocean with Fresnel reflections, volumetric clouds, soft terrain and aircraft shadows, an analytic sky with golden hour and night, and SDF aircraft with moving flaps, ailerons, elevator, rudder, gear and props. A conservative max-height mip pyramid lets terrain rays skip empty air in big jumps, and the forest-patch noise is baked into a texture shared by the GPU and the collision code, so dense foliage stays cheap without changing how it looks.
- **Detailed aircraft.** Each of the 7 aircraft is hand-built. Fuselages are shaped from 8 cross-sections, and wings and tails are tapered airfoil sections with sweep, dihedral and winglets. Flaps (with rearward Fowler travel), ailerons, elevators and rudders are separate hinged parts. You'll also see wing struts, STOL slats, engine cowls, exhaust stacks, turboprop and jet nacelles with fan faces, a cargo pod, wheel fairings, tundra tyres, twin-wheel retracting gear, a steerable nose wheel, antennas, nav lights and liveries with window frames.
- **3D cockpits.** You sit in a real cockpit with live gauges (airspeed, attitude, altimeter, turn coordinator, heading, VSI, RPM/N1, fuel) or glass PFD/ND screens on the jet and airliner. The yokes, rudder pedals and throttle levers move with your inputs, and sunlight falls through the window openings.
- **Correct control movement.** Every control surface, plus the nose or tail wheel, moves in the same direction as the input and the aircraft's actual response. A test (`tests/flight_test.cpp`) checks this for every aircraft.
- **Living world.** Light, open woodland of conifers, broadleaf trees and palms (picked by climate and altitude), boulders, mountain tors and sea stacks are part of the ray-traced heightfield. They're physically solid too: fly into a forest and you'll crash. Patchwork farmland has crop rows, wheat, ploughed soil, rapeseed, vineyards, wildflower meadows and hedgerows. There are 18 hand-placed settlements (two cities, three towns and thirteen villages and hamlets) with procedurally generated houses (gabled tile or slate roofs, plaster or brick walls, windows that light up at night) and city blocks and towers. A hand-drawn road network with lane markings links them, and town streets have pavements and street lights.
- **Airports.** Airports have arched corrugated hangars with sliding doors, control towers with glass cabs and beacons, glass-fronted terminals, fuel farms and radar domes. Runways get concrete slabs or grooved asphalt, painted runway numbers, blast-pad chevrons, aprons with parking-stand markings, and taxiway holding-position lines.
- **PBR materials.** 25 tileable PBR texture sets (albedo, roughness, normal, height, AO) are generated at startup. They cover terrain, buildings (concrete, clay tiles, slate, plaster, brick, corrugated metal), vegetation (leaves, needles, crops, wheat) and aircraft (paint, brushed metal, tyre rubber, cockpit plastic, seat fabric, leather, carpet). They're lit with GGX/Cook-Torrance shading, triplanar on cliffs, and darken and get glossier in the rain.
- **Crashes.** On impact the aircraft breaks into its nose, centre section, both wings and tail. Each piece is a separately simulated rigid body that tumbles, bounces and comes to rest, blackened and burning with glowing embers at the breaks. Skin fragments scatter around the site. The impact blows a crater into the ray-traced terrain with a scorched blast ring, smouldering embers and the trees around it flattened, and the camera orbits the wreck. Overstress an airframe in flight and it rips apart in the air: the pieces keep their momentum, tumble out of the sky trailing fire and black smoke, and each one smashes into the ground (or the sea) where it falls, with the centre section digging the crater. Crashing into water throws spray; the pieces float for a moment, then sink to the seabed trailing bubbles, and the skin fragments flutter down after them.
- **Effects.** Tyre smoke, dust and snow spray from unpaved strips, prop-wash dust, wingtip vapour, engine-start smoke puffs, crash fire and smoke, rain streaks, snowfall, raindrops on the lens, lightning, bloom and lens flare. Lighting includes runway edge, threshold and approach lights, working **PAPI** lights, plus nav, strobe, beacon and landing lights that light the terrain.
- **Engine sound.** All audio is synthesised in real time. Piston engines are modelled one cylinder at a time: each firing excites exhaust resonators, and slight differences between cylinders give the uneven idle and crank-rate rumble. On top of that is a prop blade-pass buzz and whoosh. Twins run two engines slightly out of sync so they beat against each other. Turboprops and jets get their own whine, roar and buzz-saw models. The cockpit view filters the sound, and you'll also hear a starter motor, wind, runway seams, tyre chirps, stall horn or stick shaker, gear and flap motors, rain and thunder.
- **GPS moving map (N).** An animated, north-up moving map centred on your aircraft. It shows the route with flowing dashes, checkpoint markers, runways drawn to scale, your breadcrumb trail, predicted position at 1, 2 and 5 minutes, a fuel-range ring and a radar sweep. A side panel lists next waypoint, distance, bearing, ETE, ETA, route remaining, ground speed and track, fuel range against the remaining route, destination runway and wind, and the nearest airport.
- **Checkpoint gates.** Animated holographic gates with counter-rotating segmented rims, sweeping highlights, inward pulses and sparks drifting around the rim. A trail of moving lights leads to the next gate, and flying through one sets off a shockwave and a shower of sparks.
- **Clean glass UI.** Menus and HUD panels use a consistent glass style with cyan accents and corner brackets. Buttons, cards, tabs and sliders respond to the mouse with animated glow, sheen and press feedback.
- **Muffle button (M).** A headset-style noise filter that turns down engine, wind and rolling noise for relaxed cruising.
- **Live internet radio (R).** Streams MP3/AAC stations through Windows Media Foundation. You can add your own stations.
- **Flight model.** Six degrees of freedom with stall and wing drop, flaps, ground effect, prop wash, density altitude, wind shear near the ground, gusts and turbulence, spring-damper gear with steering and brakes, and taildragger handling. Includes an autopilot (heading and altitude hold) and time acceleration in cruise.
- **Hand-designed world.** "The Solace Islands" has 16 airports on 5 islands: a flight-school field, grass farm strips, a rock in the sea, international hubs, a 5,400 ft gravel mountain pass, a volcano research strip, a glacier snow runway, a fjord town and a remote resort isle. Every approach has a cleared glide corridor.
- **Career.** There are 4 lessons and 27 story contracts in 5 chapters (Student → PPL → CPL → Owner-operator → ATP), plus endless freelance jobs. Work includes cargo, passengers, fragile loads, medevac with deadlines, VIP charters and a scenic tour. You're scored on landing softness, passenger comfort (bank and G), deadlines and fragile-cargo handling. You can rent at any airport, buy and sell 7 aircraft, and pay for ferry flights and positioning tickets.
- **No softlocks.** The campaign is checked automatically (`tests/progression_test.cpp`). Every contract is flown with an aircraft you can rent or afford, fuel range (with reserve), runway length (corrected for elevation), surface type and deadlines all fit, and the money curve needs only light freelancing or selling an old plane. Rentals never require cash up front, and if you're broke you get free courtesy rides.

## Aircraft

| Aircraft | Type | Seats / Cargo | Range | Runway | Licence |
|---|---|---|---|---|---|
| Kestrel T2 | 2-seat trainer (4-cyl) | 1 / 120 kg | 45 km | 400 m | Student |
| Wren 180 | 4-seat tourer (6-cyl) | 3 / 320 kg | 70 km | 450 m | PPL |
| Bushmaster STOL | Taildragger bush plane | 4 / 480 kg | 60 km | 220 m, gravel/snow | CPL |
| Islander Twin | Twin-piston utility | 9 / 900 kg | 90 km | 420 m, gravel/snow | CPL |
| Pelican Caravan | Single turboprop | 12 / 1,400 kg | 130 km | 550 m, gravel/snow | CPL |
| Meridian Q400 | Twin-turboprop airliner | 40 / 4,500 kg | 170 km | 1,100 m paved | ATP |
| Starling 500 | Twin-jet business jet | 7 / 700 kg | 260 km | 1,250 m paved | ATP |

## Controls

| Key | Action | Gamepad |
|---|---|---|
| W / S or ↑ / ↓ | Pitch down / up | Left stick |
| A / D or ← / → | Roll | Left stick |
| Q / E | Rudder / nosewheel | LB / RB |
| Shift / Ctrl, PgUp / PgDn, 1–9, 0 | Throttle | RT / LT |
| F / V | Flaps down / up | B / X |
| G | Landing gear (retractable aircraft) | Y or D-pad → |
| B | Parking brake (set / release) | D-pad ← |
| Space | Wheel brakes | A |
| [ / ] | Elevator trim | D-pad ↑ / ↓ |
| Z | Autopilot (A/D steers heading) | Right-stick click |
| T | Time acceleration ×1 / ×2 / ×4 (cruise only) | |
| C | Camera: chase, cockpit, orbit, flyby | Back |
| Right mouse drag, wheel | Look around, zoom | Right stick |
| L | Landing lights | |
| I | Engine restart | |
| **M** | **Muffle engine noise** | Left-stick click |
| Tab | Toggle minimap (hidden by default) | |
| **R** | **Internet radio** | |
| **N** | **GPS moving map** (mouse wheel or the on-map buttons zoom) | |
| H | Toggle HUD | |
| Esc | Pause menu (restart, settings, abandon) | Start |
| F11 / Alt+Enter | Fullscreen | |

Menus work with the mouse or a gamepad. On a gamepad, the left stick (or D-pad) moves an on-screen cursor, **A** clicks, **B** backs out (like Esc), and the right stick scrolls lists. Moving the real mouse hands control straight back to it.

<details>
<summary>Classified (spoiler)</summary>

Hold **U** and **I** together on the main menu, or click in **both sticks** on a gamepad, to open the Confidential Research Model menu. From there you can fly the **XR-9 Specter**, a VTOL research jet, for free from any airport, starting airborne or on the runway, in clear, cloudy or stormy weather.

- **Performance:** full reheat doubles the thrust, for a thrust-to-weight ratio of about 4.4 and Mach 2.5 at 3,000 m. Breaking Mach 1 throws a vapour cone and a sonic boom.
- **Thrust vectoring:** F and V (gamepad **B** / **X**) swivel the 2D nozzles from 0 to 90 degrees. Full down hovers at about 65% throttle, and the jet levels itself when you let go of the stick. It stays level when you pedal-turn on the rudder in the hover, even while still drifting. Its lift fans and nozzle louvres move with the vectoring, and downwash kicks up dust or spray.
- **Handling:** the nozzles also vector ±29° for pitch. There are no g or angle-of-attack limiters: the stick commands rotation directly, around 230°/s of pitch at Mach 1 and 315°/s of roll. The airframe fails beyond +50 / −25 g, and a full pull much above Mach 1.5 will tear it apart.
- **Exhaust:** ray-marched plumes run blue-white plasma in dry thrust and turn into long amber reheat flames with shock diamonds. They shed sparks and embers, glow at the nozzles, light up the tail and the ground below, and flare with a shock ring when the reheat lights.
- **Cockpit:** a sealed pod with no windows. You fly on a panoramic synthetic-vision display with a full HUD (pitch ladder, flight-path marker, heading tape, speed, altitude, Mach, G, nozzle angle and throttle), with annunciator lights above it. A curved instrument console holds five displays: engine gauges, power bars, a live heading-up terrain map, an attitude indicator and the VTOL/nozzle status. Side consoles carry a G-meter and angle-of-attack page, a compass page and backlit keypads, with an overhead switch panel above. Side camera displays show the left and right views. The pod is lit only by its screens, LED strips and amber footwell lights.
- **Sound:** a layered voice with a warm harmonic core whose pitch rises with power, fan whine, deep sub-bass, an exhaust roar that opens up with thrust, a crackling afterburner, the thrum of the lift fans in the hover and a deep, low double-thump sonic boom with a long rolling rumble.
- **Career:** these flights don't count towards the career or the logbook.

</details>

## Flying tips

- Fly the green rings in the lessons. The magenta HUD arrow always points at your next objective.
- On approach the HUD shows a glidepath (G/S) and localiser guide. The PAPI lights by the runway show **two white and two red** when you're on a 3° glidepath.
- A touchdown under 150 fpm earns a "butter" bonus. Over 600 fpm costs you, and over about 900 fpm collapses the gear.
- High and hot airfields need more runway. Summit Pass (5,400 ft) only works in a STOL aircraft.
- Check the range shown on the engine panel before long crossings.

## Internet radio

Press **R** in flight, or use the **Radio** button in the hub. Stations come from `%APPDATA%\AirXpress\radio_stations.txt`, which is created on first run. Add one line per station:

```
My Station|https://example.com/stream.mp3
```

Any MP3 or AAC stream over HTTP or HTTPS that Windows Media Foundation can play will work.

The presets include SomaFM, Radio Paradise and KEXP, plus free US East Coast public and college stations: WNYC and WQXR (New York), WFMU (Jersey City), WBGO (Newark), WFUV (New York), WXPN (Philadelphia), WBUR, GBH and CRB (Boston), WMBR (MIT), WUNC (Chapel Hill) and WCPE (Raleigh). When an update adds presets, they're appended to your existing station file, and your own stations are kept. The list scrolls with the mouse wheel.

## System requirements

- Windows 10 or 11, 64-bit
- A GPU with OpenGL 3.3 (anything from roughly 2012 on). A mid-range GPU from the last 5–6 years is recommended for 1080p.
- **Dynamic resolution** lowers the internal ray-tracing resolution automatically to stay above about 40 fps. You can also set the resolution scale and Low/Medium/High quality in Settings.

Save data, settings and the radio list live in `%APPDATA%\AirXpress`.

## Building

### Visual Studio 2022 or newer (MSVC)

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\Release\AirXpress.exe
```

### MinGW-w64 (on Windows, or cross-compiling from Linux)

```
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake-mingw.cmake   # omit the toolchain file on Windows
cmake --build build-win -j
```

The GitHub Actions workflow (`.github/workflows/build.yml`) builds with MSVC, runs the tests, and uploads `AirXpress-windows-x64` as an artifact on every push. Pushing a `v*` tag attaches a zip to a GitHub Release.

There are no third-party dependencies to install. The game only uses Win32, OpenGL, WinMM, XInput (loaded at runtime) and Media Foundation.

### Tests

```
ctest --test-dir build -C Release      # flight model + campaign progression
```

`tests/gameplay_test.cpp` (Linux dev build) flies Lesson 1 and a full approach and landing through the real game loop. `tests/render_harness.cpp` renders test frames headlessly with Mesa so you can check the shaders.

## Project layout

```
src/world.*        hand-designed archipelago, terrain function shared by CPU and GPU, airports
src/aircraft.*     7 aircraft specs + 6-DOF flight model, gear contacts, autopilot
src/career.*       licences, story campaign, freelance generator, economy, save/load
src/shaders.h      GLSL: ray tracer (terrain/water/clouds/aircraft SDF/buildings), sprites, post, UI
src/renderer.*     GL pipeline, procedural PBR texture synthesis, bloom/tonemap, SDF text UI
src/audio.*        procedural audio engine (engines, environment, effects, UI)
src/game*.cpp      game flow, flight session, cameras, particles, lights, HUD and menus
src/platform_win32.cpp, src/radio_win.cpp   Win32 window/input/audio output, Media Foundation radio
tools/gen_font.py  regenerates the embedded SDF font atlas
```
