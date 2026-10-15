# Spatial audio pass — October 2026

The audio listener follows the final camera position and orientation, including cockpit head turns, external views, debug cameras and the wreck scene. All playback routes enter one stereo mixer. Headphones give the clearest directional cues; ordinary stereo speakers remain supported.

| Sound | Emitter and behavior |
| --- | --- |
| Player engines | Each of up to four actual engine mounts/nozzles; independent synthesis, health and pod thrust, relative Doppler, exhaust direction, cabin filtering and ANR |
| Traffic and hostile aircraft | Stable world emitters selected by audibility; engines move continuously past the listener instead of firing a canned flyby clip |
| Wind, rain, ground roll | Separate canopy/gear sources; airspeed, sideslip, turbulence, transonic buffet, gear/flaps and surface roughness drive the layers |
| Crash and debris | Each contact emits its own impact, metal scrape, water splash or fuel blast. Waveforms, resonances and envelopes vary per event. Burning wreck parts have local fire ambience |
| Weapons and UFO | Muzzle, hit, payload and tracked UFO positions; separate cannon, energy pulse, charged heavy shot, EMP, conventional blast and plasma sounds |
| Thunder and sonic booms | Thunder waits for the sound to travel from the strike. A sonic boom reaches an external observer when the Mach cone crosses them; onboard Mach 1 produces buffet and a spoken callout |
| Speech and warnings | Character-specific seat/panel positions; comms remain intelligible with ANR. External views retain nearby virtual headset emitters |
| Radio music | Media Foundation decodes on a background worker; stereo channels feed two spatial speakers in the same mixer |
| UI | Near-field virtual sources; standard buttons and keyboard-focus controls use their screen positions |

Distance uses pressure falloff outside each emitter's physical radius, air absorption and a smooth inaudibility range, with **no minimum-volume floor**. Binaural rendering adds equal-power panning, cross-ear delay, head shadow, and simple rear/elevation filtering. It is an analytic stereo head model, not a measured/personalized HRTF, room simulation or multichannel surround renderer. Terrain/building diffraction is not simulated.

The mixer coalesces simultaneous duplicate events at the same location, preserves separate parts, and bounds active voices. Quiet tails yield to nearer sounds. Flight changes and pauses cancel delayed world events; coordinate wraps preserve their positions. A stereo-linked peak limiter caps output at 0.92 while leaving the normal mix linear. Existing master, engine, effects, voice and radio controls retain their roles.

## Speech coverage and assets

The original nine characters/controllers remain. Two additional **distinct stock voices**, not used by that cast, cover 54 supplemental cues:

- Flight operations: Kokoro `af_heart`.
- Military control: Kokoro `am_puck`.

The new cues cover combat warnings/objectives/extraction/results, free-flight and regional navigation, previously silent autoland exceptions, custom control bindings, runway overrun, stuck gear/asymmetric flaps, supersonic flight and flight outcomes. Research-card and lesson instructions continue to use their existing recordings. Pure menu labels, developer diagnostics and continuously changing instrument numbers are not automatically narrated.

`assets/voice/audio_pass.json` records exact spoken words, display-message patterns, priorities, stock voices, model/file hashes and durations. The spoken version of a dynamic regional-airport message may be concise and refer to the displayed runway or waypoint; the full dynamic name remains visible. A single explicit outcome can finish after flight end; airborne calls do not leak into menus. Duplicate/stale calls expire, and a missing audio fragment refuses the entire instruction rather than speaking a partial clearance.

New recordings are 24 kHz mono PCM16 with modest bandwidth shaping, level matching and edge fades. Production playback uses precomputed windowed-sinc phases for resampling, avoiding expensive trigonometry per sample on the game thread. Fragment gaps are trimmed without cutting word boundaries. `voice_index_extra.txt` is loaded after the imported index, so importing the original packs preserves these cues. The existing Windows packaging copies the entire voice directory.

