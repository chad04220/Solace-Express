# Unified aircraft previews, hangar scale and immediate military loading

Local review candidate based on cockpit-fix tree `aa4e7fb6ab1a5649b80cce1da1df808b40232a4e` (the same source tree published as `aa1ec799388981ead6fe0772d1ca844cac3f7dc4`). No remote push, merge or release is included in this change.

## Aircraft inspection

Hangar, Free Flight, Research and Military Career now use one shared `MenuPreviewView` state contract and one control strip. Each menu retains its own palette and surfaces: warm civilian hangar, blue research terminal and olive military field terminal.

- Identical order: rotate left, rotate right, zoom out, zoom in, Reset. Group labels and the current numeric zoom stay readable at compact sizes.
- Rotation steps are 15 degrees; zoom steps are additive 0.10, clamped to 0.75–1.35. Reset uses the same relative front-quarter pose and 1.00 zoom. Selection changes reset the selected view.
- Mouse dragging uses the same yaw/elevation sensitivity. The wheel zooms only over the aircraft stage. A focused preview strip or hovered stage accepts right-stick yaw/elevation and trigger zoom; equipment/list rails retain their scrolling controls.
- Holding a rotation/zoom button repeats after 0.35 seconds, every 0.08 seconds. Releasing, leaving a mouse button, changing focus, changing aircraft or leaving the page stops the gesture. Alt-Tab clears all preview gestures; controllers must return to neutral before re-arming, preventing held-button or analogue input from leaking into an unfocused menu. Reset never repeats a held gesture.
- The menus retain independent state. Inspection never changes equipment, aircraft ownership, money, saved progress or flight controls. Unattended aircraft remain at the selected inspection angle; the terminal's decorative animations retain their existing behavior.
- Aircraft use a common fit margin, relative initial orientation and exact off-center framing. The showroom rotates the grounded airframe rather than taking the camera through opaque walls. Its elevation is constrained by the floor and roof; research runway cameras retain their ground safety limit. Stored pitch is clamped to the displayed limits, so reversing at a floor/roof limit responds immediately.
- Military supplied-kit text moves into the existing scrollable briefing rail, freeing the same stage-plus-bottom-controls layout used elsewhere. Classified mission silhouettes still reveal geometry only, with no paint, light, emission or cockpit-screen leakage.

## Real-world hangar scale

Aircraft geometry remains in metres. The room envelope continues to fit the selected aircraft, while human-scale reference objects no longer grow with it:

- Workbench: 2.4 m-wide top, 0.90 m working height.
- Crates: 0.8 × 0.9 × 1.0 m, with 0.3 m gaps.
- Safety stripe: 1.2 m above the floor, 0.16 m thick.
- Floor: existing 3 m slab spacing and fixed-width paint; the transverse stop bar is 3.6 m long.

Furniture locations adapt to the room, outside the aircraft's entire rotating footprint. The workbench and crates stand on the same floor as the aircraft's tyres. The Kestrel/Atlas renders show the same physical props and floor spacing becoming appropriately small beside the transport. Additional contextual captures use a test-only inspection camera with the menu sidebars removed, making the actual 0.90 m bench visible alongside each craft. These captures do not change the production camera, aircraft or hangar geometry.

## Measured floor dimensions

Amber rulers and two-row WINGSPAN / LENGTH labels are real, horizontal floor geometry, not an overlaid image. The rulers track the parked aircraft's body-to-world transform and remain 12 mm above the concrete to avoid depth fighting. Labels reuse the game’s proportional SDF font atlas on floor-aligned quads, with linear texture filtering and derivative-based edge coverage matched to the actual concrete material. They face the camera and use restrained foreground-correct perspective scaling; the room, slab grid and furniture remain fixed. Classified aircraft emit no measurement geometry, labels or numeric values.

Measurements come from the exact rigid exterior vertex array already loaded or baked for rendering. Bounds collection does not alter, resize, rebake differently, or replace aircraft geometry, materials, indices, moving parts or flight physics. The renderer retains its left/right/nose/tail extrema once during the existing CPU-to-GPU upload; there is no new bake, GPU readback, cache-format change or use of nominal flight-physics dimensions. WINGSPAN is the full lateral rigid-mesh extent, including fixed tip fittings. LENGTH is the longitudinal rigid-airframe extent, including fixed spinners/nose fittings; animated appendages, propeller rotation and deployed equipment are excluded. Values are rounded to two decimal metres. Labels wait for valid mesh data rather than inventing a fallback.

