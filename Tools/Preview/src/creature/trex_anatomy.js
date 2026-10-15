// Adult Tyrannosaurus rex as a procedural SDF sculpt: a skeleton (bone heads in model space) plus
// ~240 primitive volumes (two lofts for the skull and jaw; ~85 of the rest are toe pads, knuckles
// and claws, ~40 small dermal bumps) that sdf_mesher.js smooth-blends into one skin.
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
//   1. the head, authored in its own frame (u forward from the jaw joint, v up, w = z): the upper
//      head and the lower jaw are lofts through profile stations (top line, lip line, keyhole-shaped
//      width), the lower jaw set just inside the upper lip so the lip line is a crisp crease;
//   2. body volumes: lofted elliptical sections for trunk, neck and tail, then muscles and limbs;
//   3. surface details (eye with lids and brow, brow bosses, nostrils, dermal bumps) placed by
//      ray-casting onto the volumes above, so they stay attached when the volumes are re-proportioned.
// The jaw bone is rigged, but there is no mouth interior: keep its rotation within ~0.05 rad
// (lips parting slightly); a wider gape would need teeth, gums and a tongue.
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
const SHOULDER = [2.28, 2.5, 0.4];     // glenoid, low on the front of the ribcage
const ELBOW = [2.33, 2.13, 0.48];
const WRIST = [2.53, 2.05, 0.5];

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
  const neckFolds = { amp: 0.013, wavelength: 0.19, axis: [0.8, 0.6, 0], lateral: 0.35 };

  // ===== 1. HEAD ================================================================================
  // Skull ~1.47 m from the jaw joint (u = 0) to the snout tip; about 0.88 m across the
  // quadratojugals and ~0.28 m across the premaxillae (narrow snout, broad temporal region ->
  // forward-facing orbits). Dorsal outline nearly straight from the snout (v ~0.46) up to the
  // lacrimals and postorbitals (v ~0.72) and level behind them (no cranial dome). Head depth with
  // the jaw closed ~1.0 m at the back, ~0.5 m at the snout. The upper head and the lower jaw are
  // lofts through measured-looking stations (u, top, bottom, half-width, height of the widest
  // line, squareness), so the lateral profile, the lip line and the keyhole outline seen from
  // above are controlled directly; muscles, bosses and the eye are added on top.
  const HE = (bone, region, c, radii, r = [0, 0, 0], blend = 0.1, extra = {}) => E(bone, region, H(...c), radii, HR(r), blend, extra);
  const HRC = (bone, region, a, b, ra, rb, blend = 0.05, extra = {}) => RC(bone, region, H(...a), H(...b), ra, rb, blend, extra);
  const HLOFT = (bone, region, stations, blend, extra = {}) =>
    out.push({ bone, kind: 'loft', origin: H(0, 0), rotation: HR(), stations, blend, region, ...extra });
  // Upper head stations [u, top, bottom (= lip line in front of the rictus), half-width, height of
  // the widest line, squareness]. The lip line is sinuous: high under the premaxilla, lowest under
  // the convex maxillary tooth row, rising to the rictus below the orbit (u ~0.38); behind the
  // rictus the bottom drops to the cheek, which covers the jaw joint.
  const UP = [
    [-0.06, 0.6, 0.0, 0.33, 0.28, 2.4],
    [0.12, 0.69, -0.02, 0.44, 0.26, 2.6],
    [0.32, 0.73, 0.1, 0.42, 0.27, 2.6],
    [0.52, 0.72, 0.15, 0.33, 0.21, 2.4],
    [0.72, 0.67, 0.13, 0.255, 0.18, 2.2],
    [0.95, 0.59, 0.11, 0.2, 0.16, 2.2],
    [1.18, 0.52, 0.13, 0.165, 0.18, 2.2],
    [1.34, 0.46, 0.17, 0.14, 0.23, 2.2],
  ];
  // skinSigma: crisp skull/jaw weights along the lips (the mouth then opens along the lip line).
  HLOFT('skull', 'head', UP, 0.1, { capFront: 0.15, capBack: 0.18, skinSigma: 0.012 });
  // Lower jaw: follows the upper lip ~2.5 cm inside it (the upper lip overhangs the lower one, as
  // in lizards; lips cover the teeth), its top hidden inside the upper head; deep dentary and very
  // deep surangular region behind it. Bottom profile per station.
  const jawBot = [-0.15, -0.25, -0.26, -0.21, -0.16, -0.11, -0.065, -0.01];
  HLOFT('jaw', 'jaw', UP.map(([u, , , w, c], k) => [u, c + 0.03, jawBot[k], w - 0.024, c - 0.035, 2.2]), 0.02,
    { capFront: 0.115, capBack: 0.14, skinSigma: 0.008 });
  both((s, m, mr) => {
    // Cheek skin behind the rictus, joining the upper head to the jaw over the jaw joint.
    HE('jaw', 'jaw', m([0.2, 0.08, 0.35]), [0.2, 0.12, 0.07], mr([0, 0.2, 0]), 0.08);
    HE('jaw', 'jaw', m([0.06, -0.1, 0.31]), [0.16, 0.17, 0.11], mr([0, 0.15, 0]), 0.1);          // M. pterygoideus bulge
    HE('skull', 'head', m([0.43, 0.28, 0.32]), [0.24, 0.1, 0.045], mr([0, 0.3, -0.05]), 0.1);    // jugal bulge under the orbit
    HE('skull', 'head', m([0.12, 0.2, 0.39]), [0.12, 0.14, 0.065], mr([0, 0.2, 0]), 0.1);        // quadratojugal flare
    HE('skull', 'head', m([0.2, 0.6, 0.2]), [0.24, 0.1, 0.13], mr([0, 0.15, 0]), 0.1);           // jaw muscles bulging over the skull roof
  });
  HRC('skull', 'head', [1.32, 0.44], [0.66, 0.62], 0.06, 0.085, 0.06, { scale: [1, 1, 1.2] });    // nasal ridge (rugose nasals)

  // ===== 2. BODY ================================================================================
  // ---- trunk -----------------------------------------------------------------------------------
  // Deep, short trunk: back ~3.8 m over the ilia, gastral belly ~1.95 m, ribcage ~1.05 m wide
  // (plausible soft tissue over the skeleton). The pubic boot is the lowest point, near knee height.
  const trunk = [
    { x: -0.85, top: 3.72, bot: 2.62, hw: 0.42 },
    { x: -0.3, top: 3.77, bot: 2.52, hw: 0.39 },
    { x: 0.35, top: 3.75, bot: 2.38, hw: 0.39 },
    { x: 1.0, top: 3.7, bot: 2.26, hw: 0.45 },
    { x: 1.65, top: 3.62, bot: 2.12, hw: 0.46 },
    { x: 2.2, top: 3.5, bot: 2.14, hw: 0.4 },
  ];
  loft((i) => ['pelvis', 'pelvis', 'spine1', 'spine2', 'spine3'][i], 'torso', trunk, 0.14);
  E('spine2', 'torso', [1.05, 3.47, 0], [1.5, 0.22, 0.3], [0, 0, 0.04], 0.18);                // epaxial muscles along the back
  E('pelvis', 'torso', [-0.15, 3.5, 0], [0.92, 0.26, 0.34], [0, 0, 0], 0.16);                 // ilia + sacrum
  // Belly line: deepest behind the arms, rising towards the thighs (plausible soft-tissue profile).
  E('spine2', 'belly', [1.62, 2.2, 0], [0.72, 0.26, 0.39], [0, 0, 0.1], 0.22);                // gut over the gastralia
  E('spine3', 'belly', [2.15, 2.22, 0], [0.42, 0.22, 0.32], [0, 0, 0.12], 0.18);              // chest floor (coracoids, furcula)
  RC('pelvis', 'belly', [0.12, 2.65, 0], [0.42, 2.14, 0], 0.24, 0.13, 0.2, { scale: [1, 1, 1.15] }); // pubic apron
  E('pelvis', 'belly', [0.45, 2.1, 0], [0.28, 0.09, 0.14], [0, 0, -0.08], 0.18);              // pubic boot (keel under the skin)
  E('pelvis', 'torso', [-0.6, 2.72, 0], [0.45, 0.3, 0.3], [0, 0, 0.35], 0.18);                // ischia / cloaca
  both((s, m, mr) => {
    E('spine2', 'torso', m([1.4, 2.72, 0.22]), [0.85, 0.5, 0.28], mr([0, 0, 0.02]), 0.18);      // ribcage flank (widest mid-trunk)
    E('spine3', 'torso', m([2.02, 3.0, 0.32]), [0.5, 0.42, 0.15], mr([0, 0, -0.5]), 0.22);      // scapular muscles
    E('spine3', 'torso', m([2.3, 2.45, 0.26]), [0.3, 0.32, 0.2], [0, 0, 0], 0.16);              // pectoralis
  });

  // ---- tail ------------------------------------------------------------------------------------
  // Depth and width at each tail joint (plausible: very deep base for M. caudofemoralis, laterally
  // compressed, tapering to a point). One elliptical round cone per segment on its own bone.
  const tailDepth = [1.08, 0.95, 0.82, 0.71, 0.61, 0.51, 0.42, 0.33, 0.25, 0.16, 0.05];
  const tailWidth = [0.88, 0.7, 0.57, 0.47, 0.39, 0.32, 0.26, 0.21, 0.15, 0.1, 0.035];
  const tailSt = tailDepth.map((d, i) => {
    const x = TAIL_X0 + TAIL_SEG * i, yv = tailY(x);
    // Vertebrae in the upper third: long chevrons make the part under the centra the deeper one.
    return { x, top: yv + 0.38 * d, bot: yv - 0.62 * d, hw: tailWidth[i] / 2 };
  });
  loft((i) => `tail${i + 1}`, 'tail', tailSt, (i) => [0.14, 0.06][i] ?? 0.02);
  both((s, m, mr) => {
    // M. caudofemoralis longus: bulges low on the tail base towards the femur's 4th trochanter.
    E('tail1', 'tail', m([-1.15, 2.88, 0.25]), [1.05, 0.3, 0.21], mr([0, 0.05, 0.07]), 0.3);
  });

  // ---- neck ------------------------------------------------------------------------------------
  // Short, deep S-curved neck: thick at the shoulders, rising steeply behind the broad head.
  const neck = [
    { x: 2.2, top: 3.5, bot: 2.2, hw: 0.4 },
    { x: 2.6, top: 3.6, bot: 2.4, hw: 0.38 },
    { x: 2.95, top: 3.78, bot: 2.64, hw: 0.36 },
    { x: 3.25, top: 3.97, bot: 2.88, hw: 0.35 },
  ];
  loft((i) => ['neck1', 'neck2', 'neck3'][i], 'neck', neck, 0.14);
  E('neck2', 'neck', [2.9, 3.6, 0], [0.55, 0.18, 0.22], [0, 0, 0.55], 0.14);                  // nape muscles
  both((s, m, mr) => {
    // Lateral neck muscles carrying the skin folds (M. longus colli / cervicalis, plausible).
    E('neck1', 'neck', m([2.6, 3.02, 0.13]), [0.38, 0.44, 0.22], mr([0, 0, 0.7]), 0.14, { folds: neckFolds });
    E('neck2', 'neck', m([2.96, 3.27, 0.12]), [0.34, 0.42, 0.2], mr([0, 0, 0.75]), 0.14, { folds: neckFolds });
    E('neck3', 'neck', m([3.26, 3.46, 0.14]), [0.3, 0.4, 0.21], mr([0, 0, 0.6]), 0.14, { folds: neckFolds });
  });
  E('neck3', 'neck', [3.3, 3.0, 0], [0.46, 0.24, 0.28], [0, 0, 0.55], 0.16, { folds: neckFolds }); // throat
  both((s, m, mr) => {
    // Muscles from the back of the skull to the neck (M. depressor mandibulae, transversospinalis):
    // fill the step behind the broad quadratojugal region.
    E('neck3', 'neck', m(H(-0.1, 0.3, 0.25)), [0.26, 0.3, 0.14], mr([0, 0.25, 0.3]), 0.16);
  });

  // ---- legs ------------------------------------------------------------------------------------
  both((sfx, m, mr) => {
    const th = `thigh${sfx}`, sh = `shin${sfx}`, mt = `meta${sfx}`, to = `toes${sfx}`;
    // Thigh: drumstick of M. iliotibialis / femorotibialis / ambiens (plausible volumes), standing
    // proud of the flank with small blends so its outline reads as in life.
    E(th, 'thigh', m([0.2, 2.52, 0.55]), [0.47, 0.84, 0.28], [0, 0, 0.44], 0.08);             // main mass along the femur
    E(th, 'thigh', m([0.0, 3.1, 0.46]), [0.8, 0.5, 0.28], [0, 0, 0.12], 0.16);                // iliotibialis fan under the ilium
    E(th, 'thigh', m([0.55, 2.27, 0.54]), [0.18, 0.48, 0.19], [0, 0, 0.42], 0.1);             // anterior thigh edge
    E(th, 'thigh', m([-0.2, 2.45, 0.48]), [0.28, 0.56, 0.22], [0, 0, 0.6], 0.12);             // flexors behind the femur
    E(th, 'thigh', m([0.44, 2.04, 0.52]), [0.28, 0.45, 0.25], [0, 0, 0.44], 0.1);              // lower thigh into the knee
    E(sh, 'shin', m([0.58, 1.84, 0.49]), [0.25, 0.23, 0.23], [0, 0, 0], 0.09);                 // knee
    // Shank: tibia + fibula wrapped in a heavy calf. The gastrocnemius bellies sit behind the upper
    // half and taper into the tendon above the ankle; M. fibularis on the outside and
    // M. tibialis anterior in front keep the section full (plausible volumes, cf. large ground birds).
    RC(sh, 'shin', m([0.53, 1.76, 0.48]), m([0.21, 0.86, 0.44]), 0.25, 0.145, 0.08, { scale: [1, 1, 0.95] });
    E(sh, 'shin', m([0.32, 1.5, 0.48]), [0.25, 0.58, 0.22], [0, 0, -0.34], 0.1);              // gastrocnemius
    E(sh, 'shin', m([0.43, 1.47, 0.57]), [0.15, 0.48, 0.11], [0, 0, -0.34], 0.08);            // M. fibularis (outside)
    E(sh, 'shin', m([0.52, 1.45, 0.47]), [0.13, 0.47, 0.15], [0, 0, -0.34], 0.07);            // tibialis anterior
    E(mt, 'foot', m([0.19, 0.77, 0.44]), [0.14, 0.12, 0.15], [0, 0, 0], 0.1);                 // ankle
    // Metatarsus: long arctometatarsalian block, broadest at the toe joints.
    RC(mt, 'foot', m([0.19, 0.73, 0.44]), m([0.4, 0.22, 0.43]), 0.14, 0.16, 0.06, { scale: [1, 1, 1.2] });
    E(mt, 'foot', m([0.31, 0.48, 0.44]), [0.06, 0.25, 0.13], [0, 0, 0.4], 0.05);              // flat front (scute plate)
    E(to, 'foot', m([0.38, 0.11, 0.43]), [0.21, 0.11, 0.155], [0, 0, 0], 0.12);               // metatarsal (heel) pad
    // Toes II-IV splay ~35 deg from digit III (II medial = towards the midline = -z on the right
    // foot). Each toe: phalanges as round cones with a knuckle on top and a digital pad under each
    // joint, then a broad, blunt tyrannosaurid claw curving to the ground. Digit III ~0.53 m incl.
    // claw, so the footprint (heel pad to claw III) is ~0.85 m long and ~0.7 m wide.
    const toe = (yaw, len, r0, nPh) => {
      const c = Math.cos(yaw), sn = Math.sin(yaw);
      const at = (u, y) => m([0.46 + c * (len * u + 0.04), y, 0.43 + sn * (len * u + 0.1)]);
      const js = nPh === 3 ? [0, 0.4, 0.72] : [0, 0.32, 0.58, 0.78];
      const joints = [...js, 0.86].map((u, k) => [u, 0.115 - 0.06 * u, r0 * (1 - 0.38 * u)]);
      for (let k = 0; k + 1 < joints.length; k++) {
        const [u0, y0, ra] = joints[k], [u1, y1, rb] = joints[k + 1];
        // The first phalanx flares into the metatarsal pad (larger blend); joints further out stay crisp.
        RC(to, 'foot', at(u0, y0), at(u1, y1), ra, rb * 0.92, k === 0 ? 0.07 : 0.02);
        if (k > 0) E(to, 'foot', at(u0, y0 + 0.2 * ra), [ra * 0.75, ra * 0.8, ra * 1.02], [0, -yaw, 0], 0.02); // knuckle
        E(to, 'foot', at(0.55 * u0 + 0.45 * u1, 0.5 * (y0 + y1) - 0.5 * ra), [0.36 * len * (u1 - u0) + 0.02, 0.45 * ra, 0.9 * ra],
          [0, -yaw, 0], 0.02);                                                                // digital pad
      }
      const [uc, yc, rc] = joints[joints.length - 1];
      RC(to, 'foot', at(uc, yc + 0.01), at(uc + 0.13, yc - 0.02), rc * 0.8, rc * 0.45, 0.012, { feature: 'claw' });
      RC(to, 'foot', at(uc + 0.13, yc - 0.02), at(uc + 0.24, 0.012), rc * 0.45, 0.01, 0.008, { feature: 'claw' });
    };
    toe(-0.62, 0.41, 0.08, 3);   // digit II
    toe(0, 0.48, 0.09, 4);       // digit III
    toe(0.62, 0.43, 0.078, 4);   // digit IV
    // Hallux (digit I): small, raised off the ground, on the medial back of the metatarsus.
    RC(mt, 'foot', m([0.29, 0.42, 0.36]), m([0.24, 0.28, 0.31]), 0.045, 0.032, 0.03);
    RC(mt, 'foot', m([0.24, 0.28, 0.31]), m([0.27, 0.18, 0.3]), 0.027, 0.008, 0.01, { feature: 'claw' });
  });

  // ---- arms ------------------------------------------------------------------------------------
  both((sfx, m) => {
    const ua = `upperArm${sfx}`, fa = `foreArm${sfx}`, ha = `hand${sfx}`;
    RC(ua, 'arm', m(SHOULDER), m(ELBOW), 0.1, 0.065, 0.07);
    E(ua, 'arm', m([2.3, 2.33, 0.44]), [0.08, 0.17, 0.085], [0, 0, -0.15], 0.04);             // biceps / triceps
    RC(fa, 'arm', m(ELBOW), m(WRIST), 0.065, 0.048, 0.03);
    // Two functional fingers (I and II), claws curled down; metacarpal III vestigial (omitted).
    RC(ha, 'arm', m(WRIST), m([2.61, 2.0, 0.52]), 0.045, 0.034, 0.02);                      // metacarpus
    RC(ha, 'arm', m([2.61, 2.0, 0.52]), m([2.67, 1.94, 0.53]), 0.03, 0.026, 0.012);          // digit I
    RC(ha, 'arm', m([2.67, 1.94, 0.53]), m([2.7, 1.85, 0.535]), 0.026, 0.007, 0.008, { feature: 'claw' });
    RC(ha, 'arm', m([2.6, 2.02, 0.48]), m([2.68, 1.97, 0.47]), 0.03, 0.025, 0.012);          // digit II
    RC(ha, 'arm', m([2.68, 1.97, 0.47]), m([2.72, 1.88, 0.465]), 0.024, 0.006, 0.008, { feature: 'claw' });
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
  const eyes = [];      // eye centres, for the fine detail zones

  both((s, m, mr, sg) => {
    // ---- eye: high in the skull, looking forward-outward over the narrow snout (binocular) ------
    // Eyeball ~10 cm across (plausible for an orbit ~13 cm high), meshed separately (finer) and set
    // almost flush with the lid margins. Around it: a low mound of lid tissue cut by an almond-shaped
    // palpebral fissure, a thick upper lid and a thinner lower lid rolled along its edges, a crease
    // above the upper lid, and the bony brow (lacrimal to postorbital) overhanging the whole eye.
    const eyeDir = norm(HD([0.5, 0.08, 0.86 * sg]));
    const eyeP = cast(add(H(0.56, 0.58, 0.25 * sg), eyeDir, 0.6), eyeDir.map((v) => -v));
    eyes.push(eyeP);
    const en = normal(eyeP);
    // Lid frame: x along the fissure (forward, tilted slightly up at the front), y up, z = normal.
    const fwd = norm(HD([1, 0.1, 0]));
    const bz = en, by0 = norm(cross(bz, fwd)), by = by0[1] < 0 ? by0.map((v) => -v) : by0;
    const bx = norm(cross(by, bz));
    const lidRot = basisToEuler(bx, by, cross(bx, by));
    const L = (x, y, z) => add(add(add(eyeP, bx, x), by, y), bz, z);     // point in the lid frame
    details.push(() => {
      E('skull', 'head', L(0, 0.004, -0.02), [0.1, 0.075, 0.05], lidRot, 0.04);                     // lid mound
      E('skull', 'head', L(0.002, -0.003, 0.046), [0.07, 0.027, 0.045], lidRot, 0.01, { sign: -1 });   // palpebral fissure
      // Lids: rolls arching over and under the fissure, thick in the middle, tapering into the
      // front and back corners (canthi); the upper lid is the heavier one.
      const lid = (pts) => { for (let k = 0; k + 1 < pts.length; k++) RC('skull', 'head', L(...pts[k][0]), L(...pts[k + 1][0]), pts[k][1], pts[k + 1][1], 0.008); };
      lid([[[-0.068, 0.0, 0.0], 0.006], [[-0.034, 0.021, 0.009], 0.015], [[0.004, 0.027, 0.011], 0.018], [[0.04, 0.022, 0.009], 0.015], [[0.07, 0.003, 0.0], 0.006]]);
      lid([[[-0.066, -0.004, 0.0], 0.005], [[-0.03, -0.026, 0.006], 0.011], [[0.004, -0.031, 0.007], 0.012], [[0.038, -0.025, 0.006], 0.011], [[0.068, 0.0, 0.0], 0.005]]);
      CAP('skull', 'head', L(-0.06, 0.064, 0.02), L(0.06, 0.068, 0.018), 0.007, 0.008, { sign: -1 }); // crease above the upper lid
      E('skull', 'head', L(0.0, 0.0, -0.024), [0.05, 0.05, 0.05], [0, 0, 0], 0.004, { feature: 'eye', separate: true }); // eyeball
      // Brow: lacrimal (front) to postorbital (back), above the upper lid, overhanging it.
      CAP('skull', 'head', L(0.15, 0.086, -0.004), L(-0.13, 0.096, 0.002), 0.05, 0.055);
    });
    // ---- brow bosses: postorbital boss above-behind the eye, lacrimal horn above-in-front of it.
    const po = castAt(H(0.44, 0.76, 0.2 * sg), HD([-0.1, -0.6, -sg]));
    const la = castAt(H(0.76, 0.72, 0.12 * sg), HD([0, -0.85, -0.5 * sg]));
    details.push(() => {
      E('skull', 'head', add(po, normal(po), -0.025), [0.13, 0.075, 0.1], mr(HR([0, 0.3, 0.15])), 0.045);  // postorbital boss
      E('skull', 'head', add(la, normal(la), -0.02), [0.12, 0.06, 0.07], mr(HR([0, 0.25, -0.2])), 0.04);   // lacrimal horn
    });
    // ---- antorbital fossa: the skin over the antorbital fenestra sits ~1.5 cm lower than the
    // maxilla around it (plausible), which keeps the face from looking inflated.
    const ao = castAt(H(0.92, 0.38, 0.2 * sg), HD([0, -0.15, -sg]));
    details.push(() => E('skull', 'head', add(ao, normal(ao), 0.06 - 0.015), [0.2, 0.09, 0.06], mr(HR([0, 0.2, 0.12])), 0.1, { sign: -1 }));
    // ---- nostril: external naris near the top front of the snout, opening forward-outward --------
    const na = castAt(H(1.3, 0.45, 0.09 * sg), HD([0.35, 0.1, -sg]));
    const nn = normal(na);
    details.push(() => {
      E('skull', 'head', add(na, nn, -0.02), [0.07, 0.045, 0.04], mr(HR([0, 0.4, -0.25])), 0.04);           // narial swelling
      E('skull', 'head', add(na, nn, 0.012), [0.042, 0.016, 0.03], mr(HR([0, 0.45, -0.3])), 0.012, { sign: -1 }); // opening
    });
    // The lip line itself is the crease between the upper-head loft and the lower jaw loft set just
    // inside it (section 1): an extra carved groove only produced aliasing at this scale.
    // ---- dermal bumps: rugose skin over the nasals, around the orbit and on the jugal --------------
    const R = rng(sg > 0 ? 17 : 29);
    // A bump is a sphere-ish blob sunk into the surface; `embed` (fraction of r below the surface)
    // sets how proud it stands: knobs on the brow, low flat mounds (large scales) on the cheek.
    const bump = (p, r, embed = 0.65) => { const n = normal(p); details.push(() => E('skull', 'head', add(p, n, -embed * r), [r * 1.2, r, r * 1.1], [0, 0, 0], 0.018)); };
    // Row of rugosities along each side of the nasals, from the nostril back to the lacrimal horn.
    for (let k = 0; k < 7; k++) {
      const u = 1.18 - 0.055 * k + 0.02 * R();
      bump(castAt(H(u, 0.75, (0.075 + 0.012 * k + 0.015 * R()) * sg), HD([0, -1, -0.25 * sg])), 0.018 + 0.008 * R());
    }
    // (No sculpted bumps on the cheek: at ~1 cm they sit at the mesh resolution and read as beads;
    // the skin shader's feature-scale scale clusters carry that texture instead.)
    // Knobs along the brow above the eye (cf. the rugose lacrimal / postorbital surfaces).
    for (let k = 0; k < 6; k++) {
      const a = Math.PI * (0.15 + 0.7 * (k + R()) / 6);
      bump(castAt(add(eyeP, HD([Math.cos(a), Math.sin(a), 0]), 0.115 + 0.02 * R()), eyeDir.map((v) => -v)), 0.013 + 0.007 * R());
    }
  });
  for (const fn of details) fn();
  out.eyes = eyes;
  return out;
}

export const TREX_BONES = makeBones();
export const TREX_SHAPES = makeShapes(TREX_BONES);
// Suggested main voxel (m) when the caller passes none: the body reads well at 2.5 cm, the detail
// zones below refine head, hands and feet.
TREX_SHAPES.voxel = 0.025;
// Detail zones read by sdf_mesher.js (a property of the shape list, so callers need not pass them):
// boxes meshed with voxel * scale. Their faces cross the body only on smooth surfaces (back of the
// head, metatarsus, forearm), where the coarse and fine meshes overlap invisibly.
TREX_SHAPES.zones = [
  { name: 'head', min: [3.56, 2.7, -0.8], max: [5.3, 4.7, 0.8], scale: 0.5 },
  { name: 'feet', min: [-0.25, -0.1, -1.0], max: [1.35, 0.5, 1.0], scale: 0.6 },
  { name: 'hands', min: [2.46, 1.7, -0.75], max: [2.9, 2.1, 0.75], scale: 0.6 },
  // Eyes and lids (lid rolls ~1.5 cm thick): a quarter of the body voxel.
  ...TREX_SHAPES.eyes.map((p, k) => ({
    name: `eye${k}`, min: p.map((v) => v - 0.13), max: p.map((v) => v + 0.13), scale: 0.25, band: 0.03,
  })),
];
