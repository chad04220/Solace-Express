# Working together on Solace Express

Two AI agents help develop this game: **Claude** (Claude Code, which writes and ships the code on the
`claude/compassionate-davinci-4cfo20` branch) and a **ChatGPT agent** (which plays and tests builds and suggests
improvements). They can't message each other directly, so this repository is the shared channel.

## How it works

1. **Test findings go in GitHub Issues**, one issue per problem or suggestion, using the *Test report* template
   (New issue -> Test report). Label it `from-chatgpt`. Include the release version (for example `v3.5.3`), what you
   did, what you saw, what you expected, and screenshots or the output of `analyze.bat` / `benchmark.bat` /
   `profile.bat` where they help.
2. **Claude reads the open issues** whenever the owner (chad04220) asks it to, replies in the issue with what it found
   and what it changed (with the commit), and closes it once the fix is in a release.
3. **Disagreements** are discussed in the issue. For now Claude has the final say on technical decisions; the owner
   always has the final say overall.
4. **Code changes**: the ChatGPT agent proposes changes in issues (a patch or code snippet in the issue is welcome)
   rather than pushing to the branch, so the two never edit the same files at once. Claude reviews, applies and
   tests them.

## Useful tools in the release zip

| File | What it produces |
|---|---|
| `analyze.bat` | `analysis.txt` + heat maps: CPU vs GPU, exact per-pass times, resolution scaling, feature costs, per-pixel work |
| `benchmark.bat` | frame times of several scenes at 1080p and native resolution |
| `profile.bat` | what each ray-tracing feature costs |
| `render_shots.bat` | screenshots of test scenes in `shots\` |
| `render_loading.bat` | loading-screen pictures in `loading\` |

Logs: `%APPDATA%\SolaceExpress\startup.log` (GPU, shader cache, shader errors).
