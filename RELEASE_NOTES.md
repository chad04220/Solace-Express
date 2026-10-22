## What's new in v3.52.1

A release about smoothness: fewer stutters in flight and in the menus, and the world prepared faster. Nothing new to learn.

### Smoother in flight
- **No more stutter when the tower speaks.** When a tower call began, the game prepared its voice clips in the middle of a frame: a hitch of up to about 30 ms, the worst in a normal flight. The clips are now prepared on a thread of their own just before the call, so each call starts a quarter of a second after it's made. A call that cuts in (a takeoff clearance over the airport information, say) stops the one it interrupts and follows straight on.
- **Engaging the autoland to a far airfield no longer hitches.** Planning the approach meant working out the trees and buildings under the final, up to 60 ms in one frame. That's now done in the background for the fields you're likely to land at: the GPS pick, the field the autopilot is flying to and the job's destination.
- **Saving the career no longer pauses the game.** A save also rebuilt the job board, about 12 ms. It now takes under a millisecond.

### Smoother menus and loading
- **The hub's trip estimates are worked out about 17 times faster**, under a millisecond each instead of 13 ms, with the same planned route.
- **The cut from the main menu to the hub is smooth.** Its first frame used to build the scenery around the hub's camera; that's now prepared while the menu is showing.
- **Loading a region no longer ends with a 600 ms freeze.** Its terrain goes to the graphics card a little each frame while the loading screen keeps moving.
- **With no gamepad connected,** the game no longer asks Windows for one every frame, which could cost a millisecond or more each time. It asks once a second, or at once when a device is plugged in.
- **Installing the real-Earth pack** no longer pauses the game when the pack is opened.
- **Fixed** a rare clash between the hub's background flight quote and the scenery on screen.

### The world, prepared faster
- **Real-Earth areas are prepared about a third faster:** about 7 seconds each instead of 10.5 on the test machine, whether you start a flight there or fly into the next area.
- **The islands are generated again after this update** (as after every update) in about 20 seconds instead of 32. **Generated regions** are 18 to 35% quicker.
- What's built is exactly the same as before: the terrain, roads, towns, buildings, trees, farms and bridges were checked byte for byte.

### Good to know
- These timings come from a 4-core test machine without a graphics card, so they measure the game's own work, not the drawing. Your PC's numbers will differ. Worth trying: a takeoff and a go-around with the tower talking, the hub's job list, the autoland on a long flight, saving, and flying from one real-Earth area into the next.

## What's new in v3.52.0

### The real Earth
- **Fly anywhere on the real planet.** In Free Flight, press **Get the real Earth**. The game downloads 14 MB: the index, the world's open airports with a runway of 300 m or more, its towns and its lighthouses. The land itself (terrain, coasts, land cover, roads) streams in as you fly, a few hundred kB for each area, and is kept for next time, up to 2 GB. With no connection, the places you've flown before still load.
- **Want it all on disk?** On the real-Earth screen, **Keep all of it offline** downloads the whole pack (1.5 GB). A download that breaks off carries on from where it stopped when you try again.
- **Pick any airport.** Press **Real Earth** and type an airport code (KBOS, EGLL), its name or its town. The flight starts on its main runway, on the real heading, with its other runways painted and levelled around it.
- **Keep flying.** The real Earth has no edge. The area ahead is prepared as you fly and you move into it without a loading screen: the same trees in the same places, each airport's buildings on the same side, the autopilot, the GPS pick, the tower and the traffic carried across. Beyond what's built in detail, the terrain goes on to the horizon, lit at night by its towns.
- **Solace is in the real Atlantic.** With the real Earth installed, the islands sit 640 km east of Bermuda. Fly west and Bermuda, then the American coast, come up over the horizon. **Beyond: real Earth / generated** switches between this and the generated regions.
- **The real world, as it is built.**
  - **Cities** are built over where the land cover says they are, suburbs and all.
  - **Houses** are built the way their country builds them: New England's boards under grey shingles, the Mediterranean's white and ochre under terracotta, Nordic red and yellow boarding, and more.
  - **Big airports** like Boston and JFK have long terminals with an airliner at every gate.
  - **About 9,200 real lighthouses** stand where they are and as tall as they are, on their headlands, cliff tops and harbour mouths. Gay Head stands on its cliffs, Edgartown's out in its harbour mouth.
- **A career on the real Earth.** A new career's travel and long hauls go to real airports: Bermuda first, then the American coast. On a real-Earth field, the board also offers cross-country loads to small fields up to 180 km beyond, flown without a loading screen. A real-Earth career saves in a new format (version 8). Older careers keep the generated regions; the hub's travel screen switches them.
- **Fly to a real airport.** In flight, the pause menu's route goes to any real airport, with the distance to each. The autopilot holds the course: Solace to Bermuda is 640 km.
- **Sources:** ETOPO 2022 and Copernicus GLO-90 (terrain), CGLS-LC100 (land cover), GRIP4 (roads), OurAirports, GeoNames and Wikidata, under their licences (THIRD_PARTY_NOTICES.md). Not for navigation.

