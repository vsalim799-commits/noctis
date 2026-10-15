// Adult Tyrannosaurus rex as a procedural SDF sculpt: a skeleton (bone heads in model space) plus
// ~200 primitive volumes that sdf_mesher.js smooth-blends into one skin.
//
// Units: metres. Axes: +x forward (snout), +y up, +z the animal's RIGHT side; ground at y = 0.
// L = left = -z, R = right = +z. Bones rest with identity rotation (local axes = model axes).
//
// Targets (Data/Species/hell_creek/tyrannosaurus_rex.json and the brief): total length 12 m,
// acetabulum ~3.0 m above ground (hip height 3.1 m), skull ~1.45 m long and ~0.9 m wide at the
// back, femur 1.3 m, tibia 1.15 m, metatarsus 0.6 m, footprint ~0.8 x 0.7 m, tail ~half the body
// length, mass ~7 t (6-9.5 t). Soft-tissue volumes are reconstructions: where a dimension is not
// pinned down by a measured bone the comment says "plausible", i.e. chosen to look right within
// the range seen in published skeletal reconstructions, not taken from a paper.
// Lips cover the teeth at rest (extraoral tissues, Cullen et al. 2023 — inferred), so the mesh
// has no teeth, only a closed lip line. Skin: small non-imbricate scales (Bell et al. 2017) are
// left to the skin shader; the sculpt only carries the larger forms (folds, bosses, bumps).
//
// The sculpt is built in passes:
//   1. the head, authored in its own frame (u forward from the jaw joint, v up, w = z), with a
//      cutter that squares off the upper lip and the lower jaw added after it;
//   2. body volumes: lofted elliptical sections for trunk, neck and tail, then muscles and limbs;
//   3. surface details (eye and lids, brow bosses, nostrils, lip groove, dermal bumps) placed by
//      ray-casting onto the volumes above, so they stay attached when the volumes are re-proportioned.
// Shapes are combined in array order (sdf_mesher.js): a subtraction only carves what precedes it.
import { createSdf } from './sdf_mesher.js';

// ---------------------------------------------------------------------------------------------
// Small rotation helpers (three.js 'XYZ' Euler convention, row-major 3x3 matrices).
function eulerToMat([x, y, z]) {
  const a = Math.cos(x), b = Math.sin(x), c = Math.cos(y), d = Math.sin(y), e = Math.cos(z), f = Math.sin(z);
  const ae = a * e, af = a * f, be = b * e, bf = b * f;
  return [c * e, -c * f, d, af + be * d, ae - bf * d, -b * c, bf - ae * d, be + af * d, a * c];
}
function matMul(A, B) {
  const C = new Array(9).fill(0);
  for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++) for (let k = 0; k < 3; k++) C[r * 3 + c] += A[r * 3 + k] * B[k * 3 + c];
  return C;
}
function matToEuler(m) {
  const y = Math.asin(Math.max(-1, Math.min(1, m[2])));
  if (Math.abs(m[2]) < 0.9999999) return [Math.atan2(-m[5], m[8]), y, Math.atan2(-m[1], m[0])];
  return [Math.atan2(m[7], m[4]), y, 0];
}
// Euler angles of the frame whose local x, y, z axes are the given model-space vectors.
function basisToEuler(ax, ay, az) {
  return matToEuler([ax[0], ay[0], az[0], ax[1], ay[1], az[1], ax[2], ay[2], az[2]]);
}
const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add = (p, n, t) => [p[0] + n[0] * t, p[1] + n[1] * t, p[2] + n[2] * t];

// ---------------------------------------------------------------------------------------------
// Skeleton. The leg chain uses the bone lengths above in a relaxed standing pose: femur leaning
// ~25 deg forward, tibia ~19 deg back, metatarsus ~23 deg from vertical, toes flat.
const HIP = [0, 3.0, 0.45];
const KNEE = [0.56, 1.83, 0.48];
const ANKLE = [0.18, 0.74, 0.44];
const MTP = [0.42, 0.18, 0.43];        // metatarsophalangeal joint = head of the toes bone
// Forelimb: humerus ~0.38 m, forearm ~0.21 m (Sue-sized adult); folded under the chest.
const SHOULDER = [2.3, 2.42, 0.4];     // glenoid, low on the front of the ribcage
const ELBOW = [2.36, 2.05, 0.47];
const WRIST = [2.56, 1.98, 0.49];

