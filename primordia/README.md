# Primordia

A cellular life simulation that runs in the browser. Hundreds of founding microbes compete for sunlight, dissolved
nutrients and each other in a warm sea. Every division copies a 32-gene genome with small errors, so lineages drift,
split into new species, form multicellular colonies and go extinct as seasons, currents, climate and catastrophes reshape
the water around them.

Open `index.html` in a current browser with WebGL2 (Chrome, Edge, Firefox, Safari). No build step, no server.

## What lives here

- **32 genes** per cell: body (size, shape, cell wall, spines, cilia, bioluminescence, vacuoles), diet (photosynthesis,
  chemosynthesis, predation, scavenging, parasitism), senses and behaviour (flagella, sensors, phototaxis, chemotaxis,
  hunting, caution, schooling, pheromones, toxins), physiology (heat preference and tolerance, detox, longevity,
  sporulation) and lifecycle (brood energy, mutability, adhesion, differentiation, gene sharing).
- **Emergent roles**: phototrophs, chemotrophs, predators, scavengers, parasites and omnivores, decided by what the genome
  invests in. Specialists beat generalists at their one thing.
- **Behaviour**: cells steer by gradients of light, nutrients, pheromones, toxins and temperature, chase prey, flee
  predators, school with kin, follow the scent of crowds and carrion, and are lured by glowing predators.
- **Multicellularity**: adhesive daughters stay bonded, colonies share energy along their bonds, and with differentiation
  inner cells feed while outer cells swim and fight.
- **Speciation and phylogeny**: when a lineage drifts far enough from its species' founder it becomes a new species. The
  Tree tab draws the full tree of life.
- **Disease**: viruses target a genome, so close relatives catch it and mutants escape. Survivors become immune.
- **Horizontal gene transfer**, dormant spores, parasites riding hosts, and detritus that rots back into nutrients.

## The environment

- Day and night, four seasons, and a climate that drifts from year to year.
- Ocean currents from drifting stream-function modes, plus whirlpools you add.
- Shallows and depths, moving cloud shade, upwelling zones and hydrothermal vents.
- Rock that walls off basins.
- Natural events: eruptions, upwellings, ice ages, heat waves, earthquakes, toxic seeps, meteors, mutagen bursts and plagues
  that strike whichever species dominates.

## Playing

Tools on the left: Inspect, Feed, Toxin, Heat, Cool, Sunlight, Shade, Whirlpool, Rock, Erode, Mutagen, Zap, Virus,
Tweezers and Spawn. The panel on the right has Ecosystem charts, a searchable Species list with lineage and actions, the
Tree of life, Climate settings and catastrophes, the Lab for designing your own organism with live preview and viability
notes, and a Cell inspector. Press `?` in the game for every shortcut.

## How it runs fast

- **GPU chemistry**: nutrients, toxins, pheromones and mutagen live in RGBA32F textures. A fragment shader advects them
  along the same current field the cells feel, diffuses, decays, adds upwelling and masks rock, ping-ponging between two
  render targets once per tick. The renderer samples those textures directly.
- **Asynchronous readback**: the field is copied into a pixel-pack buffer guarded by a fence and read a frame later, so
  the CPU never stalls waiting on the GPU. Cells' consumption and secretion accumulate in a delta texture uploaded once
  per frame. When float render targets are missing, a CPU path runs the same maths.
- **Structure-of-arrays pool** of 16,384 cells in typed arrays, with a free list and no per-tick allocation.
- **Counting-sort spatial hash** rebuilt every tick for collisions, contacts and perception, plus a coarse 32×32 density
  map for long-range scent.
- **Staggered perception**: each cell thinks every third tick; movement and physics run every tick.
- **Cached phenotypes** and a cached current field, so hot loops do no trig per cell.
- **Instanced rendering**: one buffer upload and one draw per layer. Cells are shaded procedurally (membrane wobble,
  chloroplasts, nucleus, maw, plated walls, beating cilia, flagella, glow halos, cysts, infection) with a cheap level of
  detail when zoomed out, and off-screen cells are culled before upload.
- **Time budget**: the loop runs as many ticks as the chosen speed asks for within about 11 ms, so the view stays smooth.
- **Throttled UI**: panels, charts and the minimap redraw at their own low rates.

## Files

| File | Contents |
| --- | --- |
| `js/util.js` | Constants, RNG, periodic noise, colour, names |
| `js/genome.js` | Genes, presets, mutation, phenotype, roles, species registry |
| `js/world.js` | Light, temperature, currents, vents, seasons, climate, painting |
| `js/gpufield.js` | GPU field compute with async readback, CPU fallback |
| `js/sim.js` | Cells, physics, behaviour, metabolism, reproduction, viruses, census |
| `js/render.js` | WebGL2 renderer and Lab preview |
| `js/events.js` | Natural events and catastrophes |
| `js/ui.js` | Tools, input, panels, charts, tree, Lab, inspector |
| `js/main.js` | Boot and frame loop |