### Photo mode
- **Press F2 in flight, or choose Photo mode in the pause menu.** The flight freezes where it is, and a free camera takes over. Fly it with W A S D, Q and E (Shift goes faster, Ctrl slower) or the left stick and the triggers. Drag the mouse or use the right stick to turn it, [ ] or the bumpers to tilt it, and the wheel to zoom. It can go up to 600 m from the aircraft, and never into the ground.
- **A panel at the right sets up the shot:**
  - the lens and the light: field of view, exposure and vignette;
  - the colour: saturation, contrast and warmth;
  - the frame: full screen, 2.39 : 1, 1.85 : 1 or square;
  - five looks to start from: Natural, Vivid, Film, Noir and Golden.

  The gates and the route are hidden unless you turn them back on. H (or Y on a gamepad) hides the panel.
- **Space (or X on a gamepad) takes the photo.** It's saved as a PNG at full resolution, without the HUD or the panel, to **Pictures\Somnium Volandi**. On Steam it's added to your Steam screenshots too.
- F2 again, Esc or B goes back to where you were. Steam friends see that you're framing a photo.

### Beyond the islands
- **The Solace Islands no longer repeat.** The map used to wrap every 100 km, so whichever way you looked from the edge, the land on the horizon was Solace again, and flying on brought you back round to it. Now the open ocean goes on past the islands' sea, out to the island regions the game generates around Solace on the Earth: the Tern Islands about 200 km south, Willow Continental Isles to the west-southwest, the Rowan and Peregrine volcanic chains, and more beyond.
- **They're there on every flight.** Jobs, lessons, trials, sorties and research flights all show the neighbouring regions rising over the horizon as you head toward them, and their ground is solid if you fly into it.
- **In Free Flight you can keep going.** Fly out over the ocean and the next region is prepared while you cross. You arrive over its islands, with its own airports, without a loading screen.
- **The regions come in three sizes.** About a third are small: two or three islands, or a pair of atolls, with a field or two between them. Another third are the size they were. The rest are large, built around a main island or a coast with an international airport, a 2,600 to 2,800 m runway beside a city, and up to ten airports in all:
  - a large archipelago has a ring of six or seven islands around its main island, each with a town;
  - a large volcanic chain has five cones on either side of a lava plain;
  - a large atoll region has six atolls around a central one with a 1,500 m strip;
  - a large fjord coast has a high plateau strip above its fjords.
- **Villages are scattered through the hills.** Away from the airfields, every region now has villages and market towns on the gentler ground (two in a small region, four in a medium one, ten in a large one), each with its own streets and buildings.
- **Every region has its own places to find.** Its volcanoes' craters, its highest summits, the capes at the ends of its long islands, its islets, the sounds between an archipelago's islands, its atolls' lagoons, the heads of its fjords and the strait between its fragments all have names: Mount Halo, Giant Peak, Cape Farewell, Selkie Sound, Pearl Lagoon, Echo Fjord. So do the wind farms, wrecks, offshore platforms, star forts, stone circles and racetracks it has. A small region has two to six to find; a large one has up to fifteen.
- **They count in your passport.** Fly near a landmark, low enough to make it out, and it's found. Land at a region's airport and it's stamped. The passport shows the region you're in and lists every region you've explored, with how many of its airports you've landed at and how many landmarks you've found. The hub map and the GPS mark every landmark with a diamond, gold once it's found.
- **Four new achievements:** Beyond the Horizon (land at an airport beyond the Solace Islands), Wayfarer (land in five regions), Charted (land at every airport in one region) and Cartographer (find 20 landmarks beyond the islands). Every Field and Grand Tour still count the islands alone.

### A career beyond the islands
- **Take your career to another region.** With a Private licence, the contracts board has a **Travel** card: an airline ticket to any region within 450 km, from about $200. A Commercial licence reaches twice as far, and the Airline Transport licence every region listed. You arrive at the region's main airport, and its board is there: cargo, passengers, medevacs, VIP charters, night freight, low-vis runs, surveys and photo flights over its own landmarks, all paying 15% more than in the islands.
- **Your aircraft stay where you park them.** One you buy in a region is based there, and rentals wait at every field. The hangar shows where each of your aircraft is parked; away from one, you rent its type instead.
- **Home is always a ticket away.** Away from the islands, the Travel card lists the Solace Islands first, and if the bank can't pay the fare, the airline flies you home on account. The campaign, the trials, Defense Command and your airline stay in the islands. Your routes keep flying and paying while you're away.
- **Long hauls: fly it there yourself.** With an aircraft that has the range (an Emissary 500 of your own, or a rented Galleon 180), the board offers long hauls: freight or passengers for another region's main airport, a couple of hundred kilometres or more across the open ocean. The HUD and the GPS point the way and give the distance, and the autopilot (Z) holds the course for the airport until 9 km out. The region ahead is prepared as you cross, with no loading screen. Deliver the load, and your career and your aircraft go on from there. Land anywhere else and the job is called off, and you're back where you set out.
- **Continue career takes you back to the region you were working in.**
- A career that never leaves the islands saves exactly as before. One with something beyond them saves in a new format (version 7), which earlier versions of the game can't read.

