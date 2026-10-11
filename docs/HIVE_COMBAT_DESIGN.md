# Hive combat core

`src/hive_combat.h/.cpp` is an enemy-only CPU simulation. It includes shared vector math, but has no OpenGL, aircraft catalogue, career, economy, save I/O, or renderer-slot dependency. It never awards money or changes aircraft ownership. The integration layer owns XR-40 loaners, player loadouts, flight damage, HUD, audiovisual effects, settlement and save policy.

## Integration contract

1. `Combat::reset(seed, worldWidth)` clears the encounter and invalidates every previous ID. A zero width disables toroidal x/z geometry.
2. Spawn explicit enemies and retain the returned nonzero IDs. Then call `startMission(config)`; this sanitizes bounded counts and mission parameters but does not clear existing actors or projectiles. Strike target IDs must name those spawned instances.
3. Pass the final frame's player position, velocity, forward and up vectors, body-local hull/wing/tail boxes, alive state and scan input to `step(dt, player, callbacks, paused)`. Each fixed tick reconstructs its own position backward from that endpoint using the supplied velocity and remaining accumulator, canonicalized over world seams. It reconstructs only the accepted 0.25-second window plus any retained sub-tick remainder, not discarded wall time. Orientation remains the final frame orientation because no angular history is supplied.
4. Supply nearest-scene `sweepFraction`, terrain-height and optional LOS callbacks. The sweep returns a first blocking fraction in [0,1], or a finite value above 1 when clear. Set `sweepIncludesTerrain=true` when it covers terrain as well as scenery; the core then skips its sampled terrain fallback. Nonfinite/negative fractions fail closed. The supplied radius must be swept against geometry, not ignored. Callback coordinates are nearest-image, unwrapped world segments; a toroidal world adapter must sample its terrain/scenery appropriately.
5. Feed actual player weapon segments into `playerShot`, blast impacts into `playerBlast`, and EMP into `playerEMP`. They do not depend on the player's aircraft type. Instant shot processing represents a swept segment already travelled by a game projectile, not permission to turn a travelling weapon into arbitrary-range hitscan. `playerShot` still returns the hit actor ID or zero; pass the optional fifth-argument `ShotContact*` to distinguish `Actor`, `Bomb`, `World`, and `None` and consume the visible projectile at the returned segment fraction. Bomb/world hits also stop the projectile despite the legacy zero actor-ID return. `None` resets the output to fraction 2 and ID 0.
6. Consume `events[0..eventCount)` and call `clearEvents()` after processing. Apply `PlayerDamage` to the actual player aircraft. Objective damage is already applied to `mission.config.objectives`. Never award settlement from a Death event; use the mission result and the career's once-only transaction handling.
7. Submit live actor state to enemy visuals with IDs kept separately from compacted render slots. `Actor::relayTarget`, `phase`, `aim` and timers provide persistent visual cues even if event presentation is interrupted. Reset on retry, launch, abort and scene transition.

Actor IDs are monotonic within a reset epoch; dead slots can be reused without reusing their IDs. A zero ID is invalid. Events distinguish source actor, target actor/objective/projectile according to event type. Projectile IDs have a separate namespace. The arrays are public to support simple renderer adapters and focused tests; production integrations should mutate them only through the core API, except configuring explicit debug scenarios.

`shiftOrigin(offset)` translates actors, locked aims, projectiles, events, objectives, extraction and scheduled spawn locations together. Use it only for actual world-origin rebasing. Ordinary seam crossings are already handled by the wrap width and shortest x/z displacement.

## Bounds and determinism

- 8 actors, 96 projectiles, 128 buffered events, 3 objectives, 6 scheduled waves, 8 strike targets.
- Fixed 1/60-second ticks, at most 15 ticks per call. A call contributes at most 0.25 seconds; excess input time is deliberately discarded rather than causing a runaway catch-up loop. Small fractional time is accumulated.
- Paused, nonpositive, nonfinite time and nonfinite player vectors produce no simulation work.
- Seeded xorshift state controls initial actor attack delay. Equal seed, inputs, callback results and tick sequence produce the same result on the same floating-point implementation. This is not a cross-platform bit-exact replay guarantee.
- Event overflow increments `droppedEvents` rather than allocating. Integrations must drain every frame and monitor overflow. Authoritative health, death and mission state do not depend on events being delivered.
- A full actor budget defers remaining wave members instead of silently dropping them. A scheduled wave is announced once even while waiting for space.

Keep combat at normal time scale in the game. The catch-up cap is a safety bound, not a substitute for a time-acceleration policy.

## Initial role tuning

These are proposed starting balance values, not measured performance or flight-test claims.

