#!/usr/bin/env python3
"""The SteamPipe build scripts for one upload by hand (the CI's steam job makes its own): an app build and its Windows
depot, the content being the release's unpacked folder with steam_api64.dll added beside the exe.

  python tools/steam/make_build_vdf.py --app 1234560 --content dist/SomniumVolandi --desc v3.51.0 [--depot 1234561] [--branch beta]
  steamcmd +login <build account> +run_app_build "%CD%\\steam\\build\\app_build.vdf" +quit

The depot is the app's id plus one unless given (Steamworks makes the first depot so). --branch sets the build live on
that beta branch; the default branch is set live on the Steamworks site, never from here (Valve's rule)."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--app', required=True, type=int)
    ap.add_argument('--depot', type=int)
    ap.add_argument('--content', required=True, help='the unpacked release folder (SomniumVolandi.exe at its top)')
    ap.add_argument('--desc', default='')
    ap.add_argument('--branch', default='', help='a beta branch to set it live on (empty: none)')
    a = ap.parse_args()
    depot = a.depot or a.app + 1
    content = Path(a.content).resolve()
    if not (content / 'SomniumVolandi.exe').is_file():
        raise SystemExit(f'{content} has no SomniumVolandi.exe at its top')
    if not (content / 'steam_api64.dll').is_file():
        raise SystemExit(f'{content} has no steam_api64.dll beside the exe (steam/README.md): without it the game is off Steam')
    out = ROOT / 'steam' / 'build'
    out.mkdir(parents=True, exist_ok=True)
    (out / f'depot_build_{depot}.vdf').write_text(f'''"DepotBuildConfig"
{{
	"DepotID" "{depot}"
	"FileMapping"
	{{
		"LocalPath" "*"
		"DepotPath" "."
		"recursive" "1"
	}}
	"FileExclusion" "*.pdb"
	"FileExclusion" "steam_appid.txt"
}}
''')
    (out / 'app_build.vdf').write_text(f'''"AppBuild"
{{
	"AppID" "{a.app}"
	"Desc" "Somnium Volandi {a.desc}"
	"ContentRoot" "{content}"
	"BuildOutput" "{out / 'output'}"
	"SetLive" "{a.branch}"
	"Preview" "0"
	"Depots"
	{{
		"{depot}" "depot_build_{depot}.vdf"
	}}
}}
''')
    print(f'{out / "app_build.vdf"}: app {a.app}, depot {depot}, from {content}' + (f', live on {a.branch}' if a.branch else ''))


if __name__ == '__main__':
    main()
