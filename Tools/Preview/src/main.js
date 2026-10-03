// Noctis — WebGL preview of the simulated valley.
// Everything placed here comes from the simulation export (terrain, water depth, vegetation
// biomass per plant functional type, animal positions / headings / postures / foot contacts,
// footprints, research vehicle). Geometry and textures are procedural stand-ins for the Unreal
// Engine 5 art; no element is moved or added to "improve" a shot.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { Sky } from 'three/addons/objects/Sky.js';
import * as TX from './textures.js';
import * as PL from './plants.js';
import { buildAnimal, SPECULATIVE_COLOUR_NOTE } from './animals.js';

const params = new URLSearchParams(location.search);
const still = params.get('still') === '1';
const viewName = params.get('view') || 'herd';
const base = params.get('data') || '../../Output/preview/preview';
const M = await (await fetch(base + '.json')).json();
const BIN = await (await fetch(base + '.bin')).arrayBuffer();
function field(name) {
  const f = M.fields[name];
  if (f.type === 'f32') return new Float32Array(BIN, f.offset, f.count);
  const u = new Uint8Array(BIN, f.offset, f.count);
  return f.type === 'u8raw' ? u : Float32Array.from(u, (v) => v / 255);
}
const F = {};
for (const k of Object.keys(M.fields)) F[k] = field(k);

const [wx0, wy0, wx1, wy1] = M.window;
const N = M.n, W = wx1 - wx0, STEP = W / (N - 1);
const cx = (wx0 + wx1) / 2, cy = (wy0 + wy1) / 2;
const zRef = 20.0;
// World (x east, y north, z up) -> three (x east, y up, z south), origin at the window centre.
const toV = (x, y, z) => new THREE.Vector3(x - cx, z - zRef, -(y - cy));
const fromV = (v) => [v.x + cx, -v.z + cy];

function bilinear(arr, n, x0, y0, step, x, y) {
  const fx = (x - x0) / step, fy = (y - y0) / step;
  const i = Math.max(0, Math.min(n - 2, Math.floor(fx))), j = Math.max(0, Math.min(n - 2, Math.floor(fy)));
  const tx = Math.min(1, Math.max(0, fx - i)), ty = Math.min(1, Math.max(0, fy - j));
  const a = arr[j * n + i], b = arr[j * n + i + 1], c = arr[(j + 1) * n + i], d = arr[(j + 1) * n + i + 1];
  return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}
const [gx0, gy0, gx1, gy1] = M.world;
const FN = M.farN, FSTEP = (gx1 - gx0) / (FN - 1);
const inWindow = (x, y, m = 0) => x > wx0 + m && x < wx1 - m && y > wy0 + m && y < wy1 - m;
function heightAt(x, y) {
  if (inWindow(x, y)) return bilinear(F.heights, N, wx0, wy0, STEP, x, y);
  return bilinear(F.farHeights, FN, gx0, gy0, FSTEP, x, y);
}
const sample = (arr, x, y) => bilinear(arr, N, wx0, wy0, STEP, x, y);
const waterAt = (x, y) => (inWindow(x, y) ? sample(F.water, x, y) : bilinear(F.farWater, FN, gx0, gy0, FSTEP, x, y));
const groundV = (x, y, dz = 0) => toV(x, y, heightAt(x, y) + dz);
const rng = TX.makeRng(20250);

// ------------------------------------------------------------------ Camera choice (needs only data)
// Candidate cameras are scored for how many animals they see without occlusion; trees are tested
// once placed (see below), so the camera is chosen after vegetation is generated.
const herdPlan = 'facultative_quadruped_hadrosaur';
const species = M.species;
const creatures = M.creatures.filter((c) => species[c.s] && species[c.s].plan);
const herd = creatures.filter((c) => species[c.s].plan === herdPlan);
const focusW = M.focus;

// ------------------------------------------------------------------ Renderer / scene
const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(1);
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.9;
document.body.appendChild(renderer.domElement);
const scene = new THREE.Scene();

// Sun from the simulated sky state.
const sunDir = new THREE.Vector3(M.sun[0], M.sun[2], -M.sun[1]).normalize();
const sunElev = Math.asin(Math.max(-1, Math.min(1, M.sun[2])));
const sky = new Sky();
sky.scale.setScalar(40000);
const su = sky.material.uniforms;
su.turbidity.value = 6.5;
su.rayleigh.value = 1.4;
su.mieCoefficient.value = 0.0055;
su.mieDirectionalG.value = 0.82;
su.sunPosition.value.copy(sunDir);
scene.add(sky);
// Image-based lighting: a sky/horizon/ground gradient (the sun itself is the directional light,
// so it is kept out of the environment map to avoid lighting shaded surfaces twice).
const pmrem = new THREE.PMREMGenerator(renderer);
const envScene = new THREE.Scene();
{
  const zen = new THREE.Color().setRGB(0.20, 0.38, 0.75), hor = new THREE.Color().setRGB(0.62, 0.70, 0.80), gnd = new THREE.Color().setRGB(0.11, 0.10, 0.07);
  const m = new THREE.ShaderMaterial({ side: THREE.BackSide, uniforms: { zen: { value: zen }, hor: { value: hor }, gnd: { value: gnd } },
    vertexShader: 'varying vec3 vD; void main(){ vD = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
    fragmentShader: 'uniform vec3 zen; uniform vec3 hor; uniform vec3 gnd; varying vec3 vD; void main(){ float y = vD.y; vec3 c = y > 0.0 ? mix(hor, zen, pow(y, 0.6)) : mix(hor * 0.5, gnd, pow(-y, 0.4)); gl_FragColor = vec4(c, 1.0); }' });
  envScene.add(new THREE.Mesh(new THREE.SphereGeometry(100, 32, 16), m));
}
scene.environment = pmrem.fromScene(envScene, 0.02).texture;
scene.environmentIntensity = 1.0;

