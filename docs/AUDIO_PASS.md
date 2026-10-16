# Spatial audio pass — October 2026

The audio listener follows the final camera position and orientation, including cockpit head turns, external views, debug cameras and the wreck scene. All playback routes enter one stereo mixer. Headphones give the clearest directional cues; ordinary stereo speakers remain supported.

| Sound | Emitter and behavior |
| --- | --- |
| Player engines | Each of up to four actual engine mounts/nozzles; independent synthesis, health and pod thrust, relative Doppler, exhaust direction, cabin filtering and ANR |
| Traffic and hostile aircraft | Stable world emitters selected by audibility; engines move continuously past the listener instead of firing a canned flyby clip |
| Wind, rain, ground roll | Separate canopy/gear sources; airspeed, sideslip, turbulence, transonic buffet and gear/flaps drive the layers. Paved, grass, gravel, snow and sand contacts have distinct textures |
| Crash and debris | Each contact emits its own impact, metal scrape, water splash or fuel blast. Waveforms, resonances and envelopes vary per event. Burning wreck parts have local fire ambience |
| Weapons and UFO | Muzzle, hit, payload and tracked UFO positions; separate cannon, energy pulse, charged heavy shot, EMP, conventional blast and plasma sounds |
| Thunder and sonic booms | Thunder waits for the sound to travel from the strike. A sonic boom reaches an external observer when the Mach cone crosses them; onboard Mach 1 produces buffet and a spoken callout |
| Speech and warnings | Character-specific seat/panel positions; comms remain intelligible with ANR. External views retain nearby virtual headset emitters |
| Radio music | Media Foundation decodes on a background worker; stereo channels feed two spatial speakers in the same mixer |
| UI | Near-field virtual sources; standard buttons and keyboard-focus controls use their screen positions |

Distance uses pressure falloff outside each emitter's physical radius, air absorption and a smooth inaudibility range, with **no minimum-volume floor**. Binaural rendering adds equal-power panning, cross-ear delay, head shadow, and simple rear/elevation filtering. It is an analytic stereo head model, not a measured/personalized HRTF, room simulation or multichannel surround renderer. Terrain/building diffraction is not simulated.

The mixer coalesces simultaneous duplicate events at the same location, preserves separate parts, and bounds active voices. Quiet tails yield to nearer sounds. Flight changes and pauses cancel delayed world events; coordinate wraps preserve their positions. A stereo-linked peak limiter caps output at 0.92 while leaving the normal mix linear. Existing master, engine, effects, voice and radio controls retain their roles.

## Speech coverage and assets

The original nine characters/controllers remain. Two additional **distinct stock voices**, not used by that cast, cover 63 supplemental cues:

- Flight operations: Kokoro `af_heart`.
- Military control: Kokoro `am_puck`.

The new cues cover combat warnings/objectives/extraction/results, free-flight and regional navigation, previously silent autoland exceptions, custom control bindings, runway overrun, stuck gear/asymmetric flaps, supersonic flight, flight outcomes and every branch of landing coaching. The Wraith startup instruction now displays the current flap/pod bindings and speaks a binding-independent instruction. Research-card and lesson instructions continue to use their existing recordings. Pure menu labels, developer diagnostics, save/asset errors, paused controller-disconnection notices and continuously changing instrument numbers are not automatically narrated.

`assets/voice/audio_pass.json` records exact spoken words, display-message patterns, priorities, stock voices, model/file hashes and durations. Generated airport names use the actual field identifier spoken in controller phonetics; prerecorded names retain their existing voices. Regional wind changes speak the exact runway even when the airport has no name recording. Dynamic guidance and coaching may give a concise spoken instruction while the display retains the full measurements/name. An explicit outcome and its landing feedback can finish on the debrief screen; leaving it cancels both. Airborne calls do not leak into menus. Duplicate/stale calls expire. `AtcVoice::kPause` marks intentional sentence gaps; an unresolved empty fragment refuses the whole transmission instead of silently omitting part of a clearance.

New recordings are 24 kHz mono PCM16 with modest bandwidth shaping, level matching and edge fades. Production playback uses precomputed windowed-sinc phases for resampling, avoiding expensive trigonometry per sample on the game thread. Fragment gaps are trimmed without cutting word boundaries. `voice_index_extra.txt` is loaded after the imported index, so importing the original packs preserves these cues. The existing Windows packaging copies the entire voice directory.

`audio_pass_validation.json` contains independent, unprompted local speech-recognition transcripts and signal measurements for every new file. The validator verifies the pinned ASR model and metadata hashes and exits unsuccessfully on transcript/signal failures. Case, punctuation, hyphenation and centreline/center line spelling are normalized. It explicitly records that automated checks are **not human listening approval**. [The compressed complete asset audit](audio-asset-audit.json.gz) records hashes, format, duration, peak, RMS and DC for all 1,131 indexed recordings; its check also rejects index/manifest/ASR drift. Regenerate a readable copy with `python tools/audio_asset_audit.py --report audit.json`. No synthesis model, Python package or speech-generation service is needed by the game.

## Reproduction

Authoring was performed with Python 3.12, `kokoro-onnx==0.6.1`, `onnxruntime==1.31.0`, `numpy==2.3.5`, `scipy==1.17.0`, and `faster-whisper==1.2.1`. Obtain the local authoring models separately:

- [Kokoro ONNX v1.0 model files](https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0): `kokoro-v1.0.onnx` and `voices-v1.0.bin`; exact hashes are in the manifest.
- [Kokoro model and voice provenance](https://huggingface.co/hexgrad/Kokoro-82M). Existing licenses are in `assets/voice/licenses/`.
- ASR model/revision/file hashes: `tools/voice_lesson_helpers/asr-model.json`.

```sh
python tools/voice_audio_pass_render.py /local/kokoro-v1.0.onnx /local/voices-v1.0.bin
python tools/voice_audio_pass_validate.py /local/faster-whisper-small.en
python tools/audio_asset_audit.py
cmake -S . -B build
cmake --build build --target audio_spatial_test audio_voice_test gameplay_test military_gameplay_test
ctest --test-dir build -R '^(audio_spatial|audio_voice|audio_assets|gameplay_loop_1|gameplay_loop_2|military_gameplay)$' --output-on-failure
```

Both authoring scripts import the existing fail-closed Linux network guard before inference libraries. ONNX telemetry is disabled before runtime initialization. Models must already be present locally; inference has no network access. The renderer regenerates changed text/voice presets or damaged/missing files and updates hashes. Re-run validation whenever a recording changes.

## Verification and remaining listening check

`audio_spatial_test` renders production PCM. Every one of the 28 one-shot effect types is checked for direction, finite/bounded output, audibility and clean termination at 44.1/48/96 kHz. Additional checks cover head turns, near/far attenuation, travel time, rear/elevation cues, explosion variation, intensity scaling, wrapped coordinates, stale-event cancellation, Doppler/Mach-cone geometry, all four engine mounts and spool-down, independent wind layers, ground materials, silent tyre spin-up on stationary VTOL touchdowns, speech, volume controls, bounded radio buffering/station cancellation, packet continuity and dense-mix peak safety. Downsampling checks require over 40 dB rejection of out-of-band tones; the sinc window grows with the rate ratio instead of aliasing high-rate audio through a fixed short filter.

`audio_voice_test` decodes every indexed recording and checks operational/coaching coverage, generated airport identifiers, bound-control variants, duplicate suppression, urgent interruption, stale expiry, cancellation, missing-fragment handling and explicit pauses. The game-loop suite checks exact radio mute during duck release and outcome/feedback playback across the inter-transmission gap. The military suite exercises actual mission transitions and combat events.

Both voice indexes accept LF and CRLF line endings; Windows editors/checkouts cannot accidentally append carriage returns to regex patterns, lookup values or lesson mission IDs. A regression fixture verifies actual clip decoding through both forms of index.

The focused `Audio regression` workflow runs sanitizer/DSP/voice/asset checks on Linux, and DSP/voice/asset plus `audio_radio_test` on native Windows, independently of aircraft baking. The Windows decoder test creates local stereo WAVs at 44.1 and 96 kHz and checks decoder output, spatial channels, rapid replacement, stop, mute and shutdown without a network connection or audio device. Surround input is folded to stereo by Media Foundation using its channel layout; format-change notifications are revalidated rather than treated as a station failure. Music volume changes have a short smoothing ramp.

The complete Windows executable and native radio test cross-compile with MinGW GCC 13. Native WASAPI/waveOut device output, live station playback and subjective headphone/speaker balance still need a Windows listening session. For Claude's review, listen to a quad-engine cockpit head turn, external traffic pass, ground-observed high-speed flyby, breakup from near/far, storm, each runway surface, and switching/pause/muting during speech/music. Check the Wraith prompt after rebinding and the landing feedback on debrief, then leave the screen mid-call to verify cancellation. Automated measurements and local-file decoding do not substitute for that listening check.

## Stereo audition

[Play or download the 43-second preview](audio-pass-preview.mp3). This is rendered by the production mixer, then encoded to 256 kbps MP3 for review:

| Time | Content |
| --- | --- |
| 0–3 s | Piston engine, canopy airflow and operations voice |
| 3–6 s | Turboprop engine and airflow |
| 6–10 s | Four research engines viewed from a moving external camera |
| 10–13 s | Aircraft passing left to right with relative Doppler |
| 13–18 s | Near-left blast/metal, right impact, distant blast and water splash |
| 18–20 s | External double-shock sonic boom |
| 20–25 s | Military controller's extraction instruction |
| 25–32.5 s | Touchdowns: paved, grass, gravel, snow and sand, in that order |
| 32.5–36.5 s | Rolling textures in the same material order |
| 36.5–43 s | Spoken landing feedback |

Recreate the lossless source with `cmake --build build --target audio_preview`, then `build/audio_preview assets/voice preview.wav`. The preview is an audition of these scenarios, not a recording of native Windows device playback.

Local validation for the completed pass: 77 spatial/DSP behavior checks; all 1,131 indexed clips decoded and audited; 63 supplemental recordings screened by unprompted ASR with 0 normalized word edits across 537 words. Gameplay-loop and military regression suites pass. The audio suites and asset audit also pass AddressSanitizer/UndefinedBehaviorSanitizer builds (the Python asset audit itself is not instrumented). LeakSanitizer is disabled, matching the repository's CI setting. Windows x64 compilation and linking pass with MinGW GCC 13. Native decoder test results are reported separately in the focused Windows CI check; cross-compilation is not a claim of device or human listening approval.