| Type/ID | Cruise / burst (m/s) | Longitudinal / lateral acceleration cap (m/s²) | Hull | Radius / ground clearance (m) |
| --- | --- | --- | --- | --- |
| Needle / 0 | 520 / 650 | 100 / 220 | 300 | 8 / 90 |
| Bastion / 1 | 240 / 340 | 35 / 55 | 800 | 11 / 130 |
| Cantor / 2 | 300 / 400 | 50 / 80 | 500 | 10 / 130 |
| Archon / 3 | 170 / 250 | 25 / 35 | 2500 | 24 / 180 |

Steering changes velocity through bounded acceleration and keeps inertia across burst recovery. Burst speed lasts two seconds with a ten-second retrigger cooldown. Terrain lookahead requests a climb; movement sweeps prevent penetration. Initial terrain overlap can climb out without an altitude teleport. Contact produces a conservative stop/climb response; this is an arcade hostile-flight controller rather than the player aircraft's aerodynamic model. The listed radii are broad terrain-avoidance envelopes matching the uploaded craft specifications. Weapon hits use shaped local proxies, described below, rather than these spheres.

### Needle: interception pulse

Charges for 0.7 seconds, locks predicted aim at the start of that visible charge, fires three travelling 900 m/s pulses with 0.15-second spacing, then makes a recovery pass for 2.5 seconds. Pulse damage is 6 and lifetime is two seconds. Lateral evasive movement after the cue defeats the locked solution. It does not continuously track the player during the charge.

Needle and Archon fixed nose stations require their solution to remain within a 20-degree forward cone. Side/rear contacts must first be approached and faced. If movement carries a locked target outside that cone during charge or burst, the weapon cancels and repositions before announcing a fresh solution. Bastion belly-store release does not use the forward-gun gate.

### Bastion: objective bombing runs

Targets the first surviving defended objective rather than dogfighting the player. When no defended objective exists (practice or strike), it attacks a locked prediction of the player flight path instead of remaining unable to fire. Two-second bay telegraph, three gravity-driven bombs at one-second spacing, and twelve-second recovery pass. Bombs deal up to 10 damage in a 40 m falloff radius, expire after 15 seconds, and can be intercepted by player weapon segments. Release velocity uses the locked target and estimated fall time, bounded to 340 m/s horizontally. A release outside that solution can miss; bombs never home or teleport to the site.

### Cantor: shield relay

Chooses one LOS-visible, non-Cantor ally within 700 m and supplies 8 shield/second up to 100. It cannot restore hull, shield itself, chain relay through another Cantor, or maintain a link through terrain. Damage interrupts the relay for four seconds. EMP removes shields and blocks both incoming relay and outgoing relay for the requested duration, capped at thirty seconds. The relay itself is the Cantor's dedicated support weapon; no invisible radial hitscan attack is implemented.

### Archon: command lance and finite escorts

Starts with 400 finite shield and 2,500 hull. A 2.5-second visible charge locks the aim before firing one 1,200 m/s, 25-damage lance, then twelve seconds of recovery. Recovery drains shield at 80/second; there is never an invulnerability gate. Hull thresholds below 70% and 35% each spawn exactly two Needle escorts when the eight-actor budget allows. There are at most two escort waves per Archon. No automatic self-shield regeneration or endless reinforcements.

A player Wraith bolt baseline is 25 damage. Player weapon heat, recoil, cooldowns, ammunition and loadout choice remain in the game/loadout layer. Enemy cadence and projectile budgets impose explicit firing limits here.

## Collision and damage

Player weapon segments compare terrain/scenery, every live hostile shaped body and shootable enemy bombs; only the nearest hit applies. Terrain/scenery wins exact ties. Travelling projectiles similarly sweep the entire travelled segment against the world and eligible targets, preventing fast-projectile tunnelling through those proxies. Moving actors and players use relative translation over the tick; orientation is held at the current frame for that short sweep. Enemy projectiles cannot hurt other Hive actors; player projectiles cannot damage defended objectives or the player through this API.

Blast damage falls off with distance to shaped bodies, requires LOS, and does not damage hull outside its radius. Hostile blast distance to the player uses the nearest supplied box. Player blast distance to an enemy uses its first shaped surface along the center ray; this may conservatively undercount a grazing blast near an extremity rather than award damage in empty envelope space. Hostile world-impact explosions back off 0.1 m along their incoming segment and lift above the terrain as needed, preventing the impact surface from occluding the blast's own origin while retaining protection on a wall's far side. Player explosion callers likewise must provide an origin clear of the impact surface. EMP has LOS/range restrictions and never damages hull. Shields absorb damage first, then hull. An instance emits Death at most once, even if further shots arrive or its slot is later reused.

