#!/usr/bin/env python3
"""Import the Solace Express voice packs into assets/voice.

    python3 tools/voice_import.py <Solace-Express-Alphabet-and-Tower-Voice-Pack> <Solace-Express-Voice-Pack>

From the alphabet-and-tower pack: the three tower controller voices. From the original pack: all six characters
(instructor, examiner, airport information, display pilot, cockpit assistant, research computer).

Every clip is checked against the SHA-256 in the pack's manifest, then stored for the game as 16 kHz 8-bit mu-law
mono WAV (the radio-filtered speech has no content above ~6 kHz, so this keeps the sound and is a sixth of the size).
assets/voice/voice_index.txt lists one clip per line:
    id <tab> file <tab> subtitle [<tab> speaker <tab> priority <tab> kind <tab> source subtitle <tab> mission]
plus the runtime lookup tables as  @key <tab> value  (template fragments, radio digits, number words, key names).
The checked-in lesson_controls.json overrides the ten legacy control-naming
recordings, and explicitly marks the other reviewed, control-independent lessons.
Every override's audio hash is verified before the index is written.
"""
import hashlib, json, os, sys, wave
import numpy as np
from scipy.signal import resample_poly

VOICES = ["tower_north", "tower_coast", "tower_valley"]

def mulaw(x):   # 16-bit PCM -> G.711 mu-law bytes
    BIAS, CLIP = 0x84, 32635
    x = np.clip(x.astype(np.int32), -32768, 32767)
    sign = (x < 0).astype(np.int32) << 7
    m = np.minimum(np.abs(x), CLIP) + BIAS
    exp = np.floor(np.log2(m)).astype(np.int32) - 7
    exp = np.clip(exp, 0, 7)
    mant = (m >> (exp + 3)) & 0x0F
    return (~(sign | (exp << 4) | mant) & 0xFF).astype(np.uint8)

def write_mulaw_wav(path, data, rate):
    import struct
    with open(path, "wb") as f:
        fmt = struct.pack("<HHIIHHH", 7, 1, rate, rate, 1, 8, 0)   # WAVE_FORMAT_MULAW, cbSize 0
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data) + (len(data) & 1)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"data" + struct.pack("<I", len(data)) + data.tobytes())
        if len(data) & 1: f.write(b"\0")

def convert(pack, line, out):
    src = os.path.join(pack, line["audio_file"])
    raw = open(src, "rb").read()
    want = line["speech_render"].get("sha256")
    if want and hashlib.sha256(raw).hexdigest() != want: sys.exit("hash mismatch: " + src)
    w = wave.open(src)
    assert w.getnchannels() == 1 and w.getsampwidth() == 2, src
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)
    y = resample_poly(x, 1, w.getframerate() // 16000)
    rel = "%s/%s.wav" % (line["speaker_id"], line["id"])
    os.makedirs(os.path.join(out, line["speaker_id"]), exist_ok=True)
    write_mulaw_wav(os.path.join(out, rel), mulaw(np.round(y)), 16000)
    return rel

def apply_lesson_overrides(rows, out):
    """Keep the truthful neutral lesson takes when importing the original packs.

    New audio has distinct filenames, so convert() cannot overwrite it with the
    older fixed-button recordings. A missing or edited take fails the import.
    """
    from pathlib import Path
    root = Path(out).resolve()
    manifest = json.loads((root / "lesson_controls.json").read_text())
    lessons = {r["id"]: r for r in manifest["lines"]}
    if len(lessons) != len(manifest["lines"]):
        raise ValueError("Duplicate lesson recording IDs")
    seen, result = set(), []
    for line in rows:
        fields = line.split("\t")
        if not fields[0].startswith("lesson."):
            result.append(line)
            continue
        cid = fields[0]
        if cid not in lessons:
            raise ValueError("Unreviewed lesson recording: " + cid)
        row = lessons[cid]
        path = (root / row["file"]).resolve()
        if not path.is_relative_to(root):
            raise ValueError("Invalid lesson recording path: " + cid)
        if hashlib.sha256(path.read_bytes()).hexdigest() != row["runtime_sha256"]:
            raise ValueError("Lesson audio hash mismatch: " + cid)
        result.append("\t".join([cid, row["file"], row["spoken_text"], row["speaker_id"],
                                  str(row["priority"]), "lesson", "", row["mission"]]))
        seen.add(cid)
    if seen != set(lessons):
        raise ValueError("Missing imported lesson IDs: " + ", ".join(sorted(set(lessons) - seen)))
    return result

def main(tower_pack, orig_pack=None):
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "voice")
    os.makedirs(out, exist_ok=True)
    rows, n = [], 0
    m = json.load(open(os.path.join(tower_pack, "manifest.json")))
    for l in m["lines"]:
        if l["speaker_id"] not in VOICES: continue
        rel = convert(tower_pack, l, out)
        rows.append("%s\t%s\t%s" % (l["id"], rel, (l.get("spoken_text") or "").replace("\t", " ")))
        n += 1
    for v in VOICES:   # lookups: template fragments, radio digits, the phonetic alphabet
        for k, cid in m["tower_atoms"][v].items(): rows.append("@%s.atom.%s\t%s" % (v, k, cid))
        for k, cid in m["radio_digits"][v].items(): rows.append("@%s.digit.%s\t%s" % (v, k, cid))
        for k, cid in m["radio_alphabet"][v].items(): rows.append("@%s.alpha.%s\t%s" % (v, k, cid))
    if orig_pack:   # the six characters, keyed by the game's own text (see AtcVoice::resolve)
        o = json.load(open(os.path.join(orig_pack, "manifest.json")))
        for l in o["lines"]:
            rel = convert(orig_pack, l, out)
            mission = next((s.get("mission_id", "") for s in l.get("sources", []) if s.get("mission_id")), "")
            alias = l.get("source_subtitle") or ""
            if alias == l["subtitle"]: alias = ""
            rows.append("\t".join([l["id"], rel, l["subtitle"], l["speaker_id"], str(l.get("priority", 40)), l["kind"], alias, mission]))
            n += 1
        for sp, atoms in o["atoms"].items():
            for k, cid in atoms.items(): rows.append("@atom.%s.%s\t%s" % (sp, k, cid))
        for k, cid in o["key_assets"].items(): rows.append("@key.%s\t%s" % (k, cid))
        for c in o["aircraft_names"]: rows.append("@craft.%s\t%s" % (c["name"], c["id"]))
        rows = apply_lesson_overrides(rows, out)
    with open(os.path.join(out, "voice_index.txt"), "w") as f:
        f.write("# Solace Express voices (tools/voice_import.py): id <tab> file <tab> subtitle [<tab> speaker <tab> priority <tab> kind <tab> alias <tab> mission], or @lookup <tab> value\n")
        f.write("\n".join(rows) + "\n")
    print("imported %d clips" % n)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
