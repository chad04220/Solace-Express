## What's new in v3.49.4

### Faster on the ground at night
- **The game now works out for itself whether to draw the ground's depth first.** On a runway at night, most of the trees in view stand behind the ground but were drawn anyway. Drawing the ground's depth first lets them be skipped: in v3.49.3's diagnostics on an RTX 3070 Laptop, it took 2.5 ms (20%) off the night runway's frame. In the other scenes it cost up to 1 ms more than it saved, so the game now measures. Every so often it draws a fraction of a second both ways, times each on your graphics card, and keeps the faster. Nothing looks different.

### Diagnostics
- **Each scene in the analysis shows what the game would choose there** ("Ground depth first, measured as in play"), with the time it measured each way.
- The graphics card's per-pass timings now miss fewer frames when the card runs a few frames behind.

### Good to know
- **v3.49.3 started your settings afresh**, along with the new name, so Render quality went back to Medium. If you played on High, set it again in Settings. The v3.49.3 diagnostics ran at Medium, so its times are not comparable with v3.49.2's (High).

Please run `diagnostics.bat` on this version, with Render quality set the way you play.

## What's new in v3.49.3

### Solace Express is now Somnium Volandi
- **A new name, "the dream of flying",** with a new icon and startup page. The game is `SomniumVolandi.exe` and the download is `SomniumVolandi-windows-x64.zip`.
- **A fresh start.** Saves, settings and radio stations now live in `%APPDATA%\SomniumVolandi`, and the shader cache in `%LOCALAPPDATA%\SomniumVolandi`. Nothing is carried over from the old `SolaceExpress` folders, so the first launch compiles the shaders again (a few minutes, once). **To keep a career,** copy the files from `%APPDATA%\SolaceExpress` into `%APPDATA%\SomniumVolandi` before you start the new version. Afterwards both old `SolaceExpress` folders can be deleted.
- The islands keep their names: Solace Capital and the Solace Islands are still on the map.

### New menus and loading screens
- **Each part of the game has its own look:** midnight blue, platinum and brass for the civilian screens; falling electric-blue code for the NIGHTGLASS research terminal; matte field panels with amber controls for military service.
- **Loading shows what it's doing:** the current step, how much of each stage is done, a checklist of the parts being prepared and the time so far. Flight preparation and the research warm-up show theirs too.
- **Settings, General, Menu animation** can reduce the menus' motion.
- **The aircraft stay in view while loading:** the details sit in a shallow strip along the bottom, so the picture of your aircraft is no longer covered. Classified aircraft in the Hangar and the military terminal show as a clear outline.

### Foveated shading (NVIDIA RTX cards)
- **When a scene is too heavy, the edges of the screen now give way before the centre.** On cards with NVIDIA's variable-rate shading (the RTX 20 series and later), Auto resolution first shades the ground and the scenery towards the screen's edges more coarsely: one shade for each 2×2 block of pixels, then each 4×4 block, in rings round the centre that widen as needed. The middle of the screen stays at full resolution. Only once the edges are as coarse as they go does the whole frame's resolution drop, as before. When the frame rate recovers, the full resolution comes back first, then the edges.
- **The aircraft, the cockpit and its displays are never coarsened,** so the instruments stay sharp.
- **A new setting, "Foveated shading"** (Settings, under Render quality), turns it off. It shows only on cards that support it; other cards keep Auto resolution exactly as it was.

### Diagnostics
- **The analysis's per-feature rows now hold steady in moving scenes.** In v3.49.2 each row was compared with one base measurement taken at the start, while the aircraft flew on and the graphics card's clocks wandered. Over a scene the frame time drifted by up to 2 ms, so in the flying scenes every feature seemed to save (or cost) about the same. Each row is now measured base, off, off, base and compared with its own base, so a steady drift cancels out.
- **The report says whether your card can do foveated shading,** times what it saves at its strongest ("foveated shading ON, max"), and saves a picture of each scene with it on (`analysis\<scene>_foveated.png`) beside the normal one, so you can see what it costs the picture.
- **Where the buildings and trees' time goes:** in testing, trees were about 85% of it, and on the night runway most of those trees were hidden behind the ground but drawn anyway. A way to skip them is built in but switched off: it draws the ground's depth before the scenery, and on the software renderer it cost more than it saved. The new "ground depth first ON" row times it on your graphics card, to decide whether to switch it on.