### Worldwide trial boards on Steam
- **Every trial now has a worldwide leaderboard on Steam,** one for each trial and aircraft, plus one for each day's daily trial. Your best time is uploaded when you finish, and the results card shows your place in the world under your place on the local board.

### Made for every screen
- **Your graphics settings now stay with each PC.** The quality, resolution, frame rate, window mode and text size are kept on the PC itself, so Steam Cloud carries your career, profile and controls between PCs without changing one PC's graphics to suit another.
- **On a Steam Deck, the first start sets the graphics for the Deck:** Low quality, 30 fps, fullscreen and slightly larger text. The game says so on the main menu, and you can change any of it in Settings.
- **Prompts on a gamepad show its buttons.** With a controller in hand, the lesson hints, the HUD's prompts, the pause menu and the photo panel draw the button itself (A, B, X and Y in their colours, and the bumpers, triggers and D-pad as badges) instead of naming the key.

### Credits
- **Credits** on the main menu roll up through who made the game and the work it stands on. The wheel or the right stick scrolls them by hand; Esc, Enter, A or B closes them.

### A demo, ready for Steam Next Fest
- The same game can now be built as a demo: the career up to the Private Pilot licence, and three aircraft (the Dawnlark, the Ambler and the Scrubrunner) in free flight. When the licence is earned, a card says what the full game goes on to and offers a way to wishlist it. A career started in the demo carries over to the full game. The Military board is a teaser, and Project NIGHTGLASS stays hidden.

### Smaller fixes
- Screenshots are compressed properly now: a typical 1080p shot is about a quarter of its raw size, written in under a fifth of a second.
- On the loading screen, the tip sits above the panel instead of over it.
- The privacy notice (version 3) now covers what the leaderboards and photo mode send to Steam: your trial scores, and the photos you take, which are added to your private Steam screenshots.

## What's new in v3.51.0

### Time Warp, up to x16, in your own hands
- **Press T to step through Time Warp: x2, x4, x8, x16, then back to real time.** It used to stop at x4, and above x2 it was hard to fly by hand. Now you can cross the islands at x16 with your own hands on the controls, not only on the autopilot.
- **While Time Warp is on, the aircraft flies assisted.** Roll and pitch with the keys, the gamepad or the joystick. Let go and it holds the bank and the climb or descent you gave it. The physics run in steps no bigger than x4, so the aircraft is as steady at x16 as at x4. All 15 aircraft were flown at x16 in the tests.
- **Time Warp switches itself off when it should, and tells you why:**
  - below 250 m over the ground, or with terrain in your path in the next 20 seconds;
  - within 3.5 km of your destination, or 1.5 km before a checkpoint;
  - on a stall warning, at high g, below 10% fuel, or when something fails;
  - on the autopilot's approach and during aerobatics.
  
  Combat still runs at real time. With the autopilot cruising, it can stay on lower and nearer the field, as before.
- **A note on performance:** above x4 the game simulates up to four times as much per frame, and the scenery streams in four times faster. On slower PCs, x8 and x16 may stutter. The game reminds you of this the first time you go above x4.

### A soundtrack of its own
- **The game now has its own music.** It's an original generative score, played live from synthesised pads, bells, plucks and bass:
  - a slow, open theme on the main menu;
  - a lighter groove in the hub and the debrief;
  - a brighter theme for the campaign's finale.
- **Short musical stingers** play when a job is done, when a flight fails, when you earn a licence or an achievement, and when you win a trial medal.
- **Settings → Music volume** sets its level (Off at 0%). **Music in the cruise** adds a quiet ambience under the engines in level flight, high above the ground. It's off by default.
- The score steps aside whenever the internet radio plays, and comes back when you turn the radio off.

### A debrief worth landing for
- **Every landing gets a grade, from S to D, out of 100.** It's made of four parts:

  | Part | Points | Full points for |
  |---|---|---|
  | Touchdown | 40 | a butter landing |
  | Zone | 25 | touching down in the first third of the runway |
  | Centreline | 15 | staying within 2 m of the centreline |
  | Approach | 20 | crossing the threshold at the right speed and height |

  Each part's score is shown under the grade.
- **Flawless flight:** three stars and an S together pay a 5% bonus and earn a point of reputation.
- **The debrief now builds up:** the lines come in one after another and the total counts up. A badge marks a **new personal best** for that aircraft, and chips list the **achievements** you earned on the flight.
- **"Up next"** names the next story job, which is already selected when you continue.
- **A licence card** celebrates each new licence and lists what it opens up: the aircraft, the kinds of work, the Military board.
- **A finale card** marks the end of the campaign. It shows your career in numbers and what's still out there to do.

