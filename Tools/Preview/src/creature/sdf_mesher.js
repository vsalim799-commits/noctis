// SDF "sculpt" -> skinned mesh.
// A creature is described as primitive volumes (ellipsoids, capsules, round cones) attached to
// bones and merged with smooth unions / smooth subtractions, like clay blobs. We sample the signed
// distance only in a narrow band around the surface (block culling), polygonise it with surface
// nets, snap every vertex onto the exact iso-surface, and derive skin weights, region ids, cavity
// and skin-fold attributes from which primitives shape each vertex. No external assets, no UVs.
//
// Shape order matters: shapes are combined in array order, so subtractions (eye sockets, mouth
// line) should come after the volumes they carve, and anything listed after a subtraction (an
// eyeball) is added back on top of it.

export const REGIONS = ['head', 'jaw', 'neck', 'torso', 'belly', 'tail', 'thigh', 'shin', 'foot', 'arm'];
// Optional per-shape `feature` tags, exported per vertex as aFeature (0 = skin) so a material can
// shade non-skin parts: eyeballs and keratin claws.
export const FEATURES = ['skin', 'eye', 'claw'];

const STRIDE = 20;          // floats per compiled shape
const BLOCK = 8;            // block edge, in grid cells, for narrow-band culling
const AO_REACH = 0.6;       // metres; longest occlusion probe along the normal

// ---------------------------------------------------------------------------------------------
// Shape compilation: everything that does not depend on the sample point is precomputed into
// flat typed arrays so the inner loops stay monomorphic and allocation-free.
function compileShapes(shapes, boneIndex) {
  const n = shapes.length;
  const S = {
    n,
    kind: new Uint8Array(n),        // 0 = ellipsoid, 1 = round cone (capsule = equal radii)
    sign: new Int8Array(n),
    bone: new Int32Array(n),
    region: new Uint8Array(n),
    feature: new Uint8Array(n),
    k: new Float64Array(n),          // smooth-min radius
    p: new Float64Array(n * STRIDE),
    box: new Float64Array(n * 6),    // min xyz, max xyz (unexpanded)
    foldAmp: new Float64Array(n),
    fold: new Float64Array(n * 6),   // 1/wavelength, axis xyz, lateral mask lo/hi
    anyFold: false,
  };
  for (let i = 0; i < n; i++) {
    const s = shapes[i];
    const bi = boneIndex.get(s.bone);
    if (bi === undefined) throw new Error(`sdf_mesher: shape ${i} references unknown bone '${s.bone}'`);
    S.bone[i] = bi;
    const ri = REGIONS.indexOf(s.region);
    S.region[i] = ri >= 0 ? ri : 3;
    S.feature[i] = Math.max(0, FEATURES.indexOf(s.feature || 'skin'));
    S.k[i] = Math.max(s.blend ?? 0.05, 1e-4);
    S.sign[i] = s.sign === -1 ? -1 : 1;
    const o = i * STRIDE, P = S.p, bo = i * 6;
    if (s.kind === 'ellipsoid') {
      S.kind[i] = 0;
      const [cx, cy, cz] = s.center, [rx, ry, rz] = s.radii;
      const [ex, ey, ez] = s.rotation || [0, 0, 0];
      // Rotation matrix of a three.js 'XYZ' Euler, row-major R (columns = local axes in model space).
      const a = Math.cos(ex), b = Math.sin(ex), c = Math.cos(ey), d = Math.sin(ey), e = Math.cos(ez), f = Math.sin(ez);
      const ae = a * e, af = a * f, be = b * e, bf = b * f;
      const R = [c * e, -c * f, d, af + be * d, ae - bf * d, -b * c, bf - ae * d, be + af * d, a * c];
      P[o] = cx; P[o + 1] = cy; P[o + 2] = cz;
      // Store M = R^T so that local = M * (p - c).
      P[o + 3] = R[0]; P[o + 4] = R[3]; P[o + 5] = R[6];
      P[o + 6] = R[1]; P[o + 7] = R[4]; P[o + 8] = R[7];
      P[o + 9] = R[2]; P[o + 10] = R[5]; P[o + 11] = R[8];
      P[o + 12] = 1 / rx; P[o + 13] = 1 / ry; P[o + 14] = 1 / rz;
      P[o + 15] = Math.min(rx, ry, rz);
      for (let r = 0; r < 3; r++) {
        const h = Math.abs(R[r * 3]) * rx + Math.abs(R[r * 3 + 1]) * ry + Math.abs(R[r * 3 + 2]) * rz;
        S.box[bo + r] = s.center[r] - h; S.box[bo + 3 + r] = s.center[r] + h;
      }
    } else if (s.kind === 'capsule' || s.kind === 'roundCone') {
      S.kind[i] = 1;
      const A = s.a, B = s.b;
      let r1 = s.ra ?? s.r ?? 0.1, r2 = s.kind === 'capsule' ? r1 : (s.rb ?? r1);
      // Optional per-axis scale about `a` (model axes) turns the circular section into an ellipse,
      // e.g. a laterally compressed tail. The distance is then a bound scaled by the smallest axis.
      const sc = s.scale || [1, 1, 1];
      const bax = (B[0] - A[0]) / sc[0], bay = (B[1] - A[1]) / sc[1], baz = (B[2] - A[2]) / sc[2];
      const l2 = Math.max(bax * bax + bay * bay + baz * baz, 1e-8), l = Math.sqrt(l2);
      // The round-cone formula needs one end sphere not to swallow the other.
      if (Math.abs(r1 - r2) > 0.98 * l) { if (r1 > r2) r2 = r1 - 0.98 * l; else r1 = r2 - 0.98 * l; }
      const rr = r1 - r2;
      P[o] = A[0]; P[o + 1] = A[1]; P[o + 2] = A[2];
      P[o + 3] = bax; P[o + 4] = bay; P[o + 5] = baz;
      P[o + 6] = l2; P[o + 7] = rr; P[o + 8] = l2 - rr * rr; P[o + 9] = 1 / l2;
      P[o + 10] = r1; P[o + 11] = r2;
      P[o + 12] = 1 / sc[0]; P[o + 13] = 1 / sc[1]; P[o + 14] = 1 / sc[2]; P[o + 15] = Math.min(sc[0], sc[1], sc[2]);
      for (let r = 0; r < 3; r++) {
        S.box[bo + r] = Math.min(A[r] - r1 * sc[r], B[r] - r2 * sc[r]);
        S.box[bo + 3 + r] = Math.max(A[r] + r1 * sc[r], B[r] + r2 * sc[r]);
      }
    } else {
      throw new Error(`sdf_mesher: unknown shape kind '${s.kind}'`);
    }
    if (s.folds && s.folds.amp > 0) {
      const ax = s.folds.axis || [1, 0, 0];
      const al = Math.hypot(ax[0], ax[1], ax[2]) || 1;
      S.foldAmp[i] = s.folds.amp;
      const F = S.fold, fo = i * 6;
      F[fo] = 1 / (s.folds.wavelength || 0.15);
      F[fo + 1] = ax[0] / al; F[fo + 2] = ax[1] / al; F[fo + 3] = ax[2] / al;
      // `lateral` restricts folds to the flanks of an ellipsoid (fraction of its local z axis).
      if (s.folds.lateral && S.kind[i] === 0) { F[fo + 4] = s.folds.lateral; F[fo + 5] = Math.min(1, s.folds.lateral + 0.35); }
      S.anyFold = true;
    }
  }
  return S;
}

