# Windows / RTX 3070 acceptance checklist

Target: native 1920×1080, no reduced internal resolution, the user's intended quality preset. Record GPU model (desktop/laptop), driver, CPU/RAM, release hash, VSync/cap and settings with every result. Use a disposable career copy.

## Frame rate and startup

- Measure CPU and GPU frame times independently, not only the capped FPS overlay. The 60 FPS budget is 16.67 ms; capture median, p95, p99, worst frame and frame-time trace.
- Test cold shader/mesh/cache startup and warm startup separately, including loading progress, fallback compilation, cancellation and closing while compilation runs.
- Capture at least a representative uninterrupted route per stress scene: dense city low pass, low-altitude foliage/coast, large airport approach, cockpit at far-map coordinates, traffic-heavy flight, and fully deployed/reheated research craft.
- Compare XR-40 partial/full cloak on/off, exterior and cockpit, motion across nearby/distant terrain. Inspect transient edges and TAA ghosting, not just still images. Record cloak buffers/VRAM and driver shader time.
- Exercise city-to-city streaming and rapid turns long enough to see shader, mesh and world-cache reuse. Record stalls separately from steady frame time.
- Do not claim a minimum 60 FPS from an average near60. Provide worst observed frame times and the exact sampled route/settings.

## Input / UI

- Radio over Hangar and flight: every click should reach only the visible overlay; no purchases, weapon fire, throttle changes or camera drag underneath.
- Repeat keyboard/D-pad toggles and rebinding; focus must stay stable after label changes and scroll into view.
- Test native1080 and supported UI-scale extremes; resize to narrow windows and check research Abort/test-card clipping, long values and tooltips.
- Disconnect/reconnect an Xbox controller in flight; test a controller assigned to a nonzero XInput slot, and verify pause and cleared held inputs after focus loss.

## Audio and platform

- Radio with Master0, station switching, pause/exit during speech, device unplug/default-device switch, missing audio device and network interruption.
- Trigger a brief hazard while another line speaks, recover, and verify stale warnings do not start afterward.
- Test a non-ASCII Windows username and install/save paths; exercise read-only/full save destination and ensure failed progress writes are visible without losing the last valid data.

## Gameplay regression acceptance

- Low-altitude Split-S must be refused or recovered safely using actual authority and current energy; higher entry-altitude controls should still complete.
- Block a runway before and after clearance. Engaged autoland must follow a feasible go-around; hold clearance must not expire into an occupied runway.
- Divert a survey before and after its checkpoints, resume with the same capable aircraft, and verify quoted remaining route, fuel and carried compliance.
- Bomb a flat patch, land over it, then make eight separated blasts and revisit; collision must agree with the visible persistent state.
- Destroy garden/farm-edge entities, leave their chunks and return; both rendering and collision should stay removed for the intended persistence lifetime.

These checks are proposed acceptance work for the user's actual Windows/GPU environment. They are not recorded passes from this VM.