const warm = Math.max(0, Math.min(1, (0.6 - sunElev) / 0.6));
const sunColor = new THREE.Color().setRGB(1.0, 0.93 - 0.12 * warm, 0.82 - 0.25 * warm);
const sun = new THREE.DirectionalLight(sunColor, 7.5 * Math.max(0.15, Math.min(1, M.lux / 60000)) + 1.5);
scene.add(sun);
scene.add(sun.target);
const hemi = new THREE.HemisphereLight(0xbcd2ee, 0x4b4430, 0.15);
scene.add(hemi);
// Aerial perspective: humid floodplain morning haze. Fog density from the weather state (+ a floor).
const hazeColor = new THREE.Color().setRGB(0.62, 0.70, 0.80).lerp(new THREE.Color(1.0, 0.86, 0.7), 0.15 * warm);
scene.fog = new THREE.FogExp2(hazeColor, Math.max(0.00020, Math.min(0.004, (M.fog || 0) * 0.5 + 0.00020)));

// ------------------------------------------------------------------ Textures
const tex = {
  fern: TX.fernFrondTexture(11),
  fern2: TX.fernFrondTexture(12, { hue: 88, sat: 44, light: 27 }),
  leaf: TX.leafClusterTexture(21),
  leafDark: TX.leafClusterTexture(22, { hue: 100, sat: 38, light: 22 }),
  spray: TX.coniferSprayTexture(31),
  palm: TX.palmFanTexture(41),
  horsetail: TX.horsetailTexture(51),
  ground: TX.groundDetailTexture(61),
  water: TX.waterNormalTexture(71),
  skin: TX.scaleNormalTexture(81),
  cloud: TX.cloudTexture(91),
  bark: TX.barkTexture(101),
};
const lin = (c) => Math.pow(c, 2.2);

// ------------------------------------------------------------------ Terrain (detail window)
const habitatColor = [
  [0.30, 0.28, 0.22], [0.47, 0.42, 0.33], [0.62, 0.57, 0.46], [0.27, 0.27, 0.20], [0.25, 0.25, 0.17], [0.30, 0.25, 0.17], [0.42, 0.37, 0.27],
  [0.37, 0.31, 0.21], [0.36, 0.27, 0.18], [0.55, 0.46, 0.34], [0.78, 0.66, 0.48], [0.70, 0.60, 0.46], [0.36, 0.34, 0.27], [0.55, 0.50, 0.36]];
function groundColor(k, jitter) {
  const hab = F.habitat[k];
  const c = habitatColor[hab] || [0.4, 0.36, 0.28];
  let r = c[0], g = c[1], b = c[2];
  const cover = Math.min(1, F.fern[k] * 0.95 + F.horsetail[k] * 0.6 + F.shrub[k] * 0.5) * 0.7;
  r = r * (1 - cover) + 0.21 * cover; g = g * (1 - cover) + 0.29 * cover; b = b * (1 - cover) + 0.11 * cover;
  const shade = 1 - 0.12 * F.canopy[k];
  const wet = 1 - 0.32 * Math.max(0, F.moisture[k] - 0.45) / 0.55;
  r *= shade * wet; g *= shade * wet; b *= shade * wet;
  const tr = Math.min(1, F.trail[k] * 1.8);
  r = r * (1 - tr) + 0.46 * tr; g = g * (1 - tr) + 0.40 * tr; b = b * (1 - tr) + 0.31 * tr;
  if (F.water[k] > 0.02) { r *= 0.7; g *= 0.72; b *= 0.7; }
  const j = 1 + jitter;
  return [lin(r * j), lin(g * j), lin(b * j)];
}
function valueNoise2(x, y, seed) {
  const h = (i, j) => { const s = Math.sin(i * 127.1 + j * 311.7 + seed * 74.7) * 43758.5453; return s - Math.floor(s); };
  const i = Math.floor(x), j = Math.floor(y), fx = x - i, fy = y - j;
  const sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  return (h(i, j) * (1 - sx) + h(i + 1, j) * sx) * (1 - sy) + (h(i, j + 1) * (1 - sx) + h(i + 1, j + 1) * sx) * sy;
}
{
  const geo = new THREE.PlaneGeometry(W, W, N - 1, N - 1);
  geo.rotateX(-Math.PI / 2);
  const pos = geo.attributes.position;
  const colors = new Float32Array(pos.count * 3);
  for (let k = 0; k < pos.count; k++) {
    const vx = pos.getX(k), vz = pos.getZ(k);
    const x = vx + cx, y = -vz + cy;
    const i = Math.round((x - wx0) / STEP), j = Math.round((y - wy0) / STEP);
    const idx = Math.max(0, Math.min(N - 1, j)) * N + Math.max(0, Math.min(N - 1, i));
    pos.setY(k, F.heights[idx] - zRef);
    const jit = (valueNoise2(x / 23, y / 23, 1) - 0.5) * 0.22 + (valueNoise2(x / 6, y / 6, 2) - 0.5) * 0.12;
    const c = groundColor(idx, jit);
    colors[k * 3] = c[0] * 1.9; colors[k * 3 + 1] = c[1] * 1.9; colors[k * 3 + 2] = c[2] * 1.9;
  }
  geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geo.computeVertexNormals();
  const uv = geo.attributes.uv;
  for (let k = 0; k < uv.count; k++) uv.setXY(k, uv.getX(k) * W / 7, uv.getY(k) * W / 7);
  const mat = new THREE.MeshStandardMaterial({ vertexColors: true, map: tex.ground, roughness: 0.96, metalness: 0 });
  const terrain = new THREE.Mesh(geo, mat);
  terrain.receiveShadow = true;
  scene.add(terrain);
}