// Distance from point to shape i (negative inside).
function shapeDist(S, i, x, y, z) {
  const P = S.p, o = i * STRIDE;
  if (S.kind[i] === 0) {
    const dx = x - P[o], dy = y - P[o + 1], dz = z - P[o + 2];
    const lx = P[o + 3] * dx + P[o + 4] * dy + P[o + 5] * dz;
    const ly = P[o + 6] * dx + P[o + 7] * dy + P[o + 8] * dz;
    const lz = P[o + 9] * dx + P[o + 10] * dy + P[o + 11] * dz;
    const qx = lx * P[o + 12], qy = ly * P[o + 13], qz = lz * P[o + 14];
    const k0 = Math.sqrt(qx * qx + qy * qy + qz * qz);
    const ux = qx * P[o + 12], uy = qy * P[o + 13], uz = qz * P[o + 14];
    const k1 = Math.sqrt(ux * ux + uy * uy + uz * uz);
    // Quilez' "improved" ellipsoid bound: exact on the surface, near-unit gradient around it.
    if (k1 < 1e-9) return -P[o + 15];
    return k0 * (k0 - 1) / k1;
  }
  // Round cone between two spheres (Quilez), single square root.
  const pax = (x - P[o]) * P[o + 12], pay = (y - P[o + 1]) * P[o + 13], paz = (z - P[o + 2]) * P[o + 14];
  const bax = P[o + 3], bay = P[o + 4], baz = P[o + 5];
  const l2 = P[o + 6], rr = P[o + 7], a2 = P[o + 8], il2 = P[o + 9];
  const yy = pax * bax + pay * bay + paz * baz;
  const zz = yy - l2;
  const wx = pax * l2 - bax * yy, wy = pay * l2 - bay * yy, wz = paz * l2 - baz * yy;
  const x2 = wx * wx + wy * wy + wz * wz;
  const y2 = yy * yy * l2;
  const z2 = zz * zz * l2;
  const k = Math.sign(rr) * rr * rr * x2;
  const ms = P[o + 15];
  if (Math.sign(zz) * a2 * z2 > k) return (Math.sqrt(x2 + z2) * il2 - P[o + 11]) * ms;
  if (Math.sign(yy) * a2 * y2 < k) return (Math.sqrt(x2 + y2) * il2 - P[o + 10]) * ms;
  return ((Math.sqrt(x2 * a2 * il2) + yy * rr) * il2 - P[o + 10]) * ms;
}

