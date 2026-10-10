#!/usr/bin/env python3
"""Compile/link the actual inline 34th startup program, without editing production.
The tiny C++ expression reader deliberately rejects unsupported assembly syntax.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def tokens(text, start=0):
    """Yield (kind, value) until the caller's delimiter; never scan inside literals."""
    i = start
    while i < len(text):
        m = re.match(r'\s+|//[^\n]*(?:\n|$)|/\*[\s\S]*?\*/', text[i:])
        if m:
            i += m.end()
            continue
        m = re.match(r'"(?:[^"\\\n]|\\.)*"', text[i:])
        if m:
            raw = m.group()
            # Only the overlapping JSON/C++ escape subset is supported. Reject
            # octal, hex, universal escapes and raw/prefixed literals rather than
            # guessing at a future C++ assembly change.
            if re.search(r'\\(?!["\\/bfnrt])', raw):
                raise ValueError('unsupported C++ string escape')
            yield ('string', json.loads(raw))
            i += m.end()
            continue
        m = re.match(r'[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*', text[i:])
        if m:
            yield ('name', m.group())
            i += m.end()
            continue
        if text[i] in '+(),;':
            yield (text[i], text[i])
            i += 1
            continue
        raise ValueError(f'unsupported assembly syntax near {text[i:i+40]!r}')


def statement_after(source, pattern):
    matches = list(re.finditer(pattern, source))
    if len(matches) != 1:
        raise ValueError(f'expected exactly one production assembly: {pattern}')
    out = []
    for t in tokens(source, matches[0].end()):
        if t[0] == ';':
            return out
        out.append(t)
    raise ValueError('unterminated production statement')


def evaluate(ts, constants):
    i = 0

    def expression():
        nonlocal i
        value = term()
        while i < len(ts) and ts[i][0] == '+':
            i += 1
            value += term()
        return value

    def term():
        nonlocal i
        if i == len(ts):
            raise ValueError('missing expression term')
        kind, value = ts[i]
        i += 1
        if kind == 'string':
            while i < len(ts) and ts[i][0] == 'string':
                value += ts[i][1]
                i += 1
            return value
        if kind == 'name' and value == 'std::string':
            if i == len(ts) or ts[i][0] != '(':
                raise ValueError('std::string requires a parenthesized literal expression')
            i += 1
            value = expression()
            if i == len(ts) or ts[i][0] != ')':
                raise ValueError('unbalanced std::string wrapper')
            i += 1
            return value
        if kind == 'name' and value in constants:
            return constants[value]
        raise ValueError(f'unsupported expression term {(kind, value)!r}')

    value = expression()
    if i != len(ts):
        raise ValueError(f'unconsumed expression tokens: {ts[i:]}')
    return value


def assemble(source, wreck_file):
    declaration = statement_after(source, r'static\s+const\s+char\s*\*\s*kShMapWreckVS\s*=')
    vertex_tail = evaluate(declaration, {})
    wreck = wreck_file.replace('\r', '')
    if not re.match(r'^//! kWreckClip(?:\n|$)', wreck):
        raise ValueError('wreck_clip.glsl is not the kWreckClip embed source')
    # Identical CR removal and leading //! block stripping to embed_shaders.cmake.
    wreck = re.sub(r'^(//![^\n]*\n)+', '', wreck)
    call = statement_after(source, r'\bprogShMapWreck\s*=\s*linkProgramCached\s*\(')
    args, part, depth = [], [], 0
    for t in call:
        if t[0] == '(':
            depth += 1
        elif t[0] == ')':
            depth -= 1
            if depth == -1:
                args.append(part)
                part = []
                continue
        if depth < 0:
            raise ValueError('tokens after linkProgramCached closing parenthesis')
        if t[0] == ',' and depth == 0:
            args.append(part)
            part = []
        else:
            part.append(t)
    if depth != -1 or part or len(args) != 3 or args[2] != [('name', 'e')]:
        raise ValueError('unsupported linkProgramCached arguments')
    constants = {'kShMapWreckVS': vertex_tail, 'kWreckClip': wreck}
    return evaluate(args[0], constants), evaluate(args[1], constants)


def self_test():
    assert evaluate(list(tokens('std::string("a\\n" "b") + known')), {'known': 'c'}) == 'a\nbc'
    assert evaluate(list(tokens('"x" /* skipped */ "y" // skipped\n + "z"')), {}) == 'xyz'
    for bad in ['unknown', 'std::string("x", 2)', '"x".append("y")', 'R"(raw)"', '"\\x41"', '"\\u0041"', '"a" +']:
        try:
            evaluate(list(tokens(bad)), {})
        except ValueError:
            pass
        else:
            raise AssertionError(f'unsupported assembly was accepted: {bad}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validator', required=True)
    args = parser.parse_args()
    self_test()
    vertex, fragment = assemble((ROOT/'src/raster_renderer.cpp').read_text(),
                                (ROOT/'src/shaders/wreck_clip.glsl').read_text())
    with tempfile.TemporaryDirectory(prefix='solace-inline-wreck-') as directory:
        paths = []
        for stage, source in [('vert', vertex), ('frag', fragment)]:
            path = Path(directory)/f'wreck_shadow.{stage}'
            path.write_text(source)
            paths.append(path)
            subprocess.run([args.validator, '-S', stage, str(path)], check=True)
            print(f'{stage}: {len(source.encode())} bytes, SHA256 {hashlib.sha256(source.encode()).hexdigest()}')
        subprocess.run([args.validator, '-l', *(str(p) for p in paths)], check=True)
    print('PASS: 1 additional production startup program; 2 exact shader stages compiled and linked')


if __name__ == '__main__':
    main()