The captured Kestrel measures 10.32 m × 7.68 m; Atlas measures 40.02 m × 42.67 m. These are the rendered exterior envelope, so they may exceed the nominal bare-wing/fuselage entries. Lines use the same measured extrema. Geometry tests cover asymmetric bounds, invalid/nonfinite meshes, exact formatting, grounded transforms, orientation, classified privacy and room containment across 96 small/large yaw/pitch poses. The annotation budget is at most 228 vertices in those tests.

## Military launch ordering

Interactive Deploy queues a contract and changes to `SCR_LOADING` without saving or setting up the mission. Because Deploy is handled while drawing the menu, the end of that same `Game::render()` replaces the old menu frame with a lightweight loading card. The already-rendered military scene is retained before any UI panels in a bounded GPU texture (at most 1280×720), and reused as the first card's aircraft artwork. The snapshot is keyed to both mission and aircraft, rejects classified/unfinished frames, preserves its actual aspect ratio and is released on shutdown. A previously cached assigned-aircraft photograph can also be used. No CPU image readback, uncached image decode, mesh bake, collision warmup or additional scene render is required to draw the card.

The Win32 loop calls `Game::framePresented()` only after successful `SwapBuffers`. `updateLoading()` cannot perform the save, loaner setup or expensive world initialization until that acknowledgement. A queued cancellation wins even immediately after presentation. Repeated Deploy activation is ignored; failed storage returns to the existing save-error state. Mission loaners, saved-attempt validation and the completed-scene readiness gate are preserved. GPU operations stay on the owning thread. Headless simulations deliberately retain their synchronous launch path; the new interactive regressions explicitly disable that shortcut.

## Verification

- Native Release aggregate: all 120 configured tests pass. The aggregate uses the repository's `FLIGHT_QUICK=1` representative flight-model coverage. No test failed.
- Preview suite: 7,192 assertions, zero failures. It covers all 15 catalog aircraft, Free Flight, Research and Military; 1920×1080, 1280×720, 960×540, 800×600 and 640×480 at 100% and 140% scale; full yaw/zoom/elevation bounds; room-triangle occlusion; tyre contact; classified privacy; input capture/release; keyboard and D-pad navigation from no focus; career-save isolation.
- Military gameplay: 675 assertions, zero failures, including real queued-card rendering/presentation ordering, delayed save/setup, duplicate activation, cancellation before/after presentation, retry and readiness isolation.
- ASan/UBSan: military gameplay, preview controls and hangar geometry pass. Leak detection is disabled in the sandbox; bounds/use-after-free and undefined-behavior checks remain active.
- Geometry checks inspect emitted triangles across 12, 16.5, 24, 42.6 and 50 m room sizes, asserting fixed metric prop dimensions, grounding, wall containment and all-yaw aircraft clearance. Geometry remains 3,960 vertices at every size.
- Windows x86-64 cross-build linked successfully. SHA-256: `cd1ed87490561dd818e8d28e41c989a35390d02bc68fb4eee775dd9c1b2c615e`.
- Native EGL/llvmpipe captures use the production renderer and actual selected aircraft meshes. The diagnostic adapter adds mesh-availability/G-buffer-pixel assertions, without changing geometry, materials, lighting or postprocessing. Captures cover all three menus, Kestrel/Atlas scale references, locked silhouettes and compact layouts. The real Deploy-click capture asserts that the very frame which receives the click ends with the loading card while the saved attempt and initialized mission remain absent.
- Native UI interaction harness passes research selection/equipment, scrolling/focus, loading-readiness, all-aircraft framing, classified privacy and Free Flight navigation checks.

Images and a verification manifest are in [`menu-preview-standardization-review/`](menu-preview-standardization-review/). Captures demonstrate layout and renderer behavior, not native Windows/controller feel or hardware performance. A native Windows runtime test is still required before making performance claims.
