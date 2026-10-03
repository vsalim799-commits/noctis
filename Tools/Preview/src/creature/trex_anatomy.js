// Adult Tyrannosaurus rex as a procedural SDF sculpt: a skeleton (bone heads in model space) plus
// ~120 primitive volumes that sdf_mesher.js smooth-blends into one skin.
//
// Units: metres. Axes: +x forward (snout), +y up, +z the animal's RIGHT side; ground at y = 0.
// L = left = -z, R = right = +z. Bones rest with identity rotation (local axes = model axes).
//
// Targets (Data/Species/hell_creek/tyrannosaurus_rex.json and the brief): total length 12 m,
// acetabulum ~3.0 m above ground, skull ~1.45 m long and ~0.9 m wide at the back, femur 1.3 m,
// tibia 1.15 m, metatarsus 0.6 m, foot ~0.8 m, tail ~half the body length, mass ~7 t.
// Soft-tissue volumes are reconstructions. Where a dimension is not pinned down by a measured
// bone, the comment says "plausible": it was chosen to look right, not taken from a paper.
// Lips cover the teeth at rest (extraoral tissues, Cullen et al. 2023 — inferred), so the mesh
// has no teeth, only a closed lip line.
//
// The sculpt is built in two passes: big volumes first, then small details (lip line, eyes,
// nostrils, brow bosses, dermal bumps) are placed by ray-casting onto the surface of those
// volumes, so they stay attached when the volumes are re-proportioned.
import { createSdf } from './sdf_mesher.js';

// ---------------------------------------------------------------------------------------------
// Skeleton. The leg chain uses the bone lengths above in a relaxed standing pose: femur leaning
// ~25 deg forward, tibia ~20 deg back, metatarsus steep, toes flat (hip 3.0 m above ground).
const HIP = [0, 3.0, 0.45];
const KNEE = [0.55, 1.83, 0.47];
const ANKLE = [0.17, 0.74, 0.43];
const MTP = [0.42, 0.17, 0.42];        // metatarsophalangeal joint = head of the toes bone
const SHOULDER = [2.42, 2.5, 0.44];    // glenoid, low on the front of the ribcage
const ELBOW = [2.36, 2.19, 0.5];
const WRIST = [2.55, 2.07, 0.5];

// Axial layout along x (plausible, from a ~2.2 m dorsal series, ~1.1 m S-curved neck and a
// 1.45 m skull): occipital condyle at x 3.55, snout tip at x 4.9, tail tip at x -7.1, so the
// tail behind the ilium (~6.2 m) is about half the 12 m total.
const TAIL_X0 = -0.85, TAIL_TIP = -7.1, TAIL_SEG = (TAIL_TIP - TAIL_X0) / 10;
// Tail held almost level, drooping slightly towards the tip (plausible neutral pose).
const tailY = (x) => 3.2 + 0.07 * (x - TAIL_X0) + 0.0032 * (x - TAIL_X0) ** 2;

const mirror = (p, s) => [p[0], p[1], p[2] * s];
// The head is authored in its own frame and lifted as a block: carriage of the head on the S-curved
// neck (plausible; living animals vary it constantly).
const HEAD_OFFSET = [0, 0.12, 0];
const hp = (p) => [p[0] + HEAD_OFFSET[0], p[1] + HEAD_OFFSET[1], p[2] + HEAD_OFFSET[2]];