Enemy proxies use the uploaded shader hull/wing/keel/shoulder polygon coordinates, extruded through role-specific thickness, plus local pods and weapon boxes. Concave polygons preserve the Archon bow fork; transformed crown profiles preserve Cantor's split canted vanes and Archon's sloping keep. They are deliberately coarse collision proxies, not exact SDF/mesh surface parity: bevels, armor-layer detail, pod recesses and some tapered small features remain approximated. Projectile expansion is conservative at prism corners. A future exact mesh collision adapter can refine these details without changing stable IDs or damage rules.

`bodyFrame(forward,up)` is shared with game rendering. Local +X is right, +Y is up, and -Z is forward. Player integrations supply at most twelve local `BodyBox` components and actual bank via `up`; a legacy spherical fallback remains only when no boxes are supplied. Enemy actor orientation is derived from velocity, matching the uploaded rigid visual adapter. `actorHitFraction` exposes the static shaped sweep for independent testing; Combat adds seam and relative-motion handling.

Weapons originate at the authored local stations via `weaponSocket`: Needle alternates ±1.36,-0.02,-3.50 m; Bastion cycles six belly cassettes at ±2.5,-1.10 and z=-3.3,-1.6,0.1 m; Archon uses the siege aperture at 0,0.08,-8.86 m. Fire events report that muzzle position, not the actor center. Physical projectile radii match the gameplay visuals: pulse 0.18 m, lance 0.75 m, bomb 0.45 m; the separate bomb blast radius remains 40 m.

The provided terrain-only fallback samples a segment in sixteen intervals and refines its first ground crossing. That fallback cannot guarantee detection of an arbitrarily narrow intervening ridge; the integration's exact/appropriate bounded sweep callback must supply complete terrain/scenery occlusion for production geometry. Ground-surface endpoints are allowed for scanning and blast LOS. Actor-to-actor ramming, continuously rotating swept volumes, articulated hull collision and exact render-triangle contact are not modeled.

## Mission criteria

- **Recon:** each configured site needs actual scan input, sensor forward-cone alignment (cosine above 0.5), range, LOS, and accumulated valid dwell (default six seconds). Invalid conditions pause accumulation. Merely entering a waypoint does nothing. All scans move the mission to Extract.
- **Defense:** success requires every scheduled wave member spawned, all live hostiles defeated, no live hostile projectiles, and at least one surviving configured site. Losing any required site fails even during extraction. All waves resolved moves the mission to Extract.
- **Strike:** only the configured stable target IDs count. Unassigned kills have no effect. Destroying every assigned target moves the mission to Extract.
- **Extraction:** Recon, Defense and Strike require entering the configured extraction radius after their objective criteria. A player death fails an active/extracting mission. Success/Failure is terminal for mission evaluation, so only one result event is emitted.
- **Practice:** scheduled waves use the same bounded combat and finish after all enemies and hostile projectiles are gone, without extraction or financial settlement.

The integration should freeze or stop the encounter when returning to debrief. Starting a new mission in an existing Combat object is intentionally not a reset.

## Verification

Standalone build and tests (no GPU, renderer or heavy model build):

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/hive_combat_test.cpp src/hive_combat.cpp -o /tmp/hive_combat_test
/tmp/hive_combat_test
```

Current result: 422 checks, zero failures, under both strict `-O2` and production-style `-O2 -ffast-math`. Coverage includes identity/bounds, seeded and grouped-frame determinism, moving-player grouped collision with remainder/cap/seam cases, pause/NaN/large-dt limits, inertia and terrain recovery, all role behaviors, authored socket origins, all-role rotated hull/gap collision, player body proxies, complete-sweep fallback bypass, fail-closed malformed queries, telegraph aim locking, forward-cone charge/burst gating and successful moving-contact reorientation, finite bursts/escorts, nearest occlusion and exact optional contact results, swept fast hits, friendly filtering, shootable bombs, blast falloff and near/far-side wall-impact occlusion, relay cap/LOS/EMP, one-death semantics, recon/extraction, deferred/scheduled defense waves, strike IDs, objective/player failure, seam geometry and reset. Combat, loadout and production world-sweep numeric guards use IEEE-754 exponent-bit validation from `finite_float.h`, because fast-math can optimize away `std::isfinite` checks; NaN, signed infinities, subnormals and maximum finite values have dedicated coverage.

The focused test also passes AddressSanitizer + UndefinedBehaviorSanitizer with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer itself cannot initialize under this executor's ptrace environment; this is not a leak-check pass. Native Windows, integrated flight handling, real collision callbacks, visuals/audio, controller inputs, gameplay balance and frame performance require separate integration QA.