Please run `diagnostics.bat` on this version, and play a little with Auto resolution on: the screen's edges should soften only when the frame rate drops.

## What's new in v3.49.2

### Military service
- **A new military terminal** with nine funded sorties in the XR aircraft, from an XR-10 survey of your own airfield to an XR-40 strike on the Hive's command group: reconnaissance at speed and altitude in the XR-30, a precision hover scan, interception, two installation defenses and a relay strike. Each comes with its own aircraft, equipment and award, and military awards stay separate from your civilian money.
- **Missions unlock in order.** Only the first is open at the start. Each sortie you complete (objective done, then extracted) opens the next, and finished ones can be flown again. Locked missions show their aircraft as a classified silhouette.
- The loading screen shows the aircraft you're actually flying, and Escape or controller B there no longer also acts on the menu behind it.

### Hangar and research previews
- **Rotate, zoom and reset the aircraft** in the career Hangar, Free Flight and the research terminal.
- The XR-40's equipment choices in the research terminal moved to the side panel, so they no longer cover the aircraft, and the preview carries the hardware you picked.

### Saves
- **Save format 6** records which military missions you've completed. Your existing saves load forward with their service record intact. Older versions of the game can't read format 6, so keep a copy of your saves if you switch back.

### The diagnostics, corrected
- **v3.49.1's new rows for the parts of the ground measured the wrong things.** Their switches shared settings with the analysis's other probes: "roads on the ground" also hid every building and tree, "towns' ground and fields" hid the whole ground, and "ground detail layers" hid the ray-traced parts of the aircraft. So the roads looked like the most expensive part of the ground, when it was really the buildings and trees (2.5 to 3.7 ms on the runway and low-level scenes). Each row now measures only its own part, a new row times the roads' painting alone, and a check stops the settings colliding again.

### Faster on the ground
- **The roads cost less to draw.** Each pixel near a road now looks the road up once instead of twice, and checks only the roads actually passing through its patch of ground: about 3 instead of 15, on average. On the test renderer, low over a town, the roads now take about 35 to 40% less time to draw, and the ground as a whole about 5% less. Nothing looks different.

### Behind the scenes
- Codex's offline tools for building real-world terrain can now sample elevation about 16 times faster when NumPy is installed, writing byte-identical files. Nothing in the game uses them yet.

Please run `diagnostics.bat` on this version.

## What's new in v3.49.1

### Faster on the ground
- **Taxiing, take-offs and landings cost the graphics card much less.** Since v3.47 the view reaches the horizon, and the ground was drawn from the far side in: from a runway, every distant copy of the islands was drawn into the same few rows just above the horizon, its fields and all, and then hidden behind the nearer ground. The ground is now drawn nearest first, so what's hidden is skipped before it is shaded. Nothing looks different. In testing, the night scene on Cedar Ridge's runway took about 40% less time to draw its world, and the cockpit view about 10% less. The flight from altitude is unchanged.
- **A correction to v3.49.0's notes:** compressing the photographed ground textures halves the memory they take, but it was not what made the runway views slow. The fix above is.

### Clearer loading
- **The loading screen and the diagnostics now say what preparing the aircraft is doing.** Every aircraft comes ready-built, and after an update most of the wait is each aircraft compiling its shader programs, once. The progress line now says "Loaded the <aircraft>'s body, ready-built", and "Compiling the <aircraft>'s shader programs (once after an update)" while they compile, instead of "building the aircraft bodies" and "from the cache".

### Diagnostics
- **The analysis times the ground's shading part by part:** the airfields, the roads, the towns' ground and fields, the detail layers and anisotropic filtering each get a row of their own, so the next report shows exactly where the ground's time goes on your graphics card.

### Behind the scenes
- Codex's offline tools for building real-world terrain from public map data (the first step towards a real Earth) are now part of the project, with their tests. Nothing in the game uses them yet.

Please run `diagnostics.bat` on this version.

## What's new in v3.49.0