// Head frame: origin at the jaw joint (quadrate-articular), u along the skull towards the snout,
// v up, w = z. Head carried ~7 deg nose-down (plausible relaxed carriage; living animals vary it).
const JAW_JOINT = [3.46, 3.47];
const HEAD_PITCH = 0.12;
const cp = Math.cos(HEAD_PITCH), sp = Math.sin(HEAD_PITCH);
const H = (u, v, w = 0) => [JAW_JOINT[0] + u * cp + v * sp, JAW_JOINT[1] - u * sp + v * cp, w];
const HEAD_MAT = eulerToMat([0, 0, -HEAD_PITCH]);
const HR = (r = [0, 0, 0]) => matToEuler(matMul(HEAD_MAT, eulerToMat(r)));
const HD = (d) => [d[0] * cp + d[1] * sp, -d[0] * sp + d[1] * cp, d[2]];   // head-frame direction -> model

// Axial layout along x (plausible, from a ~2.3 m dorsal series, a short S-curved neck and a
// 1.45 m skull): snout tip at x ~4.95, tail tip at x -7.05 -> 12.0 m; the tail behind the
// ilium (~6.2 m) is about half the total length.
const TAIL_X0 = -0.85, TAIL_TIP = -7.05, TAIL_SEG = (TAIL_TIP - TAIL_X0) / 10;
// Vertebral line of the tail: held almost level, drooping slightly towards the tip.
const tailY = (x) => 3.3 + 0.045 * (x - TAIL_X0) - 0.0022 * (x - TAIL_X0) ** 2;

const mirror = (p, s) => [p[0], p[1], p[2] * s];

