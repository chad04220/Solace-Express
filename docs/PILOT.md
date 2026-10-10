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
