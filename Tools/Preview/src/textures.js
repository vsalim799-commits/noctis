// Procedural textures drawn on 2D canvases (no external art assets). They stand in for the
// photogrammetry / hand-authored textures of the Unreal Engine build.
import * as THREE from 'three';

export function makeRng(seed) {
  let s = seed >>> 0;
  return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
}

function canvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return c;
}

function toTexture(c, { repeat = false, color = true } = {}) {
  const t = new THREE.CanvasTexture(c);
  if (color) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  t.generateMipmaps = true;
  if (repeat) t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}

const hsl = (h, s, l, a = 1) => `hsla(${h},${s}%,${l}%,${a})`;

// Pinnate fern frond: rachis along the texture's vertical axis (base at the bottom).
export function fernFrondTexture(seed = 1, { hue = 98, sat = 48, light = 30 } = {}) {
  const r = makeRng(seed);
  const c = canvas(256, 512);
  const g = c.getContext('2d');
  g.translate(128, 506);
  const pinnae = 30;
  for (let i = 0; i < pinnae; i++) {
    const t = i / pinnae;
    const y = -12 - t * 480;
    const bend = Math.sin(t * 2.2) * 10;
    const len = 118 * Math.pow(Math.sin(Math.PI * (0.12 + 0.88 * t)), 0.7) * (1 - 0.55 * t);
    for (const side of [-1, 1]) {
      const ang = side * (1.25 - 0.55 * t) + (r() - 0.5) * 0.08;
      const dx = Math.sin(ang), dy = -Math.cos(ang);
      const pinnules = Math.max(3, Math.round(len / 7));
      for (let k = 0; k < pinnules; k++) {
        const u = k / pinnules;
        const px = bend + dx * len * u, py = y + dy * len * u;
        const pl = (1 - u * 0.85) * 9 + 2;
        const l = light + (r() - 0.5) * 8 + u * 4;
        g.fillStyle = hsl(hue + (r() - 0.5) * 10 + t * 8, sat, l);
        for (const s2 of [-1, 1]) {
          g.save();
          g.translate(px, py);
          g.rotate(ang + s2 * 1.1 - side * 0.35);
          g.beginPath();
          g.ellipse(0, -pl * 0.5, pl * 0.32, pl * 0.62, 0, 0, Math.PI * 2);
          g.fill();
          g.restore();
        }
      }
      g.strokeStyle = hsl(hue - 10, sat - 10, light - 6);
      g.lineWidth = 1.6 * (1 - t) + 0.6;
      g.beginPath(); g.moveTo(bend, y); g.lineTo(bend + dx * len, y + dy * len); g.stroke();
    }
  }
  g.strokeStyle = hsl(hue - 30, 35, 22);
  g.lineWidth = 4;
  g.beginPath(); g.moveTo(0, 0);
  for (let i = 0; i <= 20; i++) { const t = i / 20; g.lineTo(Math.sin(t * 2.2) * 10, -t * 495); }
  g.stroke();
  return toTexture(c);
}

// Cluster of simple dicot leaves on twigs (crown cards of angiosperm trees and shrubs).
export function leafClusterTexture(seed = 2, { hue = 92, sat = 42, light = 27 } = {}) {
  const r = makeRng(seed);
  const c = canvas(256, 256);
  const g = c.getContext('2d');
  const twigs = 4;
  for (let tw = 0; tw < twigs; tw++) {
    const ox = 128 + (r() - 0.5) * 40, oy = 128 + (r() - 0.5) * 40;
    const baseAng = r() * Math.PI * 2;
    const leaves = 13;
    for (let k = 0; k < leaves; k++) {
      const a = baseAng + (r() - 0.5) * 2.6;
      const d = 18 + r() * 70;
      const x = ox + Math.cos(a) * d, y = oy + Math.sin(a) * d;
      const L = 26 + r() * 22, Wd = L * (0.36 + r() * 0.12);
      g.save();
      g.translate(x, y);
      g.rotate(a + (r() - 0.5) * 0.9);
      const l = light + (r() - 0.5) * 12;
      const grad = g.createLinearGradient(-Wd, 0, Wd, 0);
      grad.addColorStop(0, hsl(hue + (r() - 0.5) * 14, sat, l - 5));
      grad.addColorStop(0.5, hsl(hue + 4, sat + 6, l + 5));
      grad.addColorStop(1, hsl(hue - 4, sat, l - 3));
      g.fillStyle = grad;
      g.beginPath();
      g.moveTo(0, 0);
      g.bezierCurveTo(Wd, L * 0.25, Wd * 0.8, L * 0.75, 0, L);
      g.bezierCurveTo(-Wd * 0.8, L * 0.75, -Wd, L * 0.25, 0, 0);
      g.fill();
      g.strokeStyle = hsl(hue, sat - 10, l + 12, 0.6);
      g.lineWidth = 1;
      g.beginPath(); g.moveTo(0, 2); g.lineTo(0, L * 0.9); g.stroke();
      g.restore();
      g.strokeStyle = hsl(30, 30, 25);
      g.lineWidth = 1.5;
      g.beginPath(); g.moveTo(ox, oy); g.lineTo(x, y); g.stroke();
    }
  }
  return toTexture(c);
}

