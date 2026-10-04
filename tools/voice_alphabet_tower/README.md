# Solace Express — alphabet and airport tower voice extension

This adds A–Z and 0–9 to each of the six existing voices and to three new
generic airport controller voices. The original voice pack remains separate.

| Contents | Per voice | Total |
| --- | ---: | ---: |
| Letter names and ordinary digits | 36 × 9 voices | 324 |
| Full airport controller calls | 40 × 3 new voices | 120 |
| ATC phrase fragments, phonetic alphabet and radio digits | 57 × 3 new voices | 171 |
| Runtime clips | | 615 |

Original voices: Cal Morgan, Rosa Vance, Ellis Park, Finn Mercer, Aster and Nyx.
New voices: North Tower (American male), Coast Tower (American female), and
Valley Tower (British male). All use distinct stock synthesis presets.
British roles say zed; American roles say zee. Letter names are separate from
the phonetic Alfa–Zulu callsign clips. Radio digits include tree, fife and niner;
the ordinary 0–9 bank still says three, five and nine.

## Use the assets

`audio/` contains the ready-to-play, mono 48 kHz PCM16 WAV files. The headset
filter is already applied. `masters/audio/` preserves the audio before headset
treatment. Do not apply a second headset filter to the runtime WAVs.

`manifest.json` has every filename, role, preset, spelling lookup, frozen hash,
ATC phrase, template, playback condition and render recipe.
`symbol_resolver.py` demonstrates case-insensitive letter lookup and callsign,
runway, weather, taxi-route and frequency composition without changing voice.

```python
from symbol_resolver import VoiceBank
bank = VoiceBank('.')
letter_file = bank.file(bank.character('rosa', 'a'))
call = bank.compose('tower_coast', 'landing_clearance',
                    callsign='Solace 42', runway='09L')
```

Assign one tower voice to an airport and keep it for the exchange. Play a
procedural instruction only after the game has granted that clearance and
checked the actual runway, aircraft, traffic and phase. Greetings, hold-short,
line-up-and-wait, takeoff, pattern entries, approach, landing, runway exits,
handoffs and go-arounds are included. A go-around/emergency supersedes a queued
greeting or routine clearance. Cancel stale playback on pause, retry, crash,
flight end, changed runway or superseding clearance. Dynamic fragments have no
individual radio squelch; add a boundary cue once to the assembled transmission.
The example resolver uses a 35 ms suggested gap; tune the game's audio queue.

The phrase library is a generic game adaptation informed by FAA departure,
arrival and taxi phraseology. The resolver only selects audio; it does not
implement the game's ATC decision engine. No game-branch code was changed.

## Verification

`audio-checks.json` records format, duration, levels, hashes, source-digit byte
identity and lookup/composition checks for all 615 clips. `evidence/` includes
unprompted recognition of the 120 full tower calls, raw actor carrier checks,
independent CTC alignment boundaries, and shuffled spelling-list screens.

Raw actor targets were independently recognized before alignment. Forced
alignment only locates cuts; it is not used as proof of pronunciation. Very
short letter names can confuse recognition software, especially B/V, C/D,
G/J and O/zero over a radio filter. The listening preview exposes every clip
for a human check. This is asset validation, not a game playback/ATC test.

Thirty pre-existing Ellis/Aster/Nyx digit clips were reused byte-for-byte.
Actor letters/digits use the original Qwen voices and directions; Kokoro
letters use explicit English letter phonemes. Carrier phrases are removed.
The headset filter is the original script, with its hash frozen in the manifest.

The replay check matched three actor samples exactly. Ten Kokoro samples had
the same frame counts and waveform correlations of 0.982–0.997, but different
bytes. The shipped ONNX model contains random noise operators. Preserve the
delivered WAVs for byte identity; re-screen regenerated audio before using it.

## Reproduce

The `regeneration/` directory contains ordinary, readable Python source,
model/release metadata, recipes, licenses and offline rendering safeguards.
Public models are downloaded in a separate process. Rendering is then offline.
See its `REGENERATE.md`. Final audio hashes are frozen for comparison; a hash
difference after regeneration requires a fresh speech/listening review.

The original 521-clip pack is not included or modified by this extension.