// Skin-fold profile: |sin| gives soft rolls with sharp creases between them. The phase is warped by
// cheap low-frequency waves so the folds wander instead of forming perfect parallel rings.
function foldPhase(S, i, x, y, z) {
  const f = S.fold, o = i * 6;
  return (x * f[o + 1] + y * f[o + 2] + z * f[o + 3]) * f[o]
    + 0.45 * Math.sin(3.1 * y + 2.3 * z + 0.7 * x) + 0.25 * Math.sin(5.7 * x - 3.3 * y + 1.9 * z);
}
// Folds break up along their length (skin creases are not continuous rings) and vary in depth.
function foldAmp(S, i, t, x, y, z) {
  const b = Math.sin(5.3 * x - 7.9 * y + 3.1 * z + 0.9 * t) * Math.cos(2.3 * y + 4.1 * z - 1.3 * x + 0.4 * t);
  let u = (b + 0.3) / 0.9;
  u = u < 0 ? 0 : u > 1 ? 1 : u;
  return S.foldAmp[i] * (0.25 + 0.75 * u * u * (3 - 2 * u));
}
function foldWeight(S, i, di, d, x, y, z) {
  const w = 1 - Math.max(di - d, 0) / (0.5 * S.k[i] + 0.03);
  if (w <= 0) return 0;
  let m = w * w * (3 - 2 * w);
  const F = S.fold, fo = i * 6;
  if (F[fo + 5] > 0) {
    const P = S.p, o = i * STRIDE;
    const dx = x - P[o], dy = y - P[o + 1], dz = z - P[o + 2];
    const qx = (P[o + 3] * dx + P[o + 4] * dy + P[o + 5] * dz) * P[o + 12];
    const qy = (P[o + 6] * dx + P[o + 7] * dy + P[o + 8] * dz) * P[o + 13];
    const qz = (P[o + 9] * dx + P[o + 10] * dy + P[o + 11] * dz) * P[o + 14];
    const lat = Math.abs(qz) / (Math.sqrt(qx * qx + qy * qy + qz * qz) + 1e-9);
    let t = (lat - F[fo + 4]) / (F[fo + 5] - F[fo + 4]);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    m *= t * t * (3 - 2 * t);
  }
  return m;
}

// Combined field over a culled shape list. `scratch` collects (shape, distance) of fold shapes.
function evalList(S, list, start, end, x, y, z, scratch) {
  let d = 1e9, nf = 0;
  for (let q = start; q < end; q++) {
    const i = list[q];
    const di = shapeDist(S, i, x, y, z);
    const kk = S.k[i];
    if (S.sign[i] > 0) {
      const h = kk - Math.abs(d - di);
      d = (d < di ? d : di) - (h > 0 ? h * h * 0.25 / kk : 0);
    } else {
      const nd = -di, h = kk - Math.abs(d - nd);
      d = (d > nd ? d : nd) + (h > 0 ? h * h * 0.25 / kk : 0);
    }
    if (S.foldAmp[i] > 0) { scratch[nf * 2] = i; scratch[nf * 2 + 1] = di; nf++; }
  }
  for (let f = 0; f < nf; f++) {
    const i = scratch[f * 2], di = scratch[f * 2 + 1];
    const w = foldWeight(S, i, di, d, x, y, z);
    if (w <= 0) continue;
    const t = foldPhase(S, i, x, y, z);
    d -= foldAmp(S, i, t, x, y, z) * w * Math.abs(Math.sin(Math.PI * t));
  }
  return d;
}

// Crease intensity in [0,1] at a surface point (1 = bottom of a fold).
function foldCrease(S, list, start, end, x, y, z) {
  if (!S.anyFold) return 0;
  // Reference field without fold displacement, to weight each fold shape.
  let d = 1e9;
  const dist = [];
  for (let q = start; q < end; q++) {
    const i = list[q];
    const di = shapeDist(S, i, x, y, z), kk = S.k[i];
    if (S.sign[i] > 0) { const h = kk - Math.abs(d - di); d = Math.min(d, di) - (h > 0 ? h * h * 0.25 / kk : 0); }
    else { const nd = -di, h = kk - Math.abs(d - nd); d = Math.max(d, nd) + (h > 0 ? h * h * 0.25 / kk : 0); }
    if (S.foldAmp[i] > 0) dist.push(i, di);
  }
  let c = 0;
  for (let f = 0; f < dist.length; f += 2) {
    const i = dist[f], w = foldWeight(S, i, dist[f + 1], d, x, y, z);
    if (w <= 0) continue;
    const t = foldPhase(S, i, x, y, z), g = Math.abs(Math.sin(Math.PI * t));
    c = Math.max(c, w * Math.pow(1 - g, 3) * foldAmp(S, i, t, x, y, z) / S.foldAmp[i]);
  }
  return c;
}

