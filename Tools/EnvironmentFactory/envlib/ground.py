"""Stage 'ground': tileable 2K ground texture sets (BaseColor, Normal (DirectX), Roughness, Height 16 bit).

Every map tiles exactly: periodic FFT noise, periodic Worley cells, integer-lattice ripple directions, and
discrete elements (leaves, needles, pebbles, footprints) stamped with wrap-around. Height is the master field
(displacement / parallax / landscape height blending); the normal map is derived from it.

Deviation from a node-based Blender bake, on purpose: the same procedural fields are evaluated with numpy so
the tiles are exactly periodic and reproducible, then checked in Blender/Cycles (swatches, valley renders).
Physical tile sizes are in MATERIALS (metres per tile).
"""
from __future__ import annotations

import math
import os
import time

import numpy as np

from . import common
from . import texgen as tg
from .paint import Canvas

N = 2048

# name -> (tile size m, habitats / substrates it is meant for, description FR)
MATERIALS = {
    "FloodplainSilt": (2.0, "floodplain_open, levee_woodland ; substrats silt/clay", "Limon de plaine d'inondation, fentes de dessiccation, débris fins"),
    "RiverbankMudTracks": (2.5, "riverbank ; substrat mud", "Boue de berge humide avec empreintes (théropode, hadrosaure, petit tridactyle) et flaques"),
    "PointBarSand": (2.0, "sandbar, chenal peu profond ; substrat sand", "Sable de barre de méandre à rides de courant, débris végétaux dans les creux, petits galets"),
    "LeafLitterBroadleaf": (2.0, "riparian_forest, levee_woodland ; substrat forest_soil", "Litière de feuilles de dicotylédones et brindilles"),
    "LeafLitterConifer": (2.0, "upland_conifer_forest, backswamp boisé ; forest_soil/peat", "Litière de rameaux caducs et aiguilles de Cupressaceae, petits cônes"),
    "BackswampOrganicMud": (2.0, "backswamp ; substrats peat/mud/clay", "Vase organique de marais arrière, eau stagnante, débris végétaux"),
    "CutbankStrata": (4.0, "cutbank_cliff (affleurements) ; substrats rock/clay", "Coupe de berge : alternance grès / silts / argiles / niveaux carbonés"),
    "RiverGravel": (1.5, "channel ; substrat gravel", "Graviers et galets roulés du fond du chenal, matrice sableuse, film d'algues"),
}


# ----------------------------------------------------------------------------------------------- helpers
def _wrap_offsets(x, y, r):
    offs = [(0, 0)]
    for dx in (-N, 0, N):
        for dy in (-N, 0, N):
            if (dx, dy) == (0, 0):
                continue
            if (dx and (x + dx + r < 0 or x + dx - r > N)) or (dy and (y + dy + r < 0 or y + dy - r > N)):
                continue
            offs.append((dx, dy))
    return offs


def stamp_max(H, C, R, cx, cy, r, fn):
    """Wrap-around stamp: fn(dx, dy) on a (2r+1)^2 window returns (h, mask, col, rough); keeps the highest surface."""
    ix = (np.arange(int(cx) - r, int(cx) + r + 1)) % N
    iy = (np.arange(int(cy) - r, int(cy) + r + 1)) % N
    gy, gx = np.mgrid[-r:r + 1, -r:r + 1]
    dx = gx + (int(cx) - cx) + 0.5
    dy = gy + (int(cy) - cy) + 0.5
    h, m, col, rough = fn(dx, dy)
    sub = np.ix_(iy, ix)
    cur = H[sub]
    take = (h > cur) & (m > 0)
    w = np.where(take, m, 0.0)
    H[sub] = np.where(take, cur * (1 - w) + h * w, cur)
    C[sub] = C[sub] * (1 - w[..., None]) + col * w[..., None]
    R[sub] = R[sub] * (1 - w) + rough * w
    return take