// ------------------------------------------------------------------ Far terrain (whole valley, horizon)
{
  const SZ = gx1 - gx0;
  const geo = new THREE.PlaneGeometry(SZ, SZ, FN - 1, FN - 1);
  geo.rotateX(-Math.PI / 2);
  geo.translate(gx0 + SZ / 2 - cx, 0, -(gy0 + SZ / 2 - cy));
  const pos = geo.attributes.position;
  const colors = new Float32Array(pos.count * 3);
  for (let k = 0; k < pos.count; k++) {
    const x = pos.getX(k) + cx, y = -pos.getZ(k) + cy;
    const i = Math.round((x - gx0) / FSTEP), j = Math.round((y - gy0) / FSTEP);
    const idx = Math.max(0, Math.min(FN - 1, j)) * FN + Math.max(0, Math.min(FN - 1, i));
    let h = F.farHeights[idx] - zRef;
    if (inWindow(x, y, FSTEP * 1.2)) h -= 6; // hidden under the detailed terrain
    pos.setY(k, h);
    const cov = F.farCanopy[idx];
    const wet = F.farWater[idx] > 0.05 ? 0.7 : 1.0;
    const j2 = 1 + (valueNoise2(x / 90, y / 90, 3) - 0.5) * 0.25;
    const r = (0.40 * (1 - cov) + 0.16 * cov) * wet * j2, g = (0.36 * (1 - cov) + 0.24 * cov) * wet * j2, b = (0.25 * (1 - cov) + 0.12 * cov) * wet * j2;
    colors[k * 3] = lin(r); colors[k * 3 + 1] = lin(g); colors[k * 3 + 2] = lin(b);
  }
  geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geo.computeVertexNormals();
  const far = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 1 }));
  far.receiveShadow = true;
  scene.add(far);
}

// ------------------------------------------------------------------ Water
const waterMat = new THREE.MeshStandardMaterial({ color: new THREE.Color().setRGB(lin(0.20), lin(0.24), lin(0.19)), roughness: 0.06, metalness: 0.0,
  normalMap: tex.water, normalScale: new THREE.Vector2(0.18, 0.18), transparent: true, opacity: 0.93, envMapIntensity: 1.6 });
tex.water.repeat.set(1, 1);
function waterMesh(depthArr, heightArr, n, x0, y0, step, skipWindow) {
  const surf = new Float32Array(n * n);
  const wet = new Uint8Array(n * n);
  for (let k = 0; k < n * n; k++) { wet[k] = depthArr[k] > 0.02 ? 1 : 0; surf[k] = heightArr[k] + depthArr[k]; }
  // Extend the water level flat under dry banks next to wet cells (the bank then clips it naturally).
  const level = surf.slice();
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
    const k = j * n + i;
    if (wet[k]) continue;
    let best = -1e9;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
      const ii = i + di, jj = j + dj;
      if (ii < 0 || jj < 0 || ii >= n || jj >= n) continue;
      const kk = jj * n + ii;
      if (wet[kk]) best = Math.max(best, surf[kk]);
    }
    if (best > -1e8) level[k] = best;
  }
  const pos = [], uv = [], idx = [];
  const map = new Int32Array(n * n).fill(-1);
  const need = (k) => {
    if (map[k] >= 0) return map[k];
    const i = k % n, j = Math.floor(k / n);
    const x = x0 + i * step, y = y0 + j * step;
    const v = toV(x, y, level[k] + 0.02);
    map[k] = pos.length / 3;
    pos.push(v.x, v.y, v.z);
    uv.push(x / 18, y / 18);
    return map[k];
  };
  for (let j = 0; j < n - 1; j++) for (let i = 0; i < n - 1; i++) {
    const a = j * n + i, b = a + 1, c = a + n, d = c + 1;
    if (!(wet[a] || wet[b] || wet[c] || wet[d])) continue;
    if (skipWindow && inWindow(x0 + i * step, y0 + j * step, -step)) continue;
    idx.push(need(a), need(b), need(c), need(b), need(d), need(c));
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  const m = new THREE.Mesh(g, waterMat);
  m.receiveShadow = true;
  return m;
}
scene.add(waterMesh(F.water, F.heights, N, wx0, wy0, STEP, false));
scene.add(waterMesh(F.farWater, F.farHeights, FN, gx0, gy0, FSTEP, true));

