#!/usr/bin/env python3
"""Import the tower controller voices from the Solace Express alphabet-and-tower voice pack into assets/voice.

    python3 tools/voice_import.py <unzipped Solace-Express-Alphabet-and-Tower-Voice-Pack folder>

Every clip is checked against the SHA-256 in the pack's manifest, then stored for the game as 16 kHz 8-bit mu-law
mono WAV (the radio-filtered speech has no content above ~6 kHz, so this keeps the sound and is a sixth of the size).
assets/voice/voice_index.txt lists one clip per line:  id <tab> file <tab> spoken text
plus the runtime lookup tables (the templates' fragments and the radio digits) as  @key <tab> clip id.
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

def main(pack):
    m = json.load(open(os.path.join(pack, "manifest.json")))
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "voice")
    os.makedirs(out, exist_ok=True)
    rows, n = [], 0
    for l in m["lines"]:
        if l["speaker_id"] not in VOICES: continue
        src = os.path.join(pack, l["audio_file"])
        raw = open(src, "rb").read()
        want = l["speech_render"].get("sha256")
        if want and hashlib.sha256(raw).hexdigest() != want: sys.exit("hash mismatch: " + src)
        w = wave.open(src)
        assert w.getnchannels() == 1 and w.getsampwidth() == 2, src
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)
        y = resample_poly(x, 1, w.getframerate() // 16000)
        rel = "%s/%s.wav" % (l["speaker_id"], l["id"])
        os.makedirs(os.path.join(out, l["speaker_id"]), exist_ok=True)
        write_mulaw_wav(os.path.join(out, rel), mulaw(np.round(y)), 16000)
        rows.append("%s\t%s\t%s" % (l["id"], rel, (l.get("spoken_text") or "").replace("\t", " ")))
        n += 1
    for v in VOICES:   # lookups: template fragments, radio digits, the phonetic alphabet
        for k, cid in m["tower_atoms"][v].items(): rows.append("@%s.atom.%s\t%s" % (v, k, cid))
        for k, cid in m["radio_digits"][v].items(): rows.append("@%s.digit.%s\t%s" % (v, k, cid))
        for k, cid in m["radio_alphabet"][v].items(): rows.append("@%s.alpha.%s\t%s" % (v, k, cid))
    with open(os.path.join(out, "voice_index.txt"), "w") as f:
        f.write("# Solace Express tower voices (tools/voice_import.py); id <tab> file <tab> text, or @lookup <tab> id\n")
        f.write("\n".join(rows) + "\n")
    print("imported %d clips" % n)

if __name__ == "__main__":
    main(sys.argv[1])
