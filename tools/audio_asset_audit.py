#!/usr/bin/env python3
"""Audit every shipped voice waveform and supplemental provenance, without TTS/ASR dependencies.

The C++ audio_voice test additionally exercises the production decoder. Signal
measurements and recognition transcripts are evidence, not human listening approval.
"""
import argparse
import gzip
import hashlib
import json
import math
import struct
from pathlib import Path


def read_pcm(path):
    data = path.read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', f'{path}: not WAV'
    fmt = pcm = None
    offset = 12
    while offset + 8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        assert offset + size <= len(data), f'{path}: truncated chunk'
        chunk = data[offset:offset + size]
        if kind == b'fmt ': fmt = struct.unpack_from('<HHIIHH', chunk)
        if kind == b'data': pcm = chunk
        offset += size + (size & 1)
    assert fmt and pcm, f'{path}: missing format/data'
    encoding, channels, rate, _, _, bits = fmt
    assert channels == 1 and 8000 <= rate <= 192000, f'{path}: format'
    if encoding == 1 and bits == 16:
        assert len(pcm) % 2 == 0, f'{path}: partial PCM sample'
        samples = [v[0] / 32768 for v in struct.iter_unpack('<h', pcm)]
    else:
        assert encoding == 7 and bits == 8, f'{path}: unsupported voice encoding'
        samples = []
        for byte in pcm:
            u = ~byte & 255
            value = (((u & 15) << 3) + 132) << ((u >> 4) & 7)
            samples.append((-(value - 132) if u & 128 else value - 132) / 32768)
    return data, rate, encoding, samples


def audit(root):
    rows = {}
    for name in ('voice_index.txt', 'voice_index_extra.txt'):
        for line in (root / name).read_text(encoding='utf-8').splitlines():
            if not line or line[0] in '#@': continue
            fields = line.split('\t')
            assert len(fields) >= 3 and fields[0] not in rows, f'duplicate/malformed index row: {line}'
            rows[fields[0]] = fields
    report = []
    hashes = {}
    for clip, fields in sorted(rows.items()):
        path = (root / fields[1]).resolve()
        assert path.is_relative_to(root.resolve()), f'{clip}: asset outside voice directory'
        raw, rate, encoding, samples = read_pcm(path)
        seconds = len(samples) / rate
        peak = max(map(abs, samples))
        rms = math.sqrt(sum(x*x for x in samples) / len(samples))
        dc = sum(samples) / len(samples)
        assert .02 <= seconds <= 120 and peak > .003 and rms > .0001, f'{clip}: empty/inaudible voice'
        assert peak < .999 and abs(dc) < .02, f'{clip}: clipped or DC-biased voice'
        hashes[clip] = hashlib.sha256(raw).hexdigest()
        report.append(dict(id=clip, file=fields[1], sha256=hashes[clip], rate=rate, encoding=encoding,
                           seconds=round(seconds, 4), peak=round(peak, 6), rms=round(rms, 6), dc=round(dc, 6)))
    manifest = json.loads((root / 'audio_pass.json').read_text())
    validation = json.loads((root / 'audio_pass_validation.json').read_text())
    screened = {r['id']: r for r in validation['clips']}
    for line in manifest['lines']:
        clip = line['id']
        assert rows[clip][1:7] == [line['file'], line['text'], line['speaker'], str(line['priority']), line['kind'], line['match']], f'{clip}: index drift'
        assert hashes[clip] == line['sha256'] == screened[clip]['sha256'], f'{clip}: waveform/provenance drift'
        assert screened[clip]['expected'] == line['text'] and screened[clip]['word_edits'] == 0, f'{clip}: speech screen requires review'
    assert set(screened) == {r['id'] for r in manifest['lines']}, 'ASR screen is incomplete/stale'
    assert manifest['validation'].get('asr_passed') and validation.get('passed'), 'speech screen did not pass'
    return dict(indexed_clips=len(rows), supplemental_clips=len(screened), passed=True,
                human_listening_approved=False, clips=report)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--voice-dir', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/voice')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.voice_dir)
    if args.report:
        data = (json.dumps(result, indent=2) + '\n').encode('utf-8')
        args.report.write_bytes(gzip.compress(data, mtime=0) if args.report.suffix == '.gz' else data)
    print(f"Audio assets: {result['indexed_clips']} waveforms, {result['supplemental_clips']} supplemental provenance/ASR records: PASS")