// ------------------------------------------------------------------ Vegetation
// Tree counts follow canopy cover / crown area; species mix follows the per-PFT biomass.
const trees = []; // {x, y, z, type, h, r, base} for occlusion tests
const CROWN = { conifer: 0.19, broadleaf: 0.36, palm: 0.32 };
for (let j = 0; j < N - 1; j++) for (let i = 0; i < N - 1; i++) {
  const k = j * N + i;
  if (F.water[k] > 0.03) continue;
  const can = F.canopy[k];
  if (can < 0.02) continue;
  const dc = F.conifer[k], da = F.angio[k], dp = F.palm[k];
  const tot = dc + da + dp;
  if (tot < 0.01) continue;
  const cellA = STEP * STEP;
  const types = [['conifer', dc, 18, 34], ['broadleaf', da, 10, 24], ['palm', dp, 3.5, 7.5]];
  for (const [type, d, hmin, hmax] of types) {
    if (d <= 0) continue;
    const h0 = hmin + (hmax - hmin) * (0.35 + 0.65 * Math.min(1, d * 1.3));
    const radius = CROWN[type] * h0;
    const share = can * d / tot;
    const expected = share * cellA / (Math.PI * radius * radius) * 1.15;
    let count = Math.floor(expected + rng());
    while (count-- > 0) {
      const x = wx0 + (i + rng()) * STEP, y = wy0 + (j + rng()) * STEP;
      const h = h0 * (0.7 + rng() * 0.5);
      trees.push({ x, y, z: heightAt(x, y), type, h, r: CROWN[type] * h, base: type === 'conifer' ? 0.28 * h : (type === 'palm' ? 0.6 * h : 0.45 * h), v: Math.floor(rng() * 4) });
    }
  }
}

// Spatial grid for occlusion queries.
const GRID = 32;
const tgrid = new Map();
for (const t of trees) {
  const key = Math.floor(t.x / GRID) + ',' + Math.floor(t.y / GRID);
  if (!tgrid.has(key)) tgrid.set(key, []);
  tgrid.get(key).push(t);
}
function occluded(a, b, ignoreRadius = 0) {
  // a, b: world [x, y, z]. Samples the segment against tree crowns/trunks and terrain.
  const d = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const steps = Math.max(4, Math.ceil(d / 2));
  let hits = 0;
  for (let s = 1; s < steps; s++) {
    const t = s / steps;
    const x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
    if (Math.hypot(x - b[0], y - b[1]) < ignoreRadius) continue;
    if (z < heightAt(x, y) + 0.2) return 99;
    const key = Math.floor(x / GRID) + ',' + Math.floor(y / GRID);
    for (const di of [-1, 0, 1]) for (const dj of [-1, 0, 1]) {
      const list = tgrid.get((Math.floor(x / GRID) + di) + ',' + (Math.floor(y / GRID) + dj));
      if (!list) continue;
      for (const tr of list) {
        const dd = Math.hypot(tr.x - x, tr.y - y);
        const zt = z - tr.z;
        if (zt > tr.h) continue;
        if (zt > tr.base && dd < tr.r * 0.75) hits++;
        else if (dd < 0.6) hits += 2;
      }
    }
    void key;
  }
  return hits;
}

