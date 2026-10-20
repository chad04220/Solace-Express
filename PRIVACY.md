# Somnium Volandi - Privacy Notice

Version 3, 2026. This notice explains what Somnium Volandi ("the game") does with data. The game is made by CDAIII
("we", "us").

## The short version

The game has no accounts, no telemetry, no analytics and no advertising. It never sends anything to us. Everything it
saves stays on your computer, and the only network connections it makes are to internet radio stations you add
yourself and, in the Steam version, to Steam (see "Steam" below).

## What the game stores on your computer

| Where | What | Why |
|---|---|---|
| `%APPDATA%\SomniumVolandi` | `career.sav` and its backup `career.sav.bak`, `settings.cfg`, `joystick.cfg`, `radio_stations.txt` | Your career, settings, key and joystick bindings, trial records and radio stations |
| `%APPDATA%\SomniumVolandi` | `startup.log`, and `error.log` if a launch failed | Your graphics card and driver name, the audio backend, the names of connected joysticks, and any shader or start-up errors, to help solve problems |
| `%APPDATA%\SomniumVolandi\crash` | A crash dump and a copy of the log, only if the game crashes | To find what went wrong; see "Crash reports" below |
| `%APPDATA%\SomniumVolandi` | `report_<time>.txt`, only when you press **Report a problem** in the pause menu | What was happening at that moment (the aircraft, its position, the weather, recent messages), for you to add to and send if you choose |
| `%LOCALAPPDATA%\SomniumVolandi` | Compiled shaders, built aircraft bodies, and `display.cfg` (this PC's graphics settings) | To start faster next time, and to keep each PC's graphics its own |
| `Pictures\Somnium Volandi` | The photos you take in photo mode, only when you take one | They're yours to keep and share |

None of this leaves your computer unless you send it. Deleting these folders removes all of it; uninstalling the game
doesn't delete your career, so you can reinstall without losing it.

## Internet radio

When you play a station, your computer connects directly to that station's server through Windows Media Foundation.
Like any website, the station sees your IP address and basic request details, and its own privacy policy applies. The
game ships with no stations; you choose every one.

## Steam

When you play the game from Steam, it tells the Steam client on your computer which achievements you have earned and
a short status for your friends list (for example "Flying the Skerry Twin from Glacier Station to Solace Capital").
When you finish a trial it sends your score to that trial's Steam leaderboard, where other players see it next to
your Steam name; a photo you take in photo mode is also added to your Steam screenshots, which stay private until you
share them. If you have Steam Cloud turned on, Steam keeps a copy of your career, profile, settings and trial records. Valve
handles all of that under its own privacy policy; none of it reaches us. Outside Steam, or in the version from our
own download, the game doesn't talk to Steam at all.

## Diagnostics

`diagnostics.bat` collects a report to help us fix performance problems: your graphics card, driver, processor, memory,
Windows version, monitors and refresh rates, benchmark timings, screenshots of test scenes, the logs above, the
summaries of any crash reports (not the crash dumps) and your settings file. It saves the report as a zip file next to the game and sends nothing. If you choose to send it to us,
we use it only to diagnose and fix problems, keep it only as long as that takes, and delete it sooner if you ask.

## Crash reports

If the game crashes, it writes a small crash dump and a copy of the log to `%APPDATA%\SomniumVolandi\crash` and tells
you about it on the next launch. A crash dump records the state of the game's program at the moment of the crash; it
can include fragments of the game's memory, such as the names of files the game had open. It isn't uploaded. If you
choose to send it to us, we treat it like a diagnostics report.

## Children

The game collects no personal data from anyone, including children.

## Your rights

Because we receive no data unless you send it, there is normally nothing for us to access, correct or delete. If you
have sent us a diagnostics report or crash dump, you can ask for a copy or ask us to delete it.

## Changes

If the game's handling of data changes, we will update this notice and say so in the release notes.

## Contact

CDAIII - contact address to be published with the release.