// Flat feathery spray of a deciduous taxodiaceous conifer (linear leaves in two ranks).
export function coniferSprayTexture(seed = 3, { hue = 104, sat = 44, light = 30 } = {}) {
  const r = makeRng(seed);
  const c = canvas(256, 256);
  const g = c.getContext('2d');
  // Main twig from the left edge (attachment) to the right edge (tip).
  const shoots = 9;
  g.strokeStyle = hsl(28, 35, 26);
  g.lineWidth = 3;
  g.beginPath(); g.moveTo(0, 128); g.quadraticCurveTo(128, 120 + (r() - 0.5) * 16, 250, 128 + (r() - 0.5) * 20); g.stroke();
  for (let s = 0; s < shoots; s++) {
    const t = 0.08 + s / shoots * 0.88;
    const x0 = t * 245, y0 = 128 + (r() - 0.5) * 6;
    for (const side of [-1, 1]) {
      const ang = side * (0.75 + r() * 0.35) - 0.15;
      const len = (1 - t * 0.6) * (70 + r() * 30);
      const ex = x0 + Math.cos(ang) * len, ey = y0 + Math.sin(ang) * len;
      const needles = Math.round(len / 4.5);
      for (let k = 1; k < needles; k++) {
        const u = k / needles;
        const px = x0 + (ex - x0) * u, py = y0 + (ey - y0) * u;
        const nl = 9 * (1 - u * 0.5);
        g.strokeStyle = hsl(hue + (r() - 0.5) * 10, sat, light + (r() - 0.5) * 10);
        g.lineWidth = 2;
        for (const s2 of [-1, 1]) {
          const na = ang + s2 * 1.35;
          g.beginPath(); g.moveTo(px, py); g.lineTo(px + Math.cos(na) * nl, py + Math.sin(na) * nl); g.stroke();
        }
      }
      g.strokeStyle = hsl(60, 25, 30);
      g.lineWidth = 1;
      g.beginPath(); g.moveTo(x0, y0); g.lineTo(ex, ey); g.stroke();
    }
  }
  return toTexture(c);
}

// Costapalmate fan leaf (Sabalites-type palm), petiole attachment at the bottom centre.
export function palmFanTexture(seed = 4) {
  const r = makeRng(seed);
  const c = canvas(256, 256);
  const g = c.getContext('2d');
  g.translate(128, 250);
  const segs = 34;
  for (let i = 0; i < segs; i++) {
    const a = -Math.PI * 0.92 + (i / (segs - 1)) * Math.PI * 0.84 + (r() - 0.5) * 0.03;
    const len = 210 + (r() - 0.5) * 30;
    const w = 6.5;
    g.save();
    g.rotate(a + Math.PI / 2);
    const l = 30 + (r() - 0.5) * 8;
    g.fillStyle = hsl(95 + (r() - 0.5) * 10, 28, l);
    g.beginPath();
    g.moveTo(0, -18);
    g.lineTo(w, -len * 0.55);
    g.lineTo(w * 0.4, -len);
    g.lineTo(0, -len * 0.92);
    g.lineTo(-w * 0.4, -len);
    g.lineTo(-w, -len * 0.55);
    g.closePath();
    g.fill();
    g.strokeStyle = hsl(80, 20, l + 10, 0.7);
    g.lineWidth = 1;
    g.beginPath(); g.moveTo(0, -18); g.lineTo(0, -len * 0.9); g.stroke();
    g.restore();
  }
  return toTexture(c);
}