def stamp_add(H, cx, cy, r, fn):
    ix = (np.arange(int(cx) - r, int(cx) + r + 1)) % N
    iy = (np.arange(int(cy) - r, int(cy) + r + 1)) % N
    gy, gx = np.mgrid[-r:r + 1, -r:r + 1]
    dx = gx + (int(cx) - cx) + 0.5
    dy = gy + (int(cy) - cy) + 0.5
    d = fn(dx, dy)
    H[np.ix_(iy, ix)] += d
    return d


def pebble_fn(rx, ry, ang, top, color, rough, rng, flat=0.6):
    ca, sa = math.cos(ang), math.sin(ang)
    col = np.asarray(color)

    def fn(dx, dy):
        lx = (dx * ca + dy * sa) / rx
        ly = (-dx * sa + dy * ca) / ry
        rr = lx * lx + ly * ly
        inside = rr < 1
        prof = np.sqrt(np.clip(1 - rr, 0, 1)) ** flat
        h = top * prof
        m = np.clip((1 - np.sqrt(rr)) * min(rx, ry) * 0.8, 0, 1)
        shade = 0.82 + 0.25 * prof[..., None]
        speck = 1 + 0.08 * np.sin(dx * 1.7 + rng.uniform(0, 6)) * np.cos(dy * 2.3)
        return h * inside, m, col[None, None, :] * shade * speck[..., None], np.full(dx.shape, rough)
    return fn


def lattice_wave(k_cycles, warp, amp=1.0, asym=0.0):
    """Periodic wave along an integer lattice direction (k_cycles = (nx, ny) cycles per tile)."""
    yy, xx = np.mgrid[0:N, 0:N] / N
    ph = (xx * k_cycles[0] + yy * k_cycles[1]) + warp
    f = ph % 1.0
    if asym:  # asymmetric current ripple: long stoss side, short lee side
        s = np.where(f < 1 - asym, f / (1 - asym), (1 - f) / asym)
        return amp * s
    return amp * (0.5 + 0.5 * np.sin(2 * np.pi * ph))


def finish(name, col, H, rough, strength, extra=None):
    H = (H - H.min()) / max(H.max() - H.min(), 1e-9)
    maps = dict(basecolor=np.clip(col, 0, 1), height=H, roughness=np.clip(rough, 0.02, 1),
                normal=tg.normal_from_height(H, strength, wrap=True))
    files = common.save_texture_set("Ground_" + name, maps, ("BaseColor", "Normal", "Roughness", "Height"), directory=common.GROUND_DIR)
    if extra:
        files.update(extra)
    return files


def soil_base(seed, color, var=0.25, hamp=1.0):
    n1 = tg.fbm((N, N), seed, octaves=((3, 1.0), (12, 0.6), (48, 0.4), (192, 0.25)))
    n2 = tg.fft_noise((N, N), seed + 1, beta=2.0, lo=2, hi=10)
    col = np.asarray(color)[None, None, :] * (1 - var / 2 + var * n1[..., None]) * (0.9 + 0.2 * n2[..., None])
    return col, n1 * hamp, n2


# ----------------------------------------------------------------------------------------------- materials
def floodplain_silt(seed):
    rng = np.random.default_rng(seed)
    col, H, n2 = soil_base(seed, (0.50, 0.44, 0.35), var=0.2, hamp=0.25)
    cells = tg.worley((N, N), seed + 2, 90, periodic=True, metric="f2-f1")
    crack = 1 - tg.smoothstep(0.0, 5.0 + 3 * n2, cells)
    curl = tg.smoothstep(0.0, 40.0, cells)  # polygon edges curl up slightly
    H = H + 0.35 * (1 - curl) * (1 - crack) - 0.8 * crack
    col = col * (1 - 0.55 * crack[..., None]) + np.array([0.18, 0.15, 0.12]) * 0.55 * crack[..., None]
    dry = tg.smoothstep(0.4, 0.8, n2)
    col = col * (0.92 + 0.16 * dry[..., None])
    rough = 0.88 + 0.08 * dry - 0.1 * crack
    R = rough.copy()
    for _ in range(140):  # fine plant debris
        x, y = rng.uniform(0, N, 2)
        L = rng.uniform(8, 40)
        a = rng.uniform(0, np.pi)
        w = rng.uniform(1.0, 2.5)
        c = np.array([0.32, 0.25, 0.17]) * rng.uniform(0.6, 1.1)

        def fn(dx, dy, a=a, L=L, w=w, c=c):
            u = dx * math.cos(a) + dy * math.sin(a)
            v = -dx * math.sin(a) + dy * math.cos(a)
            m = np.clip(w - np.abs(v) + 0.5, 0, 1) * (np.abs(u) < L / 2)
            return 0.6 + 0 * u, m, np.broadcast_to(c, dx.shape + (3,)), np.full(dx.shape, 0.8)
        stamp_max(H, col, R, x, y, int(L / 2 + 3), fn)
    return finish("FloodplainSilt", col, H, R, 10.0)


