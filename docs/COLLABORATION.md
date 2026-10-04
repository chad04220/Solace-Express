# Working together on Solace Express

Two AI agents work on this game for the owner (**chad04220**), who has the final say on everything:

- **Claude** (Claude Code) writes and ships the code on the `claude/compassionate-davinci-4cfo20` branch, reviews
  everything that comes in, and makes the releases. Claude has the final say on technical decisions.
- **Codex** (the ChatGPT agent) tests builds, reviews source, produces assets (the voice packs) and prototypes larger
  changes (the shared flight model) on its own branches.

Other reviewers' work (for example Jimmy's source review and design proposals) arrives the same way as Codex's: as a
review branch or a post in the coordination issue. Claude checks each finding against the current code before acting on
it.

They can't message each other directly, so this repository is the shared channel.

## Where to talk

- **[Issue #2](https://github.com/chad04220/Solace-Express/issues/2) is the coordination thread.** Handoffs, review
  results, requests and claims of files go there. Every post uses the same heading lines:

  ```
  Agent:  who is posting (Claude / Codex)
  Task:   what the post is about
  Status: done / in progress / waiting on ... / not ready
  Files:  the files changed or claimed
  Base:   the branch and commit the work is based on or was tested against
  Result: what was done or found, with evidence (test output, probe logs, numbers)
  Next:   what happens next and who does it
  ```

- **One-off bugs** can still be filed as their own issue with the *Test report* template (New issue -> Test report),
  with the release version, what you did, what you saw and what you expected.
- **Claim files before editing them.** Say in issue #2 which files you are about to change so the two never edit the
  same file at the same time.

## How code moves

1. **Claude's branch is the game.** Only Claude pushes to `claude/compassionate-davinci-4cfo20`. Every push builds
   and passes the full test suite first (`ctest`, 10 suites: flight model, campaign progression, saves, airport layouts,
   terrain envelope, hull meshes, shader checks, scenery ray casts, the gameplay loop with voices and towers). The
   Windows build is cross-compiled before Windows-only changes are pushed.
2. **Codex works on its own `codex/...` branches** (for example `codex/flight-physics`,
   `codex/source-review-...`, `codex/voice-...`) and posts the branch and commit in issue #2. Claude reviews it,
   takes what it agrees with (merging, porting or re-implementing), and replies with the commit and the test results.
   Codex never pushes to Claude's branch or `main`.
3. **Plain source only.** Code and data are handed over as ordinary files on a branch. Never as encoded packets
   (base64, archives pasted into comments): those are not decoded or applied.
4. **Large binary assets** (voice recordings) that are too big for a comment come to the owner as a download link.
   Claude imports them with the repo's tools (`tools/voice_import.py` for the voice packs, which verifies each clip's
   SHA-256 against the pack's manifest). Licences go in `assets/voice/licenses/` and `THIRD_PARTY_NOTICES.md`.
5. **Reviews** should separate what was reproduced from what was only traced in the source, include the probe or
   test that shows it, and name the commit they were run against. Claude re-runs the probes against its fix and
   reports the before/after numbers.
6. **Disagreements** are settled in issue #2: Claude decides technical questions, the owner decides everything else.

## Who tests what

| Who | Environment | Tests |
|---|---|---|
| **Owner** | Windows PC with a GPU and a controller | Real play: visuals, performance, controls, audio mix, the release zip |
| **Codex** | Linux, **no GPU** | Headless builds and test suites, sanitizers (ASan, UBSan, TSan), probes against the real source, software-GL (llvmpipe) shader probes, scripted flights through the game loop, asset generation |
| **Claude** | Linux, no GPU | Everything above, plus software-rendered screenshots (llvmpipe) to check scenes and UI layouts, and the Windows cross-build |

So GPU performance, real-hardware visuals and controller feel are the owner's checks. Requests to Codex should be
things it can do headlessly.

## Releases

- A release happens only when the owner names a version (for example "push v3.9").
- Claude runs the `build.yml` workflow on its branch with `release_tag` set. The workflow builds on Windows, runs the
  tests, packages the zip (game, voices, loading images, the tools below) and publishes the GitHub release.
- `RELEASE_NOTES.md` is the release text: Claude keeps it up to date with every change since the last release, and
  starts a fresh one after each release.

## Useful tools in the release zip

| File | What it produces |
|---|---|
| `analyze.bat` | `analysis.txt` + heat maps: CPU vs GPU, exact per-pass times, resolution scaling, feature costs, per-pixel work |
| `benchmark.bat` | frame times of several scenes at 1080p and native resolution |
| `profile.bat` | what each ray-tracing feature costs |
| `render_shots.bat` | screenshots of test scenes in `shots\` |
| `render_loading.bat` | loading-screen pictures in `loading\` |
| `render_menu.bat` | `menu.mp4`: the main-menu montage pre-rendered, played on the menu instead of the live ray tracing |

Logs: `%APPDATA%\SolaceExpress\startup.log` (GPU, shader cache, shader errors). Saves and settings are in the same
folder (`career.sav` with its `.bak`, `settings.cfg`, `radio_stations.txt`).
