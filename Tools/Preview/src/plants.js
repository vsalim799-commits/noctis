// Procedural plant geometry: Maastrichtian floodplain vegetation built from alpha-tested cards.
// Each builder returns { wood, leaves } BufferGeometries for a unit-height plant (scaled per instance).
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { makeRng } from './textures.js';

// Quad with its attachment edge at the origin, extending along +x (length) and z (width).
function card(len, width, uv = [0, 0, 1, 1]) {
  const g = new THREE.PlaneGeometry(len, width);
  g.rotateX(-Math.PI / 2);
  g.translate(len / 2, 0, 0);
  const a = g.attributes.uv;
  for (let i = 0; i < a.count; i++) a.setXY(i, uv[0] + a.getX(i) * (uv[2] - uv[0]), uv[1] + a.getY(i) * (uv[3] - uv[1]));
  return g;
}

// Replace normals by directions from a centre point (soft, volumetric foliage shading).
function sphericalNormals(geo, center, upBias = 0.5) {
  const p = geo.attributes.position, n = geo.attributes.normal;
  const v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.set(p.getX(i), p.getY(i), p.getZ(i)).sub(center);
    v.y += upBias * v.length();
    v.normalize();
    n.setXYZ(i, v.x, v.y, v.z);
  }
  return geo;
}

