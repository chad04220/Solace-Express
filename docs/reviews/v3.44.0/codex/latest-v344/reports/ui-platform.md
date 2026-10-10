# v3.44.0 applicability: UI, audio and platform

Latest release: `b22c2fca66fff88b62801a79f9f0c6edf06dcb9e`. Compared against the fully exercised v3.43.0 source `91212bf4a131c02d2da41efbdb09248a09bc3599`.

All ten files listed in `../logs/ui-source-equivalence.json` are byte-identical, including UI/research UI, Wraith weapon input, platform, ATC/WAV decoder, radio, audio, and the stock UI harness. The only `game.cpp` changes are an include and render-only volcano append, captured in `../logs/game-source-delta.diff`; relevant gameplay/input/comms functions are unchanged, and their old line locations advance by one after the include.

## Finding applicability

The entire original UI/audio finding set remains present by exact source comparison: UI-01 radio input ownership (purchase and weapon effects), UI-02 research Abort overlap, UI-03 dynamic-label focus loss, UI-05 keyboard/D-pad rebinding reachability, UI-06 narrow-window clipping, UI-07 FOV clipping, AU-01 master/radio gain, AU-02 paused ducking, AU-03 stale hazard messages, and conditional corrupted-WAV AU-04.

Original reproduction logs and screenshots were produced on v3.43.0. They have not been relabeled as fresh v3.44 captures. Exact equality establishes applicability of the relevant control code; the latest full stock build/tests are recorded by the root audit separately. Native Windows/device and actual internet-stream limitations remain unchanged.

The ten extra unindexed WAV files do not themselves establish a defect. The original stock suite decoded all 1,068 indexed recordings cleanly; malformed diagnostic fixtures remain separate conditional hardening failures.

No production changes or new publication actions were performed.