### Life on the islands
- **Traffic on the roads.** Cars and lorries drive the highways, roads, lanes and streets around you, up to 1,200 at a time, each in its lane on the right of its road. Lorries keep to a highway's slow lane. They keep their distance, wait their turn at junctions and turn round at dead ends. At night their headlights and tail lights burn, and from the air each light shows as a point.
- **The towns light up as it gets dark,** a window at a time rather than all at once. The street lamps come on at dusk with the pools of light under them. Around 18:00 the first windows are lit, and by 18:15 most of the town.
- **Ferries between the islands.** Five double-ended ferries join every island into one network, crossing at 17 knots from terminal to terminal and waiting at each end before coming back. Each terminal has a stone causeway out from the shore, a loading ramp under its lifting frame, and a mooring post either side of the berth. At night the ferries' saloons are lit and their navigation lights show.
- **Yachts in the harbours:** 150 of them moored off the coastal towns, each with an anchor light at night.

### Landmarks worth flying past
- **A wind farm** of ten turbines along a ridge, turning into the wind, red lights on their nacelles at night.
- **A stone circle** on a bare summit, **a wreck** aground on a reef in the shallows, and **an offshore platform** far out to sea, its flare burning.
- **Three observatory domes** on Mount Kaleo's crater rim, **a star fort** on a cape, and on **Lighthouse Key** the keepers' cottages and a landing stage with a fishing boat alongside.
- **Solace Capital's stadium,** its stands lit at night, and **Port Verde's container terminal:** a quay with four cranes, stacks of containers and a ship alongside.
- **A racetrack** on Kaleo's eastern plain: a 3.9 km circuit with its kerbs, gravel traps and start line, the pit lane and garages, grandstands, the teams' transporters and a car park.
- **The Fjordhaven dam:** a concrete arch dam across a mountain gorge, holding a lake. The lake is real water: you can see its shore from any distance and land on it as on the sea.
- **The strait bridge,** a cable-stayed span between two 55 m concrete towers on the highway to Northpoint.

### Bridges, farms and the roadside
- **The longest crossings have proper bridges:** a steel truss on the highways, a white tied arch on the roads, and stone-arched viaducts on the mountain lanes. They're solid: fly under the arches, not into them.
- **About 380 farms** stand out in the farmland, each at the end of its own gravel track: a farmhouse, barns and silos, a machine shed and a vehicle round a yard. Hedges and trees line the fields.
- **Along the roads:** about 23,500 street lamps that light at night, guardrails on the highways and over the embankments, wooden poles carrying their wires along the country roads, and direction and name signs.

### Smoother on the ground
- **The photographed ground textures are now compressed** to a quarter of their size. Taxiing, taking off and landing look the same as before but should cost the graphics card noticeably less. In v3.48.0's diagnostics, the scene on a runway at night took 15.5 ms a frame; the textures were most of that.

### Looking right
- **Sea stacks stand off the cliffs only,** in small clusters at the foot of the steep coasts. They no longer stand out in open bays.
- **The airfields' grass is mown in broad bands** along the runway, with patches of longer grass between. A low airfield by the beach has patches of sand rather than a pale haze over it.

### Good to know
- **The first launch after updating takes longer, once.** The islands are regenerated with the new roads, farms and landmarks.
- Your career saves as before; nothing in it changes.

Please run `diagnostics.bat` on this version: its report now shows what the photographed ground textures cost on your graphics card ("2K ground photographs").

## What's new in v3.48.0

### Military service and the Hive (a first look)
- **A Military list in the career hub's Contracts tab** (the "Military" button beside Work and Trials). Military service has its own record: rank, service credits, sorties, successes and intelligence. Your civilian jobs, aircraft, loans, reputation and money are never touched by it.
- **The service lends you the aircraft, fuel and stores.** A reconnaissance flight goes in an ordinary loaner: fly within 1.5 km of each of the three sites with the site ahead of you until the scan completes (five seconds each), then fly to the extraction point. Defense and strike flights go in an XR-40 outfitted for the job. A mission ends at its objective and the extraction, not on landing.
- **Four enemy craft of the Hive:** the Needle interceptor (charges, then fires a burst of three pulses: break away when you see the charge), the Bastion bomber (flies bombing runs at the site you defend; its bombs can be shot down), the Cantor relay (shields its allies; an EMP knocks the link down) and the Archon command ship (heavily shielded, calls in Needle escorts as it is damaged). Each is a detailed model of its own.
- **The XR-40's weapons:** three forward weapons (Pulse laser, heat-limited; Kinetic burst, a cannon with finite rounds and ballistic drop; Charged heavy, hold to charge) and three payloads (Plasma, a wide blast; Penetrator, compact and hard-hitting; EMP, which strips shields and relays but does no hull damage). Each is modelled on the aircraft, and its weight and recoil count.
- **A practice range for the XR-40:** in the research terminal, pick the XR-40 and Free Roam, then choose a forward weapon, a payload and the wave's leader. Pause during the flight to spawn a wave or to clear and rearm. Practice never touches your career.
- This is a first version: recon sites aren't defended yet, and a mission interrupted mid-flight can't be resumed (it is cleared safely).

