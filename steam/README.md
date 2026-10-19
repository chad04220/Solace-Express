# Somnium Volandi on Steam

What the Steam release needs from this repository, and the settings to enter on the Steamworks site
(partner.steamgames.com). The game's side is `src/steam.h`: inert unless Valve's `steam_api64.dll` is beside the exe.

## What's here

| Path | What | Where it goes |
|---|---|---|
| `achievements.csv` | The 37 achievements: API name, display name, description, hidden flag, icons | Steamworks > *Stats & Achievements*, one row each |
| `achievements/ACH_*.png` | Each achievement's icon (256 px), earned and `_locked` | Uploaded with each achievement |
| `achievements/contact_sheet.jpg` | Every icon at 64 px, earned over locked | For a look, not for upload |
| `rich_presence_english.vdf` | The status friends see ("Flying the Skerry Twin from Glacier Station to Solace Capital") | Steamworks > *Community* > *Rich Presence localization* |
| `store/library_logo.png` | The title on transparency, 1280x720 | *Store presence* > *Graphical assets* > *Library logo* |
| `store/community_icon.jpg` | The identity, 184 px | *Community* > *Community icon* |
| `redist/` | Where `steam_api64.dll` goes (its README) | The Steam depot only |

`tools/steam/make_steam_assets.py` makes the table, the icons, the logo and the community icon from the game's own
list (`src/profile.cpp`); run it again after adding an achievement (`tests/steam_test.cpp` fails until you do).
The client icon is `assets/icon.ico`.

## The Steamworks settings

**Installation > General**: launch option 1, executable `SomniumVolandi.exe`, no arguments, OS Windows, 64-bit.
Launch option 2 (optional), *Collect diagnostics*: executable `diagnostics.bat`.

**Installation > Redistributables**: none. The exe has the C++ runtime built in and uses only Windows' own DLLs.

**Steam Cloud** (*Application* > *Steam Cloud*): byte quota 200 MB, 500 files. Auto-Cloud, root `WinAppDataRoaming`,
subdirectory `SomniumVolandi`, one rule per pattern: `career.sav`, `career.sav.bak`, `profile.cfg`, `settings.cfg`,
`joystick.cfg`, `radio_stations.txt`, `ghost_*.bin`. Not the logs, the `crash` folder or `report_*.txt`; nothing in
`%LOCALAPPDATA%\SomniumVolandi` (the shader and aircraft caches, rebuilt by each PC).

**Steam Input** (*Application* > *Steam Input*): *Opt out* on PC (the game reads XInput pads and DirectInput
joysticks, throttles and pedals itself, and Steam's remapping would get in the way of the flight gear). On the Steam
Deck Steam Input stays on and presents an Xbox pad: choose the *Gamepad* template as the Deck's default.

**Stats & Achievements**: enter each row of `achievements.csv`, API name exactly as written, *Hidden* where it says
1, with both icons; then *Publish*. The game sets each as it's earned and, at every start, every one already earned.

**Rich Presence**: upload `rich_presence_english.vdf`. A translation is another `<language>` file with the same
tokens; the game needs no new build for it.

**Depots**: the first depot (the app's id + 1), Windows 64-bit, all languages. The content is the release folder as
the CI makes it, with `steam_api64.dll` added.

## Uploading a build

**By CI**: set the repository *variable* `STEAM_APP_ID` (and `STEAM_BRANCH`, else `beta`) and the *secrets*
`STEAM_USERNAME` (a build-only Steam account with Steamworks edit rights on the app) and `STEAM_CONFIG_VDF` (that
account's `config/config.vdf` from a steamcmd session that has signed in once with Steam Guard, base64-encoded). With
those, every release (`build.yml`'s `release_tag`) is uploaded to that branch too. Set it live for everyone on the
Steamworks site (*Builds*), never from CI (Valve's rule).

**By hand**: unpack the release zip, add `steam_api64.dll`, then

```
python tools/steam/make_build_vdf.py --app <app id> --content <folder> --desc v3.51.0 --branch beta
steamcmd +login <build account> +run_app_build "<repo>\steam\build\app_build.vdf" +quit
```

## Testing the Steam build

1. Install the build from its beta branch (*Properties* > *Betas*), or start a local copy with Steam running and a
   `steam_appid.txt` holding the app id beside the exe. `startup.log` in `%APPDATA%\SomniumVolandi` says
   `Steam: on` (or why not).
2. Earn *Wheels Up* (any first flight): the Steam toast shows; it is in the game's library page.
3. A friend sees the status change between the menu, the hub and a flight.
4. Shift+Tab in flight: the overlay opens and the flight pauses.
5. On a Deck: the first start's name field brings up the keyboard (X brings it up again).
6. Remove `steam_appid.txt` before any upload (the depot excludes it anyway).