def _footprint_sdf(kind, L):
    """Returns fn(dx, dy) -> depth (>0 inside) for a print pointing to -y (image up), length L px."""
    if kind == "theropod":
        toes = [(-30, 0.62, 0.065), (0, 0.80, 0.075), (30, 0.60, 0.065)]
        heel = (0.13, 0.11)
        claw = True
    elif kind == "hadrosaur":
        toes = [(-32, 0.62, 0.20), (0, 0.72, 0.23), (32, 0.62, 0.20)]
        heel = (0.36, 0.30)
        claw = False
    else:  # small tridactyl (bird / small theropod)
        toes = [(-50, 0.95, 0.045), (0, 1.0, 0.05), (50, 0.95, 0.045)]
        heel = (0.08, 0.08)
        claw = True

    def fn(dx, dy):
        y0 = L * 0.35  # heel centre below the origin
        d = np.full(dx.shape, 1e9)
        for ang, ln, wd in toes:
            a = math.radians(ang)
            ux, uy = math.sin(a), -math.cos(a)
            px, py = dx, dy - y0 + L * 0.05
            t = np.clip((px * ux + py * uy) / (ln * L), 0, 1)
            qx, qy = px - ux * t * ln * L, py - uy * t * ln * L
            w = wd * L * (1 - (0.55 if claw else 0.2) * t ** 3)
            d = np.minimum(d, np.sqrt(qx * qx + qy * qy) - w)
        hx, hy = heel
        e = np.sqrt((dx / (hx * L)) ** 2 + ((dy - y0) / (hy * L)) ** 2) - 1
        d = np.minimum(d, e * min(hx, hy) * L)
        return d
    return fn