// Several jointed horsetail (Equisetum) stems with node sheaths and whorled branches.
export function horsetailTexture(seed = 5) {
  const r = makeRng(seed);
  const c = canvas(128, 512);
  const g = c.getContext('2d');
  for (let s = 0; s < 6; s++) {
    const x = 12 + r() * 104;
    const top = 40 + r() * 140;
    const lean = (r() - 0.5) * 18;
    const nodes = 9;
    for (let k = 0; k < nodes; k++) {
      const y0 = 512 - (512 - top) * (k / nodes), y1 = 512 - (512 - top) * ((k + 1) / nodes);
      const xx0 = x + lean * (k / nodes), xx1 = x + lean * ((k + 1) / nodes);
      g.strokeStyle = hsl(85 + (r() - 0.5) * 10, 38, 34 + (r() - 0.5) * 6);
      g.lineWidth = 5 - k * 0.3;
      g.beginPath(); g.moveTo(xx0, y0); g.lineTo(xx1, y1); g.stroke();
      g.strokeStyle = hsl(40, 25, 20);
      g.lineWidth = 2;
      g.beginPath(); g.moveTo(xx1 - 3, y1); g.lineTo(xx1 + 3, y1); g.stroke();
      if (k > 1 && k < nodes - 1) {
        g.strokeStyle = hsl(90, 35, 36, 0.85);
        g.lineWidth = 1;
        for (let b = 0; b < 6; b++) {
          const a = (b / 6) * Math.PI - Math.PI;
          const bl = 14 - k;
          g.beginPath(); g.moveTo(xx1, y1); g.lineTo(xx1 + Math.cos(a) * bl, y1 + Math.abs(Math.sin(a)) * bl * 0.6 + 2); g.stroke();
        }
      }
    }
  }
  return toTexture(c);
}

// Value-noise helper on a tiling lattice.
function tileNoise(size, cells, seed) {
  const r = makeRng(seed);
  const lat = new Float32Array(cells * cells).map(() => r());
  const out = new Float32Array(size * size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const fx = x / size * cells, fy = y / size * cells;
    const i = Math.floor(fx), j = Math.floor(fy), tx = fx - i, ty = fy - j;
    const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const at = (a, b) => lat[((b % cells + cells) % cells) * cells + ((a % cells + cells) % cells)];
    const v = (at(i, j) * (1 - sx) + at(i + 1, j) * sx) * (1 - sy) + (at(i, j + 1) * (1 - sx) + at(i + 1, j + 1) * sx) * sy;
    out[y * size + x] = v;
  }
  return out;
}

function fbmTile(size, seed, octaves = [[4, 0.5], [8, 0.25], [16, 0.15], [32, 0.1]]) {
  const acc = new Float32Array(size * size);
  octaves.forEach(([cells, amp], k) => {
    const n = tileNoise(size, cells, seed + k * 101);
    for (let i = 0; i < acc.length; i++) acc[i] += n[i] * amp;
  });
  return acc;
}

