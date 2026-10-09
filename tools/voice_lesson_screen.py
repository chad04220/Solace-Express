#!/usr/bin/env python3
"""Unprompted offline recognition of generated lesson WAVs; never a listening claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "voice_lesson_helpers"))
os.environ.update(HF_HUB_OFFLINE="1", HF_HUB_DISABLE_TELEMETRY="1",
                  TOKENIZERS_PARALLELISM="false", UCX_VFS_ENABLE="n")


def normalize(text):
    return " ".join(re.findall(r"[a-z0-9]+", text.lower()))


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("pack", type=Path)
    p.add_argument("--model-dir", type=Path, required=True)
    p.add_argument("--only", action="append")
    a = p.parse_args()
    expected_hash = "62b2a45b05ee59acb4a5341b33ee35e041395d378d418a18acfe4c9e768ee37a"
    assert digest(a.model_dir / "model.bin") == expected_hash
    import offline_guard
    import numpy as np
    import soundfile as sf
    from faster_whisper import WhisperModel
    model = WhisperModel(str(a.model_dir), device="cpu", compute_type="int8",
                         cpu_threads=4, num_workers=1, local_files_only=True)
    rows = json.loads((a.pack / "manifest.json").read_text())["lines"]
    target = a.pack / "speech-screen.json"
    result = json.loads(target.read_text()) if target.exists() else {
        "model": "Systran/faster-whisper-small.en", "revision": "d1d751a5f8271d482d14ca55d9e2deeebbae577f",
        "model_sha256": expected_hash,
        "method": "CPU int8, English, beam 5, temperature 0; independent clips, no prompt or reference words",
        "human_listening": "not performed", "checks": [],
    }
    for row in rows:
        if a.only and row["id"] not in a.only:
            continue
        path = a.pack / row["runtime_file"]
        data, rate = sf.read(path, dtype="float32")
        assert rate == 16000 and data.ndim == 1
        data = np.pad(data, (4800, 7200))
        segments, _ = model.transcribe(data, language="en", beam_size=5, temperature=0,
                                      condition_on_previous_text=False, vad_filter=False,
                                      no_speech_threshold=0.9)
        recognized = " ".join(s.text.strip() for s in segments).strip()
        same = normalize(recognized) == normalize(row["spoken_text"])
        check = {"id": row["id"], "file": row["runtime_file"], "sha256": digest(path),
                 "expected": row["spoken_text"], "recognized": recognized,
                 "literal_normalized_match": same,
                 "review_status": "literal match" if same else "review required"}
        result["checks"] = [c for c in result["checks"] if c["id"] != row["id"]] + [check]
        target.write_text(json.dumps(result, indent=2) + "\n")
        print(row["id"], repr(recognized), "MATCH" if same else "REVIEW", flush=True)


if __name__ == "__main__":
    main()