function strip(points, widths, normalsUp = true) {
  // Ribbon through 3D points, width along the local side vector (horizontal).
  const pos = [], uv = [], idx = [];
  const n = points.length;
  for (let i = 0; i < n; i++) {
    const p = points[i];
    const t = points[Math.min(n - 1, i + 1)].clone().sub(points[Math.max(0, i - 1)]).normalize();
    const side = new THREE.Vector3(0, 1, 0).cross(t).normalize();
    if (side.lengthSq() < 1e-6) side.set(1, 0, 0);
    const w = widths[i] / 2;
    pos.push(p.x - side.x * w, p.y - side.y * w, p.z - side.z * w, p.x + side.x * w, p.y + side.y * w, p.z + side.z * w);
    const v = i / (n - 1);
    uv.push(0, v, 1, v);
    if (i < n - 1) { const k = i * 2; idx.push(k, k + 1, k + 2, k + 1, k + 3, k + 2); }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  if (normalsUp) {
    const nn = g.attributes.normal;
    for (let i = 0; i < nn.count; i++) { const y = Math.abs(nn.getY(i)); nn.setXYZ(i, nn.getX(i) * 0.4, y + 0.6, nn.getZ(i) * 0.4); }
    g.normalizeNormals?.();
    const v = new THREE.Vector3();
    for (let i = 0; i < nn.count; i++) { v.set(nn.getX(i), nn.getY(i), nn.getZ(i)).normalize(); nn.setXYZ(i, v.x, v.y, v.z); }
  }
  return g;
}

function trunk(height, rBase, rTop, lean, segments = 7, seed = 1) {
  const r = makeRng(seed);
  const pts = [];
  const rings = 6;
  for (let i = 0; i <= rings; i++) {
    const t = i / rings;
    pts.push(new THREE.Vector3(lean.x * t * t + (r() - 0.5) * 0.01, t * height, lean.y * t * t + (r() - 0.5) * 0.01));
  }
  return tube(pts, pts.map((_, i) => rBase + (rTop - rBase) * Math.pow(i / rings, 0.8)), segments, height * 4);
}

// Generic tube along a polyline with per-point radius.
export function tube(points, radii, segments = 8, vRepeat = 1) {
  const pos = [], uv = [], idx = [];
  const n = points.length;
  let prevSide = null;
  for (let i = 0; i < n; i++) {
    const t = points[Math.min(n - 1, i + 1)].clone().sub(points[Math.max(0, i - 1)]).normalize();
    let side = prevSide ? prevSide.clone().sub(t.clone().multiplyScalar(prevSide.dot(t))).normalize() : new THREE.Vector3(1, 0, 0).cross(t);
    if (side.lengthSq() < 1e-6) side = new THREE.Vector3(0, 0, 1).cross(t);
    side.normalize();
    prevSide = side;
    const up = t.clone().cross(side).normalize();
    for (let k = 0; k <= segments; k++) {
      const a = (k / segments) * Math.PI * 2;
      const d = side.clone().multiplyScalar(Math.cos(a)).add(up.clone().multiplyScalar(Math.sin(a)));
      pos.push(points[i].x + d.x * radii[i], points[i].y + d.y * radii[i], points[i].z + d.z * radii[i]);
      uv.push(k / segments, (i / (n - 1)) * vRepeat);
    }
    if (i < n - 1) for (let k = 0; k < segments; k++) {
      const a = i * (segments + 1) + k, b = a + segments + 1;
      idx.push(a, b, a + 1, b, b + 1, a + 1);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

// Tall deciduous taxodiaceous conifer (Metasequoia / Glyptostrobus habit): straight bole,
// narrow conical crown of near-horizontal flat sprays in tiers.
export function coniferGeometry(seed) {
  const r = makeRng(seed * 7919 + 11);
  const wood = [trunk(1.0, 0.022, 0.003, new THREE.Vector2((r() - 0.5) * 0.02, (r() - 0.5) * 0.02), 7, seed)];
  const leaves = [];
  const whorls = 19;
  const crownBase = 0.22 + r() * 0.12;
  for (let w = 0; w < whorls; w++) {
    const t = w / (whorls - 1);
    const y = crownBase + (0.97 - crownBase) * t;
    const radius = 0.15 * Math.pow(1 - t, 0.8) + 0.03;
    const nb = t > 0.85 ? 4 : 5 + Math.round(r());
    for (let b = 0; b < nb; b++) {
      const az = (b / nb) * Math.PI * 2 + w * 2.4 + (r() - 0.5) * 0.6;
      const len = radius * (0.85 + r() * 0.35);
      for (let layer = 0; layer < 2; layer++) {
        const g = card(len, len * 0.8);
        g.rotateX(layer === 0 ? 0 : 0.9); // second layer twisted about the branch axis
        g.rotateZ(-0.12 - r() * 0.25 - (1 - t) * 0.15); // slight droop
        g.rotateY(az);
        g.translate(0, y, 0);
        leaves.push(g);
      }
      if (len > 0.08) {
        const bp = [new THREE.Vector3(0, y, 0), new THREE.Vector3(Math.cos(az) * len * 0.7, y - len * 0.06, -Math.sin(az) * len * 0.7)];
        wood.push(tube(bp, [0.003, 0.0012], 3));
      }
    }
  }
  // Leader at the apex.
  for (let k = 0; k < 2; k++) {
    const g = card(0.07, 0.05);
    g.rotateZ(Math.PI / 2 - 0.1);
    g.rotateY(k * Math.PI / 2);
    g.translate(0, 0.93, 0);
    leaves.push(g);
  }
  const lg = mergeGeometries(leaves);
  sphericalNormals(lg, new THREE.Vector3(0, 0.55, 0), 0.6);
  return { wood: mergeGeometries(wood), leaves: lg, crownRadius: 0.19, crownBase };
}

// Broadleaf angiosperm tree: short bole, spreading limbs, irregular rounded crown of leaf cards.
export function broadleafGeometry(seed) {
  const r = makeRng(seed * 104729 + 3);
  const wood = [trunk(0.55, 0.035, 0.018, new THREE.Vector2((r() - 0.5) * 0.06, (r() - 0.5) * 0.06), 7, seed)];
  const leaves = [];
  const limbs = 4 + Math.round(r() * 2);
  const centers = [];
  for (let l = 0; l < limbs; l++) {
    const az = (l / limbs) * Math.PI * 2 + (r() - 0.5) * 0.8;
    const out = 0.16 + r() * 0.14;
    const y0 = 0.38 + r() * 0.15;
    const end = new THREE.Vector3(Math.cos(az) * out, 0.62 + r() * 0.18, -Math.sin(az) * out);
    const mid = new THREE.Vector3(Math.cos(az) * out * 0.45, (y0 + end.y) / 2 + 0.02, -Math.sin(az) * out * 0.45);
    wood.push(tube([new THREE.Vector3(0, y0, 0), mid, end], [0.018, 0.011, 0.004], 5));
    centers.push(end);
  }
  centers.push(new THREE.Vector3(0, 0.8, 0));
  for (const c of centers) {
    const nCards = 16;
    const R = 0.15 + r() * 0.05;
    for (let k = 0; k < nCards; k++) {
      const u = r() * 2 - 1, th = r() * Math.PI * 2, rr = R * (0.55 + 0.45 * Math.cbrt(r()));
      const dir = new THREE.Vector3(Math.sqrt(1 - u * u) * Math.cos(th), u * 0.75, Math.sqrt(1 - u * u) * Math.sin(th));
      const size = 0.13 + r() * 0.06;
      const g = new THREE.PlaneGeometry(size, size);
      g.rotateX(-Math.PI / 2 + (r() - 0.5) * 1.6);
      g.rotateZ((r() - 0.5) * 1.6);
      g.rotateY(r() * Math.PI * 2);
      g.translate(c.x + dir.x * rr, c.y + dir.y * rr, c.z + dir.z * rr);
      leaves.push(g);
    }
  }
  const lg = mergeGeometries(leaves);
  sphericalNormals(lg, new THREE.Vector3(0, 0.7, 0), 0.5);
  return { wood: mergeGeometries(wood), leaves: lg, crownRadius: 0.36, crownBase: 0.5 };
}

// Fan palm (Sabalites type): slender stem, crown of costapalmate fans on long petioles.
export function palmGeometry(seed) {
  const r = makeRng(seed * 15485863 + 5);
  const stemTop = 0.68 + r() * 0.1;
  const lean = new THREE.Vector2((r() - 0.5) * 0.18, (r() - 0.5) * 0.18);
  const wood = [trunk(stemTop, 0.045, 0.035, lean, 7, seed)];
  const top = new THREE.Vector3(lean.x, stemTop, lean.y);
  const leaves = [];
  const fronds = 16;
  for (let f = 0; f < fronds; f++) {
    const az = (f / fronds) * Math.PI * 2 + (r() - 0.5) * 0.4;
    const elev = 0.75 - r() * 1.25; // radians above horizontal
    const plen = 0.12 + r() * 0.05;
    const dir = new THREE.Vector3(Math.cos(az) * Math.cos(elev), Math.sin(elev), -Math.sin(az) * Math.cos(elev));
    const pEnd = top.clone().add(dir.clone().multiplyScalar(plen));
    wood.push(tube([top, pEnd], [0.006, 0.004], 4));
    const size = 0.2 + r() * 0.05;
    const g = new THREE.PlaneGeometry(size, size);
    g.translate(0, size / 2, 0);              // petiole attachment at the bottom edge
    g.rotateX(-Math.PI / 2 + 0.6 + (r() - 0.5) * 0.5); // lay the fan out along the frond direction, folded / cupped
    g.rotateY(-Math.PI / 2);
    g.rotateZ(elev * 0.7 - 0.45);
    g.rotateY(az);
    g.translate(pEnd.x, pEnd.y, pEnd.z);
    leaves.push(g);
  }
  const lg = mergeGeometries(leaves);
  sphericalNormals(lg, top.clone().add(new THREE.Vector3(0, -0.05, 0)), 0.7);
  return { wood: mergeGeometries(wood), leaves: lg, crownRadius: 0.32, crownBase: stemTop - 0.12 };
}

// Ground fern clump: arching fronds (unit = 1 m frond length).
export function fernGeometry(seed) {
  const r = makeRng(seed * 7727 + 9);
  const fronds = 7 + Math.round(r() * 4);
  const parts = [];
  for (let f = 0; f < fronds; f++) {
    const az = (f / fronds) * Math.PI * 2 + (r() - 0.5) * 0.7;
    const len = 0.75 + r() * 0.35;
    const elev0 = 0.95 + r() * 0.35;
    const pts = [], widths = [];
    const segs = 5;
    let p = new THREE.Vector3(0, 0, 0);
    for (let s = 0; s <= segs; s++) {
      const t = s / segs;
      pts.push(p.clone());
      widths.push(0.36 * Math.sin(Math.PI * (0.08 + 0.92 * t)) + 0.04);
      const elev = elev0 - t * (1.7 + r() * 0.3);
      p = p.clone().add(new THREE.Vector3(Math.cos(az) * Math.cos(elev), Math.sin(elev), -Math.sin(az) * Math.cos(elev)).multiplyScalar(len / segs));
    }
    parts.push(strip(pts, widths, true));
  }
  return mergeGeometries(parts);
}

// Horsetail stand: crossed cards (unit = 1 m tall).
export function horsetailGeometry(seed) {
  const r = makeRng(seed * 31 + 1);
  const parts = [];
  for (let k = 0; k < 3; k++) {
    const g = new THREE.PlaneGeometry(0.45, 1.0);
    g.translate(0, 0.5, 0);
    g.rotateY(k * Math.PI / 3 + r() * 0.3);
    parts.push(g);
  }
  const m = mergeGeometries(parts);
  const n = m.attributes.normal;
  for (let i = 0; i < n.count; i++) n.setXYZ(i, 0, 1, 0);
  return m;
}

// Low angiosperm shrub / understory clump (unit radius 1 m).
export function shrubGeometry(seed) {
  const r = makeRng(seed * 977 + 13);
  const parts = [];
  for (let k = 0; k < 22; k++) {
    const th = r() * Math.PI * 2, u = r();
    const rr = 0.25 + Math.sqrt(u) * 0.7;
    const size = 0.55 + r() * 0.35;
    const g = new THREE.PlaneGeometry(size, size);
    g.rotateX(-Math.PI / 2 + (r() - 0.5) * 1.8);
    g.rotateY(r() * 6.28);
    g.translate(Math.cos(th) * rr, 0.25 + (1 - rr) * 0.6 + r() * 0.2, Math.sin(th) * rr);
    parts.push(g);
  }
  const m = mergeGeometries(parts);
  sphericalNormals(m, new THREE.Vector3(0, 0.1, 0), 0.6);
  return m;
}

// Low-detail stand-ins for distant trees.
export function farConiferGeometry() {
  const g = new THREE.ConeGeometry(0.17, 0.78, 6);
  g.translate(0, 0.22 + 0.39, 0);
  return g;
}
export function farBroadleafGeometry() {
  const g = new THREE.IcosahedronGeometry(0.34, 0);
  g.scale(1, 0.7, 1);
  g.translate(0, 0.68, 0);
  return g;
}

// Foliage material: alpha-tested, double-sided, without the back-face normal flip (so the
// custom volumetric normals light both sides of a card consistently).
export function foliageMaterial(map, { color = 0xffffff, roughness = 0.82, translucency = 0.18 } = {}) {
  const m = new THREE.MeshStandardMaterial({ map, color, roughness, metalness: 0, alphaTest: 0.42, side: THREE.DoubleSide });
  m.onBeforeCompile = (shader) => {
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <normal_fragment_begin>', THREE.ShaderChunk.normal_fragment_begin.replace('normal *= faceDirection;', ''))
      .replace('#include <map_fragment>', `#include <map_fragment>
#ifdef USE_MAP
  // Keep alpha-tested foliage from thinning out in the smaller mip levels.
  vec2 mdx = dFdx(vMapUv * 256.0), mdy = dFdy(vMapUv * 256.0);
  float mlod = 0.5 * log2(max(max(dot(mdx, mdx), dot(mdy, mdy)), 1e-6));
  diffuseColor.a *= 1.0 + max(0.0, mlod) * 0.35;
#endif`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>\n totalEmissiveRadiance += diffuseColor.rgb * ${translucency.toFixed(3)};`);
  };
  return m;
}
