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
- **3D cockpits.** You sit in a detailed cockpit with live gauges (airspeed, attitude, altimeter, turn coordinator, heading, VSI, RPM/N1, fuel) in raised bezels, or glass PFD/ND screens and a centre engine display on the jet and airliner. There's a lit radio stack, eyeball vents, a centre pedestal with trim wheel and fuel selector, throttle, mixture and flap levers, metal yokes with rubber grips, toe-brake pedals, bolstered seats with harnesses on seat rails, trim panels with armrests and door handles, and an overhead console. Everything moves with your inputs and uses PBR materials (brushed metal, rubber, leather or cloth, carpet, fabric headliner). Sunlight falls through the windows; at night a glareshield LED strip floods the panel and a dim amber dome light glows overhead, all from modelled light fixtures. In the cockpit view the outside world is only traced through the windows.
- **Correct control movement.** Every control surface, plus the nose or tail wheel, moves in the same direction as the input and the aircraft's actual response. A test (`tests/flight_test.cpp`) checks this for every aircraft.
- **Living world.** Light, open woodland of conifers, broadleaf trees and palms (picked by climate and altitude), boulders, mountain tors and sea stacks are part of the ray-traced heightfield. They're physically solid too: fly into a forest and you'll crash. Patchwork farmland has crop rows, wheat, ploughed soil, rapeseed, vineyards, wildflower meadows and hedgerows. There are 18 hand-placed settlements (two cities, three towns and thirteen villages and hamlets) with procedurally generated houses (gabled tile or slate roofs, plaster or brick walls, windows that light up at night) and city blocks and towers. A hand-drawn road network with lane markings links them, and town streets have pavements and street lights.
- **Airports.** Airports have arched corrugated hangars with sliding doors, control towers with glass cabs and beacons, glass-fronted terminals, fuel farms and radar domes. Runways get concrete slabs or grooved asphalt, painted runway numbers, blast-pad chevrons, aprons with parking-stand markings, and taxiway holding-position lines.
- **PBR materials.** 25 tileable PBR texture sets (albedo, roughness, normal, height, AO) are generated at startup. They cover terrain, buildings (concrete, clay tiles, slate, plaster, brick, corrugated metal), vegetation (leaves, needles, crops, wheat) and aircraft (paint, brushed metal, tyre rubber, cockpit plastic, seat fabric, leather, carpet). They're lit with GGX/Cook-Torrance shading, triplanar on cliffs, and darken and get glossier in the rain.
- **Air traffic.** The sky is busy. At the airports around you, aircraft park on the apron stands, taxi along the yellow lines with spacing, hold short, take off, fly a full circuit (crosswind, downwind, base, final), land on a glideslope, roll out, taxi back to their stand and go again. Arrivals join the downwind leg and are sequenced behind each other, some departures leave the area, and everyone holds or goes around if you're on the runway. Cruising traffic crosses the map at altitude. Every couple of minutes a formation of two or three XR-9s rips past at 400 m/s in reheat, sonic booms and all, before pulling up into a climbing roll. A two-ship Kestrel display team flies loops, rolls, barrel rolls, Immelmanns, split-S, Cuban eights, four-point rolls and hammerheads with white and red smoke over the nearest airport. All traffic is ray traced with the full aircraft models, casts soft shadows, shows nav lights, beacons, strobes and landing lights at night, and can be switched off in Settings (*Air traffic*). Watch out: a mid-air is fatal.
- **Close encounters.** Every so often, when you're well clear of the ground, something swoops in from behind and settles alongside. Watch the hatch.
- **Crashes.** On impact the aircraft breaks into its nose, centre section, both wings and tail. Each piece is a separately simulated rigid body that tumbles, bounces and comes to rest, blackened and burning with glowing embers at the breaks. Skin fragments scatter around the site. The impact blows a crater into the ray-traced terrain with a scorched blast ring, smouldering embers and the trees around it flattened, and the camera orbits the wreck. Overstress an airframe in flight and it rips apart in the air: the pieces keep their momentum, tumble out of the sky trailing fire and black smoke, and each one smashes into the ground (or the sea) where it falls, with the centre section digging the crater. Crashing into water throws spray; the pieces float for a moment, then sink to the seabed trailing bubbles, and the skin fragments flutter down after them.
- **Effects.** Tyre smoke, dust and snow spray from unpaved strips, prop-wash dust, wingtip vapour, engine-start smoke puffs, crash fire and smoke, rain streaks, snowfall, raindrops on the lens, lightning, bloom and lens flare. Lighting includes runway edge, threshold and approach lights, working **PAPI** lights, plus nav, strobe, beacon and landing lights that light the terrain.
- **Engine sound.** All audio is synthesised in real time. Piston engines are modelled one cylinder at a time: each firing excites exhaust resonators, and slight differences between cylinders give the uneven idle and crank-rate rumble. On top of that is a prop blade-pass buzz and whoosh. Twins run two engines slightly out of sync so they beat against each other. Turboprops and jets get their own whine, roar and buzz-saw models. The cockpit view filters the sound, and you'll also hear a starter motor, wind, runway seams, tyre chirps, stall horn or stick shaker, gear and flap motors, rain and thunder.
- **GPS moving map (N).** An animated, north-up moving map centred on your aircraft. It shows the route with flowing dashes, checkpoint markers, runways drawn to scale, your breadcrumb trail, predicted position at 1, 2 and 5 minutes, a fuel-range ring and a radar sweep. A side panel lists next waypoint, distance, bearing, ETE, ETA, route remaining, ground speed and track, fuel range against the remaining route, destination runway and wind, and the nearest airport.
- **Checkpoint gates.** Animated holographic gates with counter-rotating segmented rims, sweeping highlights, inward pulses and sparks drifting around the rim. A trail of moving lights leads to the next gate, and flying through one sets off a shockwave and a shower of sparks.
- **Clean glass UI.** Menus and HUD panels use a consistent glass style with cyan accents and corner brackets. Buttons, cards, tabs and sliders respond to the mouse with animated glow, sheen and press feedback.
- **Muffle button (M).** A headset-style noise filter that turns down engine, wind and rolling noise for relaxed cruising.
- **Live internet radio (R).** Streams MP3/AAC stations through Windows Media Foundation. You can add your own stations.
- **Flight model.** Six degrees of freedom with stall and wing drop, flaps, ground effect, prop wash, density altitude, wind shear near the ground, gusts and turbulence, spring-damper gear with steering and brakes, and taildragger handling. Includes time acceleration in cruise and a full autopilot (see below).
- **Autopilot and autoland.** Press **Z** for heading, altitude and speed hold: A/D turns the heading bug, W/S moves the altitude target and the throttle keys set the speed (a number key hands the throttle back to you). Pick an airport on the GPS map (click it, or Tab / D-pad, then **ENGAGE AUTOLAND** or Enter) and the autopilot flies you there and lands. It picks the runway end from the wind and the terrain, cruises above the high ground, descends in an orbit over the lowest ground near the approach, intercepts the localizer and a glidepath that steepens where hills demand it, sets flaps and gear, flares, brakes to a stop and sets the parking brake. If it isn't stable on final it goes around and tries again. The XR-9 comes to a hover over the runway and lands vertically. The GPS draws the planned approach, the HUD shows an autopilot banner, and time acceleration stays available while it's en route. Any stick input hands control back.
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
| Z | Autopilot: hold, or autoland at the airport picked on the GPS | Right-stick click |
| T | Time acceleration ×1 / ×2 / ×4 (cruise only) | |
| C | Camera: chase, cockpit, orbit, flyby | Back |
| Right mouse drag, wheel | Look around, zoom | Right stick |
| L | Landing lights | |
| I | Engine restart | |
| **M** | **Muffle engine noise** | Left-stick click |
| Tab | Toggle minimap (hidden by default); with the GPS open, pick the autoland airport | D-pad ← / → on the GPS |
| **R** | **Internet radio** | |
| **N** | **GPS moving map** (mouse wheel or the on-map buttons zoom) | |
| H | Toggle HUD | |
| Esc | Pause menu (restart, settings, abandon) | Start |
| F11 / Alt+Enter | Fullscreen | |
| F3 | Performance readout (fps, GPU time, resolution) | |
| Enter / Space (after a crash) | Skip the crash sequence | A |

