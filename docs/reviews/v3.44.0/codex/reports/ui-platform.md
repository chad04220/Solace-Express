# Solace Express v3.43.0: UI, input, audio and Windows platform audit

## Scope and provenance

- Released source only: commit `91212bf4a131c02d2da41efbdb09248a09bc3599` in `solace-v343-audit`. No production files changed; no fixes, commits, pushes or later development code used.
- Actual native Linux execution of release Game/UI/ATC/audio functions; surfaceless EGL/OpenGL on llvmpipe (LLVM 19.1.7). UI images are production-widget layout previews with **no live 3D backdrop**. Packaged loading photographs are existing assets, not newly verified scenes.
- Diagnostic UI copy adds only an observation hook recording button IDs, labels and rectangles. The original input handlers, hit testing, draw order, purchase transactions and navigation functions run unchanged. It links all other release `game_objs`. State is isolated under each preview's `isolated-save` directory. Stock harness also runs unchanged against those objects.
- Radio gain probe records the exact gain submitted by release `Game::feedAudio`; native Media Foundation playback is not executed. Voice queue tests decode real release voice assets and run the actual PCM mixer.
- No physical controller, Windows desktop, WASAPI device, NVIDIA/RTX GPU, driver timeout, SSO or real network-stream playback was available. Windows conclusions below are explicitly source inspection, not successful Windows runtime tests. Root owns aggregate build/CTest results; renderer worker owns actual 3D validation.

## Reproduced findings

### UI-01 — Radio overlay pointer input leaks into economic and flight actions [P1]

**Source:** `src/game_ui.cpp:125-161` (button accepts the same mouse press without consuming it), `:517-524` (active hub tab draws before radio), `:982-991` (immediate acquisition transactions).

**Player steps:** Have at least $30,000 and the Wren's required licence, open Hangar, select Wren 180, open Radio, click visible “SomaFM Secret Agent.” At 1920×1080 / 100%, a valid shared hit is `(1607,432)`. At 1280×720 / 140%, `(1062.1,403.2)` reproduces it.

**Expected:** Select a station only. Covered economic controls are inert.

**Actual:** The hidden “Buy new / $30,000” button executes and saves a purchase, then the station is selected in the same draw. Starting fixture money `$1,000,000`, fleet size `0`; after one click money `$970,000`, fleet size `1`, station index `2`. The isolated `career.sav` really contains `money 970000` and the Wren entry. This is not merely a stale display or stub-radio error.

**Evidence:** `logs/ui-platform/interactions-1080.log`, `interactions-720-140.log`; `previews/ui-platform/1080/radio-before.png`, `radio-after.png` (settled frame), `radio-purchase-state.png` (radio closed, ownership/Sell visible). Source-equivalent isolated fixture was independently rerun by root.

**Impact/confidence:** High confidence. Ordinary use can unexpectedly spend career money; other covered immediate transactions share the same architecture. Only the purchase case is claimed as executed.

**Recommendation:** Topmost overlay must own pointer hit testing before underlying controls; exclude/disable covered widgets and prevent one press reaching more than one action. Merely consuming the press in the later radio pass would be too late. Add overlay-on-every-economic-tab regressions.

#### Additional impact of UI-01: radio clicks arm/fire XR-40 weapons

**Source:** `src/game_wraith.cpp:62-65` suppresses mouse fire for `showMap`, but not `showRadio`; release `Game::update` runs flight input before UI drawing. `src/game.cpp:86` also permits the fire action in overlay context.

**Player steps:** Start an airborne XR-40 research flight with weapons safe; open Radio. Left-click a station or other radio control. After the turrets deploy, click another radio control.

**Expected:** Radio clicks do not operate aircraft weapons.

**Actual (actual Game::update):** `radio=1, paused=0, armed=0, shots=0`; first click makes `armed=1`; after two seconds turrets have `laser_deployed=1`; second click produces `shots=1` while the radio remains open and aircraft is uncrashed. The harness supplies the mouse-down/pressed state that a radio click supplies and executes flight logic without GL. It does not claim Windows stream playback or destruction of a particular target.