`audio_pass_validation.json` contains independent, unprompted local speech-recognition transcripts and signal measurements for every new file. It explicitly records that automated checks are **not human listening approval**. No synthesis model, Python package or speech-generation service is needed by the game.

## Reproduction

Authoring was performed with Python 3.12, `kokoro-onnx==0.6.1`, `onnxruntime==1.31.0`, `numpy==2.3.5`, `scipy==1.17.0`, and `faster-whisper==1.2.1`. Obtain the local authoring models separately:

- [Kokoro ONNX v1.0 model files](https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0): `kokoro-v1.0.onnx` and `voices-v1.0.bin`; exact hashes are in the manifest.
- [Kokoro model and voice provenance](https://huggingface.co/hexgrad/Kokoro-82M). Existing licenses are in `assets/voice/licenses/`.
- ASR model/revision/file hashes: `tools/voice_lesson_helpers/asr-model.json`.

```sh
python tools/voice_audio_pass_render.py /local/kokoro-v1.0.onnx /local/voices-v1.0.bin
python tools/voice_audio_pass_validate.py /local/faster-whisper-small.en
cmake -S . -B build
cmake --build build --target audio_spatial_test audio_voice_test gameplay_test military_gameplay_test
ctest --test-dir build -R '^(audio_spatial|audio_voice|gameplay_loop_1|military_gameplay)$' --output-on-failure
```

Both authoring scripts import the existing fail-closed Linux network guard before inference libraries. ONNX telemetry is disabled before runtime initialization. Models must already be present locally; inference has no network access. The renderer regenerates changed text/voice presets or damaged/missing files and updates hashes. Re-run validation whenever a recording changes.

## Verification and remaining listening check

`audio_spatial_test` renders production PCM and checks source direction under head turns, near/far attenuation, travel time, rear/elevation cues, explosion variation, intensity scaling, wrapped coordinates, stale-event cancellation, Doppler/Mach-cone geometry, all four engine mounts and spool-down, independent wind layers, speech at 44.1/48/96 kHz, volume controls, bounded radio buffering/station cancellation, resampling across packet boundaries and dense-mix peak safety.

`audio_voice_test` decodes every indexed recording and checks operational coverage, duplicate suppression, urgent interruption, stale expiry, cancellation and missing-fragment handling. The game-loop suite also checks that an explicit outcome survives the flight-end transition and is canceled when leaving its screen. The military suite exercises actual mission transitions and combat events.

The complete Windows executable was cross-compiled with MinGW GCC 13. Native WASAPI/waveOut output, live station playback and subjective headphone/speaker balance still need a Windows listening session. A useful smoke test is a quad-engine cockpit head turn, external traffic pass, high-speed flyby viewed from the ground, a breakup followed from near and far, a storm, and switching/pause/muting during speech and music. The automated measurements do not substitute for that listening check.

## Stereo audition

[Play or download the 25-second preview](audio-pass-preview.mp3). This is rendered by the production mixer, then encoded to 256 kbps MP3 for review:

| Time | Content |
| --- | --- |
| 0–3 s | Piston engine, canopy airflow and operations voice |
| 3–6 s | Turboprop engine and airflow |
| 6–10 s | Four research engines viewed from a moving external camera |
| 10–13 s | Aircraft passing left to right with relative Doppler |
| 13–18 s | Near-left blast/metal, right impact, distant blast and water splash |
| 18–20 s | External double-shock sonic boom |
| 20–25 s | Military controller's extraction instruction |

Recreate the lossless source with `cmake --build build --target audio_preview`, then `build/audio_preview assets/voice preview.wav`. The preview is an audition of these scenarios, not a recording of native Windows device playback.

Local validation for this pass: 46 spatial/DSP behavior checks; all 1,122 indexed clips decoded; 54 supplemental recordings screened by unprompted ASR with 0 normalized word edits across 397 words; gameplay-loop and military regression suites passed. The audio suites also passed AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer could not inspect this host's isolated `/proc` and was disabled, matching the repository's CI setting. Windows x64 compilation and linking passed with MinGW GCC 13.