def riverbank_mud(seed):
    rng = np.random.default_rng(seed)
    col, H, n2 = soil_base(seed, (0.30, 0.26, 0.20), var=0.25, hamp=0.4)
    sheen = tg.fft_noise((N, N), seed + 5, beta=2.0, lo=3, hi=30)
    H = H + 0.25 * tg.fft_noise((N, N), seed + 6, beta=1.0, lo=60, hi=500)
    px_per_m = N / MATERIALS["RiverbankMudTracks"][0]
    prints = []
    # theropod trackway (prints ~0.32 m) and a hadrosaur pair (~0.42 m), small tridactyl tracks (~0.08 m)
    heading = rng.uniform(0, 2 * np.pi)
    start = rng.uniform(0, N, 2)
    for k in range(3):  # pace ~0.8 m (slow walk), alternating left / right
        side = 1 if k % 2 else -1
        pos = start + np.array([math.cos(heading), math.sin(heading)]) * k * 0.8 * px_per_m
        pos = pos + np.array([-math.sin(heading), math.cos(heading)]) * side * 0.12 * px_per_m
        prints.append(("theropod", 0.32 * px_per_m, pos % N, heading))
    h2 = heading + rng.uniform(1.5, 2.5)
    s2 = (start + np.array([N * 0.45, N * 0.3])) % N
    for k in range(2):
        side = 1 if k % 2 else -1
        pos = s2 + np.array([math.cos(h2), math.sin(h2)]) * k * 0.9 * px_per_m
        pos = pos + np.array([-math.sin(h2), math.cos(h2)]) * side * 0.22 * px_per_m
        prints.append(("hadrosaur", 0.42 * px_per_m, pos % N, h2))
    for k in range(14):
        h3 = rng.uniform(0, 2 * np.pi)
        prints.append(("small", 0.075 * px_per_m, rng.uniform(0, N, 2), h3))
    fresh = np.zeros((N, N))
    big = [(pos, L) for kind, L, pos, hd in prints if kind != "small"]
    for kind, L, pos, hd in prints:
        if kind == "small" and any(np.linalg.norm(((pos - q + N / 2) % N) - N / 2) < Lq * 0.9 for q, Lq in big):
            continue  # keep small tracks off the large prints
        sdf = _footprint_sdf(kind, L)
        rot = hd + np.pi / 2  # print axis (-y local) -> heading
        ca, sa = math.cos(rot), math.sin(rot)
        depth = (0.9 if kind != "small" else 0.35)
        r = int(L * 1.1)

        def fn(dx, dy, sdf=sdf, ca=ca, sa=sa, depth=depth, L=L, kind=kind):
            lx = dx * ca + dy * sa
            ly = -dx * sa + dy * ca
            d = sdf(lx, ly)
            wall = L * (0.10 if kind != "small" else 0.05)
            inside = tg.smoothstep(0.0, wall, -d)
            front = 0.75 + 0.5 * np.clip(-ly / L, -0.3, 0.7)  # toes pressed deeper than the heel
            rim = np.exp(-((d - L * 0.045) / (L * 0.05)) ** 2) * (d > -L * 0.01)
            return depth * (-inside * front + 0.3 * rim)
        d = stamp_add(H, pos[0], pos[1], r, fn)
        stamp_add(fresh, pos[0], pos[1], r, lambda dx, dy, d=d: np.clip(-d, 0, None))
    # water ponding only in the deepest parts of the prints
    lev = np.quantile(H, 0.006)
    water = tg.blur((H < lev).astype(float), 1.2)
    wet = tg.smoothstep(0.3, 0.8, sheen)
    col = col * (1 - 0.25 * wet[..., None])
    col = col * (1 - 0.25 * tg.smoothstep(0.0, 0.6, fresh)[..., None])
    col = col * (1 - water[..., None] * 0.45) + np.array([0.17, 0.16, 0.12]) * water[..., None] * 0.45
    rough = 0.55 - 0.3 * wet - 0.45 * water + 0.15 * (1 - n2)
    H = np.where(water > 0.5, np.maximum(H, lev), H)
    return finish("RiverbankMudTracks", col, H, rough, 14.0)


def point_bar_sand(seed):
    rng = np.random.default_rng(seed)
    col, H, n2 = soil_base(seed, (0.62, 0.56, 0.44), var=0.12, hamp=0.15)
    warp = (tg.fft_noise((N, N), seed + 3, beta=2.5, lo=1, hi=5) - 0.5) * 1.6
    rip = lattice_wave((15, 3), warp, amp=1.0, asym=0.25)
    rip2 = lattice_wave((-4, 21), warp * 0.5, amp=0.25)
    grain = tg.fft_noise((N, N), seed + 4, beta=0.0, lo=300)
    H = H + 0.9 * rip + 0.15 * rip2 + 0.08 * grain
    trough = 1 - tg.smoothstep(0.05, 0.35, rip)
    org = trough * tg.smoothstep(0.45, 0.75, tg.fft_noise((N, N), seed + 8, beta=2.0, lo=4, hi=40))
    col = col * (0.95 + 0.1 * grain[..., None]) * (0.96 + 0.06 * rip[..., None])
    col = col * (1 - 0.6 * org[..., None]) + np.array([0.18, 0.14, 0.10]) * 0.6 * org[..., None]
    heavy = tg.smoothstep(0.7, 0.9, tg.fft_noise((N, N), seed + 9, beta=1.5, lo=20, hi=200)) * trough
    col = col * (1 - 0.4 * heavy[..., None]) + np.array([0.25, 0.22, 0.2]) * 0.4 * heavy[..., None]
    R = 0.92 - 0.05 * org
    for _ in range(160):
        x, y = rng.uniform(0, N, 2)
        rx = rng.uniform(4, 14)
        c = np.array([rng.uniform(0.35, 0.65)] * 3) * np.array([1.0, 0.95, 0.88]) * rng.uniform(0.7, 1.1)
        stamp_max(H, col, R, x, y, int(rx * 1.3) + 2, pebble_fn(rx, rx * rng.uniform(0.6, 0.9), rng.uniform(0, 3.14),
                                                                   H.max() * 0.6 + rx * 0.02, c, 0.7, rng))
    return finish("PointBarSand", col, H, R, 9.0)


