'use strict';
// Genes, archetype presets, mutation, genetic distance, roles and the species registry.

// Every gene is a float in [0,1]. Order is the storage layout of each genome.
P.GENES = [
  // key, label, group, hint
  ['SIZE', 'Size', 'Body', 'Cell radius. Big cells eat small ones but cost more to run.'],
  ['SHAPE', 'Elongation', 'Body', 'Round to rod-shaped. Rods swim faster, rounds resist bites.'],
  ['HUE', 'Pigment', 'Body', 'Colour. Neutral marker gene that drifts with lineage.'],
  ['ARMOR', 'Cell wall', 'Body', 'Reduces bite and toxin damage. Heavy and slow.'],
  ['SPIKES', 'Spines', 'Body', 'Hurt anything that bites or rams this cell.'],
  ['CILIA', 'Cilia', 'Body', 'Agility: how fast the cell can turn.'],
  ['GLOW', 'Bioluminescence', 'Body', 'Glows. Light-seeking prey swim toward it.'],
  ['STORAGE', 'Vacuoles', 'Body', 'Energy storage capacity. Survive nights and famines.'],
  ['PHOTO', 'Photosynthesis', 'Diet', 'Energy from sunlight. Strongest in shallow, sunny water.'],
  ['CHEMO', 'Chemosynthesis', 'Diet', 'Absorbs dissolved nutrients from the water.'],
  ['PRED', 'Predation', 'Diet', 'Bites and digests other cells.'],
  ['SCAV', 'Scavenging', 'Diet', 'Eats detritus left by the dead.'],
  ['PARASITE', 'Parasitism', 'Diet', 'Latches onto larger hosts and drains them.'],
  ['MOTOR', 'Flagella', 'Senses', 'Swimming power. Expensive.'],
  ['SENSE', 'Sensors', 'Senses', 'How far the cell can perceive others.'],
  ['LIGHTSEEK', 'Phototaxis', 'Senses', 'Swims toward light (and toward glowing lures).'],
  ['TRACK', 'Chemotaxis', 'Senses', 'Follows pheromone trails and nutrient gradients.'],
  ['AGGRO', 'Hunting drive', 'Behaviour', 'How hard it chases prey or hosts.'],
  ['FEAR', 'Caution', 'Behaviour', 'How hard it flees predators.'],
  ['SOCIAL', 'Schooling', 'Behaviour', 'Swims with its own kind.'],
  ['SIGNAL', 'Pheromones', 'Behaviour', 'Leaves a chemical trail that kin can follow.'],
  ['TOXIN', 'Toxin glands', 'Behaviour', 'Releases poison into the water around it.'],
  ['TEMP_OPT', 'Heat preference', 'Physiology', 'Preferred temperature, from −5 °C to 45 °C.'],
  ['TEMP_TOL', 'Heat tolerance', 'Physiology', 'Range of survivable temperatures. Costs upkeep.'],
  ['RESIST', 'Detox', 'Physiology', 'Resistance to toxins and viruses.'],
  ['LIFESPAN', 'Longevity', 'Physiology', 'Maximum age. Costs upkeep.'],
  ['SPORE', 'Sporulation', 'Physiology', 'Forms a dormant spore when starving or freezing.'],
  ['REPRO', 'Brood energy', 'Lifecycle', 'Energy saved before dividing. High = sturdier young, slower growth.'],
  ['MUTATION', 'Mutability', 'Lifecycle', 'How much offspring genomes vary.'],
  ['ADHESION', 'Adhesion', 'Lifecycle', 'Daughters stay attached, forming colonies and bodies.'],
  ['DIFFER', 'Differentiation', 'Lifecycle', 'Colony cells specialise: inner cells feed, outer cells swim and fight.'],
  ['CONJ', 'Gene sharing', 'Lifecycle', 'Swaps genes with similar cells on contact.'],
];
P.G = P.GENES.length;
P.GI = {};
P.GENES.forEach((g, i) => (P.GI[g[0]] = i));
const GI = P.GI;

P.ROLES = {
  photo: { label: 'Phototroph', color: '#86d65f' },
  chemo: { label: 'Chemotroph', color: '#4fc9bd' },
  pred: { label: 'Predator', color: '#ff6a55' },
  scav: { label: 'Scavenger', color: '#d2a865' },
  para: { label: 'Parasite', color: '#c58bff' },
  omni: { label: 'Omnivore', color: '#f0c64c' },
};
P.ROLE_KEYS = Object.keys(P.ROLES);

P.defaultGenome = () => {
  const g = new Float32Array(P.G);
  g.fill(0.3);
  g[GI.HUE] = P.rand();
  g[GI.TEMP_OPT] = 0.5; g[GI.TEMP_TOL] = 0.35; g[GI.REPRO] = 0.4; g[GI.MUTATION] = 0.35;
  g[GI.PRED] = 0; g[GI.SCAV] = 0.05; g[GI.PARASITE] = 0; g[GI.TOXIN] = 0; g[GI.ADHESION] = 0.1;
  g[GI.GLOW] = 0; g[GI.SPIKES] = 0.05; g[GI.DIFFER] = 0; g[GI.SPORE] = 0.1; g[GI.CONJ] = 0.1;
  return g;
};
P.genomeFrom = (o) => {
  const g = P.defaultGenome();
  for (const k in o) if (k in GI) g[GI[k]] = o[k];
  return g;
};