### New work, and more to do after the campaign
- **Photo flights** on the job board: a photographer wants one of the landmarks from the air. Fly through the gate that lines you up, then the gate over the landmark for the shot, and land at the destination. The shot pays a 10% bonus. Miss the gate over the landmark and the job pays half.
- **Sightseeing requests:** on passenger jobs that pass near a landmark, the passengers may ask to see it on the way. A gold marker shows where it is. Fly by within sight of it and they tip 8% of the pay, plus a point of reputation. It's optional.
- **Five legendary contracts** open once the campaign is done:
  - **The Island Marathon:** the islands end to end in the Emissary.
  - **Summit Pass in the Snow:** in the Scrubrunner.
  - **The Midnight Medevac:** through a storm.
  - **Into the Crater:** onto Mount Kaleo's caldera strip.
  - **The Galleon Night Run:** to Port Verde at midnight in rain.

  Each one pays big every time you fly it, and the board marks the ones you've flown.
- **New achievements:** Living Legend for flying all five legendary contracts, and Airline Tycoon for an airline that has earned $1,000,000.

### A better first start
- **A welcome on the first start:** tell the tower what to call you, choose **Assisted** or **Realistic** flying (you can change either later in Settings), and see the road from Student to the XR jets.
- **A lesson you've failed twice in a row** offers to try again in calm air.
- **The Military board now opens after ten flights in your logbook.** Before that, a card explains the XR programme and shows how many flights you have. If you've already flown a sortie, the board stays open. When it opens, Defense Command tells you.
- **The research terminal is now on the main menu** as **Project NIGHTGLASS** once you hold the Airline Transport licence or have finished the campaign. The licence card mentions the invitation. The hidden U + I shortcut still works.

### Trials worth replaying
- **Medals:** each trial now awards gold, silver or bronze. The marks are set for the aircraft you fly:
  - **Spot landing:** points.
  - **STOL contest:** your landing roll, against that aircraft's runway needs.
  - **Gate runs:** your time, against the course flown at that aircraft's cruise speed.
  - **Formation run:** seconds out of formation.

  The trial's briefing shows the marks, and each trial's card in the hub shows the best medal you've won there.
- **A results card** after every trial shows your score, the medal, your place on the local board and the medal marks. **Retry** flies the same trial again in the same aircraft.
- **Gate splits:** each gate on a gate run shows your time and how far ahead or behind your best run you are. The results card lists every split.
- **Race your ghost:** on a gate run, a marker shows where your best run was at the same moment, with the distance ahead or behind.
- **The daily course now has a board for each day.** Times from different days' courses were mixed together before.

### Your pilot profile
- **37 achievements**, from Wheels Up and Butter to Crab Walk, Deadstick, Grand Tour and Golden Wings, plus a few secret ones. Each one appears in a banner as you earn it, with a chime.
- **Airport stamps:** your first landing at each airport stamps your passport.
- **Landmarks to find:** fly over the wind farm, the stone circle, the wreck on the reef, the offshore platform, the observatory, the star fort, the racetrack, the Fjordhaven dam, the stadium, the container port, Lighthouse Key and the strait bridge to add them to your passport.
- **Logbook → Achievements** shows what you've earned and when, and how far along you are on the rest. **Logbook → Passport** shows every airport, landmark and sighting. The left column now lists your softest touchdown in each aircraft type.
- The profile covers all your careers and is saved in `profile.cfg` beside your settings.

### Flight assists
- **Keys and gamepads now fly with assists by default.** The roll and pitch inputs turn the aircraft, and when you let go it holds its bank and its flight path. A bank under 10° rolls back to wings level. The rudder is coordinated for you, and your own rudder adds to it. Near the stall the nose comes down by itself. The bank stops at 67°.
- **Settings → Flying → Flight assists** has three choices:
  - **Off:** every input moves the controls directly, as before.
  - **Keys and pad:** the default. A joystick still flies directly.
  - **All inputs:** the joystick is assisted too.
- The research aircraft keep their own fly-by-wire and fly assisted only in Time Warp. The autopilot works as before.

### Landing and arriving
- **One touchdown scale everywhere:**

  | Grade | Vertical speed |
  |---|---|
  | Butter | under 150 fpm |
  | Smooth | under 250 fpm |
  | Good | up to 350 fpm |
  | Firm | up to 600 fpm |
  | Hard | over 600 fpm |

  The voice callout, the pay, the checkride, the instructor's coaching and the aircraft's wear used to grade touchdowns in slightly different ways. They now all use this scale.
- **A landing stamp:** the moment you touch down, the grade and your vertical speed appear in the middle of the screen for a few seconds, and the camera gives a bump that matches how hard you landed.
- **Jobs now end at the stand, not on the runway.** After you land, the game welcomes you to the airport. Taxi clear of the runway and set the parking brake (B) to finish. To end the job where you are, choose **Finish flight here** in the pause menu. Lessons, trials and practice flights still end when you stop, as their briefings say.
- **Shut down the engine before you set the parking brake to earn the airmanship bonus:** hold the engine key (I) for a second while you're stopped. The bonus is 2% of the pay, shown as "Taxied clear and shut down". You couldn't earn it before, because the job ended as soon as you stopped. Press the engine key again to restart. The ENGINE OFF warning no longer sounds after you shut down on the ground on purpose.