// ------------------------------------------------------------------ Camera selection
function chooseCamera(name) {
  const fw = new THREE.Vector3();
  const target = (list) => {
    let sx = 0, sy = 0, sz = 0;
    list.forEach((c) => { sx += c.x; sy += c.y; sz += c.z + c.hip * 0.8; });
    return [sx / list.length, sy / list.length, sz / list.length];
  };
  const scoreView = (cam, tgt, subjects, fovDeg) => {
    // Visible subjects within the horizontal field of view and not occluded.
    const dirx = tgt[0] - cam[0], diry = tgt[1] - cam[1];
    const dl = Math.hypot(dirx, diry);
    let s = 0;
    for (const c of subjects) {
      const vx = c.x - cam[0], vy = c.y - cam[1];
      const dist = Math.hypot(vx, vy);
      if (dist < 4 || dist > 260) continue;
      const cosA = (vx * dirx + vy * diry) / (dist * dl);
      if (cosA < Math.cos((fovDeg / 2) * Math.PI / 180)) continue;
      const occ = occluded(cam, [c.x, c.y, c.z + c.hip * 0.9], c.len * 0.4);
      if (occ === 0) s += 1 + Math.min(1, c.len / 8) * 0.5 + Math.max(0, 1 - dist / 120) * 0.8;
    }
    return s;
  };
  const near = (p, list, n) => list.slice().sort((a, b) => Math.hypot(a.x - p[0], a.y - p[1]) - Math.hypot(b.x - p[0], b.y - p[1])).slice(0, n);
  const okStand = (x, y) => waterAt(x, y) < 0.05 && !trees.some((t) => Math.hypot(t.x - x, t.y - y) < 1.5);
  void fw;
  if (name === 'aerial') {
    const f = focusW;
    const r = (M.rivers[0] || []).reduce((best, p) => { const d = Math.hypot(p[0] - f[0], p[1] - f[1]); return d < best.d ? { d, p } : best; }, { d: 1e9, p: f });
    const dx = r.p[0] - f[0], dy = r.p[1] - f[1], dl = Math.hypot(dx, dy) || 1;
    const ux = dx / dl, uy = dy / dl;
    const camW = [f[0] - ux * 520 - uy * 160, f[1] - uy * 520 + ux * 160];
    const tgt = [f[0] + ux * 260, f[1] + uy * 260];
    return { pos: groundV(camW[0], camW[1], 230), look: groundV(tgt[0], tgt[1], 0), fov: 50, label: 'Vue drone (230 m)' };
  }
  if (name === 'drone') {
    const f = focusW;
    const r = (M.rivers[0] || []).reduce((best, p) => { const d = Math.hypot(p[0] - f[0], p[1] - f[1]); return d < best.d ? { d, p } : best; }, { d: 1e9, p: f });
    const dx = r.p[0] - f[0], dy = r.p[1] - f[1], dl = Math.hypot(dx, dy) || 1;
    const ux = dx / dl, uy = dy / dl;
    let best = null;
    for (let a = -1.2; a <= 1.2; a += 0.3) {
      const ca = Math.cos(a), sa = Math.sin(a);
      const vx = ux * ca - uy * sa, vy = ux * sa + uy * ca;
      const camW = [f[0] - vx * 170, f[1] - vy * 170];
      const cam = [camW[0], camW[1], heightAt(camW[0], camW[1]) + 60];
      const tgt = target(herd.length ? herd : creatures);
      const sc = scoreView(cam, tgt, creatures, 55);
      if (!best || sc > best.sc) best = { sc, cam, tgt };
    }
    return { pos: toV(...best.cam), look: toV(best.tgt[0], best.tgt[1], best.tgt[2] - 2), fov: 48, label: 'Vue drone (60 m)' };
  }
  if (name === 'closeup' || name === 'closeup2') {
    // Side view of one herd member (debug of body shapes).
    const t = target(herd.length ? herd : creatures);
    const pick = near(t, herd.length ? herd : creatures, name === 'closeup' ? 1 : 3).pop();
    const side = pick.h + Math.PI / 2 + (name === 'closeup2' ? 0.6 : 0);
    const d = pick.len * 1.6;
    const camXY = [pick.x + Math.cos(side) * d, pick.y + Math.sin(side) * d];
    return { pos: groundV(camXY[0], camXY[1], 1.65), look: toV(pick.x, pick.y, pick.z + pick.hip * 0.8), fov: 45, label: 'Gros plan (débogage des silhouettes)' };
  }
  // Ground views: researcher eye height (1.65 m) or the rover roof (3.4 m).
  let subjects = herd.length ? herd : creatures;
  let dmin = 45, dmax = 95, eye = 3.4, fov = 40, label = 'Point de vue à 3,4 m (hauteur du toit du labo mobile)';
  if (name === 'low') {
    const others = creatures.filter((c) => species[c.s].plan !== herdPlan && c.len > 2);
    if (others.length) {
      // The largest non-herd group present (by count of nearby same-species individuals).
      const bySp = {};
      others.forEach((c) => { (bySp[c.s] = bySp[c.s] || []).push(c); });
      const pick = Object.values(bySp).sort((a, b) => b.length * b[0].len - a.length * a[0].len)[0];
      subjects = pick;
    }
    dmin = 16; dmax = 40; eye = 1.65; fov = 42; label = 'Au sol (1,65 m)';
  }
  if (name === 'river') {
    // Bank of the reach nearest to the herd, looking along/over the channel.
    const f = focusW;
    let best = null;
    for (const river of M.rivers) for (let k = 1; k < river.length - 1; k++) {
      const p = river[k];
      if (!inWindow(p[0], p[1], 60)) continue;
      const tx = river[k + 1][0] - river[k - 1][0], ty = river[k + 1][1] - river[k - 1][1], tl = Math.hypot(tx, ty) || 1;
      for (const side of [-1, 1]) {
        const off = p[2] * 0.5 + 6;
        const camW = [p[0] - ty / tl * off * side, p[1] + tx / tl * off * side];
        if (!okStand(camW[0], camW[1])) continue;
        const cam = [camW[0], camW[1], heightAt(camW[0], camW[1]) + 1.65];
        for (const dir of [-1, 1]) {
          const lookW = [camW[0] + tx / tl * dir * 220 + (f[0] - camW[0]) * 0.15, camW[1] + ty / tl * dir * 220 + (f[1] - camW[1]) * 0.15];
          const tgt = [lookW[0], lookW[1], heightAt(lookW[0], lookW[1]) + 3];
          let water = 0;
          for (let s = 1; s <= 10; s++) { const t = s / 10; if (waterAt(camW[0] + (lookW[0] - camW[0]) * t, camW[1] + (lookW[1] - camW[1]) * t) > 0.1) water++; }
          const sunBehind = sunDir.x * (tgt[0] - cam[0]) - sunDir.z * (tgt[1] - cam[1]) < 0 ? 1.5 : 0;
          const sc = water * 0.6 + sunBehind + scoreView(cam, tgt, creatures, 60) * 1.5 - occluded(cam, tgt) * 0.02 - Math.hypot(p[0] - f[0], p[1] - f[1]) / 400;
          if (!best || sc > best.sc) best = { sc, cam, tgt };
        }
      }
    }
    if (best) return { pos: toV(...best.cam), look: toV(...best.tgt), fov: 55, label: 'Berge de la rivière (1,65 m)' };
  }
  const tgtAll = target(subjects);
  let best = null;
  for (let a = 0; a < 36; a++) {
    for (const d of [dmin, (dmin + dmax) / 2, dmax]) {
      const ang = a / 36 * Math.PI * 2;
      const camW = [tgtAll[0] + Math.cos(ang) * d, tgtAll[1] + Math.sin(ang) * d];
      if (!inWindow(camW[0], camW[1], 20) || !okStand(camW[0], camW[1])) continue;
      const cam = [camW[0], camW[1], heightAt(camW[0], camW[1]) + eye];
      const nearest = near(camW, subjects, 8);
      const tgt = target(nearest);
      const sc = scoreView(cam, tgt, subjects, fov * (innerWidth / innerHeight)) + scoreView(cam, tgt, creatures, fov * 1.6) * 0.3
        + (sunDir.x * (tgt[0] - cam[0]) - sunDir.z * (tgt[1] - cam[1]) < 0 ? 0.6 : 0); // prefer the sun behind or beside the observer
      if (!best || sc > best.sc) best = { sc, cam, tgt };
    }
  }
  if (!best) best = { cam: [tgtAll[0] - 60, tgtAll[1] - 60, heightAt(tgtAll[0] - 60, tgtAll[1] - 60) + eye], tgt: tgtAll };
  return { pos: toV(...best.cam), look: toV(best.tgt[0], best.tgt[1], best.tgt[2] - 0.5), fov, label };
}
const view = chooseCamera(viewName);
const camera = new THREE.PerspectiveCamera(view.fov, innerWidth / innerHeight, 0.3, 60000);
camera.position.copy(view.pos);
camera.lookAt(view.look);
const camW = fromV(camera.position);