// Archetypes used to seed the primordial soup and offered as Lab presets.
P.PRESETS = {
  'Drifting alga': { SIZE: 0.2, PHOTO: 0.95, CHEMO: 0.1, MOTOR: 0.05, LIGHTSEEK: 0.6, STORAGE: 0.5, CILIA: 0.1, HUE: 0.3, REPRO: 0.3, SHAPE: 0.1 },
  'Sun colony': { SIZE: 0.35, PHOTO: 1, MOTOR: 0.02, ADHESION: 0.95, DIFFER: 0.7, STORAGE: 0.6, ARMOR: 0.3, HUE: 0.36, REPRO: 0.35, SPIKES: 0.2, LIFESPAN: 0.6 },
  'Vent eater': { SIZE: 0.3, CHEMO: 0.95, PHOTO: 0.05, MOTOR: 0.25, TRACK: 0.8, TEMP_OPT: 0.75, TEMP_TOL: 0.4, HUE: 0.48, SENSE: 0.3 },
  'Grazer': { SIZE: 0.45, PRED: 0.75, SCAV: 0.25, MOTOR: 0.55, SENSE: 0.55, AGGRO: 0.6, FEAR: 0.6, SOCIAL: 0.6, CILIA: 0.5, HUE: 0.12, SHAPE: 0.4 },
  'Hunter': { SIZE: 0.7, PRED: 1, MOTOR: 0.8, SENSE: 0.85, AGGRO: 0.95, FEAR: 0.15, CILIA: 0.7, SPIKES: 0.35, HUE: 0.0, SHAPE: 0.6, STORAGE: 0.5, REPRO: 0.6 },
  'Scavenger': { SIZE: 0.4, SCAV: 0.95, CHEMO: 0.25, PHOTO: 0.05, MOTOR: 0.45, SENSE: 0.6, FEAR: 0.8, ARMOR: 0.55, SPIKES: 0.45, TRACK: 0.4, HUE: 0.09, SHAPE: 0.3, TEMP_TOL: 0.55, STORAGE: 0.7 },
  'Parasite': { SIZE: 0.1, PARASITE: 1, PRED: 0.1, MOTOR: 0.6, SENSE: 0.6, AGGRO: 0.8, CILIA: 0.6, HUE: 0.8, REPRO: 0.25, LIFESPAN: 0.2 },
  'Toxic bloom': { SIZE: 0.25, PHOTO: 0.8, TOXIN: 0.8, RESIST: 0.95, MOTOR: 0.1, HUE: 0.88, SPORE: 0.5 },
  'Anglerfish': { SIZE: 0.8, PRED: 0.95, GLOW: 0.9, MOTOR: 0.2, SENSE: 0.5, AGGRO: 0.5, ARMOR: 0.4, HUE: 0.62, STORAGE: 0.8, LIFESPAN: 0.7 },
  'Armoured tank': { SIZE: 0.75, PHOTO: 0.4, CHEMO: 0.4, ARMOR: 0.95, SPIKES: 0.8, MOTOR: 0.15, HUE: 0.18, LIFESPAN: 0.8, STORAGE: 0.7 },
  'Swarm': { SIZE: 0.12, PRED: 0.6, SCAV: 0.4, MOTOR: 0.7, SOCIAL: 0.95, SIGNAL: 0.8, TRACK: 0.9, SENSE: 0.5, AGGRO: 0.7, HUE: 0.55, REPRO: 0.2 },
  'Cryophile': { SIZE: 0.4, PHOTO: 0.5, CHEMO: 0.5, TEMP_OPT: 0.15, TEMP_TOL: 0.3, SPORE: 0.8, STORAGE: 0.7, HUE: 0.6, MOTOR: 0.2 },
  'Leviathan': { SIZE: 0.9, PRED: 0.8, SCAV: 0.2, MOTOR: 0.6, ADHESION: 0.85, DIFFER: 0.9, SENSE: 0.9, AGGRO: 0.8, ARMOR: 0.5, SPIKES: 0.5, CILIA: 0.6, HUE: 0.02, LIFESPAN: 0.8, STORAGE: 0.8, REPRO: 0.7 },
};