### Feel
- **Camera shake:** touchdowns, turbulence, gusts, the stall buffet, high g, sonic booms, bombs and crashes now shake the view. The shake is lighter in the cockpit. **Settings → Flying → Camera shake** sets its strength from off to 150%.
- **Gamepad rumble** on Xbox-compatible controllers, for the same moments. Turn it off in **Settings → Flying → Gamepad rumble**.
- **Speed sensation:** the chase view widens by up to 5° as you speed up. Turn it off in **Settings → Flying → Speed sensation**.
- **The chase view recentres:** 4 seconds after you last swung it, it eases back behind the aircraft, so a brushed right stick no longer loses your heading. The orbit view still stays where you put it. **Settings → Flying → Chase view** turns this off.
- **Passengers applaud** a butter landing, and on a gate run each gate's chime climbs a note higher.
- **The debrief rises in** over the scene, instead of cutting in.

### More settings
- **Random failures:** On, for worn and rented aircraft as before, or Off.
- **Air:** **As forecast**, or **Calm**, which takes two thirds off every gust and all the turbulence.
- **Flight HUD:** **Full**, or **Essentials**, which hides the systems tiles. The wind components and the g meter then show only when they matter.

### Ready for Steam
- **The game can now run as a Steam game, from this same build.** When Valve's `steam_api64.dll` sits beside the exe, as it will in the Steam version:
  - your achievements are mirrored to Steam as you earn them, and the ones you already have are sent at the next start;
  - your friends see what you're doing, for example "Flying the Skerry Twin from Glacier Station to Solace Capital";
  - opening the Steam overlay pauses your flight;
  - on a Steam Deck, the first start's name field brings up the on-screen keyboard (X brings it up again).
- **Without the DLL nothing changes,** and the download from this page doesn't include it. There's no DRM: outside Steam, the game runs as it always has. `startup.log` says whether Steam is on, and if not, why.
- **The privacy notice is updated (version 2)** with what Steam itself receives. The game still sends nothing to us.
- The repository now has the Steam store's achievement table, all 74 achievement icons (earned and locked), the rich presence text, the library logo, upload scripts and an optional upload step in the release workflow (`steam/README.md`).

### Smaller fixes
- The debrief no longer cuts the last row of the pay list in half when the list is long.
- Long warning labels shrink to fit their box, and the "press I to restart" prompt under ENGINE OFF has a dark backing so you can read it over a bright sky.
- The instructor's hints get a third line when they need one, instead of being cut off.
- Local flights that start and end at the same airport now say "Local flight" and how many checkpoints they have, instead of "0 km".
- The job board's tabs are written the same way: WORK, TRIALS, MILITARY.
- The main menu's footer and the Settings page show the game's version.

### Checkpoints
- **The checkpoint marker no longer draws over your aircraft or the HUD.** The magenta diamond, its distance label and the dashed line down to the ground now go behind the aircraft wherever the airframe is in front of them. In the cockpit, the windscreen frame and the instrument panel hide them too, so you only see the marker through the glass. Every HUD panel, the map and the menus now draw over it. When the checkpoint is off-screen, the arrow at the screen's edge also stays under the HUD panels.
- **A new route from checkpoint to checkpoint.** The sparse dotted trail is gone. A smooth neon lane now curves from each gate to the next: it leaves each gate the way the gate faces, and it climbs or descends between gates in a gentle S. Chevrons flow along it toward the next gate, and a slow wave of light runs the same way. Its edges stay sharp at any distance and any resolution. It keeps its colour against a bright sky and hides behind terrain and aircraft. The leg you're flying now is shown too, from the gate you just passed, so you can follow it into the next gate; the legs after that are fainter.

### The first start after an update, measured
- **v3.50.2's side-by-side compiling was faster, but not by as much as expected.** On the RTX 3070 laptop the aircraft's shader programs took 105 seconds, down from 156 seconds one after another, instead of the estimated 35. Each helper compiled every program two to three times slower than the game itself does, even the last one when it was compiling alone.
- **The helpers now ask Windows to run them at full speed.** Windows can run a process that has no window of its own in a power-saving mode, at lower clock speeds. That may be what slowed them down.
- **The diagnostics now record each program's compile time and CPU time.** The next diagnostics will show whether the slowdown is gone. If it isn't, they'll show whether the cause is the driver's compiler sharing the cores or the CPU running slower.

## What's new in v3.50.2