function makeBones() {
  const B = [
    { name: 'root', parent: null, head: [0, 0, 0] },
    { name: 'pelvis', parent: 'root', head: [0, 3.0, 0] },          // acetabulum height
    { name: 'spine1', parent: 'pelvis', head: [0.75, 3.2, 0] },
    { name: 'spine2', parent: 'spine1', head: [1.5, 3.2, 0] },
    { name: 'spine3', parent: 'spine2', head: [2.25, 3.15, 0] },
    { name: 'neck1', parent: 'spine3', head: [2.7, 3.15, 0] },
    { name: 'neck2', parent: 'neck1', head: [3.0, 3.36, 0] },
    { name: 'neck3', parent: 'neck2', head: [3.28, 3.55, 0] },
    { name: 'skull', parent: 'neck3', head: hp([3.55, 3.56, 0]) },  // occipital condyle
    { name: 'jaw', parent: 'skull', head: hp([3.6, 3.27, 0]) },     // quadrate-articular joint
  ];
  for (let i = 1; i <= 10; i++) {
    const x = TAIL_X0 + TAIL_SEG * (i - 1);
    B.push({ name: `tail${i}`, parent: i === 1 ? 'pelvis' : `tail${i - 1}`, head: [x, tailY(x), 0] });
  }
  for (const [sfx, s] of [['L', -1], ['R', 1]]) {
    B.push({ name: `thigh${sfx}`, parent: 'pelvis', head: mirror(HIP, s) });
    B.push({ name: `shin${sfx}`, parent: `thigh${sfx}`, head: mirror(KNEE, s) });
    B.push({ name: `meta${sfx}`, parent: `shin${sfx}`, head: mirror(ANKLE, s) });
    B.push({ name: `toes${sfx}`, parent: `meta${sfx}`, head: mirror(MTP, s) });
    B.push({ name: `upperArm${sfx}`, parent: 'spine3', head: mirror(SHOULDER, s) });
    B.push({ name: `foreArm${sfx}`, parent: `upperArm${sfx}`, head: mirror(ELBOW, s) });
    B.push({ name: `hand${sfx}`, parent: `foreArm${sfx}`, head: mirror(WRIST, s) });
  }
  return B;
}

// Small deterministic RNG so the dermal-bump scatter is identical on every build.
function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
}