### Cities with a skyline
- **Four new building types for the cities:** a 236 m supertall that steps back twice and ends in a spire, a 124 m round glass tower, a 20-storey residential tower block with balconies up its front, and a mid-rise office block with shops at street level whose windows and signs light up at night.
- **Each city's skyline now rises to its centre.** Every city has a landmark supertall near its heart, ringed by skyscrapers and round towers, then towers and tower blocks, then the office blocks, then flats, terraces and houses. A district's character no longer pushes a city's tallest buildings out of its centre.
- **The suburbs are filled in.** Short closes now run off the streets into the open ground behind the houses, about a thousand of them. That adds about 4,000 homes, close to 27,000 buildings in all, so the middles of the blocks are lived in rather than left as fields.

### A controller for everything in the menus
- **The D-pad and A now reach every control in every menu:** the job board's cards, the job's aircraft, the research terminal's aircraft, test cards and options, and everything that was already reachable. Moving past the end of a list with the D-pad scrolls it.
- **Values change with left and right.** With the fuel row highlighted, the D-pad's left and right take off or add 5% of the tanks. The same works on every settings slider, and on the research terminal's launch site and time of day.
- **The right stick scrolls the window you have highlighted,** and the highlight moves along with the scroll. A window with nothing to press in it, like a long briefing, is highlighted by its scroll bar.

### Faster career menu
- The job board works out its estimates once and keeps them, so moving around the board, the job details and the hangar no longer recalculates every route each frame. (Prepared by Codex.)

### Fixes
- **Starting a new career over a saved one opened the Settings tab** if you had visited Settings from the main menu first. "Overwrite save", "New career" and "Continue career" now always open on the job board.
- **The diagnostics no longer charge your career.** Their crash and break-up scenes used to save a fee into your career. Every part of the diagnostics now leaves your career and settings untouched.
- **The diagnostics report is clearer.** It shows how many aircraft bodies loaded ready-built, from your PC's cache or were built from scratch, and how long the aircraft's own shader programs took to compile, which is most of that step's time now.

### Good to know
- **Keep a copy of your career save if you might go back to an older version.** This version saves careers in a new format (with room for the military record), and v3.47.0 and earlier can't open a career it has saved. It reads your existing career as before.
- **The first launch after updating takes longer, once.** The islands are regenerated: the suburbs have new streets and the cities new buildings.

Please run `diagnostics.bat` on this version, and fly a military mission and the XR-40 practice range with the controller too.

## What's new in v3.47.0

### Roads laid out the way roads are
- **No road is steeper than its kind allows:** 6% on the highways, 9% on the roads, 12% on the country lanes. The routes are now found at those grades, so a road can follow the ground. Before, lanes reached 19%, and stretches of road stood tens of metres off a hillside or cut deep into it.
- **Up a mountain, the roads climb in switchbacks:** long legs across the slope and proper hairpins, not a sawtooth of short zigzags.
- **The curves are round.** Every curve is at least as wide as its road needs (450 m on the highways, 140 m on the roads, 45 m on the lanes), except a hairpin up a slope. Each curve is drawn smoothly. Crests and dips are rounded too, longer on the faster roads.
- **The ground under a road is a flat, level bed,** with banks either side cut or filled at about 1 in 2 and rounded at the top and bottom. Before, the bed could tilt where pieces of road met, and a highway's could sit a third of a metre off its own slope.
- **No more doubled roads.** Where a road's way follows another road, it now joins it at a junction and leaves it at another. Before, it ran alongside a few metres away, about 73 km of doubled road in all.
- **From the air,** the ground along the roads is drawn in finer detail out to about 2 km, and road surfaces are lit as the flat surfaces they are. A distant road no longer looks as if it lies tilted on the hillside.
- Beside an airfield, a road lies on the airfield's ground as it is, as before. The airfields themselves are untouched.