// Per-block shape lists (CSR). A shape is listed for a block when its box, grown by `margin(i)`,
// touches the block. Order is preserved, which keeps the smooth-union order of the sculpt.
function buildBlockLists(S, grid, marginOf) {
  const { ox, oy, oz, h, nbx, nby, nbz } = grid;
  const bs = BLOCK * h;
  const nb = nbx * nby * nbz;
  const ranges = new Int32Array(S.n * 6);
  const count = new Int32Array(nb + 1);
  const clampi = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
  for (let i = 0; i < S.n; i++) {
    const m = marginOf(i), b = i * 6;
    // Block bx covers points [bx*B, bx*B+B-1]; include the one-cell seam to the next block.
    const r = [
      clampi(Math.floor((S.box[b] - m - ox) / bs - 1e-9) , 0, nbx - 1),
      clampi(Math.floor((S.box[b + 1] - m - oy) / bs - 1e-9), 0, nby - 1),
      clampi(Math.floor((S.box[b + 2] - m - oz) / bs - 1e-9), 0, nbz - 1),
      clampi(Math.floor((S.box[b + 3] + m - ox) / bs), 0, nbx - 1),
      clampi(Math.floor((S.box[b + 4] + m - oy) / bs), 0, nby - 1),
      clampi(Math.floor((S.box[b + 5] + m - oz) / bs), 0, nbz - 1),
    ];
    for (let q = 0; q < 6; q++) ranges[b + q] = r[q];
    for (let bz = r[2]; bz <= r[5]; bz++) for (let by = r[1]; by <= r[4]; by++) for (let bx = r[0]; bx <= r[3]; bx++)
      count[bx + nbx * (by + nby * bz) + 1]++;
  }
  for (let b = 0; b < nb; b++) count[b + 1] += count[b];
  const list = new Int32Array(count[nb]);
  const fill = count.slice(0, nb);
  for (let i = 0; i < S.n; i++) {
    const b = i * 6;
    for (let bz = ranges[b + 2]; bz <= ranges[b + 5]; bz++) for (let by = ranges[b + 1]; by <= ranges[b + 4]; by++)
      for (let bx = ranges[b]; bx <= ranges[b + 3]; bx++) list[fill[bx + nbx * (by + nby * bz)]++] = i;
  }
  return { off: count, list };
}

// Unculled field evaluator (debugging, measuring the sculpt, placing details on the surface).
export function createSdf(bones, shapes) {
  const S = compileShapes(shapes, new Map(bones.map((b, i) => [b.name, i])));
  const all = Int32Array.from({ length: S.n }, (_, i) => i);
  const scratch = new Float64Array(2 * S.n + 2);
  return (x, y, z) => evalList(S, all, 0, S.n, x, y, z, scratch);
}

// ---------------------------------------------------------------------------------------------
export function cacheKey(bones, shapes, opts = {}) {
  const s = JSON.stringify([bones, shapes, opts.voxel ?? 0.035, 'v1']);
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193); }
  return (h >>> 0).toString(16);
}

/**
 * Build a skinned mesh from a bone list and an SDF shape list.
 * opts: { voxel = 0.035 m, material, smoothIterations = 6, onProgress(stage) }
 */
