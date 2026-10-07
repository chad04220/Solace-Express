## What's new
### The autopilot flies harder, and lands where it's safe
- The autopilot now flies every aircraft to its limits: hard turns at up to the bank and g the airframe takes, and tight circuits. It flies gently (25 degrees of bank, soft climbs and descents) only with passengers or a fragile load aboard.
- The research craft (XR-10, XR-20, XR-30, XR-40) come down the final fast. A few kilometres out they close the throttle, rear up belly-first into the airflow to shed the speed, then drop the nose back onto the glidepath. The fly-by-wire XR-30 and XR-40 snap to about 65 degrees nose-up in under a second. The XR-40 then goes to its hover and lands vertically.
- Autoland checks the field before committing. It refuses, and says why, when:
  - the runway is too short for the type, or for the tailwind (the Starling at Gull Rock);
  - the ground keeps the approach too high to descend onto;
  - there is high ground where it would turn onto the final.

  It then holds heading and height. The autopilot picks the other runway end when that one is safe.
- Fixed: a gentle (passenger) autoland could fly into high ground. At Kettle Lake the Starling joined the final 19 km out and descended into a ridge. The final is now captured only inside the area the planner checked, it keeps clear of the ground ahead until the gate, and a go-around climbs clear of the ground all round before turning.
- After a refused autoland, any stick input hands control back, as the message says. A wind change that makes the autoland unsafe says so, and why.
- Stopping past the runway's end is reported as an overrun ("parking brake set"), not "Autoland complete".
- Every case in the autoland sweep lands or is refused: 870 in the normal law (864 landed, 6 refused) and 648 in the gentle law (645 landed, 3 refused).

### Cockpits
- Smooth window frames: the openings have a rounded trim lip. Before, their edges were jagged against the sky and the Starling's side sill was a row of teeth.
- The sun visors fold flat against the headliner. A loose piece of the copilot's visor no longer hangs as a black plate at the top of the windscreen.
- The magnetic compass has a card you can read: ticks, numerals, the red lubber line, lit at night. Before, it was a plain black box.
- Sun shadows on the cabin walls have soft edges instead of small steps, at no cost in frame rate.
- Sunlight through the windows bounces off the floor, seats and panel, so the roof and posts are no longer black in full sun.
- The XR-30's console displays are solid to their glass: no more floating fragments. The Mantis's canards no longer cross the footwells. The window openings' cut faces are in the frames' trim colour, not pale wedges.
- The cockpit's sun shadows were sometimes read from the wrong texture. They are now cached in the aircraft's frame, so the cockpit costs no more than in v3.29.2.

### Menus and loading
- The loading screen before a flight is ready almost at once: the hub builds the scenery round both ends of its runway while you choose a job. Before, 1,069 scenery chunks were still left to build after 10 s in the hub.
- Every menu shows its scene live; the old menu video is gone.
- New loading-screen pictures for every airport and every aircraft in flight, the four research craft included.
- Starting a flight in the air always has the gear up (where it retracts) and the brakes released.
- Every list can be reached at any screen size and UI scale:
  - The aircraft chooser lists your own aircraft first and scrolls with the wheel or the right stick, so rentals can't push one out of view.
  - The Work list and General Settings scroll.
  - The hub's tabs always fit.
  - The debrief scrolls above its total and buttons.
- The HUD's wind readout no longer runs into the crosswind and headwind components.
- The launch's loading text says "Loading" or "Building" by what each step really did. A damaged island cache is rebuilt, never half-read.

### Career
- Restart keeps the flight's mode: a practice approach or a trial restarts off the books, and a job's leg restarts from where the job waited.
- Retry from the debrief needs an aircraft that can fly the job (one repossessed, sold or unsuitable sends you to the chooser with the reason). Changing aircraft on a job clears its paid ferry as well as its hire, and the job card quotes what will actually be charged.
