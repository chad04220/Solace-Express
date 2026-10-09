#!/usr/bin/env python3
"""Render binding-independent lesson instructions with the original stock Ryan voice.

Install the pinned requirements and download the public model separately. This
worker loads only local safetensors, then removes its own network capability before
importing speech libraries. It does not edit the game's index or overwrite takes.
See voice_lesson_helpers/README.md for generation and acceptance instructions.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import resource
import sys
import time

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "voice_lesson_helpers"))
os.environ.update(HF_HUB_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1",
                  TOKENIZERS_PARALLELISM="false", OMP_NUM_THREADS="4")

MODEL = "Qwen/Qwen3-TTS-12Hz-1.7B-CustomVoice"
REVISION = "0c0e3051f131929182e2c023b9537f8b1c68adfe"
HASHES = {
    "model.safetensors": "38b1d5971bdbd982b561cccec982669a53b0537c3cf5e9bd4778ed07bb2f5137",
    "speech_tokenizer/model.safetensors": "836b7b357f5ea43e889936a3709af68dfe3751881acefe4ecf0dbd30ba571258",
}
DIRECTION = ("Speak clear English as an experienced flight instructor. Warm, patient and engaged, "
             "with natural changes in pitch and clear emphasis on the important actions. "
             "Use conversational sentence pauses.")


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path, help="JSON list of lesson IDs and spoken_text")
    p.add_argument("--model-dir", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--only", action="append", help="Render only this lesson ID")
    args = p.parse_args()
    rows = json.loads(args.input.read_text())
    if isinstance(rows, dict):
        rows = rows["lines"]
    rows = [r for r in rows if r.get("regenerated", True) and (not args.only or r["id"] in args.only)]
    assert rows and len({r["id"] for r in rows}) == len(rows)
    for name, expected in HASHES.items():
        if digest(args.model_dir / name) != expected:
            raise ValueError("Model hash mismatch: " + name)

    import offline_guard  # Fail-closed network restriction, only for this process.
    import numpy as np
    import soundfile as sf
    from scipy.signal import resample_poly
    import torch
    from qwen_tts import Qwen3TTSModel
    from audio_master import master
    from headset_filter import process
    from voice_import import mulaw, write_mulaw_wav

    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.manual_seed(20261008)
    print("Loading pinned Ryan model in CPU bfloat16", flush=True)
    model = Qwen3TTSModel.from_pretrained(
        str(args.model_dir), device_map="cpu", dtype=torch.bfloat16,
        attn_implementation="sdpa", low_cpu_mem_usage=True, local_files_only=True,
        use_safetensors=True,
    )
    assert "ryan" in [s.lower() for s in model.get_supported_speakers()]
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / "manifest.json"
    result = json.loads(manifest_path.read_text()) if manifest_path.exists() else {
        "model": MODEL, "model_revision": REVISION, "model_sha256": HASHES,
        "speaker": "Ryan", "speaker_type": "stock synthetic", "language": "English",
        "direction": DIRECTION, "device": "CPU", "dtype": "bfloat16",
        "attention": "sdpa", "seed": 20261008, "max_new_tokens": 512,
        "dependencies": {k: importlib.metadata.version(k) for k in
                         ("qwen-tts", "torch", "torchaudio", "transformers", "scipy", "numpy")},
        "filter_profile": "intercom", "generator_sha256": digest(__file__),
        "filter_sha256": digest(ROOT / "voice_lesson_helpers/headset_filter.py"),
        "mastering_sha256": digest(ROOT / "voice_lesson_helpers/audio_master.py"),
        "offline_guard_sha256": digest(ROOT / "voice_lesson_helpers/offline_guard.py"),
        "human_listening": "not performed", "lines": [],
    }
    for row in rows:
        row = dict(row)
        cid = row["id"]
        assert cid.startswith("lesson.") and "/" not in cid and ".." not in cid
        paths = {"master_file": f"masters/{cid}.wav", "audio_file": f"audio/{cid}.wav",
                 "runtime_file": f"runtime/instructor/{cid}.controls-neutral.wav"}
        if any((args.output / path).exists() for path in paths.values()):
            raise FileExistsError("Refusing to overwrite take: " + cid)
        start = time.monotonic()
        with torch.inference_mode():
            wavs, sr = model.generate_custom_voice(
                text=row["spoken_text"], speaker="Ryan", language="English", instruct=DIRECTION,
                max_new_tokens=512,
            )
        assert sr == 24000 and len(wavs) == 1
        mastered = resample_poly(master(wavs[0], sr), 2, 1).astype("float32")
        filtered = process(mastered, 48000, "intercom", cid, False)
        assert np.isfinite(filtered).all() and 0.5 < len(filtered) / 48000 < 35
        assert np.max(np.abs(filtered)) <= 0.8
        for key, data in (("master_file", mastered), ("audio_file", filtered)):
            path = args.output / paths[key]
            path.parent.mkdir(parents=True, exist_ok=True)
            sf.write(path, data, 48000, subtype="PCM_16")
        # Use the project's production G.711 conversion, matching voice_import.
        filtered_pcm, _ = sf.read(args.output / paths["audio_file"], dtype="int16")
        runtime = np.round(resample_poly(filtered_pcm.astype(np.float64), 1, 3))
        path = args.output / paths["runtime_file"]
        path.parent.mkdir(parents=True, exist_ok=True)
        write_mulaw_wav(str(path), mulaw(runtime), 16000)
        row.update(paths)
        row["sha256"] = {key: digest(args.output / path) for key, path in paths.items()}
        row["seconds"] = len(filtered) / 48000
        row["peak"] = float(np.max(np.abs(filtered)))
        row["rms"] = float(np.sqrt(np.mean(filtered ** 2)))
        row["elapsed_seconds"] = time.monotonic() - start
        row["max_rss_kib"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        result["lines"].append(row)
        manifest_path.write_text(json.dumps(result, indent=2) + "\n")
        print(f"Generated {cid}: {row['seconds']:.2f}s; {row['elapsed_seconds']:.1f}s CPU wall; "
              f"peak RSS {row['max_rss_kib'] / 1024:.1f} MiB", flush=True)


if __name__ == "__main__":
    main()