### Towns, villages and cities that grow like real ones
- **No more circles.** Each settlement has grown out from its centre over the ground around it: along its roads, up the gentler slopes, stopping at the shore and the airfields. Steep ground holds it back. The woods and fields come right up to the last houses and fill the gaps between the suburbs.
- **Real streets.** Every street is now a road of its own, graded into the ground, with kerbs, pavements and street lamps that light at night. It meets the roads and the other streets level at its junctions. A town or a city has a grid of blocks at its centre, turned to its main road. Round that, streets branch off, curve with the lie of the land, and end in a turning or meet the next. A village grows along its main road with a few lanes off it.
- **Buildings face their street.** Every building stands on its own lot along a street or a road, set back from it as its kind would be. At a city's heart stand the towers, with offices and flats round them, then shops and terraces, then houses with front and back gardens. Farmhouses sit at the edge, and warehouses line the roads out of the bigger towns.
- **Each place has its centre.** A church stands on the main road, with a park across the way in the towns and cities. The town-centre streets are lined with trees, and the gardens have their own.
- About 23,000 buildings in all, up from about 14,000.

### Bridges
- The roads now cross the water and the steepest mountain gullies on bridges: twelve in all, seven over the water. Each is a concrete deck on piers, carrying its road's surface and markings between parapets, with an abutment at each end. The bridges are lit and cast shadows like the buildings, and they're solid: fly under the deck between the piers, not into it. Until now, a road stopped at the water's edge and carried on from the far side.
- **Each bridge joins its road without a seam.** The deck is as wide as the road, and its ends are square to it. The road meets the deck at the deck's height, and the markings run straight on across. Under each end, the embankment slopes away beneath the first span.
- Nothing grows up through a bridge. Trees and buildings under one stay below its deck.

### A round world, all the way to space
- **The world is now drawn as a planet the size of the Earth.** Climb, and the horizon drops away below you and curves. The islands' copies, repeating every 100 km, spread across the planet as far as you can see. Distant ground sinks below the horizon the way it does on a real planet. Near you nothing changes: runways, approaches and everything within a few kilometres look exactly as before.
- **The view now reaches the horizon** instead of stopping at 40 km. From 10 km up you can see ground hundreds of kilometres away; from 100 km up, thousands. The haze thins as you climb, so the view straight down stays clear while the horizon fades into the bright band of air along it.
- **The sky thins out with height.** Above about 20 km it darkens to deep blue, and by 100 km it is black, with a glowing blue limb along the curve of the planet. The sun becomes a hard white disc above the air, and at sunset you can see day turn to night across the planet below. Clouds follow the curve too.
- The far copies of the islands are drawn from a lighter mesh. In our tests the view from orbit took less time to draw than flying low over a town.

### The sun, the moon and the stars
- **The real night sky.** About 8,400 real stars, every one the eye can see on the darkest night, from the Yale Bright Star Catalogue. They're at their real brightness and in their real colours: Betelgeuse orange, Rigel blue-white. Orion, the Pleiades and every other constellation are where they should be, and the sky turns with the hours. The Milky Way glows on the darkest nights.
- **How many you see depends on the sky behind them.** On a dark night they're all out; in twilight only the brightest show. Near the horizon they're dimmer and twinkle. By day there are none, until you climb high enough that the sky above goes dark: from about 20-30 km up the stars are out in daylight, and above the air they never go away.
- **The sun and the moon are drawn at the size they look to the eye.** In the sky each is half a degree across. The game shows a 55-74° view on a monitor that fills about 30° of your vision, so at their true size they would look half as big as they do through a real window; they're drawn twice that size to make up for it. The sun is darker towards its edge, a little flattened as it sets, and its glare comes from the bloom.
- **The moon is the same size as the sun, as it is in the real sky.** It goes through its phases, matching the real moon's phase on today's date. A new moon is a hairline sliver near the sun, with the rest of its disc faintly lit by earthshine, and a full moon rises as the sun sets. It shows its darker seas, is pale by day and bright by night, and reddens low down. Moonlight now comes from where the moon actually is, and it is stronger nearer full moon.

### The aircraft come ready-built
- **All 15 aircraft now ship with their bodies already built,** at full quality, outside and cockpit, the research craft included. The game no longer has to build them on your PC on the first launch after an update. It checks each one against the version it came with, and if one is missing or doesn't match, it builds that one itself as before. (Prepared by Codex.)

### Good to know
- **The first launch after updating takes longer, once.** The islands are regenerated: some roads take different routes, and every settlement is laid out anew.

