// Dump the three.js procedural T. rex anatomy (Tools/Preview/src/creature/trex_anatomy.js) to JSON so
// the Blender creature factory can read the same skeleton and SDF primitives.
//   node Tools/CreatureFactory/export_anatomy.mjs [out.json]
// Default output: Build/Creatures/tyrannosaurus_rex/anatomy_base_from_js.json (the raw dump). The
// curated, improved spec read by the factory is Data/Anatomy/tyrannosaurus_rex.json, produced from
// this dump by `factory.py tyrannosaurus_rex --stages anatomy` (see README.md).
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '..', '..');
const out = resolve(process.argv[2] || resolve(repo, 'Build/Creatures/tyrannosaurus_rex/anatomy_base_from_js.json'));
const mod = await import(pathToFileURL(resolve(repo, 'Tools/Preview/src/creature/trex_anatomy.js')).href);

const round = (v) => (Array.isArray(v) ? v.map(round) : typeof v === 'number' ? Math.round(v * 1e6) / 1e6 : v);
const clean = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, typeof v === 'object' && v !== null && !Array.isArray(v) ? clean(v) : round(v)]));

const doc = {
  schema: 'noctis.anatomy.js_dump/1',
  source: 'Tools/Preview/src/creature/trex_anatomy.js',
  generated: new Date().toISOString(),
  axes: '+x forward (snout), +y up, +z animal right; metres; ground y = 0',
  bones: mod.TREX_BONES.map(clean),
  shapes: mod.TREX_SHAPES.map((s, i) => ({ id: i, ...clean(s) })),
};
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, JSON.stringify(doc, null, 1));
console.log(`wrote ${out}: ${doc.bones.length} bones, ${doc.shapes.length} shapes`);