def _litter_canvas(seed, soil):
    cv = Canvas(N, N, ss=1, seed=seed)
    col, H, _ = soil_base(seed, soil, var=0.3, hamp=0.6)
    cv.col[:] = col
    cv.height[:] = H * 0.5
    cv.alpha[:] = 1
    cv.rough[:] = 0.85
    return cv


def _wrapped(cv, x, y, r, draw):
    for dx, dy in _wrap_offsets(x, y, r):
        draw(x + dx, y + dy)


def leaf_litter_broadleaf(seed):
    rng = np.random.default_rng(seed)
    cv = _litter_canvas(seed, (0.20, 0.15, 0.10))
    palette = [(0.42, 0.30, 0.17), (0.33, 0.22, 0.13), (0.50, 0.38, 0.20), (0.26, 0.19, 0.12), (0.45, 0.40, 0.25), (0.34, 0.33, 0.18)]
    px_per_m = N / MATERIALS["LeafLitterBroadleaf"][0]
    for i in range(1500):
        x, y = rng.uniform(0, N, 2)
        c = np.array(palette[rng.integers(len(palette))]) * rng.uniform(0.75, 1.15)
        L = rng.uniform(0.06, 0.16) * px_per_m
        a = rng.uniform(0, 2 * np.pi)
        shape = rng.choice(["elliptic", "ovate", "lanceolate"])
        wr = {"elliptic": 0.42, "ovate": 0.55, "lanceolate": 0.22}[shape]
        _wrapped(cv, x, y, L * 1.2, lambda X, Y: cv.leaf((X, Y), a, L, L * wr, c, rng, shape=shape, peak=0.4, tip_power=1.2,
                                                       veins=7, vein_angle=55, damage=0.5, necrosis=rng.uniform(0, 0.6),
                                                       transl=0, rough=0.75, dome=0.25, curl=rng.normal(0, 0.15), edge_dark=0.25))
    for i in range(45):  # twigs
        x, y = rng.uniform(0, N, 2)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(0.1, 0.4) * px_per_m
        p1 = (x + math.cos(a) * L, y + math.sin(a) * L)
        w = rng.uniform(4, 10)
        _wrapped(cv, x, y, L + 10, lambda X, Y: cv.stroke([(X, Y), (X + math.cos(a) * L, Y + math.sin(a) * L)], w, w * 0.6,
                                                         (0.30, 0.24, 0.17), height=1.0, rough=0.8))
    return finish("LeafLitterBroadleaf", cv.col, cv.height, cv.rough, 10.0)