Please run `diagnostics.bat` on this version.

## What's new in v3.46.0

### Two new aircraft
- **Larkspur L4**: a four-seat shoulder-wing touring single with a three-blade propeller and covered wheels. It cruises a little faster than the Wren and needs about 600 m of paved runway. PPL; $46,000 to buy, $360 to rent.
- **Atlas A180**: a 180-seat twin-engine airliner, the largest aircraft you can fly. It has swept wings and two underwing turbofans whose fans you can watch turn, spool down and stop. Its main gear folds into bays under the wings, and it has a full transport flight deck. It needs about 1,910 m of runway, so only Solace Capital and Port Verde International can take it. Kaleo Regional's 1,900 m is just short. ATP; $1,600,000 to buy, $18,000 to rent.
- Both have their own exteriors and cockpits, and both are in Free Flight and in the career hangar. The four research craft stay where they were.

### Gear and breakup, on every aircraft
- In a crash, fixed gear and lowered retractable gear now come away in three pieces: the left main, the right main, and the nose or tail wheel. Gear that is retracted, or barely starting down, stays with the airframe, and the bay doors stay on it too.
- A broken-up airframe keeps its pieces as they were when they separated. A propeller that comes off goes with its spinner and tumbles with it, and the controls, wheels and fans stop where they were.
- The covered wheels on the Kestrel and the Larkspur now leave the tread showing underneath, and the nose wheel's cover turns with it.

### Fleet visual pass
- Every aircraft, the research craft included, was checked from outside and in the cockpit. 28 cockpit defects are fixed: windshield joins, controls and fittings that clipped, seat webbing, and trim. So is some shading on the Islander's control surfaces.

### XR-40 VTOL power
- At the full 90° pod setting you can now hold the power directly. Hold throttle-up (Shift, PgUp or +) for full power, or hold 1–9 for part power. RT gives proportional power. Let go and it drops to idle at the engines' usual spool rate. The other pod settings, forward flight and the autopilot work as before.

### Good to know
- **The first launch after updating takes longer, once.** The aircraft bodies and their learned performance are rebuilt.
- **A career with a Larkspur or an Atlas in it can't be opened by an older version.** If you might go back to an older version, keep a copy of your save from before you buy, rent or finance one.
- The new aircraft's names aren't recorded for the voices yet. They show on screen, and the voice says the rest of the line.
- On the autopilot, the Atlas lands with both engines running in calm air and in wind. With one engine out, its autoland doesn't always make it, so fly those approaches yourself. Fully loaded, its passenger-comfort bank limit can run about a degree over.

Please run `diagnostics.bat` on this version. Fly the Larkspur and the Atlas in Free Flight first, and look over the other aircraft's cockpits too.

## What's new in v3.45.1

### Fixed: the ground lost its textures
- In v3.45.0 the ground lost its textures: hillsides and fields came out snow-white, and runways, fields, towns and roads were missing. A grey stripe could also run straight across beaches and shallows.
- The cause was the change that made the map wrap. It swapped the ground's height with its north–south position in the terrain shader, so the shader thought every patch of ground was thousands of metres up. It also looked for the runways and roads in the wrong place.
- The fix puts the height back. A test now checks that the shader keeps the height in place.

### Correction to the v3.45.0 notes
- The roads cross water and deep valleys on bridge spans, but the bridges themselves aren't modelled yet. For now, a road stops at the water's edge and carries on from the far side. The bridge decks are next on the list.

## What's new in v3.45.0

### Fly one way forever
- **The islands now sit in 10 km of open sea, and the map wraps at its edges.** Fly east past the sea and you come in from the west, over the far side's islands. The same holds north and south and on the diagonals. There's no edge of the chart any more: no warnings to turn back, and no flight is lost out there.
- **You see the far side's islands before you cross.** As you head out to sea, their hills and towns come up on the horizon. The sea, its waves and swell, the clouds and the rain all repeat with the map, so there's no visible join where you cross.
- **The crossing doesn't disturb the flight.** The aircraft, the air it's flying in (the gusts and eddies carry on as they were), the camera, the smoke, the wingtip vapour, the wreckage and the traffic flying near you all move across together.
- **The GPS and the minimap show the far side.** Across the seam they draw its islands, airports and checkpoints. The bearing, distance, ETE and route-remaining figures, and the target marker in the world, all take the shorter way round the map.
- **The autopilot takes the short way too.** Sent to a field across the seam, it flies over the open sea to get there instead of turning back over the islands. Its approach patterns still stay on the field's side of the seam.

