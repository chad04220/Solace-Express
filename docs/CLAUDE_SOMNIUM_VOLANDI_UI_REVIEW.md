# Somnium Volandi — UI and loading rebrand

The game now presents itself as **Somnium Volandi** (“the dream of flight”). Civilian screens share a midnight-blue, platinum and brass identity; NIGHTGLASS Research has moving emerald code; Military Career uses matte field instruments and amber deployment controls. Startup and pre-flight loading explain the work behind their progress bars.

This implementation is on `codex/somnium-volandi-ui-rebrand-20261012`, based on the active default branch `claude/compassionate-davinci-4cfo20` at `a6cb4b7409442da824df1bf0339fb9e06e8bee94` (v3.49.2 diagnostics update). It is prepared for Claude's review and integration. No merge or release deployment is part of this handoff.

## Identity and navigation

| Surface | Identity | Practical behavior |
| --- | --- | --- |
| Startup | Wing-and-horizon icon, two-line title, navigation chart | Current operation, task bar, subsystem checklist and overall progress |
| Main menu and civilian hub | Midnight blue, platinum text, brass selection | Existing career, free flight, hangar, airline, logbook, settings and controls navigation |
| Research | Emerald on deep green-black, terminal typography | Falling code and crossing packets; periodic glyph face, swept wing and helix; opaque data rails and a quiet region around the live aircraft |
| Military Career | Charcoal/olive metal, edge wear, recessed fasteners | Existing mission register, saved service metrics, briefing, equipment, preview controls and clear amber deployment action |
| Flight preparation | Context-specific brass, emerald or field amber | Scene, scenery, terrain-lighting and readiness stage bars; remaining chunks, rendered frames and elapsed time |

`src/ui_identity.h` owns the brand colours, vector drawing helpers, field surfaces and code effect. The effect uses the game's existing glyph atlas. Fixed character-cell positions preserve the designs despite the atlas's proportional font. The work is bounded to 76 columns × 40 rows, and has no extra shaders, textures, simulation entities or expanding particle buffers.

The SVG and PNG icon, seven individually antialiased ICO frames (16, 24, 32, 48, 64, 128 and 256 px), window title and Windows file metadata use the new identity. `python tools/generate_icon.py` regenerates the assets with Pillow. Main navigation and preview controls retain their current keyboard, mouse and controller behavior. Research's existing U+I / paired-stick access is preserved.

Settings → General → **Menu animation** persists `menuMotion` in `settings.cfg`. Reduced motion freezes the code field, stops the unattended Research turntable, fixes the intro's chart sweep and stops loading-photo zoom. Operational progress, controls and the functional access sequence still work.

## Loading behavior

The percentages describe **completed workflow work**, not predicted elapsed time. They are neither time-driven nor an ETA. Some expensive graphics-driver calls expose no internal progress; the UI names the operation and displays elapsed time and a “task running” status when completion has not advanced. Decorative movement never credits work.

`LoadPacer` retains monotonic overall progress and adds a single locked snapshot with subsystem groups, the current stage, elapsed/idle time and an independent bounded-task counter. Task counters may restart when the next GPU field or batch begins; that does not decrease or inflate overall completed work. Skipped optional cache work remains separate from completed work.

The startup plan now includes:

- Settled graphics-program counts and the observed island-generation work. Island sampling and smoothing advance as real rows complete; later stages name drainage, road routing, settlements, masks, envelopes, bridges, landmarks and cache writing. Cached islands report their reconstruction stages separately.
- Pilot performance/recovery checks, renderer setup and menu scenery.
- Base surface-material layers reported from their workers through the locked tracker. These callbacks never present a window or touch GL.
- Every prewarmed exterior/cockpit view, with completed mesh-phase callbacks and an exact GPU-row task bar inside each field evaluation. Cached views complete when their real preparation frame returns.

The intro reads the tracker from its independent context and draws the actual completed-work fraction directly. It shows subsystem progress, task completion counts, total initialization time and time in the current stage. The main-context fallback uses the same snapshot.

Research warm-up also shows measured mesh-phase/batch activity. Its overall bar reads `resWork`; the decorative biometric sequence can ease toward that work without changing the loading percentage. The terminal still waits for the existing scenery/bake/lighting readiness gate.

Pre-flight progress exposes the existing scene work as four stages: 10% initial rendered setup, 65% observed scenery completion, 15% terrain-lighting readiness and 10% verification. A completed launch still requires **two distinct stable rendered frames**. Repeated updates without a render cannot open the flight. Optional absent terrain-shadow programs still cannot deadlock readiness. Percentages below completion are floored and capped at 99%; 100% belongs to readiness.

## Build and integration

The existing CMake **target** `SolaceExpress` is kept for compatibility with current build commands and CI selection. Its output name is now **`SomniumVolandi.exe`**. The project name, release display name, distribution folder, Windows artifact names and ZIP are rebranded. `diagnostics.bat` launches the new executable. The internal portable-aircraft bundle name stays compatible with the current producer/consumer jobs.

Existing `%APPDATA%\SolaceExpress` saves/settings/radio data and `%LOCALAPPDATA%\SolaceExpress` caches remain in place. The repository name, world place names such as Solace Capital, cache formats and shader/cache environment switches are retained. This branch does not add a career-format migration; it inherits the base's v6 saves. The README's old format-5 headline was corrected to match that existing base.

The conservative aircraft provenance contract fingerprints several renderer/mesh files touched by loading observations. **Use the normal matching-source aircraft producer and packaging workflow.** Do not carry forward an old mesh manifest or weaken provenance checks to avoid regeneration. The instrumentation does not alter geometry calculations, but the current source manifest must still match the packaged assets.

One review-tool repair aligns `prepare_ui_hangar_adapter.py` with the base's already-renamed `fpFlat` argument: its assertions are inserted after the production flat-to-round frame conversion. The adapter remains test-only. Its capture diagnostic was corrected to describe the startup programs it actually builds.

## Visual review

[Preview gallery and validation](somnium-volandi-review/README.md) contains the new icon, startup/ready screens, real 3D menu/Hangar/Research/Military captures and explicit UI-only layouts for the XR-40 terminal and compact views. The isolated code-field captures show the face, wing and helix at useful points in the animation cycle.

UI-only loading screenshots use deterministic tracker/scene fixtures to exercise layout. They are not traces of loading a particular PC. Live 3D captures use the production renderer on Mesa llvmpipe; the fleet adapter adds preparation/availability assertions and is identified in the logs.

Native C++ builds, a Windows x64 MinGW cross-build, targeted completion/concurrency/input tests and rendered layout checks passed. See the validation record for exact scope. **A native Windows run on the owner's GPU remains the integration check** for first/cached startup, driver compilation, taskbar icon, controller feel and frame times. Cross-compilation and software-render captures do not establish those hardware results.

Claude should review the visual system and loading observers, run the usual CI/packaging workflow, inspect first/cached starts and the UI on Windows, and then merge this branch into the active game branch when satisfied.
