# Solace Express — Aircraft Pair, Revision 2

Osprey C6 and XR-17 Mantis, revised to correct gaps/asymmetry and add distinct interiors.

## What changed

- Osprey's main gear bays now lie fully ahead of the flap hinges. Its matching model/flight wing-root fields moved aft together, with regression checks rerun.
- Mantis control surfaces now match their parent wing/fin contours, with properly aligned hinge pockets. Main gear shutters mirror outward; the nose shutter stays centered. Closed bay covers and mechanical attachments were checked and repaired.
- Osprey gains an ivory/copper coastal-charter cabin, tobacco upholstery, cocoa webbing and four passenger seats, while keeping the baseline twin-engine instruments and animated flight controls.
- Mantis gains a bespoke graphite/amber research cockpit with framed camera panes, instrument surrounds, switches, supported consoles, harnessed seat, vents and task lights.

## Read these files

- Osprey-C6/AUTHORING.md — corrected exterior/model rows and flight checks.
- Osprey-C6/FIXES.md — geometry audit and intentional asymmetry exceptions.
- Osprey-C6/interior/README.md — dedicated static cabin hook, material instructions and preview/check reproduction.
- XR-17-Mantis/AUTHORING.md and FIXES.md — custom airframe, rig contract and repairs.
- XR-17-Mantis/INTERIOR.md — unique interior source, layout and fit/control checks. The replacement cabin source and metadata are included alongside the custom aircraft.

The Osprey interior is authored as a separate integration hook because the original two-row schema cannot define an entirely unique cabin. Use it with the corrected rows to obtain the requested complete design.

## Integration status

This remains an authoring package, not a shipped game update. Published as a review-only proposal subtree, without production integration. Original authoring and game-test baseline: a2987713ceeb33e3a97560d0c6b85efaf0e52af8. See README.md for the distinct publication base and fresh verification.

The new renderer's mesh-bake/rig interfaces, research registry, live camera feeds and weapon gameplay still need integration. Research preview screens explicitly use static reference markings. Osprey preview gauges use actual baseline instrument code with fixed preview inputs. Neither is a gameplay screenshot or a physical-GPU benchmark.

## Combined roster

On this baseline, insert Osprey at index 7, shift XR-9 to 8 and XR-11 to 9. Resolve Mantis research registration explicitly; simply appending it while keeping the existing array-size-minus-two rule incorrectly adds a research aircraft to the career count. Resolve indices against the actual target branch and preserve existing save IDs.

See individual validation reports for sample counts, exclusions and limitations. Sampled SDF/pose checks are not a proof of every possible mesh-bake state. Test the final integrated renderer and both aircraft on the target hardware before release.