### Roads
- A real road network now runs across the islands: 104 km of highway and 256 km of roads. It joins all 26 settlements and every airfield on an island that has one.
- The roads are built into the ground, with cuts and embankments. Each class has its own markings: dual carriageways with a central reserve, two-lane roads with centre and edge lines, lanes, and gravel tracks.
- Kaleo's empty eastern plain now has a port city (Kailani), a market town (Canefield) and three villages. Palm Bay's island, Far Isle and Nordholm's coast have a village each.

### Fixes from the v3.44.0 review
Claude and Codex both reviewed v3.44.0 in depth. Everything they found that affects play is fixed here, each with a test:
- **Diagnostics never touch your career or settings.** `diagnostics.bat` flies its crash scenes on a fresh career in memory and writes no save, settings or station file. It used to charge the crashes to your career.
- **Medevac and VIP:** rough air costs only the g it puts on the aircraft. Five minutes of straight and level flight on the autopilot in P4's air used to leave the patient at 10%.
- **Airline:** your routes fly by the minutes you fly, not one leg for every landing. Ten 90-second circuits used to pay ten legs.
- **Radio panel:** a click on a station no longer reaches the button beneath it. One click over the Hangar used to buy a Wren; in the XR-40 it fired the weapons.
- **Autoland:**
  - It flies the tower's go-around. It used to land anyway, and you were fined $500.
  - If it can't climb away, it lands, and that isn't held against you.
  - With no power, it glides a straight-in final it can reach, or tells you none is in reach. A Kestrel 2.5 km out used to ditch.
  - In passenger mode it settles onto the runway past the aim point instead of floating. A loaded Q400 floated 850 m and ran off Meadowbrook.
  - A go-around keeps the gear down until it is climbing clear.
- **Split-S:** the research jets climb to the height the figure needs before starting it. From 700 m they used to fly into the sea.
- **Job quotes:**
  - The background flight now steers its take-off roll. 12 of 61 quoted take-offs used to run off the runway, and their cards waited forever.
  - Deadlines come from the planned flight in its own weather, plus a quarter: 37 of 40 sampled timed jobs can now be made, up from 29.
  - P4 allows 20 minutes.
  - A4 accepts only the Starling, the type its client asks for.
- **Jobs with checkpoints:** you can take off again from a field's runway, or set the parking brake there to end the leg as a diversion. The next leg is planned, quoted and drawn over the checkpoints still left. A hold or go-around you ignored on one leg is still charged when the job is delivered.
- **Tower:**
  - Traffic that lines up after you're cleared to land goes around.
  - Nothing lines up in front of you on final.
  - After a long hold the tower clears the traffic that is stuck, never you onto it.
  - An autoland to a GPS alternate talks to that field's tower.
  - A hold counts from where you are once you've heard the call.
- **Sound:**
  - The radio follows the master volume.
  - Pausing mid-call brings the music back up.
  - A hazard warning is dropped once the hazard has passed.
  - Damaged voice clips are refused rather than played.
- **World:**
  - The craters you see are the ones the wheels, weapons and wreckage meet.
  - What a plasma bomb flattened stays flattened.
  - A wreck on a scenery boundary is solid on both sides of it.
- **Settings and menus:**
  - The UI scales to the window's width as well as its height.
  - The Windows window can't be shrunk below 800 × 600.
  - Toggles keep the keyboard focus as their labels change.
  - The hub tabs, settings pages and key bindings can be reached with the keyboard and the D-pad.
  - The FOV readout fits its box.
- **Twin engines:** an engine that has stopped no longer burns fuel. A twin on one engine burns half.
- **Saves:** malformed saves are rejected. These include two aircraft of the same type, an open loan with no payment, one aircraft on two routes, and negative reputation.
- **Research flights** start with their own fuel, and a test card is signed off only once it is on record.

### Correction
- The v3.44.0 notes said the new textures use about 180 MB more GPU memory. The real figure is about **317 MiB**; the 180 MB was measured from an earlier, smaller set. Low quality loads the same 2K set.

### Good to know
- **The first launch after updating takes longer, once.** The islands are regenerated, because the roads are now built into the ground.

Please run `diagnostics.bat` on this version.
