#!/usr/bin/env python3
"""The Windows tools' Games are diskless (platform_win32.cpp): --bench, --shots, --profile, --analyze and --loadshots make
a Game of their own for each scene, besides the tool's own. Each must be diskless, as the tool's is, or a scene's
flight - a crash scene's fees and settlement - is saved into the player's career folder: v3.47.0's diagnostics did
that for their break-up scenes. A source contract (the Windows code doesn't run on the Linux tests): every Game
platform_win32.cpp news, and the tool's own, is set diskless before it is initialised."""
import pathlib, re, sys

src = (pathlib.Path(__file__).resolve().parents[1] / 'src' / 'platform_win32.cpp').read_text(encoding='utf-8')
failures = 0
made = [m.start() for m in re.finditer(r'\bnew Game\s*\(\s*\)', src)]
for at in made:
    line = src.count('\n', 0, at) + 1
    rest = src[at:]
    init = re.search(r'->init(Headless)?\s*\(', rest)
    before = rest[:init.start()] if init else rest[:400]
    if not re.search(r'->diskless\s*=\s*true', before):
        print('FAIL: platform_win32.cpp:%d: a tool Game is not set diskless before it is initialised' % line)
        failures += 1
if not re.search(r'game\.diskless\s*=\s*tool\s*;', src):
    print("FAIL: the tool's own Game is not diskless (game.diskless = tool)")
    failures += 1
if not made:
    print('FAIL: no tool Games found: the contract no longer matches the source')
    failures += 1
print('tool_games_diskless: %d Games checked, %d failures' % (len(made), failures))
sys.exit(1 if failures else 0)
