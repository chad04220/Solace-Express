## What's new

**Voices**
- A cast of six on your headset: the instructor and the checkride examiner, airport and weather information, the Spectre display pilot, the cockpit assistant (checkpoints, flaps and gear, landing ratings, warnings, outcomes) and the research computer
- Every line plays when the game shows its message; one comms queue puts warnings first, replaces outdated lever calls and says each lesson hint once
- Hints that name gamepad buttons play only when you fly with a gamepad

**Tower controllers on the radio**
- Three recorded tower controller voices (North, Coast and Valley Tower); each airport keeps one
- Calls come only from what's happening on your flight: a greeting, your takeoff clearance with the real wind, the handoff once you're clear (or "remain in the pattern" on circuits), straight-in or left-downwind joining instructions, the landing clearance on final, and "exit the runway when able" on the landing roll
- Radio squelch around each call, the call text on screen, and nothing stale after a pause, crash or the end of a flight
- The towers call you by the registration painted on your aircraft, the way real controllers do: in full on first contact ("Sierra X-ray Alfa Bravo Charlie"), then abbreviated ("Sierra Bravo Charlie")
- The AI traffic is part of the exchange: hold for traffic on final or on the runway before takeoff, "number two, follow the aircraft ahead" (with a wake caution behind a much heavier aircraft), "continue approach, the runway is occupied", and a go-around call if it's still occupied on short final
- Only airports with a tower talk: the farm, island, mountain and glacier strips are uncontrolled and quiet
- A voice volume slider in Settings; the radio music and engine dip while someone is talking

**Career menu redesign**
- A cleaner, higher-tech career screen in the main menu's style: dark glass panels with hairline edges over the live scene, a drifting scan line, and the icon and title with the licence beneath
- Bank, reputation and location as stat chips; numbered tabs with a gliding underline
- Small upper-case labels in the detail lists, a framed route map, and speed / range / payload meters in the hangar
- The settings page fits a 720p window without spilling off the panel

**XR-9 Specter: no more VTOL**
- The XR-9 is now a conventional runway jet: its nozzles no longer swivel down, so it can't hover or land vertically
- The lift fans and their ducts in the wings are gone; the cranked delta wing is solid again
- The 2D nozzles still vector ±29° in pitch with the stick, for its high pitch rates
- The cockpit's VTOL page is now a thrust-vectoring page (nozzle angle, gear, reheat), the HUD shows the thrust-vector angle and the annunciator lights TVC
- The XR-9 has no flaps: F / V do nothing in it. It lands fast on its delta wing, at about 140 kt
- Autoland flies the XR-9 down a normal approach, flare and rollout; the XR-11 still comes to a hover and lands vertically

**Fixes**
- The takeoff clearance comes only from the tower now (the "Engine running" message no longer clears you)
- No "Positive climb!" on flights that start in the air
- A warning (repeated every 8 s) as you approach the edge of the chart, before the point where the flight is lost
- Landing at another airport (a diversion) leaves you and your aircraft there instead of back at the departure field
- Switching to another window pauses a flight in progress and releases held keys and mouse buttons
- The "take off again" reminder for unflown checkpoints can no longer be skipped at low frame rates
- Settings are written only when something changes (and through a temp file), not every frame while the settings page is open
- The settings page shows where radio_stations.txt lives
