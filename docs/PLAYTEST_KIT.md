# Closed playtest kit

How to run a closed playtest of Somnium Volandi: what to set up first, what testers get and do, what comes back, and
how to triage it. The tools it relies on are in the game and `diagnostics.bat`. Nothing in the game uploads anything;
testers send what they choose (`PRIVACY.md`).

## Before inviting anyone

- [ ] **Make the repository private** (GitHub → Settings → General → Danger zone → Change visibility). The source is
      public today, and so is every release zip.
- [ ] **Sign the build** (`docs/CODE_SIGNING.md`). Without it, SmartScreen warns every tester.
- [ ] **Fill in the contact address** at the end of `PRIVACY.md`, and have the EULA and privacy notice reviewed for
      the countries the testers are in.
- [ ] **Decide on recording.** The EULA allows videos and streams. For a closed test you may want testers not to
      publish until launch: say so in the invitation, or add a short playtest agreement.
- [ ] **Pick one feedback channel** (a private Discord channel, a form, or email) and put it in the invitation.
- [ ] **Release a build** with `release_tag` (`.github/workflows/build.yml`). Download its `SomniumVolandi-symbols`
      artifact and keep it with the version number. Artifacts expire after 90 days, and a crash dump can only be read
      against the exact build's symbols.

## The invitation (send with the zip)

> Thanks for testing Somnium Volandi. Unzip it anywhere and run `SomniumVolandi.exe`. The first start compiles the
> shaders for your graphics card, which takes one to three minutes; later starts take seconds.
>
> 1. On the first day, run `diagnostics.bat quick` (about 3 minutes) and send us the zip it makes. It tells us about
>    your PC and how fast the game runs on it.
> 2. Play as you like, with the week's focus below in mind.
> 3. When something goes wrong, press **Esc → Report a problem** at that moment. It saves what was happening (the
>    aircraft, where it was, the weather and the last messages) and opens it in Notepad. Add a line about what
>    happened, save it, and send it to us.
> 4. If the game crashes, the next start says so and shows where the crash report is. Send the whole `crash` folder.
> 5. If you can leave your PC running for two hours once, run `diagnostics.bat soak` and send the zip. It checks that
>    nothing slows down or grows over a long session.
>
> Everything stays on your PC unless you send it. The game sends nothing by itself.

## What to test, week by week

| Week | Focus | What to look for |
|---|---|---|
| 1 | The first hours: the lessons, the first licence, the first jobs | Is it clear what to do next? Any lesson that's hard to pass, or a hint that doesn't match the controls? |
| 2 | The career: the hangar, buying and financing, the airline, the military career | Prices and pay that feel wrong, a job you can't finish, anything lost after quitting and restarting |
| 3 | Free Flight, the Earth regions, the research craft | Loading times, stutters, scenery popping in, the research craft's cockpits |
| 4 | Controls and settings: keyboard, gamepad, joystick, throttle and pedals; resolution, full screen, UI scale | A control that can't be bound or doesn't respond; text that's cut off or overlaps at your resolution |

Every week, also say how it ran: smooth, the odd stutter, or slow.

## The feedback form

Ask the same questions every week, so the answers compare across weeks:

1. How much did you enjoy this week's sessions? (1–5)
2. How clear was what to do next? (1–5)
3. How smoothly did it run on your PC? (1–5)
4. Did anything make you want to stop playing? What?
5. The best moment this week?
6. Anything confusing, unfair or broken that you haven't already reported?
7. Hours played this week (roughly).

## Triage, for the developer

- **Crash reports.** `crash-<time>.txt` says what failed and where (the module and offset). Open
  `crash-<time>.dmp` in Visual Studio or WinDbg with that build's `SomniumVolandi.pdb` beside it for the stack. Group
  crashes by module and offset: the same place twice is one bug.
- **Problem reports.** `report_<time>.txt` gives the aircraft, its position, the weather and the last messages, so
  you can rebuild the moment: start the same aircraft at the same airport, set the weather, and fly to the position.
- **Diagnostics.** Run `python tools/hardware_matrix.py <zips…> --md matrix.md` over every tester's diagnostics.
  It gives one row per PC, with each scene's frame rate and 1% low against the 60 fps target, and the soak's verdict
  (`docs/HARDWARE_MATRIX.md`).
- **Before each new build.** `ctest` includes the headless soak (`soak`: a long session in one game, nothing grows)
  and the frozen save format (`save_format`: last week's careers still load). Keep both passing, so testers never
  lose a career to an update.