def leaf_litter_conifer(seed):
    rng = np.random.default_rng(seed)
    cv = _litter_canvas(seed, (0.24, 0.16, 0.10))
    px_per_m = N / MATERIALS["LeafLitterConifer"][0]
    rust = [(0.52, 0.30, 0.15), (0.44, 0.26, 0.14), (0.36, 0.22, 0.13), (0.58, 0.38, 0.20), (0.30, 0.20, 0.12)]
    for i in range(1500):  # shed deciduous shoots with two ranks of needles
        x, y = rng.uniform(0, N, 2)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(0.05, 0.12) * px_per_m
        c = np.array(rust[rng.integers(len(rust))]) * rng.uniform(0.8, 1.15)
        nl = 0.018 * px_per_m * rng.uniform(0.8, 1.2)

        def draw(X, Y, a=a, L=L, c=c, nl=nl):
            d = np.array([math.cos(a), math.sin(a)])
            cv.stroke([(X, Y), (X + d[0] * L, Y + d[1] * L)], 2.5, 1.5, c * 0.8, height=0.5, rough=0.8)
            for k in range(int(L / 5)):
                q = np.array([X, Y]) + d * (k + 0.5) * 5
                for s in (-1, 1):
                    cv.leaf(q, a + s * math.radians(rng.uniform(65, 85)), nl, 3.0, c, rng, shape="linear", veins=0, midrib=0.5,
                            dome=0.2, rough=0.75, transl=0)
        _wrapped(cv, x, y, L + nl + 4, draw)
    for i in range(7000):  # loose needles
        x, y = rng.uniform(0, N, 2)
        c = np.array(rust[rng.integers(len(rust))]) * rng.uniform(0.7, 1.1)
        a = rng.uniform(0, 2 * np.pi)
        nl = 0.02 * px_per_m * rng.uniform(0.7, 1.2)
        _wrapped(cv, x, y, nl + 3, lambda X, Y: cv.leaf((X, Y), a, nl, 3.0, c, rng, shape="linear", veins=0, midrib=0.4,
                                                        dome=0.2, rough=0.75, transl=0))
    for i in range(40):  # small globose cones
        x, y = rng.uniform(0, N, 2)
        r = rng.uniform(0.008, 0.013) * px_per_m

        def cone(X, Y, r=r):
            cv.ellipse((X, Y), r, r * rng.uniform(0.85, 1.0), 0, (0.30, 0.20, 0.12), height=2.0, rough=0.8)
            for k in range(6):
                aa = k * 1.05 + rng.uniform(0, 0.3)
                cv.ellipse((X + math.cos(aa) * r * 0.45, Y + math.sin(aa) * r * 0.45), r * 0.32, r * 0.25, aa,
                           (0.38, 0.26, 0.15), height=2.2, rough=0.8)
        _wrapped(cv, x, y, r + 4, cone)
    return finish("LeafLitterConifer", cv.col, cv.height, cv.rough, 10.0)


def backswamp_mud(seed):
    rng = np.random.default_rng(seed)
    col, H, n2 = soil_base(seed, (0.16, 0.13, 0.10), var=0.3, hamp=0.8)
    fib = tg.fft_noise((N, N), seed + 2, beta=1.0, lo=40, hi=400, aniso=(3.0, 1.0))
    H = H + 0.2 * fib
    lev = np.quantile(H, 0.30)
    water = tg.smoothstep(0.0, 0.02, lev - H)
    col = col * (1 - water[..., None] * 0.55) + np.array([0.06, 0.06, 0.04]) * water[..., None] * 0.55
    R = 0.6 - 0.5 * water + 0.15 * n2
    cv = Canvas(N, N, ss=1, seed=seed + 1)  # floating / embedded plant debris
    cv.col[:] = col
    cv.height[:] = np.maximum(H, lev)
    cv.rough[:] = R
    cv.alpha[:] = 1
    for i in range(500):
        x, y = rng.uniform(0, N, 2)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(15, 90)
        c = np.array([0.25, 0.19, 0.12]) * rng.uniform(0.6, 1.2)
        if rng.random() < 0.6:
            _wrapped(cv, x, y, L + 4, lambda X, Y: cv.stroke([(X, Y), (X + math.cos(a) * L, Y + math.sin(a) * L)],
                                                           rng.uniform(2, 5), 1.5, c, height=0.3, rough=0.7))
        else:
            _wrapped(cv, x, y, L + 4, lambda X, Y: cv.leaf((X, Y), a, L, L * 0.4, c, rng, shape="elliptic", veins=5,
                                                         dome=0.1, rough=0.6, necrosis=0.5, transl=0))
    return finish("BackswampOrganicMud", cv.col, cv.height, cv.rough, 8.0)


