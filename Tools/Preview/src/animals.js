// Procedural animal bodies driven by the simulated state (position, heading, posture, foot
// placements). Proportions follow the body plan of each species; colours are SPECULATIVE
// (integument colour is unknown for these taxa) and use neutral countershading.
import * as THREE from 'three';
import { makeRng } from './textures.js';

// Body axis stations: [x, y, halfWidth, halfHeight] in body lengths, x forward from the hip joint,
// y relative to hip-joint height. `neck` = index of the first station that belongs to the neck/head.
const PLANS = {
  facultative_quadruped_hadrosaur: {
    stations: [[-0.50, -0.02, 0.004, 0.006], [-0.44, -0.01, 0.010, 0.018], [-0.36, 0.005, 0.016, 0.032], [-0.27, 0.02, 0.024, 0.046],
      [-0.18, 0.03, 0.034, 0.060], [-0.09, 0.035, 0.050, 0.075], [0.0, 0.03, 0.070, 0.092], [0.06, 0.015, 0.082, 0.104], [0.12, 0.0, 0.086, 0.108],
      [0.18, -0.01, 0.080, 0.100], [0.23, -0.01, 0.066, 0.084], [0.27, 0.0, 0.042, 0.055], [0.30, 0.015, 0.032, 0.044], [0.33, 0.03, 0.028, 0.038],
      [0.36, 0.04, 0.028, 0.040], [0.39, 0.035, 0.026, 0.036], [0.42, 0.025, 0.021, 0.026], [0.45, 0.015, 0.019, 0.017], [0.48, 0.008, 0.022, 0.011], [0.50, 0.004, 0.019, 0.007]],
    neck: 11, quad: true, hind: 'digitigrade', fore: 'hoof', foreLen: 0.70,
    dorsal: [0.34, 0.31, 0.25], ventral: [0.50, 0.46, 0.37], pattern: 0.14,
  },
  quadruped_ceratopsian: {
    stations: [[-0.36, -0.06, 0.008, 0.010], [-0.30, -0.035, 0.018, 0.026], [-0.22, -0.01, 0.034, 0.046], [-0.12, 0.01, 0.060, 0.072],
      [0.0, 0.02, 0.095, 0.105], [0.08, 0.02, 0.120, 0.120], [0.16, 0.0, 0.125, 0.122], [0.24, -0.03, 0.110, 0.110], [0.30, -0.05, 0.080, 0.085],
      [0.34, -0.03, 0.060, 0.070], [0.38, -0.01, 0.070, 0.085], [0.44, -0.02, 0.062, 0.078], [0.50, -0.04, 0.050, 0.065], [0.56, -0.07, 0.034, 0.048],
      [0.61, -0.10, 0.018, 0.028], [0.64, -0.12, 0.006, 0.010]],
    neck: 9, quad: true, hind: 'graviportal', fore: 'graviportal', foreLen: 0.80, frill: true, horns: true,
    dorsal: [0.36, 0.31, 0.25], ventral: [0.50, 0.45, 0.36], pattern: 0.12,
  },
  quadruped_ankylosaur: {
    stations: [[-0.52, -0.02, 0.040, 0.030], [-0.46, -0.02, 0.012, 0.012], [-0.36, 0.0, 0.020, 0.020], [-0.24, 0.02, 0.045, 0.040], [-0.12, 0.03, 0.10, 0.065],
      [0.0, 0.03, 0.15, 0.085], [0.10, 0.02, 0.165, 0.090], [0.20, 0.0, 0.155, 0.085], [0.28, -0.02, 0.12, 0.070], [0.34, -0.03, 0.07, 0.050],
      [0.38, -0.035, 0.06, 0.045], [0.43, -0.04, 0.07, 0.045], [0.48, -0.05, 0.06, 0.035], [0.51, -0.06, 0.03, 0.020]],
    neck: 9, quad: true, hind: 'graviportal', fore: 'graviportal', foreLen: 0.9, armour: true, club: true,
    dorsal: [0.34, 0.30, 0.24], ventral: [0.52, 0.47, 0.38], pattern: 0.08,
  },
  biped_large_theropod: {
    stations: [[-0.55, 0.02, 0.004, 0.006], [-0.46, 0.025, 0.012, 0.020], [-0.36, 0.03, 0.020, 0.034], [-0.26, 0.03, 0.034, 0.054],
      [-0.16, 0.025, 0.046, 0.068], [-0.07, 0.015, 0.056, 0.080], [0.02, 0.0, 0.058, 0.084], [0.10, -0.015, 0.060, 0.088], [0.17, -0.01, 0.050, 0.074],
      [0.22, 0.01, 0.036, 0.052], [0.26, 0.035, 0.032, 0.044], [0.29, 0.05, 0.034, 0.046], [0.32, 0.055, 0.036, 0.050], [0.36, 0.045, 0.032, 0.044],
      [0.40, 0.035, 0.024, 0.036], [0.43, 0.028, 0.017, 0.026], [0.45, 0.024, 0.008, 0.012]],
    neck: 9, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.06,
    dorsal: [0.33, 0.29, 0.23], ventral: [0.60, 0.55, 0.45], pattern: 0.10,
  },
  biped_small_theropod: {
    stations: [[-0.60, 0.05, 0.004, 0.006], [-0.48, 0.05, 0.012, 0.016], [-0.34, 0.05, 0.022, 0.028], [-0.20, 0.045, 0.034, 0.040], [-0.06, 0.04, 0.050, 0.060],
      [0.06, 0.04, 0.058, 0.074], [0.15, 0.05, 0.050, 0.066], [0.21, 0.07, 0.030, 0.034], [0.26, 0.10, 0.024, 0.026], [0.30, 0.12, 0.030, 0.036],
      [0.35, 0.115, 0.026, 0.030], [0.39, 0.105, 0.015, 0.018], [0.41, 0.10, 0.006, 0.008]],
    neck: 7, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.16, feathers: true,
    dorsal: [0.36, 0.30, 0.24], ventral: [0.62, 0.56, 0.46], pattern: 0.12,
  },
  biped_small_ornithischian: {
    stations: [[-0.58, 0.03, 0.004, 0.006], [-0.46, 0.035, 0.012, 0.018], [-0.32, 0.04, 0.024, 0.034], [-0.18, 0.04, 0.040, 0.054], [-0.05, 0.035, 0.060, 0.074],
      [0.06, 0.025, 0.068, 0.082], [0.15, 0.02, 0.060, 0.072], [0.22, 0.03, 0.040, 0.046], [0.27, 0.06, 0.026, 0.030], [0.31, 0.085, 0.024, 0.028],
      [0.35, 0.09, 0.026, 0.032], [0.39, 0.08, 0.020, 0.024], [0.42, 0.07, 0.008, 0.010]],
    neck: 7, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.12,
    dorsal: [0.38, 0.34, 0.26], ventral: [0.62, 0.58, 0.46], pattern: 0.14,
  },
  biped_pachycephalosaur: {
    stations: [[-0.56, 0.02, 0.004, 0.006], [-0.44, 0.03, 0.014, 0.020], [-0.30, 0.035, 0.028, 0.038], [-0.16, 0.035, 0.046, 0.058], [-0.04, 0.03, 0.070, 0.080],
      [0.07, 0.02, 0.080, 0.088], [0.16, 0.01, 0.070, 0.078], [0.23, 0.02, 0.046, 0.050], [0.28, 0.05, 0.034, 0.038], [0.32, 0.08, 0.034, 0.040],
      [0.36, 0.09, 0.040, 0.050], [0.40, 0.08, 0.032, 0.040], [0.43, 0.065, 0.018, 0.022], [0.45, 0.055, 0.008, 0.010]],
    neck: 7, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.10, dome: true,
    dorsal: [0.38, 0.33, 0.26], ventral: [0.62, 0.57, 0.46], pattern: 0.12,
  },
  biped_ornithomimid: {
    stations: [[-0.55, 0.04, 0.004, 0.006], [-0.44, 0.045, 0.012, 0.016], [-0.30, 0.05, 0.022, 0.030], [-0.16, 0.05, 0.036, 0.048], [-0.03, 0.045, 0.054, 0.066],
      [0.08, 0.04, 0.060, 0.072], [0.16, 0.05, 0.048, 0.058], [0.21, 0.08, 0.020, 0.022], [0.25, 0.14, 0.016, 0.018], [0.28, 0.20, 0.016, 0.018],
      [0.31, 0.23, 0.020, 0.022], [0.34, 0.225, 0.014, 0.014], [0.36, 0.22, 0.005, 0.006]],
    neck: 7, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.2, feathers: true,
    dorsal: [0.40, 0.36, 0.30], ventral: [0.70, 0.66, 0.58], pattern: 0.08,
  },
  biped_oviraptorosaur: {
    stations: [[-0.45, 0.05, 0.004, 0.006], [-0.36, 0.05, 0.016, 0.020], [-0.24, 0.05, 0.032, 0.040], [-0.10, 0.045, 0.054, 0.066], [0.04, 0.045, 0.064, 0.080],
      [0.14, 0.06, 0.054, 0.068], [0.20, 0.10, 0.024, 0.026], [0.25, 0.17, 0.020, 0.022], [0.29, 0.23, 0.020, 0.024], [0.33, 0.25, 0.026, 0.040],
      [0.37, 0.24, 0.018, 0.030], [0.39, 0.23, 0.006, 0.010]],
    neck: 6, quad: false, pitch: 0.0, hind: 'digitigrade', arms: 0.22, feathers: true,
    dorsal: [0.34, 0.30, 0.26], ventral: [0.62, 0.58, 0.50], pattern: 0.10,
  },
  quadruped_crocodylian: {
    stations: [[-0.52, 0.0, 0.004, 0.004], [-0.40, 0.0, 0.020, 0.020], [-0.26, 0.0, 0.040, 0.034], [-0.12, 0.0, 0.065, 0.045], [0.0, 0.0, 0.085, 0.050],
      [0.12, 0.0, 0.090, 0.050], [0.22, 0.0, 0.075, 0.044], [0.30, 0.0, 0.050, 0.034], [0.36, 0.0, 0.048, 0.030], [0.42, -0.005, 0.034, 0.020],
      [0.48, -0.01, 0.020, 0.012], [0.52, -0.01, 0.010, 0.008]],
    neck: 8, quad: true, hind: 'sprawl', fore: 'sprawl', foreLen: 1.0,
    dorsal: [0.24, 0.26, 0.20], ventral: [0.55, 0.53, 0.42], pattern: 0.15,
  },
  aquatic_reptile: {
    stations: [[-0.52, 0.0, 0.004, 0.004], [-0.40, 0.0, 0.018, 0.022], [-0.26, 0.0, 0.036, 0.032], [-0.12, 0.0, 0.060, 0.042], [0.0, 0.0, 0.080, 0.048],
      [0.12, 0.0, 0.085, 0.048], [0.22, 0.0, 0.068, 0.040], [0.29, 0.0, 0.050, 0.032], [0.34, 0.0, 0.044, 0.028], [0.40, 0.0, 0.016, 0.014],
      [0.47, 0.0, 0.009, 0.008], [0.52, 0.0, 0.004, 0.004]],
    neck: 8, quad: true, hind: 'sprawl', fore: 'sprawl', foreLen: 1.0,
    dorsal: [0.22, 0.24, 0.20], ventral: [0.50, 0.50, 0.42], pattern: 0.12,
  },
  quadruped_small_mammal: {
    stations: [[-0.48, 0.0, 0.010, 0.010], [-0.30, 0.02, 0.025, 0.025], [-0.14, 0.04, 0.06, 0.06], [0.0, 0.05, 0.10, 0.10], [0.14, 0.05, 0.11, 0.11],
      [0.26, 0.04, 0.09, 0.09], [0.34, 0.04, 0.07, 0.07], [0.40, 0.04, 0.06, 0.06], [0.46, 0.03, 0.04, 0.04], [0.50, 0.02, 0.012, 0.012]],
    neck: 6, quad: true, hind: 'mammal', fore: 'mammal', foreLen: 1.0, fur: true,
    dorsal: [0.24, 0.20, 0.16], ventral: [0.42, 0.36, 0.30], pattern: 0.06,
  },
  quadruped_azhdarchid: {
    stations: [[-0.16, 0.0, 0.010, 0.010], [-0.08, 0.02, 0.05, 0.05], [0.04, 0.05, 0.07, 0.07], [0.12, 0.08, 0.05, 0.05], [0.20, 0.20, 0.025, 0.025],
      [0.28, 0.40, 0.022, 0.022], [0.34, 0.55, 0.024, 0.024], [0.40, 0.62, 0.05, 0.06], [0.52, 0.58, 0.035, 0.040], [0.68, 0.50, 0.015, 0.018], [0.80, 0.45, 0.003, 0.004]],
    neck: 4, quad: true, hind: 'mammal', fore: 'wing', foreLen: 1.6,
    dorsal: [0.42, 0.38, 0.34], ventral: [0.70, 0.68, 0.64], pattern: 0.06,
  },
};

