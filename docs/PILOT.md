# The pilot: the autopilot rework

The owner's brief: when the autopilot is engaged it should be as if a professional pilot is flying the aircraft. It flies
every aircraft extremely well, knows everything it needs to all the time, adjusts smoothly, and responds to changing
weather. Not a set of rules: an intelligent pilot that takes over when asked.

Chosen (October 2026):
- **How it thinks:** a look-ahead pilot.
- **What it does:** the whole flight, the weather, the decisions, the emergencies and aerobatics.
- **How you work with it:** it takes command. You can still ask for things, and you can take the controls back at any time.

## How it thinks: look ahead, then choose

The present autopilot is a chain of stages, each with hand-tuned thresholds (go around if 40 m low, flare at this height,
slow down 2.5 km out). Each threshold fixed one flight that went wrong, and none of them knows why it is there.

The pilot instead keeps asking the same question: *what happens next if I do this?* It predicts the aircraft's motion
from the aircraft's own measured performance, in the wind and weather it can see. It compares the candidate futures,
and flies the best one. It scores them as a captain would:

1. **Safety margins.** Terrain and obstacles, the stall, the structure, the weather (storm cells, icing, shear), the
   runway it can stop on, and the fuel it lands with. These are not weighed against comfort; a future that breaks one
   is not taken.
2. **A stabilised, smooth flight.** On the path and on speed early. No more bank, pitch, g or jerk than the job needs,
   and gentler with passengers or a fragile load.
3. **Time and fuel.**

Most "decisions" fall out of this without a rule. A go-around is chosen when every way of continuing touches down long,
fast, hard or off the centreline. A diversion happens when the destination's best landing breaks a margin, or the fuel
to get there and land does. Climbing out of icing is the future in which the ice stops growing.

## The layers

**1. Awareness**, every step:
- *The aircraft:* its state, its energy, its envelope now (`ApEnvelope`: weight, density altitude, configuration,
  engine health, ice), and its fuel and endurance.
- *The air it measures:* the wind (ground velocity minus air velocity, smoothed), the gust and turbulence levels, the
  vertical air motion, and the wind's change with height (shear), learned as it climbs and descends.
- *The weather it can see:* the forecast (`Weather`), and the radar picture ahead along the route (`wxfield::rainAt`,
  `cloudDensity`: storm cells, cloud, precipitation). Also the terrain's effect on the wind: lee sink and rotors
  downwind of ridges (`wxfield::local`).
- *The ground:* terrain and obstacles along the predicted path, and the runways (length, surface, wind components, the
  landing distance at this weight).

**2. The plan** (the captain), re-made every few seconds and at once when something changes:
- Departure: the runway into the wind, the take-off roll and rotation, the initial climb.
- The climb to a cruise level chosen for the terrain, the wind and the weather.
- The route, around storm cells and over high ground.
- The descent point.
- The approach: which runway, and straight-in or a pattern to it.
- The landing and rollout.
- An alternate, with the fuel to reach it.

**3. The path** (the next minute): a smooth flight path the aircraft can fly, in position, speed, climb or descent,
bank, configuration and power. Its turns, climbs and descents come from the envelope, and the next 60 seconds are
always flyable.

**4. The hands**: fly that path. It predicts tens of seconds ahead with a point-mass model of this aircraft (its thrust,
drag and lift from the learned performance), and picks the load factor, bank and power that track the path smoothly
within the limits. Gusts are measured and answered with feed-forward rather than waited out. The existing inner rate
loops (`apRates`, scaled from each type's learned response) turn those into stick, rudder and throttle.

**5. Checking with the real flight model**: before it commits to something it can't undo (the flare, a go-around, a
figure), it flies the next 10–20 seconds on a copy of the aircraft in the full flight model. If the copy touches down
too hard or breaks a margin, it chooses again.

## Its duties

- **The whole flight**: take-off roll to rollout, at any field the type may use.
- **Weather**: gusts and turbulence (gust-adjusted speeds, smooth answers), wind shear, crosswind landings, steering
  around storm cells, and leaving icing.
- **Decisions**: go-arounds, holds, the runway, a diversion, and saying why on the HUD's status line.
- **Emergencies**: engine failure (the best glide, to the field it can reach in this wind), flying on with failed
  systems, and a forced landing on the best ground when no field is in reach.
- **Aerobatics**: figures as paths to fly, with their energy checked before entry. One figure at a time. Any input
  from the player hands the aircraft back at once.
- **Requests while engaged**: an altitude, a speed, a heading, a field to land at, or "hold here", worked into the plan.

## How we'll know it's better: the checkride

`tests/pilot_exam.cpp` flies a set of scenarios. Each type at every field it may use, in calm, crosswind, gusts,
shear, storm cells and icing, with engine failures and short fields. It scores what an examiner would:
- **Stabilised approaches:** on speed within 5 kt and on the path by 300 m above the field.
- **The touchdown:** sink rate, zone, and the centreline.
- **The rollout:** stopped on the runway.
- **Comfort:** peak and rms g and jerk, bank and pitch.
- **Margins:** terrain clearance, stall margin, fuel at landing.
- **Decisions:** the right go-arounds and diversions, and none needless.
- **The time against the plan.**

The present autopilot is scored first, as the baseline. Each stage has to beat it without losing anywhere.

## Limits it works within

