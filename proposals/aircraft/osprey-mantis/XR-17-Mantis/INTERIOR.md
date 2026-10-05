# Mantis cockpit integration

The cockpit is an original compact angular graphite/amber research station, inspired by the baseline XR-11's angular fighter-seat shell, bolsters, harness, framed instrument blade and fixture lighting, and the XR-9's framed side-camera bays and control banks. References inspected read-only: src/shaders/raytrace_wraith_cockpit.glsl lines 85–170 and src/shaders/raytrace_fs.glsl lines255–340 in Solace-Express a298771.

## Exact integration

This release already combines the reviewed exterior and cabin. build_interior.py regenerates only the complete partOn(40) block from its component definitions; part40_replacement.glsl is the matching standalone snippet. It preserves ids41–44, all rig definitions and the sealed hull cavity. All115 cabin components remain STATIC under id40; the six front switch stems, console keys and annunciators are fixed illustrative controls, not promised moving switches. No geometry uses time or undeclared channels.

Use feed_cameras.json and materials.json from this deliverable. Ids110–111 are unchanged;112 stays a camera-pane material with explicit static preview marking;113–119 add graphite, upholstery, amber diffusers, webbing, illustrative reference instruments, vent graphite and brushed aluminum. The shader function mantisCabinMaterial(m,p) is body-space material shading and is included by the standalone renderer; it must be explicitly registered in the game renderer if these material patterns are wanted there. This package does not install live camera or avionics runtime support.

Three visible feed surfaces, with body-space centers and half-sizes:
- front: (0,.410,-4.123), (.410,.177), inward normal+Z
- port: (-.547,.365,-3.51), (.370,.172), inward normal+X
- starboard: (.547,.365,-3.51), (.370,.172), inward normal-X

The JSON also records solid slab centers, 12mm half-depth and 6mm corner radius. Registration is on the inner front surface, not center of the24mm-thick slab. Eye-relative centers are derived from eye(0,.65,-3.35). Existing external mounts remain(0,.2,-8.05),(-1.05,.5,-3.48),(1.05,.5,-3.48). Final exterior integrator measured these mounts against exact full exterior and hull: nose122.094mm clearance, each side158.072mm. Feed atlas hookup and feedRigOf dispatch remain pending. Panes show an amber calibration reticle and CAM/STATIC labels; other screens show REF plus illustrative bars. These do not represent live telemetry or simulated camera scenery.

## Verification

cavity_clearance.json: all115 primitives are wholly inside the exact original rounded-box cavity. Its convexity means containing every primitive AABB corner proves containment for the entire primitive, including all capsule segments. Minimum conservative clearance is10.918071mm. Smallest modeled thickness is18mm (all details exceed8mm). Static joined subcomponents intentionally overlap to avoid seams.

control_sweep.json: retained controls evaluated at81 positions per declared state interval, with7,360 surface samples for each lever and13,126 for each pedal. Minimum unintended static clearance: pitch12mm, throttle14mm, each pedal12mm. Minimum sampled cavity clearance remains≥179.55mm. Matching fixed pivot sockets and pedal slider tracks intentionally overlap their moving assemblies at their mechanical bearing interfaces and are the only exemptions. This dense sample is not a proof of continuous swept collision clearance. Pitch and throttle use their actual declared axes and angles; pedals use actual declared travel. Controls otherwise unchanged.

All five final views rendered through actual GLSL, Mesa software EGL: cockpit_front.png, cockpit_left.png, cockpit_right.png, cockpit_down.png, cockpit_aft.png. Images were visually checked for frames, labels, seat/harness, consoles and control clearance. They are standalone geometry previews, not screenshots of the game. Render with python render_preview.py. Reproduce static checks with python build_interior.py and OPENBLAS_NUM_THREADS=1 python probe_control_sweep.py.

Regenerate with python build_pack.py; python build_interior.py; python validate_pack.py; python write_authoring.py; python render_preview.py --audit. The authoring generator retains the new feed registration and material definitions, and final previews include both exterior and cabin views.