Pull g and the edges of the screen take on a faint red tint that slowly closes in, deepening to dark red and then black as the load approaches what the airframe can take (from about 1.8 g in regular aircraft; negative g reddens them too). The XR-9's damped cell starts at 4 g and only reaches the full ring near 50 g.

Menus work with the mouse or a gamepad. On a gamepad, the left stick (or D-pad) moves an on-screen cursor, **A** clicks, **B** backs out (like Esc), and the right stick scrolls lists. Moving the real mouse hands control straight back to it.

<details>
<summary>Classified (spoiler)</summary>

Can't wait for a close encounter? Hold **J** and **K** together for a second while flying and they'll come to you.

Bored on a long leg? Hold **O** and **P** together for a second while flying and the **Spectre display pair** joins you: two XR-9s in red-and-gold and blue-and-white livery, trailing coloured smoke. They fly a show around you all the way to your destination: synchronised rolls in formation, a double helix around your flight path, a head-on knife-edge crossover with a fly-by roar, synchronised loops off your nose, and a split-and-cross with one high and one low. The radio calls each act. Hold O + P again to send them home; they also rock their wings and pull away in reheat when you settle onto an approach.

Hold **U** and **I** together on the main menu, or click in **both sticks** on a gamepad, to open the Confidential Research Model menu. From there you can fly the **XR-9 Specter**, a VTOL research jet, for free from any airport, starting airborne or on the runway, in clear, cloudy or stormy weather.