- **60 fps.** The pilot costs about 0.3 ms a frame on average. The heavy planning is spread over frames.
- **Career quotes.** A whole flight flown in the background (`simulateFlightMinutes`) stays under about 2 s of CPU.
- **Determinism.** The same flight replays the same: the weather fields are functions of place and time.
- **Voice.** Only lines that are recorded can be spoken. Everything else goes to the HUD's status line.

## Stages

Each stage is released when it passes the checkride better than what it replaces.

| Stage | What | Replaces |
|---|---|---|
| 0 | The checkride, and the baseline score of the present autopilot | – |
| 1 | Awareness (measured wind, gusts, shear, the radar ahead, energy) and the hands (point-mass predictive tracking) | `apControl`'s outer loops |
| 2 | The plan and the path: departure, climb, route, descent, approach, re-planned as the weather changes | `apGuidance` NAV / hold, `apPlan` |
| 3 | The landing: predicted flare and touchdown checked on the real model, crosswind technique, rollout | the final, flare and rollout |
| 4 | Decisions: go-around, hold, runway change, diversion, with the reasons shown | the go-around rules, the declined autoland |
| 5 | Emergencies: glide to a reachable field, failed systems, forced landing | – |
| 6 | Aerobatics flown as paths | `aircraft_stunt.cpp`'s steps |
| 7 | Requests while engaged | the hold mode's stick trims |
| 8 | The career's time and fuel estimates refitted to the new pilot | `kEstK`, the fuel shares |

## Progress

**Baseline: the present autopilot.** 1,470 flights.
- 93.1% landed and stopped on the runway, 4.3% declined, 2.6% failed.
- **1% of the approaches were stabilised.**
- The bank reached 72° at the median and 86° in the worst tenth.

**Stage 1, in progress.** Behind `Plane::apPro`, off until it passes the checkride; `pilot_exam` uses it with `PILOT=new`.
The present autopilot still flies exactly as it did: all 294 calm checkride flights match the baseline to the second.
- **The professional envelope, scaled to the airframe:** 1.3 g and 30 deg of bank in a civil type, up to 2.5 g and 60
  deg in a research jet built for 40 g (on the logarithm of its usable g), rolled at 8-30 deg/s; smooth g; a climb at the
  climb rate, descents up to 7 deg. Gentler with passengers (1.25 g, 25 deg).
- **The energy law (TECS):** the speed comes from the inertial acceleration, so gusts aren't chased. A throttle's worth
  of thrust is taken where the throttle is: an afterburning type's whole range overstated its dry range 2.5 times and
  the XR-40 never slowed. A jet built for it slows at up to 0.2 g.
- **The final:** the gate at 230 m for a light aircraft, 330-460 m for an airliner or a jet; configured and slowed by
  height on a planned profile, stable by 150 m (500 ft, flown visually) or 300 m (1,000 ft); a runway end weighed by
  the drop from its intercept altitude it couldn't lose before then; the path loop damped to each airframe's path lag.
- **The ground ahead, along the turn it is in:** it climbs over what's ahead on its path 250 m clear as a matter of
  course; only when its own manoeuvring g wouldn't clear it does it roll wings level and pull, as firmly as clears it
  (the XR-40 went from 34 g to 4 g turning into a ridge at 370 m/s), held for 3 s and until clear by 120 m.
- **An upset is beyond what it flies on purpose:** 15 deg past the bank it has lately been allowed. The recovery gets a
  g more, never the airframe's all; its bank stays its own (an XR-20 coming off the chart's edge was called upset,
  and turned at 85 deg and 11 g).
- **Time:** a pattern at 1.4-1.5 times the approach speed, out of the descent orbit as soon as it is down, no orbit when
  the rest can be lost on the way, no speeding up again once slowed for the hold.

**The checkride now**, all five weathers (1,470 flights, against the present autopilot; it still flies exactly as it did,
all 1,470 flights matching the baseline):

| | present | the pilot |
|---|---|---|
| Landed and stopped | 1,369 | 1,369 |
| Calm / cross / gusts / shift / storm | 284 / 284 / 278 / 276 / 247 | 284 / 284 / 276 / 279 / 246 |
| Stabilised | 719 | 1,011 |
| g rms, median | 0.32 | 0.11 |
| Jerk rms, median | 0.39 | 0.16 |
| Bank, median / worst tenth | 72 / 86 deg | 30 / 60 deg |
| g, worst tenth | 16.7 | 2.6 |
| Lowest g, worst tenth | -3.1 | 0.2 |
| Time, median | 368 s | 458 s |
| Fuel, median | 23.8 kg | 24.4 kg |

- **Gusts:** a gust's gain is let pass, a loss answered at once (filtered both ways, a storm's shear took 9 m/s off the
  Islander 20 m up before the power came). Below 100 m on the final, firm hands: 0.6-1.6 g, changed quickly.
- **The height before the turn:** up to half a g kept back from the bank when below the height it wants or sinking.
- **The chart's edge:** a turn as tight as the room left needs (up to 80 deg), when even its sharpest is too wide.
- **The sea's surface**, not its bed, is the ground the look-ahead keeps clear of.
- **Open:** the airliners and the XR-10 take 50-90% longer (a longer final from a higher gate, wider patterns); the
  XR-40 is never stabilised (its hover approach), and started near the chart's edge at 375 m/s needs up to 9.6 g to
  stay on it (the plan should turn it away and slow it first). Then the other weathers.