**Evidence:** `repros/ui-platform/weapon_radio_harness.cpp`; `logs/ui-platform/weapon-radio.log`.

**Impact/confidence:** High confidence. Benign overlay input changes weapon state and can fire in the simulated world.

**Recommendation:** Route overlay pointer events before flight actions and explicitly suppress fire/bomb/camera-drag over interactive overlays. Add mouse-down and repeated-click tests, including already-deployed weapons.


### UI-02 — Large-scale research layout lets hidden test cards intercept Abort [P2]

**Source:** `src/game_research_ui.cpp:157-166,464-465,498-511,628-642`; left card minimum heights overflow the available column, while test rows are processed before the bottom sortie bar. `:437` consumes the first matching hit. Right-hand text uses fixed unbounded placement at `:587-592,624`.

**Player steps:** Set UI scale 140%, use a 1280×720 window (the same height-normalized issue applies to equivalent aspect/scale), open the research terminal, select XR-40, click the visible Abort button at `(96.6,620.6)`.

**Expected:** Return to main menu. Test cards and sortie controls occupy separate reachable areas.

**Actual:** The hidden XR40-2 “VERTICAL FLIGHT” row consumes the click: `screen=4` (research) remains unchanged and `resCard=-1→10`, rather than `screen=0` (menu). The test-card list is visibly behind the sortie bar; several specification values also extend past the right edge. At 1920×1080/100%, the same geometric Abort target correctly returns to menu, providing a control case. Escape remains a workaround.

**Evidence:** `logs/ui-platform/interactions-720-140.log`; `previews/ui-platform/720-140-diagnostic/research-abort-before.png` and `research-abort-after.png`; stock `720-140/research40.png`. Stock `interactions` passes at this exact scale because it checks airframe selection and other bottom chips but not this overlapping test-card/Abort interaction.

**Impact/confidence:** High confidence. A visible cancellation action does a different hidden action; challenge selection and text are obstructed.

**Recommendation:** Reserve real vertical space, scroll/collapse the test-card area, bound/wrap specifications, and clip both drawing and pointer hit regions. Test hit targets at all supported scale extremes, including cancellation.

### UI-03 — Settings toggles lose keyboard/D-pad focus after changing their label [P2]

**Source:** `src/game_ui.cpp:127` includes label text in the button ID; `:1370-1373` changes that label when toggled. `src/game.cpp:1857` selects the first prior button when the old ID disappears.

**Player steps:** In Settings/General, use arrow keys or D-pad to focus Head-look (“Leans into turns”), activate with Enter/A, wait two normal frames, then activate again.

**Expected:** Focus remains on the same setting; another activation toggles it back.

**Actual:** Head-look first changes true→false, but focus moves to the hub Radio button. The second Enter opens Radio and leaves Head-look false. Reproduced with keyboard and D-pad/A at both 1080/100% and 720/140%. The harness reaches the target using only real directional navigation, not a seeded focus ID.

**Evidence:** `logs/ui-platform/interactions-1080.log`, `interactions-720-140.log`; `previews/ui-platform/1080/toggle-before.png`, `toggle-after.png`.

**Impact/confidence:** High confidence. Repeated controller/keyboard changes act on the wrong control. Other label-changing toggles use the same ID construction.

**Recommendation:** Give widgets stable semantic IDs independent of labels, display values and viewport coordinates. Include repeated toggle and UI-scale-change navigation regressions.

### AU-01 — Master volume does not mute internet radio [P2]

**Source:** `src/game.cpp:3581-3585`; master is sent only to the mixer while radio gain omits it. `src/radio_win.cpp:63-66` sends that independent gain directly to Media Foundation. Station-selection and volume-button paths also set the raw radio gain (`game_ui.cpp:1563,1576,1578`).

**Player steps:** Play radio with Radio volume at 60%, set Master volume to zero.

**Expected:** Master volume mutes every sound, including internet radio.

**Actual:** Release `feedAudio` submits `radio_gain=0.600000` with master=1 and still `0.600000` with master=0. The game mixer itself applies master at `audio.cpp:511`; radio bypasses that mixer. Native audible playback was not available, but the exact numeric gain and Windows destination path are verified.