### A much shorter first start after an update
- **Each aircraft's shader programs are now compiled side by side.** After an update, the first start compiles graphics programs for every aircraft, and they were done one after another: 46 programs, about 2.5 minutes on an RTX 3070 laptop. Several hidden helper copies of the game now compile them at the same time, while the rest of the game loads. On a 6-core CPU that should be about 35 seconds of compiling instead, most of it before the loading bar reaches the aircraft. This is an estimate from the last diagnostics; the next ones will measure it. Later starts load them from the cache as before.

### Diagnostics
- The diagnostics' screenshots no longer show a "Lesson recording unavailable" message. The diagnostics run without voices on purpose, and the message only applies when the game's voice files are missing.

## What's new in v3.50.1

### Less memory over a long session
- **Voice lines no longer pile up in memory.** Every line the game spoke used to stay loaded until you quit, and the whole cast is more than 400 MB once loaded. Now at most 64 MB of voice lines are kept: the ones you haven't heard for a while are let go and loaded again if they come back.

### The XR-40's overheat warning
- **"Weapons overheated, cooling" is said at most once every 30 seconds.** Holding the trigger overheats the lasers every few seconds, and the warning was spoken every time. It still shows on screen each time.

### Testing
- **The long-session test now runs with the voices on** and checks every list of things the game keeps (effects, messages, radio calls, traffic, the XR-40's weapons) every second, not only at the end of each flight. Before this release it flew 200 legs in one session (150 flights and 50 XR-40 sorties, with 15,150 laser shots and 300 bombs). Memory levelled off at about 225 MB after the first 30 legs, with 29 MB of it voice lines, and nothing grew after that.

## What's new in v3.50.0

### Every aircraft has a new name
All fifteen aircraft now have names of their own. This changes everywhere: the hangar, the job board, the cockpit voices, the radio calls and the research terminal. Nyx and the display pair say the new names, in the same voices as before.

| Was | Now |
|---|---|
| Kestrel T2 | Dawnlark T2 |
| Wren 180 | Ambler 180 |
| Bushmaster STOL | Scrubrunner STOL |
| Islander Twin | Skerry Twin |
| Pelican Caravan | Longshore Hauler |
| Meridian Q400 | Archipel 400 |
| Starling 500 Jet | Emissary 500 |
| Swift S6 | Cutwater S6 |
| Osprey C6 | Wavecrest C6 |
| Larkspur L4 | Thistledown L4 |
| Atlas A180 | Galleon 180 |
| XR-10 Nightjar | XR-10 Vigil |
| XR-20 Mantis | XR-20 Reverie |
| XR-30 Specter | XR-30 Nocturne |
| XR-40 Wraith | XR-40 Somnus |

The O + P display pair is now the **Nocturne display pair**.

### Joysticks, throttles and pedals
- **Any flight stick, throttle, HOTAS or rudder pedals now works**, alongside the keyboard and gamepad. The first joystick you plug in flies pitch and roll straight away.
- **Settings → Joystick** binds pitch, roll, yaw (rudder), throttle and the wheel brakes (toe brakes): click one and move the control. Each axis has its own direction, dead zone and curve, with a live meter.
- **Any action can have a joystick button** or hat direction, in a new column on the Controls page.
- **A throttle lever takes over when you move it**, and a throttle key or trigger takes it back until you move it again.
- A bound joystick unplugged in flight pauses the game, as a gamepad does.

### When something goes wrong
- **Crash reports:** if the game crashes, it saves a crash report (a dump, a summary and the log) in `%APPDATA%\SomniumVolandi\crash`. The next start tells you, with a button to open the folder. Nothing is uploaded.
- **Report a problem**, in the pause menu, writes down what was happening (the aircraft, where it was, the weather, the last messages) and opens it for you to add what went wrong.
- **`diagnostics.bat soak`** runs a two-hour session and checks that nothing slows down or grows.

### The XR-40 no longer freezes
- **The first bomb drop and the first switch to the cockpit no longer freeze the game.** The loading screen now draws the flight's views once (inside, outside, the XR-40's stores and a blast) before the flight opens, so the graphics driver finishes preparing them first.

### Realistic g limits
- **Civil aircraft now break at their certification category's limits**: the tourers +3.8 g, the Scrubrunner and Cutwater +4.4 g, the aerobatic Dawnlark +6 g, the Emissary +3.0 g and the airliners +2.5 g. Aerobatics declines a figure the airframe isn't built for.
- **The research craft take far more**: the XR-10 +45 g, the XR-20 ±34 g, the XR-30 ±180 g and the XR-40 ±300 g.

### Other changes
- **No radio stations come with the game.** Add your own: the radio panel's **Edit station list** opens the file, and **Reload** picks up your changes. A list you already have is kept.
- **The release now includes an end user licence agreement (`EULA.md`) and a privacy notice (`PRIVACY.md`).** In short, the game keeps everything on your computer and sends nothing.

### Good to know
- **Careers from earlier versions don't load.** The saved aircraft use their old names. The game keeps the old save as `career.sav.damaged` and starts a new career.
- **Joysticks have been tested only with simulated devices**, not with real hardware on Windows. Please try yours: bind each axis, check the directions and the throttle lever, and tell me what your device is called and anything that doesn't respond.

