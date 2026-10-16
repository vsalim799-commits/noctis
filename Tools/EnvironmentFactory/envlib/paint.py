"""Vector-ish painter for leaf cards: leaves, needles, stems and fronds rasterised with numpy.

A Canvas keeps colour (sRGB, float), coverage alpha, height, translucency and roughness layers at a
supersampled resolution; every primitive only touches its bounding box. Painter's order = draw order
(later primitives cover earlier ones), with a small height offset per primitive so the derived normal map
shows overlaps. resolve() returns the final maps (downsampled, colour bled outside the alpha).
"""
from __future__ import annotations

import math

import numpy as np

from . import texgen as tg


class Canvas:
    def __init__(self, w, h, ss=2, seed=0):
        self.ss = ss
        self.W, self.H = w * ss, h * ss
        self.col = np.zeros((self.H, self.W, 3))
        self.alpha = np.zeros((self.H, self.W))
        self.height = np.zeros((self.H, self.W))
        self.transl = np.zeros((self.H, self.W))
        self.rough = np.full((self.H, self.W), 0.6)
        self.z = 0.0
        # Mottling noise shared by all primitives (sampled at absolute pixel positions).
        self.mottle = tg.fbm((self.H, self.W), seed + 7, octaves=((6, 1.0), (24, 0.6), (96, 0.35)))
        self.fine = tg.fft_noise((self.H, self.W), seed + 9, beta=0.5, lo=200)

    # -------------------------------------------------------------------------------- core blend
    def _bbox(self, x0, y0, x1, y1):
        x0 = max(0, int(math.floor(x0)) - 2)
        y0 = max(0, int(math.floor(y0)) - 2)
        x1 = min(self.W, int(math.ceil(x1)) + 2)
        y1 = min(self.H, int(math.ceil(y1)) + 2)
        if x1 <= x0 or y1 <= y0:
            return None
        return x0, y0, x1, y1

    def blend(self, box, m, col, height, transl, rough):
        x0, y0, x1, y1 = box
        sl = (slice(y0, y1), slice(x0, x1))
        m = np.clip(m, 0.0, 1.0)
        if not m.any():
            return
        self.z += 0.004
        mm = m[..., None]
        self.col[sl] = self.col[sl] * (1 - mm) + col * mm
        self.height[sl] = self.height[sl] * (1 - m) + (height + self.z) * m
        self.transl[sl] = self.transl[sl] * (1 - m) + transl * m
        self.rough[sl] = self.rough[sl] * (1 - m) + rough * m
        self.alpha[sl] = np.maximum(self.alpha[sl], m)

    def grid(self, box):
        x0, y0, x1, y1 = box
        yy, xx = np.mgrid[y0:y1, x0:x1]
        return xx + 0.5, yy + 0.5

    # -------------------------------------------------------------------------------- primitives
    def stroke(self, pts, w0, w1, color, height=0.5, transl=0.05, rough=0.7, noise_amp=0.15):
        """Tapered stem along a polyline (pixel coords), width w0 -> w1 (pixels)."""
        pts = np.asarray(pts, float)
        seglen = np.linalg.norm(np.diff(pts, axis=0), axis=1)
        cum = np.concatenate([[0], np.cumsum(seglen)])
        total = max(cum[-1], 1e-6)
        for i in range(len(pts) - 1):
            a, b = pts[i], pts[i + 1]
            wa = w0 + (w1 - w0) * cum[i] / total
            wb = w0 + (w1 - w0) * cum[i + 1] / total
            r = max(wa, wb) * 0.5 + 1
            box = self._bbox(min(a[0], b[0]) - r, min(a[1], b[1]) - r, max(a[0], b[0]) + r, max(a[1], b[1]) + r)
            if box is None:
                continue
            X, Y = self.grid(box)
            ab = b - a
            L2 = max(ab @ ab, 1e-9)
            t = np.clip(((X - a[0]) * ab[0] + (Y - a[1]) * ab[1]) / L2, 0, 1)
            dx = X - (a[0] + t * ab[0])
            dy = Y - (a[1] + t * ab[1])
            d = np.sqrt(dx * dx + dy * dy)
            rad = (wa + (wb - wa) * t) * 0.5
            m = np.clip(rad - d + 0.5, 0, 1)
            prof = np.sqrt(np.clip(1 - (d / np.maximum(rad, 0.5)) ** 2, 0, 1))
            x0, y0, x1, y1 = box
            n = self.mottle[y0:y1, x0:x1]
            c = np.asarray(color)[None, None, :] * (1 + noise_amp * (n[..., None] - 0.5)) * (0.75 + 0.25 * prof[..., None])
            self.blend(box, m, c, height * prof, transl, rough)

    def leaf(self, base, angle, length, width, color, rng, shape="elliptic", peak=0.45, tip_power=1.0,
             teeth=0, tooth_depth=0.0, lobes=0, lobe_depth=0.0, veins=8, vein_angle=55.0, vein_color=None,
             midrib=1.0, petiole=0.0, petiole_color=None, curl=0.0, damage=0.0, necrosis=0.0, transl=0.8,
             rough=0.55, dome=0.6, gloss_var=0.1, edge_dark=0.15):
        """Simple leaf with pinnate venation. base = petiole attachment (pixels), angle (rad, 0 = +x, image y down),
        length/width in pixels (width = full blade width at the widest point)."""
        L = float(length)
        W = float(width) * 0.5
        d = np.array([math.cos(angle), math.sin(angle)])
        p = np.array([-d[1], d[0]])
        base = np.asarray(base, float)
        if petiole > 0:
            pc = petiole_color if petiole_color is not None else np.asarray(color) * 0.8
            tip = base + d * petiole * L
            self.stroke([base, tip], max(1.5, W * 0.12), max(1.2, W * 0.09), pc, height=0.4, transl=0.2)
            base = tip
        reach = L + W + 4
        box = self._bbox(base[0] - reach, base[1] - reach, base[0] + reach, base[1] + reach)
        if box is None:
            return
        X, Y = self.grid(box)
        rx, ry = X - base[0], Y - base[1]
        u = (rx * d[0] + ry * d[1]) / L
        v = (rx * p[0] + ry * p[1]) / L
        if curl:
            v = v - curl * u * u  # bends the midrib sideways
        uu = np.clip(u, 0, 1)
        if shape == "linear":
            half = W / L * np.sqrt(np.clip(1 - np.clip((uu - 0.5) / 0.5, -1, 1) ** 12, 0, 1))
            half = half * np.clip(1 - (np.clip(uu - 0.85, 0, 1) / 0.15) ** 2 * 0.6, 0.0, 1)
        else:
            # Asymmetric bell: widest at 'peak', pointed tip controlled by tip_power.
            a = np.where(uu < peak, uu / peak, (1 - uu) / (1 - peak))
            if shape == "ovate":
                prof = np.where(uu < peak, np.sin(0.5 * math.pi * a) ** 0.6, a ** tip_power)
            elif shape == "cordate":
                prof = np.where(uu < peak, np.sin(0.5 * math.pi * np.clip(a * 1.1 + 0.15, 0, 1)) ** 0.5, a ** tip_power)
            elif shape == "strap":
                prof = np.where(uu < 0.08, np.sqrt(uu / 0.08), np.where(uu > 0.85, ((1 - uu) / 0.15) ** 0.8, 1.0))
            else:  # elliptic / lanceolate
                prof = np.where(uu > peak, a ** tip_power, np.sin(0.5 * math.pi * a) ** 0.8)
            half = (W / L) * prof
        if lobes:
            lob = 0.5 - 0.5 * np.cos(2 * math.pi * lobes * np.clip(uu * 0.9 + 0.05, 0, 1))
            half = half * (1 - lobe_depth * lob)
        if teeth:
            saw = (uu * teeth) % 1.0
            half = half * (1 - tooth_depth * saw)
        av = np.abs(v)
        inside = (half - av) * L
        tipcut = np.minimum(u * L, (1 - u) * L)
        m = np.clip(np.minimum(inside, tipcut) + 0.5, 0, 1)
        if damage > 0 and rng.random() < damage:
            # Insect herbivory: round holes and margin feeding (cf. diverse plant-insect associations).
            for _ in range(rng.integers(1, 4)):
                cu = rng.uniform(0.2, 0.85)
                cv = rng.uniform(-1, 1) * (W / L) * 0.8
                if rng.random() < 0.5:
                    cv = math.copysign((W / L) * rng.uniform(0.8, 1.1), cv)  # margin feeding
                rr = rng.uniform(0.04, 0.12) * (W / L) * 3
                dd = np.sqrt((u - cu) ** 2 + (v - cv) ** 2)
                m = m * np.clip((dd - rr) * L + 0.5, 0, 1)
        if not m.any():
            return
        rel = np.clip(av / np.maximum(half, 1e-6), 0, 1)
        # Venation: midrib + secondary veins leaving it at vein_angle.
        mid = np.exp(-((v * L) / max(1.0, W * 0.06 * midrib + 0.6)) ** 2) if midrib > 0 else 0 * u
        k = 1.0 / math.tan(math.radians(vein_angle))
        s = (u - av * k) * veins
        dv = np.abs((s % 1.0) - 0.5) * 2  # 1 on the vein, 0 between
        sec = np.clip((dv - 0.86) / 0.14, 0, 1) * (rel < 0.92) * (u > 0.03)
        vc = np.asarray(vein_color if vein_color is not None else np.asarray(color) * 1.18 + 0.03)
        x0, y0, x1, y1 = box
        mot = self.mottle[y0:y1, x0:x1]
        fine = self.fine[y0:y1, x0:x1]
        col = np.asarray(color)[None, None, :] * (0.86 + 0.28 * mot[..., None]) * (0.97 + 0.06 * fine[..., None])
        col = col * (1 - edge_dark * rel[..., None] ** 4)
        if necrosis > 0:
            nec = tg.smoothstep(1 - necrosis, 1.0, rel + 0.25 * (mot - 0.5))
            col = col * (1 - nec[..., None]) + np.array([0.45, 0.33, 0.16])[None, None, :] * nec[..., None]
        veinw = np.clip(mid + 0.6 * sec, 0, 1)
        col = col * (1 - veinw[..., None]) + vc[None, None, :] * veinw[..., None]
        h = dome * np.sqrt(np.clip(1 - rel ** 2, 0, 1)) - 0.25 * mid + 0.1 * sec
        tr = transl * (1 - 0.6 * veinw) * (0.85 + 0.3 * mot)
        ro = rough + gloss_var * (mot - 0.5)
        self.blend(box, m, col, h, tr, ro)

    def polar_leaf(self, base, angle, R, color, rng, kind="palmate", spread=200.0, lobes=5, lobe_width=0.28,
                   sinus=0.45, teeth=0, tooth_depth=0.0, segments=36, split=0.35, notch=0.0, costa=0.0,
                   transl=0.75, rough=0.55, necrosis=0.0, damage=0.0, plication=1.0, vein_color=None, edge_dark=0.1):
        """Leaves defined in polar coordinates around the petiole junction 'base':
        kind='palmate'  lobed dicot blade (lobes, sinus depth, marginal teeth, palmate primary veins)
        kind='fan'      fan / costapalmate palm blade or Ginkgo leaf (radial folds, split segment tips, notch)."""
        base = np.asarray(base, float)
        reach = R * 1.05 + 4
        box = self._bbox(base[0] - reach, base[1] - reach, base[0] + reach, base[1] + reach)
        if box is None:
            return
        X, Y = self.grid(box)
        rx, ry = X - base[0], Y - base[1]
        d = np.array([math.cos(angle), math.sin(angle)])
        p = np.array([-d[1], d[0]])
        u = rx * d[0] + ry * d[1]
        v = rx * p[0] + ry * p[1]
        if costa > 0:
            # Costapalmate: the costa pushes the blade centre forward; folds radiate from points along it.
            u = u - costa * R * np.clip(u / (R * 0.6), 0, 1) * 0.5
        r = np.sqrt(u * u + v * v) / R
        phi = np.arctan2(v, u)
        half = math.radians(spread) * 0.5
        x0, y0, x1, y1 = box
        mot = self.mottle[y0:y1, x0:x1]
        if kind == "palmate":
            centers = np.linspace(-half * 0.8, half * 0.8, lobes) if lobes > 1 else np.array([0.0])
            lw = np.zeros_like(phi)
            for i, c in enumerate(centers):
                scale = 1.0 - 0.28 * abs(c) / max(half, 1e-6)
                lw = np.maximum(lw, scale * np.exp(-((phi - c) / lobe_width) ** 2))
            edge = (1 - sinus) + sinus * lw
            edge = edge * np.where(np.abs(phi) > half, np.clip(1 - (np.abs(phi) - half) / 0.5, 0.25, 1), 1.0)
            if teeth:
                saw = (r * teeth + phi * 2) % 1.0
                edge = edge * (1 - tooth_depth * saw)
            m = np.clip((edge - r) * R + 0.5, 0, 1)
            vein_d = np.min(np.stack([np.abs(np.sin(phi - c)) * r * R for c in centers]), axis=0)
            veins = np.exp(-(vein_d / 1.6) ** 2) * (r < edge * 0.95)
            fold = 0 * r
        else:  # fan
            seg = (phi + half) / (2 * half) * segments
            f = seg % 1.0
            ff = np.abs(f - 0.5) * 2  # 0 at segment centre, 1 at fold between segments
            tip = 1 - 0.10 * (1 - ff) ** 0.5  # pointed segment tips
            edge = 0.98 * tip
            m = np.clip((edge - r) * R + 0.5, 0, 1) * np.clip((half - np.abs(phi)) * R * np.maximum(r, 0.05) + 0.5, 0, 1)
            if split > 0:
                cut = (r > 1 - split) & (ff > 1 - 0.10 * (r - (1 - split)) / split)
                m = m * (~cut)
            if notch > 0:
                m = m * ~((np.abs(phi) < 0.06 + 0.1 * r) & (r > 1 - notch))
            fold = (1 - ff) * plication
            vein_d = np.abs(((seg * 4) % 1.0) - 0.5)
            veins = np.exp(-(vein_d / 0.08) ** 2) * 0.35
        if damage > 0 and rng.random() < damage:
            for _ in range(rng.integers(1, 3)):
                cphi = rng.uniform(-half, half) * 0.8
                cr = rng.uniform(0.4, 0.95)
                rr = rng.uniform(0.04, 0.09)
                dd = np.sqrt((r * np.cos(phi) - cr * math.cos(cphi)) ** 2 + (r * np.sin(phi) - cr * math.sin(cphi)) ** 2)
                m = m * np.clip((dd - rr) * R + 0.5, 0, 1)
        if not m.any():
            return
        col = np.asarray(color)[None, None, :] * (0.86 + 0.28 * mot[..., None])
        col = col * (1 - edge_dark * np.clip(r, 0, 1)[..., None] ** 4)
        col = col * (0.9 + 0.1 * fold[..., None])
        if necrosis > 0:
            nec = tg.smoothstep(1 - necrosis, 1.0, r + 0.2 * (mot - 0.5))
            col = col * (1 - nec[..., None]) + np.array([0.48, 0.36, 0.18])[None, None, :] * nec[..., None]
        vc = np.asarray(vein_color if vein_color is not None else np.asarray(color) * 1.15 + 0.03)
        col = col * (1 - veins[..., None]) + vc[None, None, :] * veins[..., None]
        h = 0.4 * np.sqrt(np.clip(1 - r * r, 0, 1)) + 0.5 * fold + 0.1 * veins
        self.blend(box, m, col, h, transl * (1 - 0.4 * veins), rough + 0.08 * (mot - 0.5))

    def ellipse(self, c, rx, ry, angle, color, height=0.5, transl=0.3, rough=0.6):
        reach = max(rx, ry) + 2
        box = self._bbox(c[0] - reach, c[1] - reach, c[0] + reach, c[1] + reach)
        if box is None:
            return
        X, Y = self.grid(box)
        ca, sa = math.cos(angle), math.sin(angle)
        lx = ((X - c[0]) * ca + (Y - c[1]) * sa) / rx
        ly = (-(X - c[0]) * sa + (Y - c[1]) * ca) / ry
        r = np.sqrt(lx * lx + ly * ly)
        m = np.clip((1 - r) * min(rx, ry) + 0.5, 0, 1)
        x0, y0, x1, y1 = box
        mot = self.mottle[y0:y1, x0:x1]
        col = np.asarray(color)[None, None, :] * (0.88 + 0.24 * mot[..., None])
        self.blend(box, m, col, height * np.sqrt(np.clip(1 - r * r, 0, 1)), transl, rough)

    # -------------------------------------------------------------------------------- resolve
    def resolve(self, normal_strength=6.0):
        ss = self.ss
        col = tg.downsample(self.col, ss)
        alpha = tg.downsample(self.alpha, ss)
        height = tg.downsample(self.height, ss)
        transl = tg.downsample(self.transl, ss)
        rough = tg.downsample(self.rough, ss)
        col = tg.dilate_color(col, alpha)
        transl = tg.dilate_color(transl[..., None], alpha)[..., 0]
        rough = tg.dilate_color(rough[..., None], alpha)[..., 0]
        hn = height / max(height.max(), 1e-6)
        normal = tg.normal_from_height(tg.blur(hn, 0.6, wrap=False), normal_strength, wrap=False)
        return dict(basecolor=col, opacity=alpha, normal=normal, translucency=transl, roughness=rough, height=hn)


def paste(atlas, maps, cell, grid):
    """Copies the resolved maps of one cell into an atlas dict of full-size arrays. grid = (cols, rows)."""
    cols, rows = grid
    ci, ri = cell % cols, cell // cols
    for k, v in maps.items():
        A = atlas[k]
        ch = A.shape[0] // rows
        cw = A.shape[1] // cols
        A[ri * ch:(ri + 1) * ch, ci * cw:(ci + 1) * cw] = v[:ch, :cw]


def new_atlas(size, grid):
    W, H = size
    return dict(basecolor=np.zeros((H, W, 3)), opacity=np.zeros((H, W)), normal=np.full((H, W, 3), 128, np.uint8),
                translucency=np.zeros((H, W)), roughness=np.full((H, W), 0.6), height=np.zeros((H, W)))


def cell_uv_rect(cell, grid):
    """UV rectangle (u0, v0, u1, v1) of an atlas cell, Blender UV convention (v up, image row 0 at v = 1)."""
    cols, rows = grid
    ci, ri = cell % cols, cell // cols
    u0, u1 = ci / cols, (ci + 1) / cols
    v1 = 1 - ri / rows
    v0 = 1 - (ri + 1) / rows
    return (u0, v0, u1, v1)
