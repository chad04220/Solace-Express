# Binding-independent lesson recordings

The ten control-naming instructor lines are regenerated with the original stock
Ryan speaker from Qwen3-TTS-12Hz-1.7B-CustomVoice. They use the original instructor
direction and intercom processing. No voice cloning or real person's voice is
used. The remaining fifteen lesson recordings, including examiner Rosa's seven
lines, retain their existing audio. `assets/voice/lesson_controls.json` records
the exact spoken text, file hashes and provenance for all twenty-five lessons.

The new WAV filenames end in `.controls-neutral.wav`. `voice_import.py` verifies
and reapplies the manifest after importing an original voice pack; it cannot
replace the neutral recordings with older button-specific takes. Runtime index
kind `lesson` denotes a reviewed, binding-independent clip. The resolver rejects
legacy lesson rows that still have kind `line`, including from older asset packs.

## Regenerate the ten corrected lines

Use Python 3.12 in an isolated environment. The checked-in requirements capture
the verified generation environment. Install CPU-only PyTorch and torchaudio
from their official wheel index before installing the remaining requirements.
Download the model separately from the official Hugging Face repository at
revision `0c0e3051f131929182e2c023b9537f8b1c68adfe`; retain all files, including
`speech_tokenizer/`. The renderer verifies the large safetensors files before
loading. The model and tokenizer weights together total 4,515,695,644 bytes.

```sh
python3.12 -m venv .voice-venv
.voice-venv/bin/python -m pip install torch==2.14.1+cpu torchaudio==2.11.0+cpu --index-url https://download.pytorch.org/whl/cpu
.voice-venv/bin/python -m pip install -r tools/voice_lesson_helpers/requirements.txt
.voice-venv/bin/python -m pip check
.voice-venv/bin/hf download Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice \
  --revision 0c0e3051f131929182e2c023b9537f8b1c68adfe --local-dir /path/to/qwen-model
.voice-venv/bin/python tools/voice_lesson_render.py assets/voice/lesson_controls.json \
  --model-dir /path/to/qwen-model --output /path/to/new-takes
```

Generation uses CPU bfloat16 with low-memory safetensors loading, four intra-op
threads, a single clip at a time and at most 512 new tokens per take. The first
3.13-second calibration take peaked at 4,707 MiB RSS. Do not run
the model alongside memory-intensive renderer/build jobs. The renderer's
manifest reports measured peak RSS. It enforces offline operation with a
process-local fail-closed network restriction before importing speech libraries.

The three helper modules are preserved from the project's original voice
regeneration kit: `audio_master.py`, `headset_filter.py` and `offline_guard.py`.
The `intercom` filter uses a 220 Hz high-pass, 4.5 kHz low-pass, light compression
and limiting. The production audio uses the same 16 kHz mono G.711 mu-law format
and conversion as the other game voices. Regeneration keeps 48 kHz PCM masters
and filtered versions outside the repository for review.

## Acceptance

Do not change the manifest text to claim words that were never recorded. Screen
each actual runtime WAV with unprompted offline speech recognition, review every
transcript difference, and audition the takes before a final listening approval.
ASR and successful decoding alone are not a listening pass. The checked-in
manifest states which checks were actually performed and retains their results.

For the same independent speech screen, create a separate Python 3.12 environment
with `requirements-asr.txt`, and download the official
`Systran/faster-whisper-small.en` model at revision
`d1d751a5f8271d482d14ca55d9e2deeebbae577f` to a local directory. Its file metadata is
in `asr-model.json`. The screen verifies the model hash and enforces offline
operation before loading the recognizer. It never supplies the expected script
as a prompt or recognition context.

```sh
PYTHONDONTWRITEBYTECODE=1 /path/to/asr-python tools/voice_lesson_screen.py /path/to/new-takes \
  --model-dir /path/to/faster-whisper-small.en
PYTHONDONTWRITEBYTECODE=1 python3 tools/voice_lesson_verify.py
```

The verifier checks all twenty-five real lesson rows, audio hashes, format,
neutral wording and ordinary original-pack lesson import restoration.

Only install takes whose content has been checked; update their hashes and
provenance in `lesson_controls.json`, then apply `voice_import.apply_lesson_overrides`
to the existing index. Run the gameplay suite, including actual lesson queueing
with keyboard, controller, rebound controls and mid-lesson device switching.
Re-run every indexed clip's decoder check. A source-only resolver pass does not
substitute for checking the waveform content.

Qwen3-TTS model/runtime use Apache-2.0; the existing
`assets/voice/licenses/Qwen3-TTS-Apache-2.0.txt` applies. The official sources are
https://github.com/QwenLM/Qwen3-TTS and
https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice.