## What's new in v3.49.8

### Spatial audio (prepared by Codex)
- **Every sound now comes from where it is.** Engines, traffic, wind, rain, crashes, weapons, voices, warnings, menu clicks and the radio all go through one mixer that places them around your head. Sounds follow the camera, so turn your head in the cockpit or swing the outside view and they move with it. Headphones give the clearest sense of direction; ordinary stereo speakers work too.
- **Each engine sounds from its own mount:** both of a twin's, all four of the Atlas's or the XR-40's pods. Spool one down and you hear which.
- **Aircraft pass you with Doppler**, the pitch dropping as they go by, instead of a recorded flyby. Distant sounds fade and dull with distance, with no floor.
- **Thunder arrives after the flash**, as long after as the strike is far away. Watching from outside, a supersonic aircraft's **sonic boom** reaches you when its shock wave does; on board, Mach 1 brings buffet and a callout.
- **Crashes:** each part that hits makes its own sound where it hits: impacts, scraping metal, splashes and fuel blasts, each a little different, with burning wreckage crackling where it lies.
- **On the ground:** touchdowns and rolling sound different on paved runways, grass, gravel, snow and sand. Wind changes with your speed, sideslip and turbulence.
- **Weapons:** the cannon, the pulse laser, the charged shot, EMP, the conventional blast and plasma each have their own sound, from where they fire and land.
- **Radio music** plays through the same mixer, decoded in the background.
- **More speech:** two new voices, flight operations and military control, speak 63 new cues: combat warnings and objectives, Free Flight regions, autoland problems, landing coaching and flight results.

### Good to know
- The audio has been checked by tests and a rendered preview, not yet heard on a Windows sound device. Please listen with headphones: a four-engine cockpit while turning your head, traffic passing, a crash near and far, a storm, landings on each surface, and pausing or muting while someone is speaking or music is playing. Tell me anything that sounds off.

## What's new in v3.49.7

### No more frozen menus when a flight starts
- **The loading screen now comes up the moment you start any flight:** Initiate in the research terminal, Fly or Continue on the job board, Practise, Try again after a flight, Take flight in Free Flight, and Restart from the pause menu. Until now the menu stayed frozen on screen while the flight's first scene was prepared behind it, most of all in the research terminal. Now the loading card shows straight away, with the flight's title, aircraft, airports and weather, and the save, the flight's setup and the first scene all happen behind it. (Military Deploy already worked this way.)
- **Back on the loading card returns you to where you started**, and nothing of the flight has been set up or saved yet.

### The g-force effect, redesigned
- **Pulling too hard now looks like your eye's iris closing in.** A dark red iris closes from the edges of the screen towards the middle as the load builds, until only a pinhole is left as the airframe nears breaking. Its tissue looks like a real iris: fine radial fibres, the ring round the opening, crypts and furrows, black towards the edges. Near the opening you can still just see the world through it; further out you can't, and the HUD fades away under it too.
- **The view through the opening turns dark red** and loses its colour, darkening towards the opening's edge.
- **Veins** branch in from the edges and reach just past the opening, so they creep in as it closes.
- **A racing heartbeat:** with each beat (a lub and its dub) the iris tightens, the veins swell and a dark flush runs over the view.

### Faster ground on NVIDIA cards (to be confirmed)
- **v3.49.6's diagnostics showed the graphics card working out the ground's colour for up to 19 times more pixels than end up on screen.** On the night runway it shaded 4.7 million to keep 1.5 million. The software renderer here shades each pixel once. The cause looks like how the ground shader dropped the sea floor under the water: done that way, the card can't skip ground hidden behind hills, buildings or the cockpit before shading it. The sea floor is now cut off before shading instead, and the picture is the same: on coastal scenes before and after, at most 2 pixels in half a million differ noticeably.
- The ground should cost noticeably less on your card, most of all low down and on runways. **Please run `diagnostics.bat`:** each scene now also counts the ground with "ground depth first" switched on, which shows whether this was the cause.

### Good to know
- The first launch after updating compiles the ground's and the screen's shaders again, once (the startup screen shows it). The aircraft's own shaders are unchanged.

## What's new in v3.49.6

This one is for the diagnostics; nothing in the game itself changes.

### What v3.49.5's diagnostics showed (RTX 3070 Laptop, High)
- **Faster nearly everywhere:** the forest 11.4 -> 9.9 ms, the storm 9.5 -> 8.0 ms, the night runway 14.1 -> 13.0 ms, the cockpit 10.4 -> 9.9 ms.
- **The gentler foveated shading looks clean.** At its strongest it now saves 1.1 ms on the night runway (it was 2.1 ms with the coarser edges), and the staircase in the corners is gone.
- **Ground depth first chose well:** on for the night runway and the XR-40 hover, off in the six scenes where it would have cost time.
- **One slower scene:** the XR-40 hovering over Solace Capital at night went from 17.4 to 19.3 ms, and got heavier still while it was being measured. Its ground costs three to four times what it did in v3.49.4, with the picture exactly the same. In play, ground depth first switches on there and wins back about 2 ms of it. It can't be reproduced on the software renderer here: flown on for 50 seconds there, the scene holds perfectly still.
- The first launch after updating to v3.49.5 compiled the new aircraft shaders once: about 7 s per aircraft type the first time it's flown. This update changes no shaders, so there's no wait this time.

