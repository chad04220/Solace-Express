## What's new
### One autopilot that flies the aircraft as it is
- The autopilot no longer has special cases for particular aircraft. It reads what the aircraft can do right now, many times a second, and flies to that. The readings are its weight, the air, ice on the wings, which engines are running, its flaps, and how its controls and wing respond. A new aircraft needs no autopilot code or tuning.
  - Heavy, it comes in faster, plans a weaker climb and pulls the stick further for the same g. Before, a fully loaded Q400 porpoised about the glidepath and could fly into the sea.
  - With an engine out, iced or in thin air, it plans the climb it actually has. If it can't climb at all, it lands from what it has instead of trying to go around.
  - It descends no faster than the aircraft sinks at idle in landing configuration, so it doesn't have to dive and then shed the speed again.
  - It starts the flare by how long the aircraft takes to round out, not only how quickly the nose moves. The research craft's small, heavily loaded wings build lift slowly. They now flare earlier and land softly; before, the XR-20 and XR-30 sometimes met the runway at 3 m/s.
  - Aerobatics use the same readings: entry speeds follow the stall speed as it is now.
- With passengers or a fragile load aboard, the final approach is stabilized: no more than about 500 fpm beyond the glidepath's own descent, even when it arrives high. Before, a Q400 reaching the glidepath high at Solace Capital dived onto it, porpoised and could clip the trees short of the runway.
- On the ground at speed, the autopilot steers back to the centreline over a few seconds of roll instead of a fixed amount per metre. A research jet at 65 m/s no longer swings across the runway and off its edge.
- Every case in the autoland sweep lands or is refused: 870 in the normal law (864 landed, 6 refused) and 648 in the gentle law (644 landed, 4 refused). The extra refusal is a heavy Starling with a gusting tailwind at Meadowbrook: it needs 1,141 m of the 1,100 m runway.
- New flight tests fly autolands with an engine out (Islander, Q400, Starling), fully loaded (Caravan, Q400) and iced (Kestrel, Q400).