- **Performance:** full reheat doubles the thrust, for a thrust-to-weight ratio of about 4.4 and Mach 2.5 at 3,000 m. Breaking Mach 1 throws a vapour cone and a sonic boom.
- **Thrust vectoring:** F and V (gamepad **B** / **X**) swivel the 2D nozzles from 0 to 90 degrees. Full down hovers at about 65% throttle, and the jet levels itself when you let go of the stick. It stays level when you pedal-turn on the rudder in the hover, even while still drifting. Its lift fans and nozzle louvres move with the vectoring, and downwash kicks up dust or spray.
- **Handling:** the nozzles also vector ±29° for pitch. There are no g or angle-of-attack limiters: the stick commands rotation directly, around 230°/s of pitch at Mach 1 and 315°/s of roll. The airframe fails beyond +50 / −25 g, and a full pull much above Mach 1.5 will tear it apart.
- **Exhaust:** ray-marched plumes run blue-white plasma in dry thrust and turn into long amber reheat flames with shock diamonds. They shed sparks and embers, glow at the nozzles, light up the tail and the ground below, and flare with a shock ring when the reheat lights.
- **Cockpit:** a sealed pod with no windows. You fly on a panoramic synthetic-vision display with a full HUD (pitch ladder, flight-path marker, heading tape, speed, altitude, Mach, G, nozzle angle and throttle), with annunciator lights above it. A curved instrument console holds five displays: engine gauges, power bars, a live heading-up terrain map, an attitude indicator and the VTOL/nozzle status. Side consoles carry a G-meter and angle-of-attack page, a compass page and backlit keypads, with an overhead switch panel above. Side camera displays show the left and right views. The pod is kept low-lit, only by its own modelled fixtures: the displays, two warm recessed ceiling light bars, cyan LED strips and an amber footwell light. Its carbon, composite, leather, rubber and webbing surfaces use PBR materials.
- **Sound:** a layered voice with a warm harmonic core whose pitch rises with power, fan whine, deep sub-bass, an exhaust roar that opens up with thrust, a crackling afterburner, the thrum of the lift fans in the hover and a deep, low double-thump sonic boom with a long rolling rumble.
- **Career:** these flights don't count towards the career or the logbook.

The same menu has a second airframe (click **XR-11 WRAITH** or press Tab): a stealth aerobatic research craft built for insane speed and g.

