## What's new

### Menus, hangar and Free Flight (Codex's review branch, merged)
- Redesigned menus: a consistent dark aviation look, clearer cards, panels that scroll on their own, and keyboard / gamepad focus that stays put while you scroll.
- The career hangar shows the selected aircraft parked in a 3D hangar. Buying, financing, fuel and servicing work as before.
- The four research aircraft appear after the career aircraft as classified teasers: an outline in low light, redacted details, nothing to buy.
- New **Free Flight** on the main menu: any of the nine ordinary aircraft, any of the 16 airports, on the runway or airborne, with full tanks. No money, licence or ownership needed, and nothing touches your career or save. Short runways give a warning, not a block.
- Propellers on every propeller aircraft and on traffic have tapered, curved blades and a smoother blur when spinning.
- The loading bar now counts all 41 shader programs and the rest of the start-up work, so it moves with what is actually done.
- A smoother logo and icon, and anti-aliased lines in the interface.

### Research terminal
- The terminal's boot screen has a loading bar like the launch screen's. It shows how much of the terminal's set-up is done: each research aircraft and its cockpit, the site's scenery and its terrain lighting. It says what is being loaded and how much is left, and reaches 100% only when everything is ready.
- The fingerprint, retina and neural scans now move with the loading, up to twice their old speed. A quick load gets you through in about three seconds. During a long step the scans wait, while their sweeps and traces keep moving. ACCESS GRANTED appears the moment everything is ready, where it used to hold on that page until the loading caught up.

### Scenery
- Trees and bushes keep each detail level about 10–12% farther away: trees reach 2.9 / 5 / 7.8 km on Low / Medium / High. Camera feeds and scenery shadows keep their previous budgets, and placement density and the tree models are unchanged.

### Performance
- Every cockpit draws faster. Each cockpit pixel started by copying the aircraft's shape parameters into memory before anything else, because one fuselage lookup picked its entry with a loop counter, which forces the GPU to keep the whole copy in slow per-thread memory. The lookup now picks its entry directly, so the parameters stay in fast storage. The XR-30 and XR-40 cockpits, which never draw traffic, read them straight from where the game supplies them. Nothing looks different.
- The aircraft bodies rebuild once on the first launch of this version.

### Diagnostics
- The analysis's cockpit-shading check now uses a separate build of the cockpit shader, made only when the analysis runs, so the game's own shaders carry nothing extra for it.