**Evidence:** `repros/ui-platform/audio_contract_harness.cpp`, `radio_gain_probe.cpp`; `logs/ui-platform/audio-contracts.log`.

**Impact/confidence:** High confidence in control-path defect; native audio device effect is source-supported rather than physically heard.

**Recommendation:** Centralize effective radio gain = master × radio setting × speech duck; apply it consistently on station start, settings changes and overlay buttons.

### AU-02 — Pausing during speech leaves the radio permanently ducked in menus [P2]

**Source:** `src/game.cpp:3521-3522` cancels speech and returns before `voiceDuck` can recover at `:3570`; `feedAudio` continues using the stale value at `:3584`.

**Player steps:** With music playing, pause during a spoken line, remain paused, or leave the flight for the main menu.

**Expected:** Speech is cancelled and music returns smoothly to the configured level.

**Actual:** Using a decoded release STALL recording, `voice_busy=1, duck=0.997521, radio_gain=0.210967`; after 10 seconds paused, and another 10 seconds in menu, voice is no longer busy but duck and gain remain exactly unchanged. Configured radio volume remains 0.6. Only later live-flight comms updates can release it; pause/menu never does.

**Evidence:** `logs/ui-platform/audio-contracts.log`. Real release AtcVoice and mixer state, observation-only radio gain output.

**Impact/confidence:** High confidence. Music remains unexpectedly reduced to about 35% of its configured amplitude after an interrupted call.

**Related interruption defect:** Switching stations during steady ducking bypasses the cached gain. With `voiceDuck=1`, `feedAudio` submits0.21; the same raw `setVolume(0.6)` used by station selection raises it to0.6; another `feedAudio` leaves it there because its static `sentVol` still equals the desired0.21. The radio is no longer ducked during that speech. This exact control-path sequence is also in the audio harness/log.

**Recommendation:** Advance/reset ducking on every screen, including the branch that cancels comms, and make all radio-volume mutations go through the same effective-gain owner/cache. Test pause, abandonment, main-menu return, station switch and restart during speech.

### AU-03 — Recovered hazards remain in the voice queue and speak late [P2]

**Source:** `src/game.cpp:3543-3547` queues warnings without validity metadata/group; `:1002` only rejects transmissions whose nonnegative key mismatches current ATC state. `src/atc.cpp:303-316,324-330` queues same-priority hazards and starts them later.

**Player-reachable flow:** One urgent warning is speaking; a second hazard (e.g. low-altitude fast descent → PULL UP) occurs briefly, then the player recovers before that queued warning starts.

**Expected:** Stale warnings are removed or revalidated immediately before playback.

**Actual:** Harness starts a real STALL clip, raises the real PULL UP condition for one update (airborne, low AGL, -10m/s), then restores altitude and +3m/s climb. Actual mixer and comms updates later record `PULL UP` in started history while the plane is ~1958m AGL and climbing. No condition remains to justify it. This test isolates comms conditions, not flight-model recovery performance.

**Evidence:** `logs/ui-platform/audio-contracts.log`, `audio_contract_harness.cpp`; real voice assets decoded and audio drained through `g_audio.render`.

**Impact/confidence:** High confidence. A time-sensitive auditory cue can contradict the current flight and distract the player. Tower state validation exists but does not cover these hazard messages.

**Recommendation:** Attach a warning identity and a live predicate/expiry; revalidate immediately before start, and deduplicate repeats by warning identity.

### UI-05 — Keyboard/D-pad cannot reach the binding cells [P3/accessibility]

**Source:** `src/game_ui.cpp:1325-1334` (Settings tabs), `:1490-1523` (binding cells) use mouse-only hovered/click logic without registering a focus target. Hub tabs are also mouse-only in `:499-509`; controllers have a separate shoulder shortcut, keyboards do not.

**Steps:** Open Controls from the main menu with the keyboard, then try arrows/Tab/Enter to reach a binding; or use only D-pad/A on that page.

**Actual:** The entire Controls hub registers only three focusable widgets: Radio, Main Menu, Reset to defaults. No key/button cell or General/Controls tab is reachable through focus navigation. A controller left-stick cursor or real mouse works, so this does not block all controller access.