// Shadow frustum centred on what the camera looks at.
{
  const focusPt = view.look.clone();
  const span = viewName === 'aerial' ? 1100 : (viewName === 'drone' ? 450 : 230);
  sun.position.copy(focusPt).add(sunDir.clone().multiplyScalar(1500));
  sun.target.position.copy(focusPt);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  const sc = sun.shadow.camera;
  sc.left = -span; sc.right = span; sc.top = span; sc.bottom = -span; sc.near = 100; sc.far = 3500;
  sun.shadow.bias = -0.0002;
  sun.shadow.normalBias = 0.6;
  sc.updateProjectionMatrix();
}

// ------------------------------------------------------------------ Vegetation meshes (LOD by distance to the camera)
const dummy = new THREE.Object3D();
const barkMat = new THREE.MeshStandardMaterial({ map: tex.bark, color: 0x9a8a78, roughness: 0.95 });
const mats = {
  conifer: PL.foliageMaterial(tex.spray, { color: 0xe8f0dc }),
  broadleaf: PL.foliageMaterial(tex.leaf),
  broadleafDark: PL.foliageMaterial(tex.leafDark),
  palm: PL.foliageMaterial(tex.palm, { color: 0xd8e0c8 }),
  fern: PL.foliageMaterial(tex.fern, { translucency: 0.22 }),
  fern2: PL.foliageMaterial(tex.fern2, { translucency: 0.22 }),
  horsetail: PL.foliageMaterial(tex.horsetail, { translucency: 0.1 }),
  shrub: PL.foliageMaterial(tex.leafDark, { translucency: 0.15 }),
  farConifer: new THREE.MeshStandardMaterial({ color: new THREE.Color().setRGB(lin(0.17), lin(0.25), lin(0.14)), roughness: 1, flatShading: true }),
  farBroad: new THREE.MeshStandardMaterial({ color: new THREE.Color().setRGB(lin(0.22), lin(0.30), lin(0.15)), roughness: 1, flatShading: true }),
};
const variants = {
  conifer: [0, 1, 2, 3].map((s) => PL.coniferGeometry(s + 1)),
  broadleaf: [0, 1, 2, 3].map((s) => PL.broadleafGeometry(s + 1)),
  palm: [0, 1, 2, 3].map((s) => PL.palmGeometry(s + 1)),
};
const farGeo = { conifer: PL.farConiferGeometry(), broadleaf: PL.farBroadleafGeometry(), palm: PL.farBroadleafGeometry() };
const NEAR_TREES = viewName === 'aerial' ? 1600 : 900;
const buckets = {};
const farBuckets = { conifer: [], broadleaf: [], palm: [] };
for (const t of trees) {
  const d = Math.hypot(t.x - camW[0], t.y - camW[1]);
  if (d < NEAR_TREES) {
    const key = t.type + t.v;
    (buckets[key] = buckets[key] || []).push(t);
  } else farBuckets[t.type].push(t);
}
const setT = (t, extraRot = 0) => {
  dummy.position.copy(toV(t.x, t.y, t.z - 0.15));
  dummy.rotation.set(0, (t.x * 13.1 + t.y * 7.3) % 6.283 + extraRot, 0);
  dummy.scale.setScalar(t.h);
  dummy.updateMatrix();
};
for (const [key, list] of Object.entries(buckets)) {
  const type = key.replace(/\d+$/, ''), v = +key.slice(type.length);
  const geo = variants[type][v];
  const leafMat = type === 'broadleaf' ? (v % 2 ? mats.broadleafDark : mats.broadleaf) : mats[type];
  const wood = new THREE.InstancedMesh(geo.wood, barkMat, list.length);
  const leaves = new THREE.InstancedMesh(geo.leaves, leafMat, list.length);
  list.forEach((t, k) => { setT(t); wood.setMatrixAt(k, dummy.matrix); leaves.setMatrixAt(k, dummy.matrix); });
  for (const m of [wood, leaves]) { m.castShadow = true; m.receiveShadow = true; m.frustumCulled = false; scene.add(m); }
}
for (const [type, list] of Object.entries(farBuckets)) {
  if (!list.length) continue;
  const m = new THREE.InstancedMesh(farGeo[type], type === 'conifer' ? mats.farConifer : mats.farBroad, list.length);
  list.forEach((t, k) => { setT(t); m.setMatrixAt(k, dummy.matrix); });
  m.castShadow = true; m.receiveShadow = true; m.frustumCulled = false;
  scene.add(m);
}
// Distant forest outside the detailed window (low-poly clumps, density from the valley-scale export).
{
  const list = { conifer: [], broadleaf: [] };
  for (let j = 0; j < FN; j++) for (let i = 0; i < FN; i++) {
    const k = j * FN + i;
    const x = gx0 + i * FSTEP, y = gy0 + j * FSTEP;
    if (inWindow(x, y, -FSTEP * 0.5) || F.farWater[k] > 0.05) continue;
    const can = F.farCanopy[k];
    if (can < 0.05) continue;
    const dc = F.farConifer[k], da = F.farAngio[k];
    const n = Math.floor(can * FSTEP * FSTEP / 180 + rng());
    for (let q = 0; q < n; q++) {
      const type = rng() * (dc + da + 1e-6) < dc ? 'conifer' : 'broadleaf';
      const xx = x + (rng() - 0.5) * FSTEP, yy = y + (rng() - 0.5) * FSTEP;
      list[type].push({ x: xx, y: yy, z: heightAt(xx, yy), h: (type === 'conifer' ? 24 : 16) * (0.8 + rng() * 0.5) * 1.35 });
    }
  }
  for (const [type, l] of Object.entries(list)) {
    if (!l.length) continue;
    const m = new THREE.InstancedMesh(farGeo[type], type === 'conifer' ? mats.farConifer : mats.farBroad, l.length);
    l.forEach((t, k) => { setT(t); m.setMatrixAt(k, dummy.matrix); });
    m.frustumCulled = false;
    m.receiveShadow = true;
    scene.add(m);
  }
}
// Ground layer near the camera: fern clumps, horsetail stands, shrubs.
{
  const R = viewName === 'aerial' ? 0 : (viewName === 'drone' ? 260 : 240);
  const fernGeos = [0, 1, 2].map((s) => PL.fernGeometry(s + 1));
  const lists = { fern0: [], fern1: [], fern2: [], horsetail: [], shrub: [] };
  const i0 = Math.max(0, Math.floor((camW[0] - R - wx0) / STEP)), i1 = Math.min(N - 1, Math.ceil((camW[0] + R - wx0) / STEP));
  const j0 = Math.max(0, Math.floor((camW[1] - R - wy0) / STEP)), j1 = Math.min(N - 1, Math.ceil((camW[1] + R - wy0) / STEP));
  // Expand the area towards the look direction.
  const lookW = fromV(view.look);
  const inRange = (x, y) => Math.hypot(x - camW[0], y - camW[1]) < R || Math.hypot(x - lookW[0], y - lookW[1]) < R * 0.8;
  const I0 = Math.max(0, Math.min(i0, Math.floor((lookW[0] - R - wx0) / STEP))), I1 = Math.min(N - 1, Math.max(i1, Math.ceil((lookW[0] + R - wx0) / STEP)));
  const J0 = Math.max(0, Math.min(j0, Math.floor((lookW[1] - R - wy0) / STEP))), J1 = Math.min(N - 1, Math.max(j1, Math.ceil((lookW[1] + R - wy0) / STEP)));
  for (let j = J0; j < J1; j++) for (let i = I0; i < I1; i++) {
    const k = j * N + i;
    const x = wx0 + i * STEP, y = wy0 + j * STEP;
    if (!inRange(x, y)) continue;
    if (F.water[k] > 0.02) continue;
    const tramp = Math.max(0, 1 - F.trail[k] * 2.5);
    const nf = Math.floor(F.fern[k] * 7 * tramp + rng());
    for (let q = 0; q < nf; q++) lists['fern' + Math.floor(rng() * 3)].push([x + rng() * STEP, y + rng() * STEP, 0.7 + rng() * 1.0]);
    const nh = Math.floor(F.horsetail[k] * 4 * tramp + rng() * 0.8);
    for (let q = 0; q < nh; q++) lists.horsetail.push([x + rng() * STEP, y + rng() * STEP, 0.7 + rng() * 0.9]);
    const ns = Math.floor(F.shrub[k] * 2.0 * tramp + rng() * 0.6);
    for (let q = 0; q < ns; q++) lists.shrub.push([x + rng() * STEP, y + rng() * STEP, 0.6 + rng() * 0.9]);
  }
  const place = (geo, mat, list) => {
    if (!list.length) return;
    const m = new THREE.InstancedMesh(geo, mat, list.length);
    list.forEach(([x, y, s], k) => {
      dummy.position.copy(toV(x, y, heightAt(x, y) - 0.05));
      dummy.rotation.set((rng() - 0.5) * 0.15, rng() * 6.283, (rng() - 0.5) * 0.15);
      dummy.scale.setScalar(s);
      dummy.updateMatrix();
      m.setMatrixAt(k, dummy.matrix);
    });
    m.receiveShadow = true; m.castShadow = true; m.frustumCulled = false;
    scene.add(m);
  };
  place(fernGeos[0], mats.fern, lists.fern0);
  place(fernGeos[1], mats.fern2, lists.fern1);
  place(fernGeos[2], mats.fern, lists.fern2);
  place(PL.horsetailGeometry(1), mats.horsetail, lists.horsetail);
  place(PL.shrubGeometry(1), mats.shrub, lists.shrub);
}