// ---------------------------------------------------------------------------------------------
// Shapes. Emitters take right-side (+z) coordinates; `both()` also emits the mirrored left copy.
function makeShapes(bones) {
  const out = [];
  const E = (bone, region, center, radii, rotation = [0, 0, 0], blend = 0.1, extra = {}) =>
    out.push({ bone, kind: 'ellipsoid', center, radii, rotation, blend, region, ...extra });
  const RC = (bone, region, a, b, ra, rb, blend = 0.05, extra = {}) =>
    out.push({ bone, kind: 'roundCone', a, b, ra, rb, blend, region, ...extra });
  const CAP = (bone, region, a, b, r, blend = 0.02, extra = {}) =>
    out.push({ bone, kind: 'capsule', a, b, ra: r, blend, region, ...extra });
  // Mirror through the sagittal plane: z -> -z, Euler XYZ (rx, ry, rz) -> (-rx, -ry, rz).
  const both = (fn) => {
    for (const [sfx, s] of [['R', 1], ['L', -1]]) {
      fn(sfx, (p) => [p[0], p[1], p[2] * s], (r) => (s > 0 ? r : [-r[0], -r[1], r[2]]), s);
    }
  };
  // Neck skin folds: bands across the neck axis, only on the flanks (not on the nape or throat).
  const neckFolds = { amp: 0.016, wavelength: 0.18, axis: [0.83, 0.56, 0], lateral: 0.35 };

  // ---- trunk -----------------------------------------------------------------------------------
  // Deep ribcage (back ~3.6 m, gastral belly ~1.95 m) narrowing to the shoulders (plausible soft
  // tissue over the skeleton). The pubic boot is the lowest point, at about knee height.
  E('spine2', 'torso', [1.35, 2.8, 0], [1.12, 0.8, 0.53], [0, 0, 0.03], 0.22);            // ribcage
  E('spine3', 'torso', [2.28, 2.76, 0], [0.6, 0.62, 0.47], [0, 0, -0.15], 0.22);          // chest
  E('spine1', 'torso', [0.4, 2.97, 0], [0.75, 0.6, 0.42], [0, 0, 0], 0.22);               // loins
  E('spine2', 'torso', [1.05, 3.38, 0], [1.65, 0.23, 0.33], [0, 0, 0.02], 0.2);           // epaxial muscles
  E('spine1', 'belly', [1.1, 2.22, 0], [0.85, 0.25, 0.4], [0, 0, 0.05], 0.25);            // gut
  E('spine3', 'belly', [2.2, 2.25, 0], [0.46, 0.23, 0.36], [0, 0, 0.1], 0.22);            // chest floor (coracoids, furcula)
  RC('pelvis', 'belly', [0.1, 2.7, 0], [0.42, 2.02, 0], 0.24, 0.14, 0.2);                // pubis apron
  E('pelvis', 'belly', [0.4, 1.98, 0], [0.34, 0.09, 0.13], [0, 0, -0.1], 0.2);            // pubic boot (keel under the belly skin)
  both((s, m, mr) => {
    E('spine3', 'torso', m([2.15, 2.95, 0.33]), [0.48, 0.42, 0.17], mr([0, 0, -0.45]), 0.18); // scapular muscles
    E('spine3', 'torso', m([2.2, 2.85, 0.4]), [0.5, 0.16, 0.13], mr([0, 0, -1.0]), 0.12);    // scapula blade under the skin
    E('spine3', 'torso', m([2.58, 2.56, 0.25]), [0.3, 0.3, 0.2], [0, 0, 0], 0.16);           // pectoralis
  });

  // ---- pelvis ----------------------------------------------------------------------------------
  E('pelvis', 'torso', [-0.05, 3.3, 0], [0.92, 0.4, 0.42], [0, 0, 0], 0.2);               // ilia + sacrum
  E('pelvis', 'torso', [-0.25, 3.55, 0], [0.75, 0.15, 0.24], [0, 0, 0], 0.15);            // sacral spines
  E('pelvis', 'torso', [-0.55, 2.74, 0], [0.42, 0.32, 0.28], [0, 0, 0.3], 0.2);           // ischia / cloaca

  // ---- tail ------------------------------------------------------------------------------------
  // Depth and width at each tail joint (plausible: very deep base for M. caudofemoralis, laterally
  // compressed, tapering to a point). One elliptical round cone per segment on its own bone so the
  // tail bends smoothly; tiny blends between segments avoid smooth-min bulges at the joints.
  const tailDepth = [1.06, 0.96, 0.86, 0.76, 0.66, 0.56, 0.46, 0.36, 0.26, 0.16, 0.05];
  const tailWidth = [0.82, 0.68, 0.57, 0.48, 0.4, 0.33, 0.26, 0.2, 0.14, 0.09, 0.03];
  for (let i = 0; i < 10; i++) {
    const x0 = TAIL_X0 + TAIL_SEG * i, x1 = x0 + TAIL_SEG;
    const sq = 0.5 * (tailWidth[i] / tailDepth[i] + tailWidth[i + 1] / tailDepth[i + 1]);
    // Section centre slightly below the vertebrae (long chevrons hang below the centra).
    RC(`tail${i + 1}`, 'tail', [x0, tailY(x0) - 0.05 * tailDepth[i], 0], [x1, tailY(x1) - 0.05 * tailDepth[i + 1], 0],
      tailDepth[i] / 2, tailDepth[i + 1] / 2, [0.2, 0.06][i] ?? 0.02, { scale: [1, 1, sq] });
  }
  both((s, m) => {
    // M. caudofemoralis longus: bulges from the tail base towards the femur's 4th trochanter.
    E('tail1', 'tail', m([-0.8, 2.86, 0.25]), [0.85, 0.33, 0.23], [0, 0, 0.06], 0.2);
  });

  // ---- neck ------------------------------------------------------------------------------------
  // Short, deep S-curved neck: thick at the shoulders, ~0.85 m deep and narrower than the skull
  // just behind the head.
  E('neck1', 'neck', [2.72, 3.08, 0], [0.5, 0.6, 0.42], [0, 0, 0.65], 0.22, { folds: neckFolds });
  E('neck2', 'neck', [3.02, 3.32, 0], [0.45, 0.52, 0.39], [0, 0, 0.8], 0.2, { folds: neckFolds });
  E('neck3', 'neck', [3.3, 3.5, 0], [0.38, 0.47, 0.35], [0, 0, 0.6], 0.18, { folds: neckFolds });
  E('neck2', 'neck', [3.02, 3.62, 0], [0.6, 0.2, 0.24], [0, 0, 0.5], 0.18);                // nape muscles
  E('neck3', 'neck', [3.38, 3.06, 0], [0.45, 0.27, 0.31], [0, 0, 0.45], 0.18, { folds: neckFolds }); // throat
  E('neck1', 'neck', [2.72, 2.62, 0], [0.42, 0.34, 0.38], [0, 0, 0.6], 0.2);              // base of the throat

  // ---- head (skull bone) -----------------------------------------------------------------------
  // Skull 1.45 m from the occiput (x ~3.42) to the snout tip (x ~4.9); ~0.9 m wide across the
  // quadratojugals, narrow snout with near-vertical flanks down to the lip line (y ~3.25).
  E('skull', 'head', hp([3.7, 3.7, 0]), [0.3, 0.28, 0.29], [0, 0, 0], 0.2);                    // braincase / temporal
  E('skull', 'head', hp([3.86, 3.83, 0]), [0.32, 0.12, 0.26], [0, 0, -0.05], 0.12);          // broad skull roof over the orbits
  RC('skull', 'head', hp([4.74, 3.46, 0]), hp([3.98, 3.6, 0]), 0.17, 0.33, 0.12, { scale: [1, 1.04, 0.76] }); // snout
  E('skull', 'head', hp([4.77, 3.43, 0]), [0.12, 0.14, 0.11], [0, 0, -0.2], 0.08);           // premaxilla
  E('skull', 'head', hp([4.79, 3.33, 0]), [0.12, 0.08, 0.115], [0, 0, 0], 0.06);               // front of the upper lip
  both((s, m, mr) => {
    E('skull', 'head', m(hp([3.64, 3.84, 0.15])), [0.22, 0.14, 0.15], [0, 0, 0], 0.18);         // jaw-closing muscles
    E('skull', 'head', m(hp([4.28, 3.45, 0.14])), [0.55, 0.21, 0.09], mr([0, 0.2, -0.04]), 0.12); // maxillary flank
    E('skull', 'head', m(hp([3.76, 3.5, 0.28])), [0.3, 0.22, 0.13], mr([0, 0.3, 0]), 0.15);      // jugal / postorbital
    E('skull', 'head', m(hp([3.57, 3.38, 0.35])), [0.14, 0.18, 0.1], [0, 0, 0], 0.16);             // quadratojugal flare
  });

  // ---- lower jaw (jaw bone) --------------------------------------------------------------------
  E('jaw', 'jaw', hp([4.58, 3.15, 0]), [0.19, 0.11, 0.11], [0, 0, -0.05], 0.07);              // chin (tucked behind the premaxilla)
  RC('jaw', 'jaw', hp([4.52, 3.1, 0]), hp([3.75, 2.97, 0]), 0.09, 0.14, 0.1, { scale: [1, 1, 1.7] }); // throat floor between the rami
  both((s, m, mr) => {
    E('jaw', 'jaw', m(hp([4.24, 3.14, 0.1])), [0.5, 0.11, 0.065], mr([0.3, 0.24, 0.06]), 0.07);   // dentary (lip side tilted out)
    E('jaw', 'jaw', m(hp([3.78, 3.08, 0.2])), [0.34, 0.19, 0.1], mr([0.3, 0.26, 0]), 0.14);      // surangular
    E('jaw', 'jaw', m(hp([3.6, 3.06, 0.29])), [0.2, 0.2, 0.13], [0, 0, 0], 0.18);                // M. pterygoideus bulge
  });

  // ---- legs ------------------------------------------------------------------------------------
  both((sfx, m, mr) => {
    const th = `thigh${sfx}`, sh = `shin${sfx}`, mt = `meta${sfx}`, to = `toes${sfx}`;
    // Thigh: drumstick of M. iliotibialis / femorotibialis / ambiens (plausible volumes),
    // standing proud of the flank so its outline reads as in life.
    E(th, 'thigh', m([0.17, 2.53, 0.54]), [0.6, 0.78, 0.3], [0, 0, 0.43], 0.15);
    E(th, 'thigh', m([0.02, 2.95, 0.5]), [0.78, 0.5, 0.31], [0, 0, 0.1], 0.2);
    E(th, 'thigh', m([0.55, 2.2, 0.54]), [0.2, 0.5, 0.19], [0, 0, 0.43], 0.12);             // anterior thigh
    E(th, 'thigh', m([-0.15, 2.4, 0.48]), [0.3, 0.55, 0.23], [0, 0, 0.55], 0.14);           // flexors
    E(th, 'thigh', m([0.42, 2.02, 0.5]), [0.24, 0.36, 0.21], [0, 0, 0.43], 0.12);           // lower thigh into the knee
    E(sh, 'shin', m([0.57, 1.86, 0.48]), [0.21, 0.21, 0.18], [0, 0, 0], 0.1);               // knee
    // Shank: tibia + fibula, gastrocnemius bulging behind the upper half.
    RC(sh, 'shin', m([0.5, 1.75, 0.47]), m([0.2, 0.85, 0.43]), 0.2, 0.135, 0.1);
    E(sh, 'shin', m([0.29, 1.47, 0.47]), [0.21, 0.46, 0.18], [0, 0, -0.35], 0.1);           // gastrocnemius
    E(sh, 'shin', m([0.48, 1.42, 0.48]), [0.11, 0.38, 0.12], [0, 0, -0.35], 0.08);           // tibialis
    E(mt, 'foot', m([0.17, 0.76, 0.43]), [0.14, 0.13, 0.13], [0, 0, 0], 0.08);              // ankle
    // Metatarsus: long arctometatarsalian block, widening to the toe joints.
    RC(mt, 'foot', m([0.18, 0.74, 0.43]), m([0.4, 0.2, 0.42]), 0.125, 0.128, 0.06);
    E(to, 'foot', m([0.36, 0.11, 0.42]), [0.16, 0.1, 0.15], [0, 0, 0], 0.06);               // metatarsal pad
    // Toes II-IV splay from the metatarsal pad (II medial = towards the midline = -z on the right
    // foot). Each toe: phalanges as round cones with a fleshy digital pad under each one, then a
    // broad, blunt tyrannosaurid claw curving to the ground. Digit III ~0.5 m incl. claw, so the
    // whole footprint is ~0.8 m long.
    const toe = (yaw, len, r0) => {
      const c = Math.cos(yaw), sn = Math.sin(yaw);
      const at = (u, y) => m([0.47 + c * (len * u + 0.03), y, 0.42 + sn * (len * u + 0.12)]);
      const joints = [[0, 0.115, r0], [0.34, 0.085, r0 * 0.85], [0.62, 0.07, r0 * 0.74], [0.84, 0.062, r0 * 0.64]];
      for (let k = 0; k + 1 < joints.length; k++) {
        const [u0, y0, ra] = joints[k], [u1, y1, rb] = joints[k + 1];
        RC(to, 'foot', at(u0, y0), at(u1, y1), ra, rb, k === 0 ? 0.03 : 0.015);
        E(to, 'foot', at(0.5 * (u0 + u1), 0.5 * (y0 + y1) - 0.45 * ra), [0.38 * len * (u1 - u0) + 0.02, 0.42 * ra, 0.85 * ra],
          [0, -yaw, 0], 0.02);                                                                   // digital pad
      }
      RC(to, 'foot', at(0.84, 0.066), at(1.08, 0.018), r0 * 0.68, 0.014, 0.012, { feature: 'claw' }); // claw
    };
    toe(-0.42, 0.42, 0.072);  // digit II
    toe(0, 0.5, 0.082);       // digit III
    toe(0.42, 0.44, 0.072);   // digit IV
    // Hallux (digit I): small, raised off the ground, on the medial back of the metatarsus.
    RC(mt, 'foot', m([0.29, 0.38, 0.34]), m([0.25, 0.25, 0.29]), 0.045, 0.032, 0.03);
    RC(mt, 'foot', m([0.25, 0.25, 0.29]), m([0.28, 0.16, 0.28]), 0.026, 0.008, 0.01, { feature: 'claw' });
  });

  // ---- arms ------------------------------------------------------------------------------------
  both((sfx, m) => {
    const ua = `upperArm${sfx}`, fa = `foreArm${sfx}`, ha = `hand${sfx}`;
    RC(ua, 'arm', m(SHOULDER), m(ELBOW), 0.09, 0.06, 0.06);
    RC(fa, 'arm', m(ELBOW), m(WRIST), 0.06, 0.045, 0.03);
    // Two functional fingers (I and II), claws curled down; metacarpal III vestigial (omitted).
    RC(ha, 'arm', m(WRIST), m([2.63, 2.02, 0.52]), 0.04, 0.03, 0.02);
    RC(ha, 'arm', m([2.63, 2.02, 0.52]), m([2.66, 1.93, 0.53]), 0.028, 0.008, 0.01, { feature: 'claw' });
    RC(ha, 'arm', m([2.6, 2.04, 0.47]), m([2.65, 1.97, 0.45]), 0.03, 0.022, 0.02);
    RC(ha, 'arm', m([2.65, 1.97, 0.45]), m([2.66, 1.89, 0.45]), 0.02, 0.006, 0.01, { feature: 'claw' });
  });

  // ---- surface details ---------------------------------------------------------------------------
  // Second pass: cast rays onto the volumes above and attach small features to the hit points.
  const field = createSdf(bones, out);
  const cast = (o, d, maxT = 1.5) => {
    const l = Math.hypot(d[0], d[1], d[2]); d = d.map((v) => v / l);
    let prev = field(...o);
    for (let t = 0.004; t < maxT; t += 0.004) {
      const f = field(o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t);
      if (f < 0) {
        const u = prev / (prev - f), th = t - 0.004 * (1 - u);
        return [o[0] + d[0] * th, o[1] + d[1] * th, o[2] + d[2] * th];
      }
      prev = f;
    }
    throw new Error(`trex_anatomy: detail ray from ${o.map((v) => v.toFixed(2))} missed the body`);
  };
  // Cast towards `target` (a point near the expected surface) along `dir`, from 0.6 m outside.
  const castAt = (target, dir) => {
    const l = Math.hypot(dir[0], dir[1], dir[2]);
    return cast(target.map((v, a) => v - 0.6 * dir[a] / l), dir);
  };
  const normal = (p) => {
    const e = 0.004, g = [0, 1, 2].map((a) => {
      const q1 = [...p], q2 = [...p]; q1[a] += e; q2[a] -= e;
      return field(...q1) - field(...q2);
    });
    const l = Math.hypot(...g) || 1;
    return g.map((v) => v / l);
  };
  const along = (p, n, t) => [p[0] + n[0] * t, p[1] + n[1] * t, p[2] + n[2] * t];
  const details = [];   // collected first so every ray sees only the big volumes

  both((s, m, mr, sg) => {
    // Lip line: closed lips, the upper lip slightly overhanging the lower one (inferred
    // extraoral tissue, Cullen et al. 2023). A shallow groove plus a rolled upper-lip edge.
    const lip = [[4.78, 3.27], [4.75, 3.262], [4.6, 3.252], [4.4, 3.245], [4.2, 3.246], [4.0, 3.252], [3.86, 3.265], [3.76, 3.285]];
    const hits = lip.map(([x, y]) => cast(hp([x, y, 0.9 * sg]), [0, 0, -sg]));
    for (let k = 0; k + 1 < hits.length; k++) {
      const a = hits[k], b = hits[k + 1];
      const na = normal(a), nb = normal(b);
      // Groove ~8 mm deep and ~4 cm wide: wide enough to survive a 2-3 cm voxel grid.
      const r = k >= hits.length - 2 ? 0.014 : 0.02;
      details.push(() => CAP('skull', 'head', along(a, na, r - 0.008), along(b, nb, r - 0.008), r, 0.014, { sign: -1 }));
    }
    // Front of the mouth: the groove wraps round the premaxilla.
    const f0 = cast(hp([5.3, 3.275, 0.07 * sg]), [-1, 0, 0]), f1 = cast(hp([5.3, 3.275, 0.0]), [-1, 0, 0]);
    details.push(() => CAP('skull', 'head', along(hits[0], normal(hits[0]), 0.012), along(f0, normal(f0), 0.012), 0.02, 0.014, { sign: -1 }));
    details.push(() => CAP('skull', 'head', along(f0, normal(f0), 0.012), along(f1, normal(f1), 0.012), 0.02, 0.014, { sign: -1 }));

    // Eye: high in the skull, looking forward-outward over the narrow snout (binocular field).
    const eyeHit = castAt(hp([3.9, 3.8, 0.27 * sg]), [0.45, 0, -sg]);
    const en = normal(eyeHit);
    // Shallow orbit depression, an eyeball standing ~4 cm proud inside it, then thick lids.
    // A shallow dish for the orbit, the eyeball bulging out of it, and lids that overlap the
    // eyeball (no slit between them: a thin pocket would not survive the voxel grid).
    details.push(() => E('skull', 'head', along(eyeHit, en, 0.0), [0.08, 0.062, 0.022], mr([0, 0.4, 0]), 0.02, { sign: -1 }));     // orbit
    details.push(() => E('skull', 'head', along(eyeHit, en, -0.036), [0.05, 0.05, 0.05], [0, 0, 0], 0.006, { feature: 'eye' }));   // eyeball
    details.push(() => E('skull', 'head', along(along(eyeHit, en, -0.022), [0, 1, 0], 0.038), [0.072, 0.024, 0.04], mr([0, 0.4, 0]), 0.02)); // upper lid
    details.push(() => E('skull', 'head', along(along(eyeHit, en, -0.026), [0, 1, 0], -0.04), [0.064, 0.02, 0.036], mr([0, 0.4, 0]), 0.02));  // lower lid
    // Brow: postorbital boss above-behind the eye, lacrimal horn above-in-front of it.
    const po = castAt(hp([3.8, 3.93, 0.22 * sg]), [0, -0.6, -sg]);
    details.push(() => E('skull', 'head', along(po, normal(po), -0.04), [0.12, 0.07, 0.09], mr([0, 0.2, 0.1]), 0.06));
    const la = castAt(hp([4.03, 3.9, 0.17 * sg]), [0, -0.8, -sg]);
    details.push(() => E('skull', 'head', along(la, normal(la), -0.03), [0.11, 0.05, 0.065], mr([0, 0.3, -0.15]), 0.06));
    // External naris near the top front of the snout, opening forward-outward.
    const na = castAt(hp([4.74, 3.52, 0.12 * sg]), [0.3, 0, -sg]);
    details.push(() => E('skull', 'head', along(na, normal(na), 0.01), [0.05, 0.018, 0.022], mr([0, 0.35, -0.3]), 0.015, { sign: -1 }));

    // Dermal bumps (rugose skin over the nasals, around the orbit and on the jugal): a seeded
    // scatter of low domes, projected on the surface.
    const R = rng(sg > 0 ? 17 : 29);
    const bump = (x, y, z, dir, r) => {
      const p = cast([x, y, z], dir);
      const n = normal(p);
      details.push(() => E('skull', 'head', along(p, n, -0.7 * r), [r * 1.2, r, r * 1.1], [0, 0, 0], 0.02));
    };
    for (let k = 0; k < 7; k++) bump(4.12 + 0.62 * R(), 4.6, (0.02 + 0.1 * R()) * sg, [0, -1, 0], 0.022 + 0.012 * R());
    for (let k = 0; k < 5; k++) {
      const a = Math.PI * (0.15 + 0.9 * R());
      const p = castAt(hp([3.9 + 0.12 * Math.cos(a), 3.8 + 0.11 * Math.sin(a), 0.27 * sg]), [0.45, 0, -sg]);
      bump(p[0] - 0.3 * 0.45, p[1], p[2] + 0.3 * sg, [0.45, 0, -sg], 0.016 + 0.01 * R());
    }
    for (let k = 0; k < 4; k++) bump(3.62 + 0.3 * R(), 3.4 + HEAD_OFFSET[1] + 0.18 * R(), 0.9 * sg, [0, 0, -sg], 0.016 + 0.008 * R());
  });
  for (const add of details) add();
  return out;
}

export const TREX_BONES = makeBones();
export const TREX_SHAPES = makeShapes(TREX_BONES);
