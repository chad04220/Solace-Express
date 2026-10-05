#!/usr/bin/env python3
"""Read-only integrity checks for the review subtree, not a game-integration test."""
import ast
import hashlib
import json
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent
allowed = {'.py', '.cpp', '.inc', '.glsl', '.csv', '.json', '.md', '.txt', '.log', '.png'}
files = [p for p in root.rglob('*') if p.is_file()]
for path in files:
    assert not path.is_symlink(), path
    assert path.suffix in allowed or path.name == 'SHA256SUMS', path
    if path.suffix == '.json':
        json.loads(path.read_text())
    elif path.suffix == '.py':
        ast.parse(path.read_text(), filename=str(path))
    elif path.suffix == '.png':
        with Image.open(path) as image:
            image.verify()
for manifest in root.rglob('SHA256SUMS.json'):
    entries = json.loads(manifest.read_text())
    for name, digest in entries.items():
        path = (manifest.parent / name).resolve()
        assert path.is_relative_to(root), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, name
    actual = {str(p.relative_to(manifest.parent)) for p in manifest.parent.rglob('*')
              if p.is_file() and p != manifest}
    if manifest.parent == root:
        assert actual == set(entries), (manifest, actual.symmetric_difference(entries))
for manifest in root.rglob('SHA256SUMS'):
    for line in manifest.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        path = (manifest.parent / name).resolve()
        assert path.is_relative_to(root), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, name
print(f'PASS: {len(files)} files; JSON, Python syntax, PNGs and SHA-256 manifests verified.')