// ------------------------------------------------------------------ Animals
const animalsGroup = new THREE.Group();
scene.add(animalsGroup);
for (const c of creatures) {
  const sp = species[c.s];
  const root = new THREE.Group();
  root.position.copy(toV(c.x, c.y, c.z));
  root.rotation.set(0, c.h, 0);
  root.updateMatrixWorld();
  const inv = root.matrixWorld.clone().invert();
  const toLocalFoot = (f) => toV(f[0], f[1], f[2]).applyMatrix4(inv);
  const body = buildAnimal(c, sp, toLocalFoot, tex.skin);
  if (!body) continue;
  root.add(body);
  animalsGroup.add(root);
}

// Footprints (simulated impressions; deeper = darker).
{
  const list = M.tracks;
  if (list.length) {
    const mesh = new THREE.InstancedMesh(new THREE.CircleGeometry(0.5, 7), new THREE.MeshStandardMaterial({ color: 0x3a2e22, roughness: 1, transparent: true, opacity: 0.75, depthWrite: false }), list.length);
    list.forEach((t, k) => {
      dummy.position.copy(toV(t[0], t[1], heightAt(t[0], t[1]) + 0.03));
      dummy.rotation.set(-Math.PI / 2, 0, t[3]);
      dummy.scale.set(t[2], t[2] * 0.85, 1);
      dummy.updateMatrix();
      mesh.setMatrixAt(k, dummy.matrix);
    });
    mesh.receiveShadow = true;
    scene.add(mesh);
  }
}