### New in the diagnostics
- **The analysis now looks at the ground in every scene, at its start and again at its end:** the frame time, how much ground is drawn, whether the cockpit and the ground's own depth were in before the ground was shaded, how many ground pixels the graphics card shaded against how many it kept, and where the camera and the aircraft are. A scene that holds still should read the same both times; where the XR-40 hover doesn't, these lines show why.

Please run `diagnostics.bat` on this version.

## What's new in v3.49.5

### Free Flight: the whole planet
- **Free Flight can now take you beyond the Solace Islands.** There are 6,143 regions round the planet, each an invented landscape at its own place on the globe: archipelagos, chains of volcanoes, fjords, atolls and pieces of continent, each with four to six airfields and the towns and roads between them. Pick one of the recommended regions, or enter a latitude and longitude.
- **Fly from one to the next.** Head out across the ocean and the region ahead is built in the background and comes up over the horizon as you fly, with no loading screen on the way.
- The career and the research terminal stay on the Solace Islands, and Free Flight never touches your career save. (Prepared by Codex.)
- This is a first version, tested on the software renderer here. How smoothly the crossings go on your graphics card is the thing to watch.

### Jet fuel
- **Jets now burn fuel the way jets do:** by the thrust they make, much more at full power and in reheat, much less high up. On full tanks and with no reserve, a Starling now reaches about 4,300 km and an Atlas about 5,800 km. Their tanks are bigger: 2,200 kg and 20 t.
- **Full tanks and a full load don't always go together.** Each jet has a maximum take-off weight: with a full cabin, the tanks can't be filled to the top.
- **The research jets burn fuel too.** The XR-30 and XR-40 start with full tanks, and an XR-40 hovering empties them in about 25 minutes.
- On ordinary short flights, a jet's fuel bill is smaller than before.
- The autopilot no longer lets an airliner slowly wallow from side to side in long, high cruise.

### Auto resolution keeps the picture sharp
- **Auto resolution now lowers the picture only where that actually buys frames.** It used to assume a heavy frame was heavy because of its pixels. Hovering over Solace Capital at night at High quality, the cost is the city's tens of thousands of buildings: in v3.49.4's diagnostics on an RTX 3070 Laptop, the whole resolution down to 67% saved 2.4 ms of 17, and the softest edges 0.3 ms. Auto still pushed both all the way down, so the picture got blurry for no gain. Now each step down is timed, and a step that saves next to nothing is taken back at once. Where the pixels are the cost (the ground at night, the storm), it works as before.
- **Flights start at the full resolution** (Auto used to start at 75% and could stay there), and the full picture comes back as soon as there's room for it.
- **Foveated shading is much gentler.** At its strongest, the middle 55% of the screen stays sharp, the whole height of it, and only the corners get the coarsest shading. Halfway, only the far left and right edges are softened.
- Settings shows what Auto is doing now: the full picture, or how far the edges and the resolution have stepped down.

### Looking over the aircraft
- **One set of preview controls everywhere:** the Hangar, Free Flight, the research terminal and military service all have the same strip: rotate left and right in 15° steps, zoom out and in, Reset. Hold a button and it repeats; drag with the mouse; on a controller, the right stick turns the aircraft and the triggers zoom. Each menu keeps its own look.
- **The hangar is to scale.** The workbench, crates and safety stripe are their real size now, so a light aircraft and the Atlas look as big as they are beside them. Rulers on the floor give each aircraft's **wingspan and length in metres**, measured from the model itself (not shown for classified aircraft).
- **Deploy goes straight to the loading screen.** In military service the loading card comes up on the frame you press Deploy, with the aircraft you were looking at, and the mission is prepared behind it. The supplied-equipment list moved into the scrolling briefing. (All prepared by Codex.)

### Cockpits
- **XR-30:** the top edge of the main display is straight again. The curved frame of the wraparound windows cut into its upper corners in a row of small teeth.
- **Atlas:** the small display in the overhead panel (heading, flaps, gear, fuel) reads the right way round; its text was mirrored. (Both prepared by Codex.)

### Fixed
- **The README in the v3.49.3 and v3.49.4 downloads had the folder names wrong** in its note on keeping your career: it named the new `SomniumVolandi` folder twice and said the "old" folders could be deleted. Don't follow that copy of it. The right way round, as the release notes said: copy your files from `%APPDATA%\SolaceExpress` into `%APPDATA%\SomniumVolandi`, and only then can the `SolaceExpress` folders go. The README is corrected.

Please fly Free Flight beyond Solace on this version, and run `diagnostics.bat`.

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