// Ground detail (silt, litter flecks, small clasts). Mostly light grey so it modulates vertex colours.
export function groundDetailTexture(seed = 6) {
  const size = 512;
  const r = makeRng(seed);
  const n = fbmTile(size, seed);
  const c = canvas(size, size);
  const g = c.getContext('2d');
  const img = g.createImageData(size, size);
  for (let i = 0; i < size * size; i++) {
    const v = 0.72 + (n[i] - 0.5) * 0.45;
    img.data[i * 4] = Math.min(255, v * 255 * 1.02);
    img.data[i * 4 + 1] = Math.min(255, v * 255);
    img.data[i * 4 + 2] = Math.min(255, v * 255 * 0.96);
    img.data[i * 4 + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  // Litter flecks and twigs.
  for (let k = 0; k < 900; k++) {
    const x = r() * size, y = r() * size;
    const l = 30 + r() * 45;
    g.fillStyle = hsl(30 + r() * 25, 30, l, 0.55);
    g.beginPath(); g.ellipse(x, y, 1 + r() * 4, 0.6 + r() * 1.5, r() * 6.28, 0, Math.PI * 2); g.fill();
  }
  g.strokeStyle = 'rgba(70,50,35,0.5)';
  for (let k = 0; k < 120; k++) {
    const x = r() * size, y = r() * size, a = r() * 6.28, l = 6 + r() * 20;
    g.lineWidth = 1 + r();
    g.beginPath(); g.moveTo(x, y); g.lineTo(x + Math.cos(a) * l, y + Math.sin(a) * l); g.stroke();
  }
  return toTexture(c, { repeat: true });
}

// Normal map from a height field (tiling).
function normalFromHeight(h, size, strength) {
  const c = canvas(size, size);
  const g = c.getContext('2d');
  const img = g.createImageData(size, size);
  const at = (x, y) => h[((y + size) % size) * size + ((x + size) % size)];
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const dx = (at(x + 1, y) - at(x - 1, y)) * strength;
    const dy = (at(x, y + 1) - at(x, y - 1)) * strength;
    const len = Math.hypot(dx, dy, 1);
    const i = (y * size + x) * 4;
    img.data[i] = (-dx / len * 0.5 + 0.5) * 255;
    img.data[i + 1] = (-dy / len * 0.5 + 0.5) * 255;
    img.data[i + 2] = (1 / len * 0.5 + 0.5) * 255;
    img.data[i + 3] = 255;
  }
  g.putImageData(img, 0, 0);
  return toTexture(c, { repeat: true, color: false });
}

export function waterNormalTexture(seed = 7) {
  const size = 256;
  const n = fbmTile(size, seed, [[8, 0.5], [16, 0.3], [32, 0.2]]);
  return normalFromHeight(n, size, 6);
}

// Reptilian skin: polygonal tubercles of varied size (Voronoi cells), as preserved in hadrosaur
// and ceratopsian skin impressions. Used as a subtle normal map.
export function scaleNormalTexture(seed = 8) {
  const size = 256;
  const r = makeRng(seed);
  const pts = [];
  for (let k = 0; k < 260; k++) pts.push([r() * size, r() * size, 0.6 + r() * 0.8]);
  const h = new Float32Array(size * size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    let d1 = 1e9, d2 = 1e9;
    for (const [px, py, w] of pts) {
      for (const ox of [-size, 0, size]) for (const oy of [-size, 0, size]) {
        const d = Math.hypot(x - px - ox, y - py - oy) / w;
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
      }
    }
    h[y * size + x] = Math.min(1, (d2 - d1) / 6);
  }
  return normalFromHeight(h, size, 2.2);
}

// Soft cumulus puff for sky sprites.
export function cloudTexture(seed = 9) {
  const size = 256;
  const n = fbmTile(size, seed, [[4, 0.5], [8, 0.3], [16, 0.2]]);
  const c = canvas(size, size);
  const g = c.getContext('2d');
  const img = g.createImageData(size, size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const dx = (x - size / 2) / (size / 2), dy = (y - size * 0.55) / (size * 0.42);
    const base = 1 - Math.hypot(dx, dy * (dy > 0 ? 1.6 : 1.0));
    const v = Math.max(0, Math.min(1, base * 1.6 + (n[y * size + x] - 0.5) * 1.4));
    const shade = 0.78 + 0.22 * Math.max(0, Math.min(1, 1 - (y / size) * 1.1 + 0.25));
    const i = (y * size + x) * 4;
    img.data[i] = 255 * shade; img.data[i + 1] = 255 * shade; img.data[i + 2] = 255 * Math.min(1, shade + 0.03);
    img.data[i + 3] = 255 * Math.pow(v, 1.4);
  }
  g.putImageData(img, 0, 0);
  return toTexture(c);
}

export function barkTexture(seed = 10) {
  const size = 128;
  const r = makeRng(seed);
  const c = canvas(size, size * 2);
  const g = c.getContext('2d');
  g.fillStyle = '#6b5a48';
  g.fillRect(0, 0, size, size * 2);
  for (let k = 0; k < 160; k++) {
    const x = r() * size;
    g.strokeStyle = hsl(25 + r() * 10, 18 + r() * 10, 20 + r() * 25, 0.7);
    g.lineWidth = 1 + r() * 3;
    g.beginPath(); g.moveTo(x, 0);
    for (let y = 0; y <= size * 2; y += 16) g.lineTo(x + Math.sin(y * 0.05 + k) * 3, y);
    g.stroke();
  }
  return toTexture(c, { repeat: true });
}
