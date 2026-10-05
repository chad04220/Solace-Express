# Mantis geometry review and corrections

## Corrected defects

- Replaced undersized flap, aileron and rudder fillers with partitions of the exact same parent wing/fin solids used for their recesses. Neutral controls now inherit the original exterior thickness and curved trailing edges, instead of ending in square notches.
- Put every control hinge on its authored seam axis. Added coaxial rounded leading pockets so deflected surfaces clear the fixed airframe. Deliberate per-edge reveal is 4 mm; it is not a missing strip of airfoil.
- Mirrored the main-gear shutter travel. The nose shutter now moves aft along the centreline rather than sideways. Fixed runners retain all three panels over their travel.
- Enlarged gear covers to overlap the complete well perimeter. Enlarged bay leaves to close the centre split and perimeter against the existing fairing. These are closed-position skin fixes, not a claim of a real pressure seal.
- Added paired inboard guide rods to each missile carrier, using its existing registered slide. Both upper spherical guide ends remain captured in the fixed housing/body over 21 sampled positions in the 0–0.48 m deployment, with at least22mm sampled overlap; no new rig IDs or channels are introduced.
- Corrected the top view to a true orthographic axis view. Added matched front, rear, top and belly silhouettes plus neutral, view-aligned even-light views, avoiding false asymmetry from directional lighting.

## Verification

`python build_pack.py` regenerates the complete registered part contract. `python validate_pack.py` runs analytical checks and the exact-GLSL geometry regression suite. `python render_preview.py --audit` regenerates the exterior previews and diagnostic views. System Mesa/EGL executes the delivered shader itself; the distance tests do not use a separate approximate Python reconstruction.

- 640,000 mirrored point pairs across eight states: neutral, half/full gear, pitch+flaps, sign-reversed roll/yaw, half/full bay, and swapped unequal store deployment/occupancy. No distance discrepancy in the sampled float readback. Intentional cockpit handedness and nav-light colour are excluded.
- Full-depth parent-volume fill checks pass for controls. 263,718 moving control-volume probes over nine states per surface found no fixed-wing/fin penetration.
- 60,000 closed bay-skin points and 18,000 gear-well cover points contain no uncovered samples.
- 6,000 cabin-boundary rays find at least 70.1 mm sampled outer-shell margin. This is a sampling result, not a global mathematical proof.
- Both engine support paths remain connected to the body. Symmetric wings, nacelles, fin, canards, gear, bay and store geometry are retained.
- Flight specification rows, mass, lift/drag, thrust and flight-test inputs are unchanged. Existing isolated flight results remain applicable; this review is not a new in-game renderer test.

See `geometry_audit.json`, `validation.log` and the `audit_*.png` images for reproducible evidence. Intentional moving clearances, open intakes/exhausts, gear wells with gear extended, deployed stores and open bay doors are not accidental holes. Runtime extraction, material registration and full game integration remain pending.

## New research cockpit

The integrated cabin is a bespoke angular graphite/amber research station inspired by the existing XR-9/XR-11 cockpit vocabulary, with framed camera panels, illustrated reference instruments, console banks, harnessed seat, vents, lighting and connected supports. All 115 static interior primitives stay inside the original cavity with at least 10.918 mm conservative clearance. The registered moving controls remain unchanged. Five dedicated cabin views and control-sweep evidence are included; see INTERIOR.md. Camera and instrument markings are explicitly static illustrations, not live avionics.