// Mutate a child genome in place. mult scales the rate (global slider, mutagen field).
P.mutate = (g, mult) => {
  const m = g[GI.MUTATION];
  const rate = (0.015 + m * 0.18) * mult;
  const sigma = 0.035 + m * 0.09;
  for (let k = 0; k < P.G; k++) {
    if (P.rand() < rate) {
      let v = P.rand() < 0.03 ? P.rand() : g[k] + P.randn() * sigma;
      if (k === GI.HUE) v = ((v % 1) + 1) % 1; // hue wraps
      g[k] = v < 0 ? 0 : v > 1 ? 1 : v;
    }
  }
  if (P.rand() < 0.04 * mult) g[GI.HUE] = (g[GI.HUE] + P.randn() * 0.03 + 1) % 1; // neutral drift
};

// Mean absolute gene difference, hue treated circularly and down-weighted.
P.gdist = (a, ao, b, bo) => {
  let s = 0;
  for (let k = 0; k < P.G; k++) {
    let d = Math.abs(a[ao + k] - b[bo + k]);
    if (k === GI.HUE) d = Math.min(d, 1 - d) * 0.6;
    s += d;
  }
  return s / P.G;
};

P.dietShares = (g, o = 0) => {
  const p = g[o + GI.PHOTO], c = g[o + GI.CHEMO], r = g[o + GI.PRED], s = g[o + GI.SCAV], q = g[o + GI.PARASITE];
  const t = p + c + r + s + q + 1e-4;
  return [p / t, c / t, r / t, s / t, q / t];
};
P.roleOf = (g, o = 0) => {
  const sh = P.dietShares(g, o);
  let best = 0;
  for (let k = 1; k < 5; k++) if (sh[k] > sh[best]) best = k;
  if (sh[best] < 0.42) return 'omni';
  return ['photo', 'chemo', 'pred', 'scav', 'para'][best];
};

// ---------- species registry ----------
P.species = [];
P.SPECIATION = 0.09; // mean gene distance from founder that defines a new species

P.newSpecies = (genome, parentId, tick, name, designed) => {
  const id = P.species.length;
  const parent = parentId >= 0 ? P.species[parentId] : null;
  const role = P.roleOf(genome);
  let genus;
  if (name) genus = null;
  else if (parent && parent.role === role && P.rand() < 0.8) genus = parent.genus;
  else genus = P.genusName();
  const sp = {
    id, parent: parentId, genome: Float32Array.from(genome), born: tick, extinct: -1,
    pop: 0, peak: 0, total: 0, role, genus: genus || (name || 'Synthetica').split(' ')[0],
    name: name || genus + ' ' + P.epithet(),
    hue: genome[GI.HUE], designed: !!designed, children: [], hist: [],
    colonial: genome[GI.ADHESION] > 0.55, depth: parent ? parent.depth + 1 : 0,
    meanSize: 0, meanSpeed: 0,
  };
  sp.color = P.hueCss(sp.hue, 0.75, 0.62);
  if (parent) parent.children.push(id);
  P.species.push(sp);
  return sp;
};

// Phenotype: everything the simulation derives from a genome. Fills `out` without allocating.
P.phen = (g, o, out) => {
  const r = 3 + g[o + GI.SIZE] * 11, el = g[o + GI.SHAPE], arm = g[o + GI.ARMOR];
  const p = g[o + GI.PHOTO], c = g[o + GI.CHEMO], d = g[o + GI.PRED], s = g[o + GI.SCAV], q = g[o + GI.PARASITE];
  const t = p + c + d + s + q + 1e-4;
  out.r = r;
  out.ePhoto = Math.pow(p / t, 1.25); out.eChemo = Math.pow(c / t, 1.25); out.ePred = Math.pow(d / t, 1.25);
  out.eScav = Math.pow(s / t, 1.25); out.ePara = Math.pow(q / t, 1.25);
  out.maxSpd = ((0.25 + g[o + GI.MOTOR] * 2.6) * (1 + el * 0.35)) / (1 + r * 0.045) / (1 + arm * 0.7);
  out.turn = 0.035 + g[o + GI.CILIA] * 0.2 - el * 0.015;
  out.senseR = 18 + g[o + GI.SENSE] * 100 + r * 2;
  out.maxE = r * r * (1.6 + g[o + GI.STORAGE] * 3.2);
  out.maxHP = r * r * 0.35 * (1 + arm * 2.5);
  out.metab = r * r * 0.0009 * (1 + 0.5 * arm + 0.35 * g[o + GI.TEMP_TOL] + 0.35 * g[o + GI.LIFESPAN] + 0.3 * g[o + GI.SENSE] +
    0.25 * g[o + GI.SPIKES] + 0.2 * g[o + GI.GLOW] + 0.2 * g[o + GI.STORAGE] + 0.15 * g[o + GI.RESIST] + 0.2 * g[o + GI.CILIA]);
  out.tOpt = -5 + 50 * g[o + GI.TEMP_OPT];
  out.tTol = 3 + 17 * g[o + GI.TEMP_TOL];
  out.maxAge = 2400 + g[o + GI.LIFESPAN] * 14000;
  out.reproF = 0.45 + g[o + GI.REPRO] * 0.5;
  return out;
};

P.speciesLabel = (sp) => sp.name + (sp.designed ? ' ★' : '');