const lin = (c) => Math.pow(c, 2.2);

// Catmull-Rom interpolation of station rows.
function interpStations(st, perSeg = 3) {
  const out = [];
  for (let i = 0; i < st.length - 1; i++) {
    const p0 = st[Math.max(0, i - 1)], p1 = st[i], p2 = st[i + 1], p3 = st[Math.min(st.length - 1, i + 2)];
    for (let k = 0; k < perSeg; k++) {
      const t = k / perSeg, t2 = t * t, t3 = t2 * t;
      out.push(p1.map((_, j) => 0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3)));
    }
  }
  out.push(st[st.length - 1].slice());
  return out;
}

// Lofted body: rings of elliptical cross-section along a 3D centre line (local frame: x forward, y up, z right).
function loft(centres, halfW, halfH, colorFn, segments = 18, uvScale = 1) {
  const pos = [], col = [], uv = [], idx = [];
  const n = centres.length;
  let along = 0;
  for (let i = 0; i < n; i++) {
    if (i > 0) along += centres[i].distanceTo(centres[i - 1]);
    const t = centres[Math.min(n - 1, i + 1)].clone().sub(centres[Math.max(0, i - 1)]).normalize();
    const side = new THREE.Vector3(0, 1, 0).cross(t).normalize();
    if (side.lengthSq() < 1e-6) side.set(0, 0, 1);
    const up = t.clone().cross(side).normalize();
    for (let k = 0; k <= segments; k++) {
      const a = (k / segments) * Math.PI * 2;
      const s = Math.sin(a), c = Math.cos(a);
      // Flattened belly, slightly narrower dorsal ridge.
      const yy = s * halfH[i] * (s < 0 ? 0.92 : 1.0);
      const xx = c * halfW[i] * (1 - 0.18 * Math.max(0, s) * Math.max(0, s));
      const p = centres[i].clone().add(side.clone().multiplyScalar(xx)).add(up.clone().multiplyScalar(yy));
      pos.push(p.x, p.y, p.z);
      const cc = colorFn(p, s, i / (n - 1));
      col.push(cc[0], cc[1], cc[2]);
      uv.push((k / segments) * 4 * uvScale, along * uvScale);
    }
    if (i < n - 1) for (let k = 0; k < segments; k++) {
      const a = i * (segments + 1) + k, b = a + segments + 1;
      idx.push(a, a + 1, b, b, a + 1, b + 1);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

function limbTube(points, radii, colorFn, segments = 10) {
  const centres = [];
  const hw = [], hh = [];
  // Subdivide joints for smooth knees/ankles.
  for (let i = 0; i < points.length - 1; i++) {
    for (let k = 0; k < 3; k++) {
      const t = k / 3;
      centres.push(points[i].clone().lerp(points[i + 1], t));
      const r = radii[i] + (radii[i + 1] - radii[i]) * t;
      hw.push(r); hh.push(r);
    }
  }
  centres.push(points[points.length - 1].clone());
  hw.push(radii[radii.length - 1] * 0.6); hh.push(radii[radii.length - 1] * 0.6);
  return loftAny(centres, hw, hh, colorFn, segments);
}

// Loft whose frames do not assume a horizontal axis (limbs).
function loftAny(centres, halfW, halfH, colorFn, segments) {
  const pos = [], col = [], uv = [], idx = [];
  const n = centres.length;
  let prev = null;
  for (let i = 0; i < n; i++) {
    const t = centres[Math.min(n - 1, i + 1)].clone().sub(centres[Math.max(0, i - 1)]).normalize();
    let side = prev ? prev.clone().sub(t.clone().multiplyScalar(prev.dot(t))).normalize() : new THREE.Vector3(1, 0, 0).cross(t).normalize();
    if (side.lengthSq() < 1e-6) side = new THREE.Vector3(0, 0, 1).cross(t).normalize();
    prev = side;
    const up = t.clone().cross(side).normalize();
    for (let k = 0; k <= segments; k++) {
      const a = (k / segments) * Math.PI * 2;
      const p = centres[i].clone().add(side.clone().multiplyScalar(Math.cos(a) * halfW[i])).add(up.clone().multiplyScalar(Math.sin(a) * halfH[i]));
      pos.push(p.x, p.y, p.z);
      const cc = colorFn(p, 0.0, i / (n - 1));
      col.push(cc[0], cc[1], cc[2]);
      uv.push(k / segments * 2, i / (n - 1) * 2);
    }
    if (i < n - 1) for (let k = 0; k < segments; k++) {
      const a = i * (segments + 1) + k, b = a + segments + 1;
      idx.push(a, b, a + 1, b, b + 1, a + 1);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

// Two-bone IK: joint position between a and c with segment lengths l1, l2, bending towards `pole`.
function solveJoint(a, c, l1, l2, pole) {
  const d = c.clone().sub(a);
  let dist = d.length();
  const dir = d.clone().normalize();
  dist = Math.min(dist, (l1 + l2) * 0.999);
  const x = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist);
  const h = Math.sqrt(Math.max(0, l1 * l1 - x * x));
  const perp = pole.clone().sub(dir.clone().multiplyScalar(pole.dot(dir))).normalize();
  return a.clone().add(dir.multiplyScalar(x)).add(perp.multiplyScalar(h));
}

function hash3(x, y, z) {
  const s = Math.sin(x * 12.9898 + y * 78.233 + z * 37.719) * 43758.5453;
  return s - Math.floor(s);
}
function vnoise(x, y, z) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = x - ix, fy = y - iy, fz = z - iz;
  const sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy), sz = fz * fz * (3 - 2 * fz);
  let v = 0;
  for (let k = 0; k < 8; k++) {
    const dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
    v += hash3(ix + dx, iy + dy, iz + dz) * (dx ? sx : 1 - sx) * (dy ? sy : 1 - sy) * (dz ? sz : 1 - sz);
  }
  return v;
}

export function buildAnimal(c, sp, toLocalFoot, skinNormal) {
  const plan = PLANS[sp.plan];
  if (!plan) return null;
  const L = c.len, H = c.hip;
  const rng = makeRng(c.id % 100000);
  const lying = c.posture === 5;
  const headDown = c.posture === 2;
  const headUp = c.posture === 1;
  const swimming = c.posture === 6 || sp.plan === 'aquatic_reptile';
  // Individual colour variation (± few %); colour itself is speculative.
  const tint = 0.92 + rng() * 0.16;
  const dorsal = plan.dorsal.map((v) => lin(v * tint));
  const ventral = plan.ventral.map((v) => lin(v * (0.95 + rng() * 0.1)));
  const seed = rng() * 100;
  const colorFn = (p, s, t) => {
    const k = Math.min(1, Math.max(0, (-s + 0.15) / 0.75));
    const n = vnoise(p.x / (L * 0.035) + seed, p.y / (L * 0.035), p.z / (L * 0.035));
    const n2 = vnoise(p.x / (L * 0.012) + seed, p.y / (L * 0.012), p.z / (L * 0.012));
    const m = 1 + (n - 0.5) * plan.pattern * 2.2 + (n2 - 0.5) * 0.08;
    return [0, 1, 2].map((j) => (dorsal[j] * (1 - k) + ventral[j] * k) * m);
  };
  const limbColor = (p) => {
    const n = vnoise(p.x / (L * 0.03) + seed, p.y / (L * 0.03), p.z / (L * 0.03));
    return dorsal.map((v, j) => (v * 0.85 + ventral[j] * 0.15) * (0.95 + (n - 0.5) * 0.2));
  };

  // Stations are normalised to one body length (snout to tail tip). The hip joint sits where the
  // simulation plants the hind feet: 0.08 L behind the reference point for quadrupeds, at it for bipeds.
  const span = plan.stations[plan.stations.length - 1][0] - plan.stations[0][0];
  const xs = 1 / span;
  const hipX = plan.quad ? -0.08 * L : 0;
  const foreFootX = (sp.plan === 'quadruped_crocodylian' || sp.plan === 'quadruped_small_mammal') ? 0.18 : 0.22;
  const shoulderRel = (foreFootX + 0.08) * L;
  const hipHalfH = plan.stations.find((q) => q[0] >= 0)[3] * L;
  const hipY = lying ? hipHalfH * 0.95 : (swimming ? 0.0 : H);
  // Quadrupeds: body pitched so the shoulder joint sits at fore-limb height (short-armed hadrosaurs
  // and ceratopsians carry the chest lower than the hips).
  let pitch = 0;
  if (plan.quad && !lying && !swimming) pitch = Math.atan2((plan.foreLen - 1) * H * 0.9, shoulderRel);
  const st = plan.stations.map((q) => [q[0] * xs, q[1], q[2], q[3]]);
  const neckBase = st[plan.neck];
  let neckAngle = 0;
  if (headDown) neckAngle = plan.quad ? -0.55 : -0.5;
  if (headUp) neckAngle = 0.3;
  if (lying) neckAngle = -0.1;
  // Compensate the body pitch so the neck posture is relative to the horizon.
  neckAngle -= pitch * 0.6;
  const centres = [], hw = [], hh = [];
  const rows = interpStations(st, 3);
  const neckX = neckBase[0];
  for (const r of rows) {
    let x = r[0] * L, y = r[1] * L;
    if (r[0] > neckX && neckAngle !== 0) {
      const dx = x - neckX * L, dy = y - neckBase[1] * L;
      const w = Math.min(1, (r[0] - neckX) / 0.07);
      const ang = neckAngle * w;
      x = neckX * L + dx * Math.cos(ang) - dy * Math.sin(ang);
      y = neckBase[1] * L + dx * Math.sin(ang) + dy * Math.cos(ang);
    }
    // Pitch about the hip joint; the tail only partly follows.
    y += x * Math.tan(pitch) * (x > 0 ? 1.0 : 0.3);
    centres.push(new THREE.Vector3(hipX + x, hipY + y, 0));
    hw.push(r[2] * L);
    hh.push(r[3] * L);
  }
  // Lying: keep the belly and the tail on the ground.
  if (lying) for (let i = 0; i < centres.length; i++) centres[i].y = Math.max(centres[i].y, hh[i] * 0.85);
  const nearestStation = (x) => {
    let bi = 0;
    for (let i = 0; i < centres.length; i++) if (Math.abs(centres[i].x - x) < Math.abs(centres[bi].x - x)) bi = i;
    return bi;
  };
  const group = new THREE.Group();
  const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: plan.fur ? 0.95 : 0.8, metalness: 0, normalMap: plan.fur ? null : skinNormal,
    normalScale: new THREE.Vector2(0.35, 0.35), envMapIntensity: 0.8 });
  const muscleMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.8, metalness: 0, envMapIntensity: 0.8 });
  const frillMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.75, metalness: 0, envMapIntensity: 0.8, side: THREE.DoubleSide });
  const body = new THREE.Mesh(loft(centres, hw, hh, colorFn, 22, 1 / Math.max(0.3, L * 0.06)), mat);
  body.castShadow = true; body.receiveShadow = true;
  group.add(body);

  // Head accessories.
  const headIdx = centres.length - 1;
  const head = centres[Math.max(0, headIdx - 6)];
  const headDir = centres[headIdx].clone().sub(centres[Math.max(0, headIdx - 9)]).normalize();
  const accMat = new THREE.MeshStandardMaterial({ color: new THREE.Color().setRGB(dorsal[0] * 0.9, dorsal[1] * 0.9, dorsal[2] * 0.9), roughness: 0.8 });
  const hornMat = new THREE.MeshStandardMaterial({ color: new THREE.Color().setRGB(lin(0.55), lin(0.50), lin(0.42)), roughness: 0.6 });
  if (plan.frill) {
    // Skull landmarks from the head stations (orbit roughly mid-skull, frill at the parietal margin).
    const skullBack = centres[nearestStation(centres[headIdx].x - 0.26 * L)];
    const orbit = centres[nearestStation(centres[headIdx].x - 0.17 * L)];
    const snout = centres[nearestStation(centres[headIdx].x - 0.06 * L)];
    // Saddle-shaped parietosquamosal frill: a shallow bowl leaning back over the neck, concave
    // forwards, so its curved lateral margins read in side view.
    const lean = 0.95; // radians back from vertical
    const n = new THREE.Vector3(Math.cos(lean), Math.sin(lean), 0);
    const u = new THREE.Vector3(-Math.sin(lean), Math.cos(lean), 0);
    const fg = new THREE.SphereGeometry(1, 30, 12, 0, Math.PI * 2, 0, 1.0);
    fg.translate(0, -1, 0);
    fg.scale(L * 0.15, L * 0.10, L * 0.16);
    const frill = new THREE.Mesh(colorGeometry(fg, () => dorsal.map((v) => v * 0.97)), frillMat);
    frill.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), n.clone().negate());
    frill.position.copy(skullBack).add(u.clone().multiplyScalar(L * 0.085)).add(n.clone().multiplyScalar(-L * 0.015));
    frill.castShadow = true; frill.receiveShadow = true;
    group.add(frill);
    for (const sz of [-1, 1]) {
      const h = new THREE.Mesh(new THREE.ConeGeometry(L * 0.014, L * 0.12, 10), hornMat);
      h.geometry.translate(0, L * 0.06, 0);
      h.position.copy(orbit).add(new THREE.Vector3(0, L * 0.04, sz * L * 0.028));
      h.rotation.set(sz * 0.12, 0, -1.05);
      h.castShadow = true;
      group.add(h);
    }
    const nh = new THREE.Mesh(new THREE.ConeGeometry(L * 0.008, L * 0.04, 10), hornMat);
    nh.geometry.translate(0, L * 0.02, 0);
    nh.position.copy(snout).add(new THREE.Vector3(0, L * 0.03, 0));
    nh.rotation.z = -0.5;
    nh.castShadow = true;
    group.add(nh);
  }
  if (plan.dome) {
    const d = new THREE.Mesh(new THREE.SphereGeometry(L * 0.032, 16, 12), hornMat);
    d.position.copy(head).add(new THREE.Vector3(-L * 0.005, L * 0.032, 0));
    d.castShadow = true;
    group.add(d);
  }
  if (plan.armour) {
    // Rows of keeled osteoderms along the back and flanks.
    for (let i = 4; i < centres.length - 14; i += 2) {
      for (const side of [-0.9, -0.55, 0, 0.55, 0.9]) {
        const p = centres[i];
        const a = side * 1.2;
        const o = new THREE.Mesh(new THREE.ConeGeometry(L * 0.012, L * 0.02, 5), hornMat);
        o.position.set(p.x, p.y + Math.cos(a) * hh[i] * 0.98, Math.sin(a) * hw[i] * 0.98);
        o.rotation.x = a;
        group.add(o);
      }
    }
  }
  if (plan.club) {
    const cl = new THREE.Mesh(new THREE.SphereGeometry(1, 14, 10), hornMat);
    cl.scale.set(L * 0.045, L * 0.028, L * 0.06);
    cl.position.copy(centres[1]);
    cl.castShadow = true;
    group.add(cl);
  }

  // Limbs: two-bone IK from the hip / shoulder joints to the simulated foot positions.
  const fwd = new THREE.Vector3(1, 0, 0);
  const feet = c.feet.map((f) => ({ p: toLocalFoot(f), contact: f[3] === 1, fore: f[4] === 1 }));
  const hipI = nearestStation(hipX);
  const shX = hipX + shoulderRel * Math.cos(pitch);
  const shI = nearestStation(shX);
  const muscle = (from, to, rFwd, rSide, t, lift) => {
    const dir = to.clone().sub(from);
    const len = dir.length();
    dir.normalize();
    const m = new THREE.Mesh(colorGeometry(new THREE.SphereGeometry(1, 16, 12), limbColor), muscleMat);
    m.scale.set(rFwd, len * 0.62, rSide);
    m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir);
    m.position.copy(from.clone().lerp(to, t)).add(new THREE.Vector3(0, lift, 0));
    m.castShadow = true; m.receiveShadow = true;
    group.add(m);
  };
  for (const f of feet) {
    const sideSign = f.p.z >= 0 ? 1 : -1;
    if (!f.fore) {
      const lat = Math.min(Math.abs(f.p.z) * 0.85 + 0.01 * L, hw[hipI] * 0.7);
      const hip = new THREE.Vector3(hipX, hipY - 0.04 * H, sideSign * lat);
      let foot = f.p.clone();
      if (lying || swimming) foot = new THREE.Vector3(hipX + L * 0.09, 0.03 * H, sideSign * (lat + 0.12 * H));
      const graviportal = plan.hind === 'graviportal', sprawl = plan.hind === 'sprawl' || plan.hind === 'mammal';
      const ornithopod = sp.plan === 'facultative_quadruped_hadrosaur' || sp.plan === 'biped_small_ornithischian' || sp.plan === 'biped_pachycephalosaur';
      const seg = graviportal ? [0.50, 0.40, 0.14] : (sprawl ? [0.45, 0.42, 0.18] : (ornithopod ? [0.47, 0.41, 0.19] : [0.41, 0.40, 0.29]));
      const mt = seg[2] * H;
      const ankle = foot.clone().add(new THREE.Vector3(-mt * 0.3, mt * 0.95, 0));
      const lFem = seg[0] * H, lTib = seg[1] * H;
      const pole = sprawl ? new THREE.Vector3(0.3, 0.6, sideSign) : fwd.clone().add(new THREE.Vector3(0, 0.25, sideSign * 0.12));
      const knee = solveJoint(hip, ankle, lFem, lTib, pole);
      const thighR = (graviportal ? 0.22 : (sprawl ? 0.16 : 0.19)) * H;
      const leg = limbTube([hip, knee, ankle, foot], [thighR * 0.8, thighR * 0.55, thighR * 0.34, thighR * 0.27], limbColor, 12);
      const m = new THREE.Mesh(leg, mat);
      m.castShadow = true; m.receiveShadow = true;
      group.add(m);
      // Thigh musculature (M. caudofemoralis / iliotibialis) merging into the flank.
      muscle(hip, knee, thighR * 0.95, thighR * 0.6, 0.36, thighR * 0.2);
      // Shank musculature.
      muscle(knee, ankle, thighR * 0.48, thighR * 0.4, 0.35, 0);
      // Toes.
      const toes = plan.hind === 'mammal' ? 5 : 3;
      for (let k = 0; k < toes; k++) {
        const ang = (k - (toes - 1) / 2) * 0.32;
        const tl = (graviportal ? 0.11 : 0.17) * H;
        const tip = foot.clone().add(new THREE.Vector3(Math.cos(ang) * tl, -0.01 * H, Math.sin(ang) * tl));
        const toe = new THREE.Mesh(limbTube([foot.clone().add(new THREE.Vector3(-0.02 * H, 0.03 * H, 0)), tip], [thighR * 0.16, thighR * 0.09], limbColor, 6), mat);
        toe.castShadow = true;
        group.add(toe);
      }
    } else {
      const lat = Math.min(Math.abs(f.p.z) * 0.9 + 0.01 * L, hw[shI] * 0.75);
      const sh = new THREE.Vector3(shX, centres[shI].y - hh[shI] * 0.35, sideSign * lat);
      let foot = f.p.clone();
      if (lying || swimming) foot = new THREE.Vector3(shX + 0.12 * H, 0.03 * H, sideSign * (lat + 0.08 * H));
      const total = Math.max(0.1, sh.distanceTo(foot));
      const manus = (plan.fore === 'hoof' ? 0.2 : 0.13) * total;
      const wrist = foot.clone().add(new THREE.Vector3(0, manus, 0));
      const k = plan.fore === 'wing' ? 0.62 : 0.5;
      const lHum = k * total, lRad = k * total * 0.95;
      const pole = plan.fore === 'sprawl' ? new THREE.Vector3(-0.2, 0.5, sideSign) : new THREE.Vector3(-1, 0.05, sideSign * 0.25);
      const elbow = solveJoint(sh, wrist, lHum, lRad, pole);
      const r1 = (plan.fore === 'graviportal' ? 0.15 : (plan.fore === 'hoof' ? 0.11 : 0.09)) * H;
      const arm = new THREE.Mesh(limbTube([sh, elbow, wrist, foot], [r1, r1 * 0.62, r1 * 0.42, r1 * 0.4], limbColor, 10), mat);
      arm.castShadow = true; arm.receiveShadow = true;
      group.add(arm);
      muscle(sh, elbow, r1 * 0.95, r1 * 0.7, 0.35, r1 * 0.15);
      muscle(elbow, wrist, r1 * 0.6, r1 * 0.5, 0.3, 0);
    }
  }
  // Biped forelimbs (held free of the ground).
  if (!plan.quad && plan.arms) {
    for (const sideSign of [-1, 1]) {
      const chest = centres[Math.round(centres.length * 0.55)];
      const sh = new THREE.Vector3(chest.x, chest.y - hh[Math.round(centres.length * 0.55)] * 0.3, sideSign * hw[Math.round(centres.length * 0.55)] * 0.7);
      const al = plan.arms * L;
      const el = sh.clone().add(new THREE.Vector3(-al * 0.2, -al * 0.45, sideSign * al * 0.1));
      const wr = el.clone().add(new THREE.Vector3(al * 0.4, -al * 0.15, 0));
      const arm = new THREE.Mesh(limbTube([sh, el, wr], [al * 0.12, al * 0.07, al * 0.05], limbColor, 7), mat);
      arm.castShadow = true;
      group.add(arm);
    }
  }
  return group;
}

function colorGeometry(g, fn) {
  const p = g.attributes.position;
  const col = new Float32Array(p.count * 3);
  const v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.set(p.getX(i), p.getY(i), p.getZ(i));
    const c = fn(v);
    col[i * 3] = c[0]; col[i * 3 + 1] = c[1]; col[i * 3 + 2] = c[2];
  }
  g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  return g;
}

export const SPECULATIVE_COLOUR_NOTE = 'Couleurs des animaux : inconnues (SPÉCULATIF) — contre-ombrage neutre.';
