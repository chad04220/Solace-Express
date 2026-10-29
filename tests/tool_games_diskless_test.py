#!/usr/bin/env python3
"""The Windows tools' Games are diskless (tool_modes_win32.cpp): --bench, --shots, --profile, --analyze and --loadshots
make a Game of their own for each scene, besides the tool's own (platform_win32.cpp). Each must be diskless, as the
tool's is, or a scene's flight - a crash scene's fees and settlement - is saved into the player's career folder:
v3.47.0's diagnostics did that for their break-up scenes. A source contract (the Windows code doesn't run on the Linux
tests): every Game the two files new, and the tool's own, is set diskless before it is initialised."""
import pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[1] / 'src'
files = {name: (root / name).read_text(encoding='utf-8') for name in ['platform_win32.cpp', 'tool_modes_win32.cpp']}
src = files['platform_win32.cpp']
failures = 0
made = []
for name, text in files.items():
    for m in re.finditer(r'\bnew Game\s*\(\s*\)', text):
        made.append((name, m.start()))
        line = text.count('\n', 0, m.start()) + 1
        rest = text[m.start():]
        init = re.search(r'->init(Headless)?\s*\(', rest)
        before = rest[:init.start()] if init else rest[:400]
        if not re.search(r'->diskless\s*=\s*true', before):
            print('FAIL: %s:%d: a tool Game is not set diskless before it is initialised' % (name, line))
            failures += 1
if not any(name == 'tool_modes_win32.cpp' for name, _ in made):
    print('FAIL: no tool Games found in tool_modes_win32.cpp: the contract no longer matches the source')
    failures += 1
if not re.search(r'game\.diskless\s*=\s*tool\s*;', src):
    print("FAIL: the tool's own Game is not diskless (game.diskless = tool)")
    failures += 1
if not made:
    print('FAIL: no tool Games found: the contract no longer matches the source')
    failures += 1
print('tool_games_diskless: %d Games checked, %d failures' % (len(made), failures))
sys.exit(1 if failures else 0)