function makeBones() {
  const B = [
    { name: 'root', parent: null, head: [0, 0, 0] },
    { name: 'pelvis', parent: 'root', head: [0, 3.0, 0] },          // acetabulum height
    { name: 'spine1', parent: 'pelvis', head: [0.7, 3.25, 0] },
    { name: 'spine2', parent: 'spine1', head: [1.45, 3.22, 0] },
    { name: 'spine3', parent: 'spine2', head: [2.15, 3.12, 0] },
    { name: 'neck1', parent: 'spine3', head: [2.6, 3.12, 0] },      // cervico-dorsal junction
    { name: 'neck2', parent: 'neck1', head: [2.95, 3.36, 0] },
    { name: 'neck3', parent: 'neck2', head: [3.22, 3.6, 0] },
    { name: 'skull', parent: 'neck3', head: H(0.04, 0.3) },         // occipital condyle
    { name: 'jaw', parent: 'skull', head: H(0, 0) },                // quadrate-articular joint
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
  // Lofted body: elliptical sections {x, top, bot, hw (half-width)} joined by round cones whose
  // section is scaled sideways. Small blends between consecutive segments avoid smooth-min bulges.
  const loft = (boneOf, region, st, blend, extra = {}) => {
    for (let i = 0; i + 1 < st.length; i++) {
      const s0 = st[i], s1 = st[i + 1];
      const r0 = (s0.top - s0.bot) / 2, r1 = (s1.top - s1.bot) / 2;
      const sq = 0.5 * (s0.hw / r0 + s1.hw / r1);
      RC(boneOf(i), region, [s0.x, (s0.top + s0.bot) / 2, 0], [s1.x, (s1.top + s1.bot) / 2, 0], r0, r1,
        typeof blend === 'function' ? blend(i) : blend, { scale: [1, 1, sq], ...extra });
    }
  };
  // Neck skin folds: bands across the neck axis, only on the flanks (not on the nape or throat).
  const neckFolds = { amp: 0.016, wavelength: 0.17, axis: [0.8, 0.6, 0], lateral: 0.35 };

  // ===== 1. HEAD ================================================================================
  // Skull ~1.45 m from the back of the quadratojugal (u ~ -0.05) to the snout tip (u ~1.47); dorsal
  // outline rising from the snout (v ~0.5) to the orbits (v ~0.72) and the temporal region (~0.78);
  // ~0.9 m across the quadratojugals, ~0.3 m across the snout tip (narrow snout, broad temporal
  // region -> forward-facing orbits). Lip line (closed mouth) at v ~0.12-0.2, sinuous.
  const HE = (bone, region, c, radii, r = [0, 0, 0], blend = 0.1, extra = {}) => E(bone, region, H(...c), radii, HR(r), blend, extra);
  const HRC = (bone, region, a, b, ra, rb, blend = 0.05, extra = {}) => RC(bone, region, H(...a), H(...b), ra, rb, blend, extra);
  // Upper head (skull bone).
  HRC('skull', 'head', [1.33, 0.37], [0.74, 0.56], 0.115, 0.165, 0.12);                          // nasal ridge
  HRC('skull', 'head', [1.32, 0.25], [0.52, 0.34], 0.135, 0.215, 0.1, { scale: [1, 1, 1.18] });  // maxillary face
  HE('skull', 'head', [1.37, 0.3, 0], [0.11, 0.17, 0.14], [0, 0, 0.1], 0.08);                   // premaxilla (blunt snout tip)
  HE('skull', 'head', [0.5, 0.66, 0], [0.3, 0.13, 0.2], [0, 0, 0], 0.12);                       // skull roof between the orbits
  HE('skull', 'head', [0.16, 0.56, 0], [0.26, 0.24, 0.24], [0, 0, 0], 0.14);                    // parietal / braincase
  HE('skull', 'head', [-0.02, 0.42, 0], [0.13, 0.3, 0.28], [0, 0, 0], 0.14);                    // occiput (merges into the neck)
  both((s, m, mr) => {
    HE('skull', 'head', m([0.82, 0.58, 0.12]), [0.24, 0.12, 0.1], mr([0, 0.18, -0.12]), 0.1);      // lacrimal region
    HE('skull', 'head', m([0.3, 0.5, 0.21]), [0.3, 0.27, 0.2], mr([0, 0.25, 0]), 0.14);           // jaw-closing muscles (M. adductor)
    HE('skull', 'head', m([0.58, 0.3, 0.24]), [0.34, 0.15, 0.11], mr([0, 0.32, 0.05]), 0.1);      // jugal (cheek under the orbit)
    HE('skull', 'head', m([0.12, 0.24, 0.33]), [0.17, 0.21, 0.13], mr([0, 0.2, 0]), 0.12);        // quadratojugal flare
  });
  // Lip cutter: squares off the bottom of the upper head along the closed-mouth line, leaving a
  // rounded upper-lip edge (the smooth-subtraction radius). Flat-topped ellipsoids chained along the
  // sinuous tooth row: higher at the premaxilla, lowest under the maxilla, rising to the rictus.
  const lipCut = [[1.5, 0.21, 0.16], [1.22, 0.16, 0.24], [0.92, 0.125, 0.26], [0.66, 0.13, 0.22], [0.47, 0.17, 0.12]];
  for (const [u, top, ru] of lipCut) HE('skull', 'head', [u, top - 0.4, 0], [ru, 0.4, 0.6], [0, 0, 0], 0.035, { sign: -1 });
  // Lower jaw (jaw bone): dentary hull narrower than the upper lip so the upper lip overhangs it.
  HRC('jaw', 'jaw', [1.3, 0.06], [0.5, -0.06], 0.115, 0.185, 0.08, { scale: [1, 1, 1.12] });    // mandible + lower lips
  HE('jaw', 'jaw', [1.31, 0.065, 0], [0.11, 0.11, 0.13], [0, 0, 0], 0.06);                      // chin
  HE('jaw', 'jaw', [0.5, -0.17, 0], [0.42, 0.13, 0.2], [0, 0, 0.05], 0.12);                      // throat skin between the rami
  both((s, m, mr) => {
    HE('jaw', 'jaw', m([0.34, -0.02, 0.25]), [0.32, 0.17, 0.1], mr([0, 0.35, 0.08]), 0.1);       // surangular
    HE('jaw', 'jaw', m([0.1, -0.1, 0.29]), [0.17, 0.19, 0.13], mr([0, 0.15, 0]), 0.12);          // M. pterygoideus bulge
  });

  // ===== 2. BODY ================================================================================
  // ---- trunk -----------------------------------------------------------------------------------
  // Deep, short trunk: back ~3.8 m over the ilia, gastral belly ~1.85 m, ribcage ~1.15 m wide
  // (plausible soft tissue over the skeleton). The pubic boot is the lowest point, near knee height.
  const trunk = [
    { x: -0.85, top: 3.74, bot: 2.6, hw: 0.5 },
    { x: -0.3, top: 3.8, bot: 2.42, hw: 0.52 },
    { x: 0.35, top: 3.79, bot: 2.2, hw: 0.5 },
    { x: 1.0, top: 3.73, bot: 1.98, hw: 0.52 },
    { x: 1.65, top: 3.64, bot: 1.95, hw: 0.53 },
    { x: 2.2, top: 3.54, bot: 2.06, hw: 0.47 },
  ];
  loft((i) => ['pelvis', 'pelvis', 'spine1', 'spine2', 'spine3'][i], 'torso', trunk, 0.14);
  E('spine2', 'torso', [1.05, 3.52, 0], [1.55, 0.24, 0.33], [0, 0, 0.04], 0.2);               // epaxial muscles along the back
  E('pelvis', 'torso', [-0.15, 3.52, 0], [0.95, 0.27, 0.36], [0, 0, 0], 0.18);                // ilia + sacrum
  E('spine1', 'belly', [1.15, 2.12, 0], [0.95, 0.3, 0.46], [0, 0, 0.03], 0.24);               // gut over the gastralia
  E('spine3', 'belly', [2.12, 2.18, 0], [0.42, 0.25, 0.36], [0, 0, 0.12], 0.2);               // chest floor (coracoids, furcula)
  RC('pelvis', 'belly', [0.12, 2.65, 0], [0.42, 1.98, 0], 0.26, 0.15, 0.2, { scale: [1, 1, 1.2] }); // pubic apron
  E('pelvis', 'belly', [0.45, 1.94, 0], [0.32, 0.1, 0.17], [0, 0, -0.08], 0.18);              // pubic boot (keel under the skin)
  E('pelvis', 'torso', [-0.6, 2.72, 0], [0.45, 0.3, 0.3], [0, 0, 0.35], 0.18);                // ischia / cloaca
  both((s, m, mr) => {
    E('spine2', 'torso', m([1.3, 2.62, 0.22]), [1.0, 0.55, 0.36], mr([0, 0, 0.02]), 0.2);      // ribcage flank
    E('spine3', 'torso', m([2.02, 3.0, 0.34]), [0.5, 0.42, 0.17], mr([0, 0, -0.5]), 0.16);      // scapular muscles
    E('spine3', 'torso', m([2.3, 2.45, 0.26]), [0.3, 0.32, 0.2], [0, 0, 0], 0.16);              // pectoralis
  });

  // ---- tail ------------------------------------------------------------------------------------
  // Depth and width at each tail joint (plausible: very deep base for M. caudofemoralis, laterally
  // compressed, tapering to a point). One elliptical round cone per segment on its own bone.
  const tailDepth = [1.12, 1.0, 0.88, 0.77, 0.66, 0.56, 0.46, 0.37, 0.28, 0.18, 0.05];
  const tailWidth = [0.98, 0.8, 0.66, 0.55, 0.46, 0.38, 0.31, 0.24, 0.18, 0.11, 0.035];
  const tailSt = tailDepth.map((d, i) => {
    const x = TAIL_X0 + TAIL_SEG * i, yv = tailY(x);
    // Vertebrae in the upper third: long chevrons make the part under the centra the deeper one.
    return { x, top: yv + 0.38 * d, bot: yv - 0.62 * d, hw: tailWidth[i] / 2 };
  });
  loft((i) => `tail${i + 1}`, 'tail', tailSt, (i) => [0.14, 0.06][i] ?? 0.02);
  both((s, m, mr) => {
    // M. caudofemoralis longus: bulges low on the tail base towards the femur's 4th trochanter.
    E('tail1', 'tail', m([-1.15, 2.86, 0.27]), [1.0, 0.33, 0.21], mr([0, 0.05, 0.07]), 0.2);
  });

  // ---- neck ------------------------------------------------------------------------------------
  // Short, deep S-curved neck: thick at the shoulders, rising steeply behind the broad head.
  const neck = [
    { x: 2.2, top: 3.54, bot: 2.06, hw: 0.47 },
    { x: 2.62, top: 3.62, bot: 2.3, hw: 0.43 },
    { x: 2.98, top: 3.8, bot: 2.62, hw: 0.38 },
    { x: 3.28, top: 3.98, bot: 2.9, hw: 0.35 },
  ];
  loft((i) => ['neck1', 'neck2', 'neck3'][i], 'neck', neck, 0.14);
  E('neck2', 'neck', [2.92, 3.62, 0], [0.55, 0.2, 0.25], [0, 0, 0.55], 0.16);                 // nape muscles
  both((s, m, mr) => {
    E('neck1', 'neck', m([2.62, 2.96, 0.18]), [0.42, 0.5, 0.26], mr([0, 0, 0.7]), 0.16, { folds: neckFolds });
    E('neck2', 'neck', m([2.98, 3.24, 0.15]), [0.38, 0.46, 0.24], mr([0, 0, 0.75]), 0.16, { folds: neckFolds });
    E('neck3', 'neck', m([3.28, 3.42, 0.14]), [0.32, 0.42, 0.22], mr([0, 0, 0.6]), 0.16, { folds: neckFolds });
  });
  E('neck3', 'neck', [3.36, 3.0, 0], [0.42, 0.22, 0.28], [0, 0, 0.5], 0.16, { folds: neckFolds }); // throat

  // ---- legs ------------------------------------------------------------------------------------
  both((sfx, m, mr) => {
    const th = `thigh${sfx}`, sh = `shin${sfx}`, mt = `meta${sfx}`, to = `toes${sfx}`;
    // Thigh: drumstick of M. iliotibialis / femorotibialis / ambiens (plausible volumes), standing
    // proud of the flank with small blends so its outline reads as in life.
    E(th, 'thigh', m([0.2, 2.52, 0.56]), [0.52, 0.86, 0.3], [0, 0, 0.44], 0.12);              // main mass along the femur
    E(th, 'thigh', m([0.0, 3.02, 0.52]), [0.85, 0.5, 0.31], [0, 0, 0.12], 0.16);              // iliotibialis fan under the ilium
    E(th, 'thigh', m([0.56, 2.25, 0.55]), [0.2, 0.5, 0.2], [0, 0, 0.42], 0.1);                // anterior thigh edge
    E(th, 'thigh', m([-0.2, 2.42, 0.5]), [0.3, 0.6, 0.24], [0, 0, 0.6], 0.12);                // flexors behind the femur
    E(th, 'thigh', m([0.45, 2.0, 0.52]), [0.26, 0.36, 0.22], [0, 0, 0.44], 0.1);              // lower thigh into the knee
    E(sh, 'shin', m([0.58, 1.84, 0.49]), [0.21, 0.2, 0.19], [0, 0, 0], 0.09);                 // knee
    // Shank: tibia + fibula, heavy gastrocnemius behind the upper half, tibialis in front.
    RC(sh, 'shin', m([0.52, 1.76, 0.48]), m([0.21, 0.86, 0.44]), 0.21, 0.135, 0.08, { scale: [1, 1, 0.95] });
    E(sh, 'shin', m([0.3, 1.46, 0.48]), [0.25, 0.5, 0.2], [0, 0, -0.34], 0.09);               // gastrocnemius
    E(sh, 'shin', m([0.5, 1.4, 0.49]), [0.12, 0.42, 0.13], [0, 0, -0.34], 0.07);              // tibialis anterior
    E(mt, 'foot', m([0.18, 0.76, 0.44]), [0.15, 0.14, 0.14], [0, 0, 0], 0.07);                // ankle
    // Metatarsus: long arctometatarsalian block, broadest at the toe joints.
    RC(mt, 'foot', m([0.19, 0.73, 0.44]), m([0.4, 0.22, 0.43]), 0.115, 0.13, 0.05, { scale: [1, 1, 1.15] });
    E(mt, 'foot', m([0.3, 0.48, 0.44]), [0.06, 0.24, 0.12], [0, 0, 0.4], 0.05);               // flat front (scute plate)
    E(to, 'foot', m([0.37, 0.115, 0.43]), [0.2, 0.11, 0.17], [0, 0, 0], 0.05);                // metatarsal (heel) pad
    // Toes II-IV splay ~28 deg from digit III (II medial = towards the midline = -z on the right
    // foot). Each toe: phalanges as round cones with a knuckle on top and a digital pad under each
    // joint, then a broad, blunt tyrannosaurid claw curving to the ground. Digit III ~0.55 m incl.
    // claw, so the footprint is ~0.8 m long and ~0.7 m wide.
    const toe = (yaw, len, r0, nPh) => {
      const c = Math.cos(yaw), sn = Math.sin(yaw);
      const at = (u, y) => m([0.46 + c * (len * u + 0.04), y, 0.43 + sn * (len * u + 0.1)]);
      const js = nPh === 3 ? [0, 0.4, 0.72] : [0, 0.32, 0.58, 0.78];
      const joints = [...js, 0.86].map((u, k) => [u, 0.115 - 0.06 * u, r0 * (1 - 0.38 * u)]);
      for (let k = 0; k + 1 < joints.length; k++) {
        const [u0, y0, ra] = joints[k], [u1, y1, rb] = joints[k + 1];
        RC(to, 'foot', at(u0, y0), at(u1, y1), ra, rb * 0.92, 0.02);
        if (k > 0) E(to, 'foot', at(u0, y0 + 0.2 * ra), [ra * 0.75, ra * 0.8, ra * 1.02], [0, -yaw, 0], 0.02); // knuckle
        E(to, 'foot', at(0.55 * u0 + 0.45 * u1, 0.5 * (y0 + y1) - 0.5 * ra), [0.36 * len * (u1 - u0) + 0.02, 0.45 * ra, 0.9 * ra],
          [0, -yaw, 0], 0.02);                                                                // digital pad
      }
      const [uc, yc, rc] = joints[joints.length - 1];
      RC(to, 'foot', at(uc, yc + 0.01), at(uc + 0.13, yc - 0.02), rc * 0.8, rc * 0.45, 0.012, { feature: 'claw' });
      RC(to, 'foot', at(uc + 0.13, yc - 0.02), at(uc + 0.24, 0.012), rc * 0.45, 0.01, 0.008, { feature: 'claw' });
    };
    toe(-0.5, 0.44, 0.075, 3);   // digit II
    toe(0, 0.52, 0.085, 4);      // digit III
    toe(0.5, 0.46, 0.074, 4);    // digit IV
    // Hallux (digit I): small, raised off the ground, on the medial back of the metatarsus.
    RC(mt, 'foot', m([0.29, 0.42, 0.36]), m([0.24, 0.28, 0.31]), 0.045, 0.032, 0.03);
    RC(mt, 'foot', m([0.24, 0.28, 0.31]), m([0.27, 0.18, 0.3]), 0.027, 0.008, 0.01, { feature: 'claw' });
  });

  // ---- arms ------------------------------------------------------------------------------------
  both((sfx, m) => {
    const ua = `upperArm${sfx}`, fa = `foreArm${sfx}`, ha = `hand${sfx}`;
    RC(ua, 'arm', m(SHOULDER), m(ELBOW), 0.1, 0.065, 0.07);
    E(ua, 'arm', m([2.31, 2.25, 0.44]), [0.08, 0.17, 0.085], [0, 0, -0.15], 0.04);            // biceps / triceps
    RC(fa, 'arm', m(ELBOW), m(WRIST), 0.065, 0.048, 0.03);
    // Two functional fingers (I and II), claws curled down; metacarpal III vestigial (omitted).
    RC(ha, 'arm', m(WRIST), m([2.64, 1.93, 0.51]), 0.042, 0.03, 0.02);
    RC(ha, 'arm', m([2.64, 1.93, 0.51]), m([2.67, 1.84, 0.52]), 0.028, 0.008, 0.01, { feature: 'claw' });
    RC(ha, 'arm', m([2.6, 1.97, 0.45]), m([2.66, 1.9, 0.44]), 0.032, 0.024, 0.02);
    RC(ha, 'arm', m([2.66, 1.9, 0.44]), m([2.68, 1.81, 0.44]), 0.022, 0.006, 0.01, { feature: 'claw' });
  });

  // ===== 3. SURFACE DETAILS =====================================================================
  // Cast rays onto the volumes above and attach small features to the hit points.
  const field = createSdf(bones, out);
  const cast = (o, d, maxT = 1.5) => {
    d = norm(d);
    let prev = field(...o);
    for (let t = 0.004; t < maxT; t += 0.004) {
      const f = field(o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t);
      if (f < 0) {
        const k = prev / (prev - f), th = t - 0.004 * (1 - k);
        return add(o, d, th);
      }
      prev = f;
    }
    throw new Error(`trex_anatomy: detail ray from ${o.map((v) => v.toFixed(2))} missed the body`);
  };
  // Cast towards `target` (a point near the expected surface) along `dir`, from 0.6 m outside.
  const castAt = (target, dir) => { const d = norm(dir); return cast(add(target, d, -0.6), d); };
  const normal = (p) => {
    const e = 0.004, g = [0, 1, 2].map((a) => {
      const q1 = [...p], q2 = [...p]; q1[a] += e; q2[a] -= e;
      return field(...q1) - field(...q2);
    });
    return norm(g);
  };
  const details = [];   // collected first so every ray sees only the big volumes

  both((s, m, mr, sg) => {
    // ---- eye: high in the skull, looking forward-outward over the narrow snout (binocular) ------
    // Eyeball ~10 cm across (plausible; orbit ~13 cm). A swelling of lid tissue around it is cut by
    // an almond-shaped opening; the eyeball sits in the opening (meshed separately, finer).
    const eyeDir = norm(HD([0.55, 0.05, 0.83 * sg]));
    const eyeP = cast(add(H(0.6, 0.6, 0.27 * sg), eyeDir, 0.6), eyeDir.map((v) => -v));
    const en = normal(eyeP);
    // Lid frame: x along the fissure (forward, tilted slightly up at the front), z = surface normal.
    const fwd = norm(HD([1, 0.12, 0]));
    const bz = en, by = norm(cross(bz, fwd)), bx = norm(cross(by, bz));
    const lidRot = basisToEuler(bx, by.map((v) => v * sg), bz.map((v) => v * sg));
    const eyeC = add(eyeP, en, -0.03);
    details.push(() => {
      E('skull', 'head', add(eyeP, en, -0.012), [0.115, 0.085, 0.045], lidRot, 0.04);           // lid swelling
      E('skull', 'head', add(eyeP, en, 0.012), [0.072, 0.03, 0.05], lidRot, 0.014, { sign: -1 }); // palpebral opening
      E('skull', 'head', add(add(eyeP, en, 0.006), by, -0.06 * sg), [0.07, 0.016, 0.02], lidRot, 0.03, { sign: -1 }); // fold under the lower lid
      E('skull', 'head', eyeC, [0.05, 0.05, 0.05], [0, 0, 0], 0.004, { feature: 'eye', separate: true }); // eyeball
    });
    // ---- brow: postorbital boss above-behind the eye, lacrimal horn above-in-front of it, and the
    // brow ridge between them: the overhang that keeps the eye in shadow.
    const po = castAt(H(0.5, 0.74, 0.22 * sg), HD([-0.1, -0.7, -sg]));
    const la = castAt(H(0.8, 0.71, 0.13 * sg), HD([0, -0.85, -0.5 * sg]));
    details.push(() => {
      E('skull', 'head', add(po, normal(po), -0.03), [0.12, 0.075, 0.1], mr(HR([0, 0.3, 0.15])), 0.05);   // postorbital boss
      E('skull', 'head', add(la, normal(la), -0.025), [0.13, 0.055, 0.07], mr(HR([0, 0.25, -0.2])), 0.05); // lacrimal horn
      CAP('skull', 'head', add(la, normal(la), -0.04), add(po, normal(po), -0.04), 0.05, 0.05);        // brow ridge
    });
    // ---- jugal ridge ("cheekbone") from below the orbit back towards the jaw joint ----------------
    const j0 = castAt(H(0.78, 0.36, 0.22 * sg), [0, 0, -sg]), j1 = castAt(H(0.25, 0.3, 0.35 * sg), [0, 0, -sg]);
    details.push(() => CAP('skull', 'head', add(j0, normal(j0), -0.04), add(j1, normal(j1), -0.045), 0.05, 0.06));
    // ---- nostril: external naris near the top front of the snout, opening forward-outward --------
    const na = castAt(H(1.3, 0.45, 0.09 * sg), HD([0.35, 0.1, -sg]));
    const nn = normal(na);
    details.push(() => {
      E('skull', 'head', add(na, nn, -0.02), [0.07, 0.045, 0.04], mr(HR([0, 0.4, -0.25])), 0.04);           // narial swelling
      E('skull', 'head', add(na, nn, 0.012), [0.042, 0.016, 0.03], mr(HR([0, 0.45, -0.3])), 0.012, { sign: -1 }); // opening
    });
    // ---- lip groove: a shallow crease where the upper lip overhangs the lower one ------------------
    const lip = [[1.4, 0.2], [1.25, 0.165], [1.05, 0.135], [0.85, 0.125], [0.68, 0.13], [0.54, 0.15], [0.44, 0.17]];
    const hits = lip.map(([u, v]) => cast(H(u, v, 0.9 * sg), [0, 0, -sg]));
    for (let k = 0; k + 1 < hits.length; k++) {
      const a = hits[k], b = hits[k + 1], r = k >= hits.length - 2 ? 0.012 : 0.016;
      details.push(() => CAP('skull', 'head', add(a, normal(a), r - 0.006), add(b, normal(b), r - 0.006), r, 0.012, { sign: -1 }));
    }
    // Front of the mouth: the groove wraps round the premaxilla.
    const f0 = cast(H(2.0, 0.205, 0.07 * sg), HD([-1, 0, 0])), f1 = cast(H(2.0, 0.21, 0.0), HD([-1, 0, 0]));
    details.push(() => CAP('skull', 'head', add(hits[0], normal(hits[0]), 0.01), add(f0, normal(f0), 0.01), 0.016, 0.012, { sign: -1 }));
    details.push(() => CAP('skull', 'head', add(f0, normal(f0), 0.01), add(f1, normal(f1), 0.01), 0.016, 0.012, { sign: -1 }));

    // ---- dermal bumps: rugose skin over the nasals, around the orbit and on the jugal --------------
    const R = rng(sg > 0 ? 17 : 29);
    const bump = (p, r) => { const n = normal(p); details.push(() => E('skull', 'head', add(p, n, -0.65 * r), [r * 1.2, r, r * 1.1], [0, 0, 0], 0.018)); };
    for (let k = 0; k < 8; k++) bump(castAt(H(0.9 + 0.45 * R(), 0.7, (0.03 + 0.08 * R()) * sg), HD([0, -1, 0])), 0.02 + 0.012 * R());
    for (let k = 0; k < 6; k++) {
      const a = Math.PI * (0.1 + 1.0 * R());
      bump(castAt(add(eyeP, HD([Math.cos(a), Math.sin(a), 0]), 0.13), eyeDir.map((v) => -v)), 0.014 + 0.008 * R());
    }
    for (let k = 0; k < 5; k++) bump(castAt(H(0.3 + 0.35 * R(), 0.22 + 0.15 * R(), 0.3 * sg), [0, 0, -sg]), 0.015 + 0.008 * R());
  });
  for (const fn of details) fn();
  return out;
}

export const TREX_BONES = makeBones();
export const TREX_SHAPES = makeShapes(TREX_BONES);