- **Airframe:** an angular, faceted stealth body with sharp chines, caret intakes, a faceted gold canopy, a diamond wing with a forward-swept trailing edge and elevons, and canted all-moving ruddervators. The skin is a PBR radar-absorbent coating with sawtooth panel seams; the cockpit is the same sealed pod as the XR-9's.
- **Four thruster pods:** two on the forward chines and two at the wing roots. Each pod is modelled in detail: a faceted nacelle with a sawtooth intake lip, a spinning 14-blade intake fan, a glowing turbine core, an iris nozzle whose petals open with thrust, three pitch vanes and two yaw vanes in the jet, the tilt trunnion, and a hydraulic actuator whose rod extends as the pod tilts. **F / V** tilts all four pods from thrust-aft (forward flight) to straight down (hover). Each pod's thrust is applied where the pod actually is, so the hover is held up by four real jets.
- **Flight control:** the fly-by-wire turns stick input into the rotation it wants, then works out how to make it. It uses the elevons and ruddervators first (their authority grows with airspeed), then the pods: thrust front against rear and left against right, the pitch and yaw vanes, and tilting the left and right pods differently. Whatever it can't make simply doesn't happen, so slow and low on power the controls go soft, just as they would on a real tilt-jet.
- **Performance:** about 2.2:1 thrust-to-weight dry and 4.6:1 with boost (above 85% throttle), Mach 3.6 at 4 km, a 400°/s roll, and about 80 g at full stick at high speed. The airframe holds +90 / -45 g.
- **Cloak (X, or D-pad ← in the air):** the cloaking field spreads from the nose to the tail in a violet wavefront. Once it has, the craft is almost invisible: you see the terrain and sky through it, slightly distorted, with only a faint glassy rim and a shimmer of the emitter lattice. Its shadow fades and its lights go dark.
- **Weapons hot (Y, keyboard or gamepad):** the laser turrets drop out of hatches in the forward chines and the bomb bay doors open. Press Y again for weapons safe: the bay closes and the lasers stow. While weapons are hot on a gamepad, **RB fires the lasers** and **LB drops a bomb**, and the rudder is frozen until you go safe again. On the keyboard, hold left mouse or Enter to fire (this goes weapons hot by itself) and press Backspace or middle mouse to bomb.
- **Lasers:** alternating crimson pulse bolts that converge 650 m ahead, out to 4.5 km, sparking off the ground or the sea and bringing down any aircraft they hit.
- **Plasma bombs:** unlimited. The belly bay's clamshell doors open, the dark-energy bomb drops from its cradle and a new one condenses in its place. Each bomb is a black sphere crawling with violet plasma. On impact it goes off in a white-violet flash: a turbulent violet fireball around a collapsing black core, arcing filaments, a plasma pillar, a shock ring racing across the ground and a deep imploding boom. It leaves a crater of fused black glass with violet embers in the cracks, and nothing nearby survives it.

</details>

## Flying tips

- Fly the green rings in the lessons. The navigation card at the top shows distance, bearing, how far to climb or descend and time en route; the heading tape below it has a magenta caret on your target's bearing with a turn cue. The magenta diamond sits on the objective itself in 3D, with a dashed stalk down to the ground so you can read its height, and turns into an arrow at the screen edge when the target is out of view.
- The wind dial (left) shows the wind at your aircraft relative to your nose, its direction, speed and gusts, and the headwind / crosswind components (amber when the crosswind is strong).
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
- **60 fps.** The game runs locked to 60 Hz: vsync on 60/120/240 Hz displays (adaptive where the driver supports it, so a late frame tears once instead of dropping to 30), and a precise frame limiter on other refresh rates.
- **Dynamic resolution** measures the GPU time of every frame and scales the internal ray-tracing resolution (50% up to the *Max render resolution* setting) to keep it within a 60 fps budget. Temporal anti-aliasing upscales the result to full display resolution, so the changes are seamless. Press **F3** for a frame-rate / GPU-time / resolution readout. Low/Medium/High quality is in Settings.

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