**Evidence:** Both diagnostic interaction logs enumerate the real focus registry; `1080/controls-focus.png`.

**Recommendation:** Register semantic focus IDs for tabs/cells and make the focused row scroll into view. Test rebinding/cancel/clear by keyboard and D-pad as well as the pointer.

### UI-06 — Narrow windows can permanently clip right-side settings [P2 at narrow windows]

**Source:** `src/game_ui.cpp:70` bases S on height; Settings panel narrows to the window at `:522`, but fixed row offsets at `:1356-1367,1384,1394` do not reflow. Only vertical scrolling exists. Win32 creates a resizable window and does not impose a corresponding minimum client width.

**Steps:** Resize the client to 800×600, set UI scale 140%, open Settings.

**Actual:** The Radio toolbar button covers SETTINGS; the Bank header overlaps the title; the 67% option and 144fps are partially clipped, and 240fps is beyond the Settings hit clip, with no horizontal scroll. This is visible in the unchanged stock UI capture, not a constructed text-size model. Keyboard can still navigate some clipped targets, which is not an adequate visible pointer UI.

**Evidence:** `previews/ui-platform/800-600-140/settings.png`, `logs/ui-platform/stock-800-600-140.log`. 1024×768/100% and standard 16:9 sizes are control cases with usable visible row groups. No assertion about a documented minimum resolution is made; no minimum is enforced by this window path.

**Recommendation:** Either enforce/document a minimum effective client width or reflow/wrap row groups and header chips based on both width and scale.

### UI-07 — FOV setting's value is clipped even at default 1080p scale [P3]

**Source:** `src/game_ui.cpp:1367,1412` renders a long value at x+556S without fitting/wrapping; only about156S remains in the general-settings panel. It is cut by the surrounding UI clip.

**Steps:** At1920×1080/100%, scroll General Settings to Field of view.

**Actual:** “55 deg outside, 74 in the cockpit” is clipped after its beginning. The cockpit FOV explanation is unavailable in full even on the ordinary default-size layout.

**Evidence:** `previews/ui-platform/1080/toggle-before.png` shows the actual FOV row in the settled scrolled view.

**Recommendation:** Put long slider descriptions on a second wrapped line or fit them to the measured remaining width.

## Conditional corrupt-asset hardening finding

### AU-04 — Truncated/empty WAV chunks are not safely rejected [P2 if assets are damaged/replaced]

**Scope distinction:** This is not a failure in a normally installed release recording. All1068 indexed release clips decode. It requires an externally corrupted, truncated or replaced WAV referenced by an index entry. The assets are loaded from disk on first use.

**Source:** Exact, unmodified `src/atc.cpp:212-217` reads fmt fields without checking the chunk's or buffer's length; `:223,233-238` underflows `in.size()-1` when there are zero decoded PCM samples.

**Reproduction:** Isolated tiny fixture directories contain a single `fixture` index entry and WAV. Compile unchanged `atc.cpp` and `audio.cpp` with ASan, UBSan and float-cast-overflow checks; call the public `AtcVoice::decodes("fixture")`.

**Expected:** Invalid/truncated files return false without memory errors.

**Actual:**
- Valid one-sample PCM: exit0, `decoded=1` (control).
- Zero-byte file: exit0, `decoded=0` (graceful rejection control).
- Valid header/fmt with zero-length data: exit1, `atc.cpp:234:39: runtime error: 1.84467e+19 is outside the range ... long unsigned int`.
- One-byte/truncated16-bit PCM sample: same undefined conversion at234.
- Truncated fmt chunk: exit1, ASan `heap-buffer-overflow`, one-byte read3 bytes beyond a31-byte allocation, stack `atc.cpp:212→readWav:217→AtcVoice::pcm:250→decodes:243`.

**Evidence:** `repros/ui-platform/wav_decoder_harness.cpp`, `make_wav_fixtures.py`, `wav-fixtures/`, `run_wav_sanitizers.sh`; `logs/ui-platform/wav-*.log`. Root independently reproduced the truncated-fmt ASan failure (expected exit1): `logs/ui-platform/root-wav-truncated.log`. No production voice file was changed.