def cutbank_strata(seed):
    """Vertical exposure, V up = stratigraphic up. Layers repeat exactly every tile (4 m): drab sandstone lenses,
    siltstones, mudstones with a cracked weathering crust, thin carbonaceous and ironstone beds, rills."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:N, 0:N]
    v = 1 - (yy + 0.5) / N  # 0 bottom .. 1 top of the tile
    types = {"sand": (0.60, 0.54, 0.42), "silt": (0.53, 0.49, 0.41), "mud": (0.47, 0.45, 0.41),
             "mudgrey": (0.42, 0.42, 0.40), "carb": (0.20, 0.18, 0.15), "iron": (0.50, 0.34, 0.22)}
    hard = {"sand": 1.0, "silt": 0.6, "mud": 0.25, "mudgrey": 0.3, "carb": 0.35, "iron": 1.15}
    seq, total = [], 0.0
    while total < 1.0:
        t = rng.choice(list(types), p=[0.27, 0.25, 0.22, 0.14, 0.07, 0.05])
        th = {"sand": rng.uniform(0.05, 0.14), "silt": rng.uniform(0.02, 0.07), "mud": rng.uniform(0.04, 0.11),
              "mudgrey": rng.uniform(0.03, 0.08), "carb": rng.uniform(0.006, 0.02), "iron": rng.uniform(0.004, 0.012)}[str(t)]
        seq.append((str(t), total, min(1.0, total + th), rng.uniform(0.88, 1.1) * np.array([1.0, 1.0, 1.0]) + rng.uniform(-0.02, 0.02) * np.array([1, 0, -1])))
        total += th
    w_lo = (tg.fft_noise((N, N), seed, beta=3.0, lo=1, hi=3) - 0.5) * 0.05     # undulating, thickening / thinning beds
    w_hi = (tg.fft_noise((N, N), seed + 9, beta=2.0, lo=6, hi=40) - 0.5) * 0.006
    vv = (v + w_lo + w_hi) % 1.0
    lam = tg.fft_noise((N, N), seed + 1, beta=1.0, lo=60, hi=600, aniso=(8.0, 1.0))
    rills = tg.fft_noise((N, N), seed + 2, beta=2.0, lo=6, hi=90, aniso=(1.0, 12.0))
    crust = tg.worley((N, N), seed + 3, 1400, periodic=True, metric="f2-f1")
    blocky = tg.worley((N, N), seed + 4, 160, periodic=True, metric="f2-f1", jitter_aspect=2.0)
    mott = tg.fbm((N, N), seed + 5, octaves=((8, 1.0), (32, 0.6), (128, 0.3)))
    col = np.zeros((N, N, 3))
    H = np.zeros((N, N))
    soft = np.zeros((N, N))
    iron = np.zeros((N, N))
    carb = np.zeros((N, N))
    for t, a0, b0, jit in seq:
        m = tg.smoothstep(-0.0015, 0.0015, vv - a0) * (1 - tg.smoothstep(-0.0015, 0.0015, vv - b0))
        c = np.array(types[t]) * jit
        lay = np.broadcast_to(c, (N, N, 3)) * (0.9 + 0.14 * lam[..., None]) * (0.92 + 0.16 * mott[..., None])
        if t == "sand":
            xb = (xx / N * 7 + vv * 25 + 0.3 * w_lo * 50) % 1.0  # inclined cross-bed laminae
            lay = lay * (0.94 + 0.08 * np.sin(2 * np.pi * xb * 3)[..., None])
        col += lay * m[..., None]
        edge = tg.smoothstep(0.0, 0.006, np.minimum(vv - a0, b0 - vv))
        H += m * hard[t] * (0.8 + 0.2 * edge)
        if t in ("mud", "mudgrey", "silt"):
            soft += m
        if t in ("sand", "iron"):
            H += m * 0.18 * tg.smoothstep(0, 12, blocky)
        if t == "iron":
            iron += m
        if t == "carb":
            carb += m
    cr = 1 - tg.smoothstep(0.0, 2.5, crust)
    H += soft * (0.12 * tg.smoothstep(0, 6, crust) - 0.1 * cr)
    col = col * (1 - 0.25 * (soft * cr)[..., None])
    H -= (0.25 + 0.5 * soft) * rills ** 2
    # stains washed downwards from ironstone and carbonaceous beds (blur along V only)
    from scipy import ndimage
    down = lambda a, s: ndimage.gaussian_filter1d(ndimage.gaussian_filter1d(a, s, axis=0, mode="wrap"), 2, axis=1, mode="wrap")
    st_i = down(iron, 40) * tg.smoothstep(0.4, 0.8, rills)
    st_c = down(carb, 25) * tg.smoothstep(0.5, 0.85, rills)
    col = col * (1 - 0.35 * st_i[..., None]) + np.array([0.48, 0.30, 0.18]) * 0.35 * st_i[..., None]
    col = col * (1 - 0.3 * st_c[..., None]) + np.array([0.22, 0.20, 0.18]) * 0.3 * st_c[..., None]
    col = col * (0.8 + 0.25 * (1 - rills ** 2)[..., None])
    H = tg.blur(H, 1.0) + 0.06 * lam
    R = 0.92 - 0.06 * (H - H.min()) / max(np.ptp(H), 1e-6)
    return finish("CutbankStrata", col, H, R, 18.0)


def river_gravel(seed):
    rng = np.random.default_rng(seed)
    col, H, n2 = soil_base(seed, (0.45, 0.42, 0.36), var=0.15, hamp=0.2)
    R = np.full((N, N), 0.85)
    px_per_m = N / MATERIALS["RiverGravel"][0]
    lith = [(0.55, 0.52, 0.47), (0.40, 0.37, 0.33), (0.62, 0.55, 0.45), (0.30, 0.28, 0.26), (0.58, 0.45, 0.35), (0.48, 0.47, 0.44)]
    for i in range(2600):
        x, y = rng.uniform(0, N, 2)
        rx = rng.lognormal(math.log(0.018 * px_per_m), 0.45)
        rx = min(rx, 0.07 * px_per_m)
        ry = rx * rng.uniform(0.55, 0.95)
        c = np.array(lith[rng.integers(len(lith))]) * rng.uniform(0.8, 1.15)
        top = rng.uniform(0.6, 1.0) * rx * 0.05 + 0.3
        stamp_max(H, col, R, x, y, int(rx) + 2, pebble_fn(rx, ry, rng.uniform(0, 3.14), top, c, 0.6, rng, flat=0.5))
    algae = tg.smoothstep(0.5, 0.8, tg.fft_noise((N, N), seed + 7, beta=2.0, lo=3, hi=40))
    col = col * (1 - 0.35 * algae[..., None]) + np.array([0.22, 0.27, 0.12]) * 0.35 * algae[..., None]
    gap = H < np.quantile(H, 0.25)
    col[gap] *= 0.6
    R = R - 0.25 * algae
    return finish("RiverGravel", col, H, R, 12.0)


BUILDERS = {
    "FloodplainSilt": floodplain_silt, "RiverbankMudTracks": riverbank_mud, "PointBarSand": point_bar_sand,
    "LeafLitterBroadleaf": leaf_litter_broadleaf, "LeafLitterConifer": leaf_litter_conifer,
    "BackswampOrganicMud": backswamp_mud, "CutbankStrata": cutbank_strata, "RiverGravel": river_gravel,
}


def run(names=None, force=False):
    common.ensure_dirs()
    out = {}
    for i, name in enumerate(names or BUILDERS):
        p = os.path.join(common.GROUND_DIR, "T_Ground_%s_BaseColor.png" % name)
        if os.path.exists(p) and not force:
            out[name] = "cached"
            continue
        t0 = time.time()
        BUILDERS[name](1000 + 17 * i)
        out[name] = round(time.time() - t0, 1)
        common.log("ground %s: %.1f s" % (name, out[name]))
    return out


def material_info():
    return {k: dict(tile_m=v[0], usage=v[1], description_fr=v[2],
                    textures={m: os.path.relpath(os.path.join(common.GROUND_DIR, "T_Ground_%s_%s.png" % (k, m)), common.ROOT)
                              for m in ("BaseColor", "Normal", "Roughness", "Height")})
            for k, v in MATERIALS.items()}