// Research rover (6x6 hybrid, 7.2 t; see Data/Equipment/equipment.json).
{
  const [vx, vy, vz, vh] = M.vehicle;
  const g = new THREE.Group();
  const white = new THREE.MeshStandardMaterial({ color: 0xd9d6cc, roughness: 0.45, metalness: 0.15 });
  const dark = new THREE.MeshStandardMaterial({ color: 0x1f1f1f, roughness: 0.9 });
  const glass = new THREE.MeshStandardMaterial({ color: 0x223040, roughness: 0.05, metalness: 0.4 });
  const box = (w, h, d, m, x, y, z) => { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m); b.position.set(x, y, z); b.castShadow = true; b.receiveShadow = true; g.add(b); return b; };
  box(4.6, 2.3, 2.5, white, -1.2, 2.35, 0);  // lab module
  box(2.2, 1.9, 2.45, white, 2.3, 2.1, 0);   // cab
  box(0.05, 0.8, 2.1, glass, 3.41, 2.55, 0); // windscreen
  box(7.4, 0.35, 2.3, dark, 0, 1.1, 0);      // chassis
  box(1.2, 0.25, 1.0, dark, -1.6, 3.62, 0);  // roof sensor mast base
  for (const sx of [-2.6, -0.9, 2.4]) for (const sz of [-1.25, 1.25]) {
    const wheel = new THREE.Mesh(new THREE.CylinderGeometry(0.62, 0.62, 0.5, 18), dark);
    wheel.rotation.x = Math.PI / 2; wheel.position.set(sx, 0.62, sz); wheel.castShadow = true; g.add(wheel);
  }
  g.position.copy(toV(vx, vy, vz));
  g.rotation.y = vh;
  scene.add(g);
}

// Cumulus (cloud cover from the weather state).
{
  const n = Math.round(10 + (M.cloud || 0) * 70);
  const mat = new THREE.SpriteMaterial({ map: tex.cloud, transparent: true, depthWrite: false, fog: false, color: 0xffffff });
  for (let k = 0; k < n; k++) {
    const a = rng() * Math.PI * 2, d = 3000 + rng() * 14000;
    const s = new THREE.Sprite(mat);
    s.position.set(camera.position.x + Math.cos(a) * d, 1300 + rng() * 900, camera.position.z + Math.sin(a) * d);
    const sz = 900 + rng() * 1600;
    s.scale.set(sz, sz * 0.55, 1);
    scene.add(s);
  }
}

// ------------------------------------------------------------------ HUD
const counts = {};
creatures.forEach((c) => { const n = species[c.s].name; counts[n] = (counts[n] || 0) + 1; });
document.getElementById('hud').innerHTML =
  `<b>NOCTIS — ${M.environment || 'Hell Creek'} · Maastrichtien</b><br>${M.time} · ${M.weather} · vent ${M.wind.toFixed(1)} m/s<br>` +
  `<span class="v">${view.label}</span>`;
document.getElementById('legend').innerHTML =
  `Fenêtre simulée 2 × 2 km : ${Object.entries(counts).map(([k, v]) => `<i>${k}</i> ×${v}`).join(' · ')}<br>` +
  `Aperçu WebGL de débogage — positions, postures, appuis des pieds, relief, eau et végétation issus de la simulation ; modèles et textures procéduraux provisoires (rendu final : Unreal Engine 5). ${SPECULATIVE_COLOUR_NOTE}`;

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.copy(view.look);
controls.update();
addEventListener('resize', () => { camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); renderer.setSize(innerWidth, innerHeight); });
function frame() { controls.update(); renderer.render(scene, camera); if (!still) requestAnimationFrame(frame); }
frame();
window.__stats = { trees: trees.length, creatures: creatures.length, view: view.label, triangles: renderer.info.render.triangles };
window.__ready = true;
