#!/usr/bin/env python3
"""Verify real lesson audio provenance/indexing and original-pack import restoration."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile

from voice_import import apply_lesson_overrides


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--voice-dir", type=Path, default=Path(__file__).resolve().parents[1] / "assets/voice")
    args = parser.parse_args()
    root = args.voice_dir
    manifest = json.loads((root / "lesson_controls.json").read_text())
    rows = (root / "voice_index.txt").read_text().splitlines()
    expected = {f"lesson.L{lesson}.phase{phase}" for lesson, count in ((1, 4), (2, 7), (3, 7), (4, 7)) for phase in range(count)}
    assert {r["id"] for r in manifest["lines"]} == expected
    assert len(manifest["lines"]) == 25
    assert sum(bool(r["regenerated"]) for r in manifest["lines"]) == 10
    assert apply_lesson_overrides(rows, root) == rows, "Index disagrees with verified lesson manifest"
    by_id = {r.split("\t")[0]: r.split("\t") for r in rows if r.startswith("lesson.")}
    for row in manifest["lines"]:
        cid = row["id"]
        assert by_id[cid][5] == "lesson" and by_id[cid][6] == ""
        assert by_id[cid][7] == row["mission"]
        assert not re.search(r"\b(?:press|tap|click|button|trigger|bumper|d-pad|directional pad|SHIFT|CTRL|RT|LT|LB|RB)\b", row["spoken_text"], re.I), cid
        data = (root / row["file"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row["runtime_sha256"], cid
        assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", cid
        offset, fmt, payload = 12, None, None
        while offset + 8 <= len(data):
            kind, size = data[offset:offset + 4], struct.unpack_from("<I", data, offset + 4)[0]
            assert offset + 8 + size <= len(data), cid
            chunk = data[offset + 8:offset + 8 + size]
            if kind == b"fmt ":
                fmt = struct.unpack_from("<HHIIHH", chunk)
            if kind == b"data":
                payload = chunk
            offset += 8 + size + (size & 1)
        assert fmt == (7, 1, 16000, 16000, 1, 8), (cid, fmt)
        assert payload and 0.5 < len(payload) / 16000 < 35 and len(set(payload)) > 64, cid
    print("25 lesson rows: exact manifest/index agreement, no fixed control names, verified audio hashes and WAV structure")

    # Verify ordinary reimport on a private copy; never alter checked-in audio.
    with tempfile.TemporaryDirectory(prefix="solace-lesson-import-") as tmp:
        test = Path(tmp)
        for row in manifest["lines"]:
            dest = test / row["file"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / row["file"], dest)
        path = test / "lesson_controls.json"
        path.write_text(json.dumps(manifest))
        assert apply_lesson_overrides(rows, test) == rows
        # Reimporting the legacy index cannot restore the old control-worded rows.
        source_lines = {r["id"]: r for r in manifest["lines"]}
        legacy = []
        for line in rows:
            fields = line.split("\t")
            if fields[0] in source_lines:
                source = source_lines[fields[0]]
                fields[1] = source.get("original_file", source["file"])
                fields[2] = source.get("original_index_text", source["spoken_text"])
                fields[5] = "line"
            legacy.append("\t".join(fields))
        assert apply_lesson_overrides(legacy, test) == rows
    print("Normal index and original-pack lesson import restoration: passed")


if __name__ == "__main__":
    main()