**Impact/confidence:** High confidence in parser memory/undefined-behavior defects, conditional asset-hardening scope. No claim of a normally shipped clip failure or observed Windows executable crash.

**Recommendation:** Validate chunk-end and minimum fmt length before field reads; require nonempty whole samples and positive sample rates; bound resampled-size arithmetic before allocation. A decode failure should leave the existing subtitle fallback usable.

Portable command after setting SOURCE_DIR/REPRO_DIR as below:

```sh
bash "$REPRO_DIR/run_wav_sanitizers.sh" /tmp/v343-wav-reproduced
```

## Positive/negative coverage

### Unchanged stock UI harness: all passed in five size/scale configurations

-1920×1080/100%;1280×720/140%;1024×768/100%;2560×1080/100%;800×600/140%.

At each configuration the unchanged `interactions` batch passes:
- Original XR-30 default and three repeated keyboard/mouse cycles through all four research airframes; incompatible test reset; site wrap in both directions; runway/airborne/weather chips; Escape return.
- Hangar/airline offscreen action focus identity and automatic vertical scrolling.
- Loading readiness requires distinct completed frames, and an absent optional shadow shader does not block readiness.
- All13 hangar catalog rows via keyboard and D-pad/A; all4 classified pointer targets; no classified economic controls; unchanged career state while selecting.
- All9 Free Flight airframes and16 airports; runway/airborne/Escape; correct selected preview model despite conflicting career selection; unchanged career state.
- CPU projection/geometry checks for all13 hangar aircraft endpoints, grounded tyre supports and parked prop state. These are **not evidence that those meshes rendered**.

The stock tests' passing result does not negate the new overlay, focus-label, research Abort or clipping failures. Those interactions are outside their assertions.

### Layout inspection

73 captured PNGs (including diagnostic and contact-sheet views; losslessly recompressed from323,746,329 to17,665,461 bytes, exact pixel equality verified) cover main menu/no-save/saved/overwrite, hangar classified entry, Free Flight setup, Settings, Controls, pause, all four research variants, progress/ready loading and intro. Key visuals inspected at full size. Main-menu and ordinary pause action groups remain within the tested screens. Intro/progress/ready copy is distinct and readable in the sampled standard-size captures; 57% does not display as100%. UI-only context is retained in logs and this report.

### Audio and input positive checks

Root's stock `LastTest.log` confirms all1068 indexed recordings decode, with0 failures (lines347 and457). There are1078 physical WAVs, including10 unindexed extras; extras alone are not a defect. Real STALL asset resolves/decodes; actual PCM mixer advances and terminates speech. Pause cancels `voiceBusy` immediately, though duck recovery fails as described. Keyboard and D-pad do reach ordinary settings controls and scroll them into view before the dynamic-ID failure. Radio scroll selection itself changes the selected station as intended, but click ownership fails. Existing cockpit camera/focus tests are part of root's aggregate suite; no separate physical-controller or rendered-display sharpness claim here.

## Windows source-inspection audit and unverified risks

These are reviewed paths/risks, not native runtime passes or additional reproduced failures:

- First-run shader helper creation is conditional on a valid shared GL context (`platform_win32.cpp:588`), preventing the hypothesized unjoined-child-thread path when ctx2 creation fails. Worker waits for/joins reader; helper has a300s timeout; failed optional work is skipped separately from completed main shader work (`:626-635,741-759,780-799`). Main-context compile fallback exists. The hypothesized ctx2-null helper-thread crash was rejected.
- Loading UI is separated onto its own unshared context/process arrangement; unsupported intro context falls back to main-thread drawing. `stopIntro` joins and pumps messages (`:651-739`). A responsive intro does not prove shader compilation finishes on real drivers.
- On WM_CLOSE during main/shared-context compilation, main breaks out of the responsive loop and synchronously joins compileThread (`:777-781`); only the optional helper process is terminated. If the driver's main compile is blocked, close can remain blocked with it. Recommend a native slow/hung compile close/cancel test; not executed here. `game.quit` is also a plain bool read by a worker, worth synchronization review.
- `error.log` is deleted only after successful renderer init on normal starts, before prewarm; tool runs preserve it (`:815-823`). Fatal renderer paths write fresh logs (`:522-526`). Successful deletion was inspected, not run on Windows. Early context/GL-entry/texture-unit failures return before that fatal logger, so support logs for those early failures are less complete.
- Audio backend probes WASAPI then falls back to waveOut (`:279-295`); WASAPI tracks default-device changes once a second and retries absent devices (`:261-274`). The normal loop ignores the `startAudio()` result and logs a backend name even if both devices fail (`:1343-1344`); native absent-device/unplug/replug/default-switch tests are still required. No device latency or recovery success claim.
- `Radio::stop` shuts down/releases player and callback; stream-open/MF errors and a20s connection timeout set visible status (`radio_win.cpp:39-77`). Native stream switch/timeout/rapid stop-start was not executed.
- Media Foundation callback writes `volatile event` and `lastHr` without an atomic/mutex handoff while the main poll reads them (`radio_win.cpp:14-26,72-74`); error sets event before lastHr. Treat as a concurrency risk requiring a synchronized message/state handoff and Windows callback stress test. No observed crash claimed.
- XInput only polls slot0 (`platform_win32.cpp:315`), clears axes/buttons when that slot disconnects, and Game pauses on pad loss. No physical multi-controller/slot-change/hotplug verification. WM_KILLFOCUS clears held keyboard/mouse state and asks Game to pause (`:111-114`).
- Paths use Win32 A APIs, MAX_PATH buffers and narrow fopen for saves/cache; non-ACP/non-ASCII usernames and long install/profile paths need native validation. No actual affected-user save failure claimed.

## Essential screenshots for the delivered bundle

Keep the full73-image matrix locally. Nominate these7 images for a small review bundle (all UI-only previews, no live3D):

1. `previews/ui-platform/1080/radio-before.png`: visible station covers purchase action.
2. `previews/ui-platform/1080/radio-purchase-state.png`: settled970k bank and owned Wren/Sell action after one station click.
3. `previews/ui-platform/720-140-diagnostic/research-abort-before.png`: overlapping test cards/sortie bar and clipped specifications.
4. `previews/ui-platform/720-140-diagnostic/research-abort-after.png`: Abort remained in research, selected hidden test.
5. `previews/ui-platform/1080/toggle-before.png`: Head-look focus before toggle and clipped FOV value.
6. `previews/ui-platform/1080/toggle-after.png`: focus has jumped to Radio.
7. `previews/ui-platform/800-600-140/settings.png`: narrow-window header/settings clipping.

## Reproduction commands/artifacts

These scripts are portable to a native Linux checkout with EGL/Mesa support. Set explicit checkout/build locations; generated includes use include search paths rather than the audit machine's absolute paths. The preparation script verifies the exact release commit and a clean source diff, and requires each observation-hook anchor to occur exactly once.

```sh
export SOURCE_DIR=/path/to/Solace-Express
export BUILD_DIR=/path/to/native-release-build
export REPRO_DIR=/path/to/extracted-audit/repros/ui-platform
# SOURCE_DIR HEAD must be91212bf4a131c02d2da41efbdb09248a09bc3599.
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target game_objs -j1
bash "$REPRO_DIR/build_repros.sh"
bash "$REPRO_DIR/run_repros.sh" /tmp/v343-ui-reproduced
```

The builder verifies CMake's recorded source directory and the32 release objects before linking. `prepare_harness.py` creates only diagnostic files beside itself; production files stay untouched. `run_repros.sh` covers1080/100%,720/140%, audio/weapon repros and the unchanged stock interactions. For the other matrix cells, run the stock executable from SOURCE_DIR using the size arguments in `logs/ui-platform/stock-*.log`.

Stock main is `repros/ui-platform/stock_ui_review_harness`, linked from unchanged `tests/ui_review_harness.cpp` plus every game object and `radio_stub.cpp`. PNG dimensions andSHA256 hashes are in `logs/ui-platform/png-manifest.json`. Final production `git status --short` remained empty; commit recorded in `logs/ui-platform/source-clean.txt`.