export function buildCreature(THREE, bones, shapes, opts = { voxel: 0.035 }) {
  const voxel = opts.voxel ?? 0.035;
  const timings = {};
  let tMark = performance.now();
  const lap = (name) => { const t = performance.now(); timings[name] = Math.round(t - tMark); tMark = t; };

  const boneIndex = new Map(bones.map((b, i) => [b.name, i]));
  const S = compileShapes(shapes, boneIndex);

  // ---- grid ----------------------------------------------------------------------------------
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < S.n; i++) {
    if (S.sign[i] < 0) continue;
    for (let r = 0; r < 3; r++) {
      lo[r] = Math.min(lo[r], S.box[i * 6 + r] - S.k[i] - S.foldAmp[i]);
      hi[r] = Math.max(hi[r], S.box[i * 6 + 3 + r] + S.k[i] + S.foldAmp[i]);
    }
  }
  const pad = 3 * voxel;
  const h = voxel;
  const nbx = Math.ceil((hi[0] - lo[0] + 2 * pad) / (BLOCK * h)) ;
  const nby = Math.ceil((hi[1] - lo[1] + 2 * pad) / (BLOCK * h));
  const nbz = Math.ceil((hi[2] - lo[2] + 2 * pad) / (BLOCK * h));
  const nx = nbx * BLOCK, ny = nby * BLOCK, nz = nbz * BLOCK;   // grid points per axis
  const grid = { ox: lo[0] - pad, oy: lo[1] - pad, oz: lo[2] - pad, h, nbx, nby, nbz };
  const { ox, oy, oz } = grid;

  // Narrow band: a block whose centre is farther than R from the surface cannot contain it
  // (1.3 = slack for the non-exact ellipsoid/smooth-min distances).
  const halfDiag = 0.5 * Math.sqrt(3) * BLOCK * h;
  const R = 1.3 * halfDiag + 2 * h;
  const fine = buildBlockLists(S, grid, (i) => S.k[i] + R + S.foldAmp[i] + h);
  const wide = buildBlockLists(S, grid, (i) => Math.max(S.k[i] + 0.05, AO_REACH) + S.foldAmp[i]);
  lap('lists');

  const field = new Float32Array(nx * ny * nz).fill(R);
  const scratch = new Float64Array(256);
  let evaluated = 0;
  for (let bz = 0; bz < nbz; bz++) for (let by = 0; by < nby; by++) for (let bx = 0; bx < nbx; bx++) {
    const b = bx + nbx * (by + nby * bz);
    const s0 = fine.off[b], s1 = fine.off[b + 1];
    if (s0 === s1) continue;
    const cx = ox + (bx * BLOCK + (BLOCK - 1) / 2) * h, cy = oy + (by * BLOCK + (BLOCK - 1) / 2) * h, cz = oz + (bz * BLOCK + (BLOCK - 1) / 2) * h;
    const dc = evalList(S, fine.list, s0, s1, cx, cy, cz, scratch);
    if (dc > R) continue;
    const far = dc < -R;
    for (let k = 0; k < BLOCK; k++) {
      const gk = bz * BLOCK + k, z = oz + gk * h;
      for (let j = 0; j < BLOCK; j++) {
        const gj = by * BLOCK + j, y = oy + gj * h;
        let idx = bx * BLOCK + nx * (gj + ny * gk);
        for (let i = 0; i < BLOCK; i++, idx++) {
          field[idx] = far ? -R : evalList(S, fine.list, s0, s1, ox + (bx * BLOCK + i) * h, y, z, scratch);
        }
      }
    }
    if (!far) evaluated++;
  }
  lap('field');

  // Field query at an arbitrary point using the block lists.
  const blockOf = (x, y, z) => {
    const bx = Math.floor((x - ox) / (BLOCK * h)), by = Math.floor((y - oy) / (BLOCK * h)), bz = Math.floor((z - oz) / (BLOCK * h));
    if (bx < 0 || by < 0 || bz < 0 || bx >= nbx || by >= nby || bz >= nbz) return -1;
    return bx + nbx * (by + nby * bz);
  };
  const sdf = (L, x, y, z) => {
    const b = blockOf(x, y, z);
    if (b < 0) return 1e3;
    const s0 = L.off[b], s1 = L.off[b + 1];
    if (s0 === s1) return 1e3;
    return evalList(S, L.list, s0, s1, x, y, z, scratch);
  };

  // ---- surface nets ----------------------------------------------------------------------------
  const sx = 1, sy = nx, sz = nx * ny;
  const cellVert = new Int32Array(nx * ny * nz).fill(-1);
  const pos = [];
  const cells = [];
  const corner = [0, sx, sy, sx + sy, sz, sx + sz, sy + sz, sx + sy + sz];
  const cOff = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]];
  const edges = [0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7];
  const v = new Float64Array(8);
  for (let k = 0; k < nz - 1; k++) for (let j = 0; j < ny - 1; j++) {
    let idx = nx * (j + ny * k);
    for (let i = 0; i < nx - 1; i++, idx++) {
      let mask = 0;
      for (let c = 0; c < 8; c++) { v[c] = field[idx + corner[c]]; if (v[c] < 0) mask |= 1 << c; }
      if (mask === 0 || mask === 255) continue;
      let px = 0, py = 0, pz = 0, cnt = 0;
      for (let e = 0; e < 24; e += 2) {
        const a = edges[e], b = edges[e + 1];
        if ((v[a] < 0) === (v[b] < 0)) continue;
        const t = v[a] / (v[a] - v[b]);
        px += cOff[a][0] + t * (cOff[b][0] - cOff[a][0]);
        py += cOff[a][1] + t * (cOff[b][1] - cOff[a][1]);
        pz += cOff[a][2] + t * (cOff[b][2] - cOff[a][2]);
        cnt++;
      }
      cellVert[idx] = pos.length / 3;
      cells.push(idx, mask);
      pos.push(ox + (i + px / cnt) * h, oy + (j + py / cnt) * h, oz + (k + pz / cnt) * h);
    }
  }
  const index = [];
  const quad = (a, b, c, d, flip) => {
    // Split along the shorter diagonal for fewer slivers.
    const dac = (pos[a * 3] - pos[c * 3]) ** 2 + (pos[a * 3 + 1] - pos[c * 3 + 1]) ** 2 + (pos[a * 3 + 2] - pos[c * 3 + 2]) ** 2;
    const dbd = (pos[b * 3] - pos[d * 3]) ** 2 + (pos[b * 3 + 1] - pos[d * 3 + 1]) ** 2 + (pos[b * 3 + 2] - pos[d * 3 + 2]) ** 2;
    if (flip) { const t = b; b = d; d = t; }
    if (dac <= dbd) index.push(a, b, c, a, c, d); else index.push(a, b, d, b, c, d);
  };
  for (let q = 0; q < cells.length; q += 2) {
    const idx = cells[q], mask = cells[q + 1];
    const i = idx % nx, j = Math.floor(idx / nx) % ny, k = Math.floor(idx / (nx * ny));
    const in0 = (mask & 1) !== 0;
    // Edge along +x from corner 0: cells around it in the (y,z) plane, ordered so the
    // winding normal is +x when the low end is inside.
    if (((mask >> 1) & 1) !== (mask & 1) && j > 0 && k > 0)
      quad(cellVert[idx - sy - sz], cellVert[idx - sz], cellVert[idx], cellVert[idx - sy], !in0);
    if (((mask >> 2) & 1) !== (mask & 1) && i > 0 && k > 0)
      quad(cellVert[idx - sx - sz], cellVert[idx - sx], cellVert[idx], cellVert[idx - sz], !in0);
    if (((mask >> 4) & 1) !== (mask & 1) && i > 0 && j > 0)
      quad(cellVert[idx - sx - sy], cellVert[idx - sy], cellVert[idx], cellVert[idx - sx], !in0);
  }
  lap('nets');

  const nV = pos.length / 3;
  const P = new Float32Array(pos);
  const N = new Float32Array(nV * 3);

  // ---- snap to iso-surface + analytic normals ----------------------------------------------------
  const eps = 0.35 * h;
  const tet = [1, -1, -1, -1, -1, 1, -1, 1, -1, 1, 1, 1];
  const grad = (x, y, z, out) => {
    let gx = 0, gy = 0, gz = 0;
    for (let t = 0; t < 12; t += 3) {
      const f = sdf(fine, x + tet[t] * eps, y + tet[t + 1] * eps, z + tet[t + 2] * eps);
      gx += tet[t] * f; gy += tet[t + 1] * f; gz += tet[t + 2] * f;
    }
    out[0] = gx; out[1] = gy; out[2] = gz;
  };
  const g = [0, 0, 0];
  for (let vi = 0; vi < nV; vi++) {
    let x = P[vi * 3], y = P[vi * 3 + 1], z = P[vi * 3 + 2];
    const x0 = x, y0 = y, z0 = z;
    for (let it = 0; it < 2; it++) {
      const d = sdf(fine, x, y, z);
      grad(x, y, z, g);
      const gl2 = g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
      if (gl2 < 1e-12) break;
      const gl = Math.sqrt(gl2);
      // Newton step along the unit gradient, clamped so thin features cannot collapse.
      let step = Math.max(-0.6 * h, Math.min(0.6 * h, d));
      x -= step * g[0] / gl; y -= step * g[1] / gl; z -= step * g[2] / gl;
    }
    // Keep the vertex near its cell (prevents fold-overs on very thin parts).
    const mx = x - x0, my = y - y0, mz = z - z0, ml = Math.sqrt(mx * mx + my * my + mz * mz);
    if (ml > 0.9 * h) { const s = 0.9 * h / ml; x = x0 + mx * s; y = y0 + my * s; z = z0 + mz * s; }
    P[vi * 3] = x; P[vi * 3 + 1] = y; P[vi * 3 + 2] = z;
    grad(x, y, z, g);
    const gl = Math.hypot(g[0], g[1], g[2]) || 1;
    N[vi * 3] = g[0] / gl; N[vi * 3 + 1] = g[1] / gl; N[vi * 3 + 2] = g[2] / gl;
  }
  lap('snap');

  // ---- cavity / occlusion from the field ---------------------------------------------------------
  // Small probes find creases (lip line, folds, toe gaps); long probes find occlusion by other body
  // parts (arm against chest, inner thigh). Both are sampled along the normal (Quilez-style SDF AO).
  const cavity = new Float32Array(nV), occl = new Float32Array(nV), crease = new Float32Array(nV);
  const smallH = [0.012, 0.025, 0.05], bigH = [0.1, 0.2, 0.35, 0.55];
  for (let vi = 0; vi < nV; vi++) {
    const x = P[vi * 3], y = P[vi * 3 + 1], z = P[vi * 3 + 2];
    const nx_ = N[vi * 3], ny_ = N[vi * 3 + 1], nz_ = N[vi * 3 + 2];
    let cs = 0, ws = 0;
    for (const hh of smallH) {
      const d = sdf(fine, x + nx_ * hh, y + ny_ * hh, z + nz_ * hh);
      cs += Math.max(0, hh - d) / hh; ws += 1;
    }
    let ob = 0, wb = 0, wt = 1;
    for (const hh of bigH) {
      const d = sdf(wide, x + nx_ * hh, y + ny_ * hh, z + nz_ * hh);
      ob += wt * Math.max(0, hh - d) / hh; wb += wt; wt *= 0.75;
    }
    const c = Math.min(1, 1.6 * cs / ws), o = Math.min(1, 1.5 * ob / wb);
    occl[vi] = o;
    const b = blockOf(x, y, z);
    crease[vi] = b >= 0 ? foldCrease(S, fine.list, fine.off[b], fine.off[b + 1], x, y, z) : 0;
    cavity[vi] = Math.min(1, Math.max(c, 0.75 * o, 0.6 * crease[vi]));
  }
  lap('cavity');

  // ---- skin weights + regions ---------------------------------------------------------------------
  // Each additive shape pulls its vertex towards its bone with a weight that decays with the
  // distance between the shape's own surface and the final surface (scaled by its blend radius).
  const nB = bones.length;
  const W = new Float32Array(nV * nB);
  const region = new Float32Array(nV), feature = new Float32Array(nV);
  for (let vi = 0; vi < nV; vi++) {
    const x = P[vi * 3], y = P[vi * 3 + 1], z = P[vi * 3 + 2];
    const b = blockOf(x, y, z);
    let best = -1, bestW = 0, sum = 0;
    if (b >= 0) {
      for (let q = fine.off[b]; q < fine.off[b + 1]; q++) {
        const i = fine.list[q];
        if (S.sign[i] < 0) continue;
        const di = shapeDist(S, i, x, y, z);
        const sig = 0.3 * S.k[i] + 0.02;
        const w = Math.exp(-Math.max(di, 0) / sig);
        if (w < 1e-3) continue;
        W[vi * nB + S.bone[i]] += w; sum += w;
        // Region / feature: the shape that actually forms the surface here (|distance| smallest,
        // so a shape that merely contains the vertex does not win).
        const score = Math.exp(-Math.abs(di) / sig);
        if (score > bestW) { bestW = score; best = i; }
      }
    }
    if (sum > 0) for (let q = 0; q < nB; q++) W[vi * nB + q] /= sum;
    else W[vi * nB] = 1;
    region[vi] = best >= 0 ? S.region[best] : 3;
    feature[vi] = best >= 0 ? S.feature[best] : 0;
  }
  // Laplacian smoothing over the mesh removes blotches where many shapes meet.
  const nT = index.length;
  const deg = new Int32Array(nV + 1);
  for (let t = 0; t < nT; t += 3) for (let e = 0; e < 3; e++) { deg[index[t + e] + 1] += 2; }
  for (let i = 0; i < nV; i++) deg[i + 1] += deg[i];
  const adj = new Int32Array(deg[nV]);
  const fillA = deg.slice(0, nV);
  for (let t = 0; t < nT; t += 3) {
    const a = index[t], b = index[t + 1], c = index[t + 2];
    adj[fillA[a]++] = b; adj[fillA[a]++] = c; adj[fillA[b]++] = c; adj[fillA[b]++] = a; adj[fillA[c]++] = a; adj[fillA[c]++] = b;
  }
  const iters = opts.smoothIterations ?? 6;
  let Wa = W, Wb = new Float32Array(W.length);
  for (let it = 0; it < iters; it++) {
    for (let vi = 0; vi < nV; vi++) {
      const s0 = deg[vi], s1 = deg[vi + 1], cnt = s1 - s0, base = vi * nB;
      if (cnt === 0) { for (let q = 0; q < nB; q++) Wb[base + q] = Wa[base + q]; continue; }
      const inv = 0.5 / cnt;
      for (let q = 0; q < nB; q++) Wb[base + q] = 0.5 * Wa[base + q];
      for (let a = s0; a < s1; a++) { const nb = adj[a] * nB; for (let q = 0; q < nB; q++) Wb[base + q] += inv * Wa[nb + q]; }
    }
    const t = Wa; Wa = Wb; Wb = t;
  }
  const skinIndex = new Uint16Array(nV * 4), skinWeight = new Float32Array(nV * 4);
  const top = [0, 0, 0, 0], topW = [0, 0, 0, 0];
  for (let vi = 0; vi < nV; vi++) {
    for (let q = 0; q < 4; q++) { top[q] = 0; topW[q] = -1; }
    const base = vi * nB;
    for (let q = 0; q < nB; q++) {
      const w = Wa[base + q];
      if (w <= topW[3]) continue;
      let s = 3;
      while (s > 0 && w > topW[s - 1]) { topW[s] = topW[s - 1]; top[s] = top[s - 1]; s--; }
      topW[s] = w; top[s] = q;
    }
    let sum = 0;
    for (let q = 0; q < 4; q++) { if (topW[q] < 0.01) topW[q] = 0; sum += topW[q]; }
    for (let q = 0; q < 4; q++) { skinIndex[vi * 4 + q] = top[q]; skinWeight[vi * 4 + q] = sum > 0 ? topW[q] / sum : (q === 0 ? 1 : 0); }
  }
  lap('skin');

  // ---- geometry + skeleton -------------------------------------------------------------------------
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(P, 3));
  geometry.setAttribute('normal', new THREE.BufferAttribute(N, 3));
  geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(skinIndex, 4));
  geometry.setAttribute('skinWeight', new THREE.BufferAttribute(skinWeight, 4));
  geometry.setAttribute('aRegion', new THREE.BufferAttribute(region, 1));
  geometry.setAttribute('aCavity', new THREE.BufferAttribute(cavity, 1));
  geometry.setAttribute('aFold', new THREE.BufferAttribute(crease, 1));
  geometry.setAttribute('aOcclusion', new THREE.BufferAttribute(occl, 1));
  geometry.setAttribute('aFeature', new THREE.BufferAttribute(feature, 1));
  geometry.setIndex(nV > 65535 ? new THREE.Uint32BufferAttribute(index, 1) : new THREE.Uint16BufferAttribute(index, 1));
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  // Bones rest with identity rotation, so every bone's local axes equal the model axes
  // (+x forward, +y up, +z right): animation code rotates them about those axes.
  const boneMap = new Map();
  const boneList = bones.map((b) => { const bone = new THREE.Bone(); bone.name = b.name; boneMap.set(b.name, bone); return bone; });
  const heads = new Map(bones.map((b) => [b.name, b.head]));
  const roots = [];
  bones.forEach((b, i) => {
    const bone = boneList[i];
    if (b.parent) {
      const ph = heads.get(b.parent);
      if (!ph) throw new Error(`sdf_mesher: bone '${b.name}' has unknown parent '${b.parent}'`);
      bone.position.set(b.head[0] - ph[0], b.head[1] - ph[1], b.head[2] - ph[2]);
      boneMap.get(b.parent).add(bone);
    } else {
      bone.position.set(b.head[0], b.head[1], b.head[2]);
      roots.push(bone);
    }
  });
  const material = opts.material || new THREE.MeshStandardMaterial({ color: 0x8c8c8c, roughness: 0.85, metalness: 0 });
  const mesh = new THREE.SkinnedMesh(geometry, material);
  mesh.name = 'creature';
  roots.forEach((r) => mesh.add(r));
  mesh.updateMatrixWorld(true);
  const skeleton = new THREE.Skeleton(boneList);
  mesh.bind(skeleton);
  // Animated limbs leave the rest-pose bounds; culling a 12 m animal is not worth the popping risk.
  mesh.frustumCulled = false;
  lap('three');

  let volume = 0;
  for (let t = 0; t < nT; t += 3) {
    const a = index[t] * 3, b = index[t + 1] * 3, c = index[t + 2] * 3;
    volume += (P[a] * (P[b + 1] * P[c + 2] - P[b + 2] * P[c + 1]) - P[a + 1] * (P[b] * P[c + 2] - P[b + 2] * P[c]) + P[a + 2] * (P[b] * P[c + 1] - P[b + 1] * P[c])) / 6;
  }
  const stats = {
    vertices: nV, triangles: nT / 3, grid: [nx, ny, nz], blocksEvaluated: evaluated, voxel, volume,
    bbox: { min: geometry.boundingBox.min.toArray(), max: geometry.boundingBox.max.toArray() }, timings,
  };
  mesh.userData.stats = stats;
  mesh.userData.regions = REGIONS;
  return { mesh, skeleton, boneMap, restGeometry: geometry, stats };
}
