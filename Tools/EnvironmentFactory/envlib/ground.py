"""Stage 'ground': tileable 2K ground texture sets (BaseColor, Normal (DirectX), Roughness, Height 16 bit), the
trackway decal atlas and the low-frequency macro-variation texture of the landscape material.

Every map tiles exactly: periodic FFT noise, periodic Worley cells, integer-lattice ripple directions, and
discrete elements (leaves, needles, pebbles) stamped with wrap-around. Height is the master field
(displacement / parallax / landscape height blending); the normal map is derived from it.

Anti-repetition rules (review of the first version): tiles are 4-6 m wide (texel ~2-3 mm), and NO memorable
discrete feature (footprints, a trackway, a big crack polygon pattern over the whole tile) is baked into a
tiling set: footprints are separate decals (T_Decal_Trackways_*, placed along corridors and banks), strong
desiccation cracks are their own layer (FloodplainSiltDry, drying hollows only). The landscape material adds
dual-scale / rotated sampling and an independent macro-noise texture (T_Ground_MacroNoise).

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
    "FloodplainSilt": (6.0, "floodplain_open, levee_woodland ; substrats silt/clay",
                       "Limon humide de plaine d'inondation : débris de fougères et d'herbacées, croûtes de mousses / hépatiques, fentes rares et discrètes"),
    "FloodplainSiltDry": (5.0, "creux de plaine en cours d'assèchement (humidité initiale faible) ; substrats silt/clay",
                          "Limon desséché à fentes de dessiccation polygonales (mares asséchées, cônes de débordement)"),
    "RiverbankMud": (5.0, "riverbank, sentiers ; substrat mud",
                     "Boue de berge humide : rigoles, flaques, débris organiques (sans empreintes : décalques séparés)"),
    "PointBarSand": (6.0, "sandbar, chenal peu profond ; substrat sand",
                     "Sable de barre de méandre à rides de courant de longueur d'onde et d'orientation variables, débris végétaux dans les creux"),
    "LeafLitterBroadleaf": (4.0, "riparian_forest, levee_woodland ; substrat forest_soil", "Litière de feuilles de dicotylédones et brindilles"),
    "LeafLitterConifer": (4.0, "upland_conifer_forest, backswamp boisé ; forest_soil/peat", "Litière de rameaux caducs et aiguilles de Cupressaceae, petits cônes"),
    "BackswampOrganicMud": (5.0, "backswamp ; substrats peat/mud/clay", "Vase organique de marais arrière, eau stagnante, débris végétaux"),
    "CutbankStrata": (4.0, "cutbank_cliff (affleurements) ; substrats rock/clay",
                      "Coupe de berge : alternance grès / silts / argiles / niveaux carbonés (projection biplanaire, V = altitude)"),
    "RiverGravel": (4.0, "channel ; substrat gravel", "Graviers et galets roulés du fond du chenal, matrice sableuse, film d'algues"),
}
MACRO_NOISE = "T_Ground_MacroNoise.png"   # RGB, tileable; landscape: UV = world / MACRO_TILE_M
MACRO_TILE_M = 240.0
DECAL_PREFIX = "Decal_Trackways"          # T_Decal_Trackways_{BaseColor,Normal,Roughness,Opacity,Height}.png
DECAL_GRID = (4, 1)
DECAL_CELL_M = (0.75, 3.0)
DECALS = [  # cell -> (UE mesh name, description FR)
    ("SM_Decal_Trackway_Theropod", "piste de grand théropode (4 empreintes tridactyles, pas ~0,85 m)"),
    ("SM_Decal_Trackway_Hadrosaur", "piste d'hadrosaure (3 empreintes larges à doigts arrondis)"),
    ("SM_Decal_Trackway_SmallTridactyl", "traces de petits tridactyles (oiseaux / petits théropodes) entrecroisées"),
    ("SM_Decal_Trackway_Trampled", "sol piétiné : empreintes superposées de tailles variées"),
]


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
def _ppm(name):
    return N / MATERIALS[name][0]


def _debris(H, col, R, rng, count, ppm, length_m=(0.02, 0.10), width_m=(0.002, 0.005), colors=((0.32, 0.25, 0.17),),
            height=0.6, rough=0.8):
    """Thin plant fragments (stems, rachis bits, twigs) stamped with wrap-around."""
    for _ in range(int(count)):
        x, y = rng.uniform(0, N, 2)
        L = rng.uniform(*length_m) * ppm
        a = rng.uniform(0, np.pi)
        w = max(0.8, rng.uniform(*width_m) * ppm)
        c = np.array(colors[rng.integers(len(colors))]) * rng.uniform(0.6, 1.15)

        def fn(dx, dy, a=a, L=L, w=w, c=c):
            u = dx * math.cos(a) + dy * math.sin(a)
            v = -dx * math.sin(a) + dy * math.cos(a)
            m = np.clip(w * 0.5 - np.abs(v) + 0.5, 0, 1) * np.clip(L / 2 - np.abs(u) + 0.5, 0, 1)
            return height + 0 * u, m, np.broadcast_to(c, dx.shape + (3,)), np.full(dx.shape, rough)
        stamp_max(H, col, R, x, y, int(L / 2 + w + 3), fn)


def floodplain_silt(seed):
    """Humid Maastrichtian floodplain floor: damp silt partly covered by fern / dicot-herb debris, dark moss and
    liverwort crusts and algal films; desiccation cracks only as faint, patchy hairlines (strong cracks are the
    separate FloodplainSiltDry layer)."""
    rng = np.random.default_rng(seed)
    ppm = _ppm("FloodplainSilt")
    col, H, n2 = soil_base(seed, (0.40, 0.35, 0.27), var=0.22, hamp=0.35)
    damp = tg.fbm((N, N), seed + 3, octaves=((4, 1.0), (12, 0.6), (40, 0.3)))
    col = col * (0.82 + 0.25 * damp[..., None])
    # faint hairline cracks in a few patches only (~12 % of the tile)
    patch = tg.smoothstep(0.62, 0.78, tg.fft_noise((N, N), seed + 4, beta=2.5, lo=2, hi=6))
    cells = tg.worley((N, N), seed + 2, int((MATERIALS["FloodplainSilt"][0] / 0.32) ** 2), periodic=True, metric="f2-f1")
    crack = (1 - tg.smoothstep(0.0, 2.2, cells)) * patch
    H = H - 0.35 * crack
    col = col * (1 - 0.35 * crack[..., None])
    R = 0.86 - 0.12 * (1 - damp) * 0.5
    # moss / liverwort crusts and algal film: dark olive, slightly raised, lumpy
    crust_n = tg.fbm((N, N), seed + 5, octaves=((16, 1.0), (64, 0.7), (256, 0.5)))
    crust = tg.smoothstep(0.58, 0.74, crust_n * (0.8 + 0.4 * tg.fft_noise((N, N), seed + 6, beta=2.0, lo=2, hi=8)))
    lump = tg.fft_noise((N, N), seed + 7, beta=0.5, lo=200, hi=700)
    crust = crust * (0.55 + 0.45 * lump)  # patchy, partly covering patina rather than opaque blobs
    cc = np.array([0.21, 0.22, 0.12]) * (0.8 + 0.35 * lump[..., None])
    col = col * (1 - crust[..., None]) + cc * crust[..., None]
    H = H + crust * (0.25 + 0.2 * lump)
    R = R * (1 - 0.15 * crust)
    alg = tg.smoothstep(0.6, 0.8, tg.fft_noise((N, N), seed + 8, beta=2.0, lo=3, hi=20)) * (1 - crust)
    col = col * (1 - 0.25 * alg[..., None]) + np.array([0.16, 0.18, 0.10]) * 0.25 * alg[..., None]
    R = np.asarray(R, float) * np.ones((N, N))
    # fern pinna fragments and dicot herb leaves (dead, partly decayed)
    cv = Canvas(N, N, ss=1, seed=seed + 9)
    cv.col[:] = col
    cv.height[:] = H * 0.5
    cv.rough[:] = R
    cv.alpha[:] = 1
    browns = [(0.30, 0.23, 0.14), (0.26, 0.20, 0.13), (0.33, 0.27, 0.16), (0.24, 0.22, 0.13), (0.28, 0.27, 0.15)]
    area = MATERIALS["FloodplainSilt"][0] ** 2
    for _ in range(int(55 * area)):
        x, y = rng.uniform(0, N, 2)
        c = np.array(browns[rng.integers(len(browns))]) * rng.uniform(0.7, 1.1)
        a = rng.uniform(0, 2 * np.pi)
        if rng.random() < 0.55:   # fern pinna fragment: short rachis with a few small pinnae
            L = rng.uniform(0.03, 0.08) * ppm

            def draw(X, Y, a=a, L=L, c=c):
                d = np.array([math.cos(a), math.sin(a)])
                cv.stroke([(X, Y), (X + d[0] * L, Y + d[1] * L)], 1.6, 1.0, c * 0.8, height=0.4, rough=0.8)
                for t in np.linspace(0.15, 0.95, 5):
                    q = np.array([X, Y]) + d * L * t
                    for sd in (-1, 1):
                        cv.leaf(q, a + sd * 1.1, L * 0.22 * (1.1 - t * 0.5), L * 0.09, c, rng, shape="elliptic", veins=0,
                                midrib=0.3, dome=0.15, rough=0.8, transl=0)
            _wrapped(cv, x, y, L + 6, draw)
        else:                     # small dicot herb leaf
            L = rng.uniform(0.025, 0.07) * ppm
            _wrapped(cv, x, y, L * 1.2, lambda X, Y, a=a, L=L, c=c: cv.leaf((X, Y), a, L, L * 0.5, c, rng, shape="ovate", peak=0.35,
                                                                           veins=5, vein_angle=50, damage=0.5,
                                                                           necrosis=rng.uniform(0.2, 0.8), transl=0,
                                                                           rough=0.8, dome=0.2, edge_dark=0.2))
    H2 = cv.height
    col2 = cv.col
    R2 = cv.rough
    _debris(H2, col2, R2, rng, 18 * area, ppm, length_m=(0.03, 0.12), width_m=(0.002, 0.004))
    return finish("FloodplainSilt", col2, H2, R2, 9.0)


def floodplain_silt_dry(seed):
    """Drying hollow / crevasse-splay silt: polygonal desiccation cracks with curled edges, sparse debris."""
    rng = np.random.default_rng(seed)
    ppm = _ppm("FloodplainSiltDry")
    col, H, n2 = soil_base(seed, (0.50, 0.44, 0.35), var=0.2, hamp=0.25)
    # several crack generations: primary polygons ~0.35 m, secondary ~0.12 m in part of the cells
    cells = tg.worley((N, N), seed + 2, int((MATERIALS["FloodplainSiltDry"][0] / 0.38) ** 2), periodic=True, metric="f2-f1")
    sec = tg.worley((N, N), seed + 3, int((MATERIALS["FloodplainSiltDry"][0] / 0.13) ** 2), periodic=True, metric="f2-f1")
    sec_on = tg.smoothstep(0.45, 0.6, tg.fft_noise((N, N), seed + 4, beta=2.0, lo=3, hi=12))
    crack = 1 - tg.smoothstep(0.0, 3.5 + 2.5 * n2, cells)
    crack2 = (1 - tg.smoothstep(0.0, 1.6, sec)) * sec_on * 0.7
    curl = tg.smoothstep(0.0, 25.0, cells)
    H = H + 0.35 * (1 - curl) * (1 - crack) - 0.8 * crack - 0.35 * crack2
    col = col * (1 - 0.55 * crack[..., None] - 0.3 * crack2[..., None]) + np.array([0.18, 0.15, 0.12]) * 0.55 * crack[..., None]
    dry = tg.smoothstep(0.4, 0.8, n2)
    col = col * (0.92 + 0.16 * dry[..., None])
    R = 0.9 + 0.06 * dry - 0.1 * crack
    _debris(H, col, R, rng, 14 * MATERIALS["FloodplainSiltDry"][0] ** 2, ppm)
    return finish("FloodplainSiltDry", col, H, R, 10.0)


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
    """Wet bank mud WITHOUT footprints (tracks are decals): runnels down the bank, small slumps, puddles in the
    lowest hollows, organic flecks and a few pebbles."""
    rng = np.random.default_rng(seed)
    ppm = _ppm("RiverbankMud")
    col, H, n2 = soil_base(seed, (0.30, 0.26, 0.20), var=0.25, hamp=0.5)
    sheen = tg.fft_noise((N, N), seed + 5, beta=2.0, lo=3, hi=30)
    rill = tg.fft_noise((N, N), seed + 6, beta=1.5, lo=6, hi=80, aniso=(6.0, 1.0))  # runnels elongated across u
    H = H + 0.25 * tg.fft_noise((N, N), seed + 7, beta=1.0, lo=60, hi=500) - 0.35 * tg.smoothstep(0.55, 0.85, rill)
    slump = tg.worley((N, N), seed + 8, 30, periodic=True, metric="f2-f1")
    H = H + 0.2 * tg.smoothstep(0.0, 60.0, slump)
    lev = np.quantile(H, 0.012)
    water = tg.blur((H < lev).astype(float), 1.5)
    wet = tg.smoothstep(0.3, 0.8, sheen)
    col = col * (1 - 0.25 * wet[..., None])
    col = col * (1 - water[..., None] * 0.45) + np.array([0.17, 0.16, 0.12]) * water[..., None] * 0.45
    R = 0.55 - 0.3 * wet - 0.45 * water + 0.15 * (1 - n2)
    H = np.where(water > 0.5, np.maximum(H, lev), H)
    R = np.asarray(R, float)
    _debris(H, col, R, rng, 16 * MATERIALS["RiverbankMud"][0] ** 2, ppm, colors=((0.22, 0.18, 0.12), (0.30, 0.24, 0.15)), height=0.3)
    for _ in range(int(3 * MATERIALS["RiverbankMud"][0] ** 2)):
        x, y = rng.uniform(0, N, 2)
        rx = rng.uniform(0.006, 0.02) * ppm
        c = np.array([rng.uniform(0.3, 0.5)] * 3) * np.array([1.0, 0.95, 0.88])
        stamp_max(H, col, R, x, y, int(rx * 1.3) + 2, pebble_fn(rx, rx * rng.uniform(0.6, 0.9), rng.uniform(0, 3.14),
                                                                  H.max() * 0.5, c, 0.6, rng))
    return finish("RiverbankMud", col, H, R, 12.0)


def point_bar_sand(seed):
    """Point-bar sand: current ripples (~0.12 m) whose orientation and wavelength drift across the tile (three
    ripple trains blended by a low-frequency mask), organic drapes in the troughs, scattered pebbles."""
    rng = np.random.default_rng(seed)
    ppm = _ppm("PointBarSand")
    tile = MATERIALS["PointBarSand"][0]
    col, H, n2 = soil_base(seed, (0.62, 0.56, 0.44), var=0.12, hamp=0.25)
    # a single ripple train whose phase is strongly domain-warped: crest orientation drifts by ~+-25 deg and the
    # wavelength by ~+-30 % across the tile (crossing trains summed together read as stitches / dashes)
    warp = (tg.fft_noise((N, N), seed + 3, beta=3.0, lo=1, hi=4) - 0.5) * 9.0
    warp = warp + (tg.fft_noise((N, N), seed + 13, beta=2.0, lo=4, hi=12) - 0.5) * 1.2
    cyc = tile / 0.12
    rip = lattice_wave((int(cyc), 7), warp, amp=1.0, asym=0.28)
    # crest breaks (linguoid tendency): fade the crests in short irregular segments
    brk = tg.smoothstep(0.30, 0.55, tg.fft_noise((N, N), seed + 14, beta=1.0, lo=int(cyc * 0.6), hi=int(cyc * 1.6)))
    rip = rip * (0.45 + 0.55 * brk)
    flat = tg.smoothstep(0.55, 0.75, tg.fft_noise((N, N), seed + 12, beta=2.0, lo=2, hi=8))  # smoothed (plane-bed) patches
    rip = rip * (1 - 0.7 * flat)
    grain = tg.fft_noise((N, N), seed + 4, beta=0.0, lo=300)
    H = H + 0.9 * rip + 0.08 * grain
    trough = (1 - tg.smoothstep(0.05, 0.35, rip)) * (1 - flat)
    org = trough * tg.smoothstep(0.45, 0.75, tg.fft_noise((N, N), seed + 8, beta=2.0, lo=4, hi=40))
    col = col * (0.95 + 0.1 * grain[..., None]) * (0.96 + 0.06 * rip[..., None])
    col = col * (1 - 0.6 * org[..., None]) + np.array([0.18, 0.14, 0.10]) * 0.6 * org[..., None]
    heavy = tg.smoothstep(0.7, 0.9, tg.fft_noise((N, N), seed + 9, beta=1.5, lo=20, hi=200)) * trough
    col = col * (1 - 0.4 * heavy[..., None]) + np.array([0.25, 0.22, 0.2]) * 0.4 * heavy[..., None]
    R = 0.92 - 0.05 * org
    for _ in range(int(9 * tile * tile)):
        x, y = rng.uniform(0, N, 2)
        rx = rng.uniform(0.004, 0.014) * ppm
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
    area = MATERIALS["LeafLitterBroadleaf"][0] ** 2
    for i in range(int(375 * area)):
        x, y = rng.uniform(0, N, 2)
        c = np.array(palette[rng.integers(len(palette))]) * rng.uniform(0.75, 1.15)
        L = rng.uniform(0.06, 0.16) * px_per_m
        a = rng.uniform(0, 2 * np.pi)
        shape = rng.choice(["elliptic", "ovate", "lanceolate"])
        wr = {"elliptic": 0.42, "ovate": 0.55, "lanceolate": 0.22}[shape]
        _wrapped(cv, x, y, L * 1.2, lambda X, Y: cv.leaf((X, Y), a, L, L * wr, c, rng, shape=shape, peak=0.4, tip_power=1.2,
                                                       veins=7, vein_angle=55, damage=0.5, necrosis=rng.uniform(0, 0.6),
                                                       transl=0, rough=0.75, dome=0.25, curl=rng.normal(0, 0.15), edge_dark=0.25))
    for i in range(int(11 * area)):  # twigs
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
    area = MATERIALS["LeafLitterConifer"][0] ** 2
    for i in range(int(375 * area)):  # shed deciduous shoots with two ranks of needles
        x, y = rng.uniform(0, N, 2)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(0.05, 0.12) * px_per_m
        c = np.array(rust[rng.integers(len(rust))]) * rng.uniform(0.8, 1.15)
        nl = 0.018 * px_per_m * rng.uniform(0.8, 1.2)

        def draw(X, Y, a=a, L=L, c=c, nl=nl):
            d = np.array([math.cos(a), math.sin(a)])
            cv.stroke([(X, Y), (X + d[0] * L, Y + d[1] * L)], 2.5, 1.5, c * 0.8, height=0.5, rough=0.8)
            step = max(2.5, 0.0025 * px_per_m)
            for k in range(int(L / step)):
                q = np.array([X, Y]) + d * (k + 0.5) * step
                for s in (-1, 1):
                    cv.leaf(q, a + s * math.radians(rng.uniform(65, 85)), nl, 3.0, c, rng, shape="linear", veins=0, midrib=0.5,
                            dome=0.2, rough=0.75, transl=0)
        _wrapped(cv, x, y, L + nl + 4, draw)
    for i in range(int(1750 * area)):  # loose needles
        x, y = rng.uniform(0, N, 2)
        c = np.array(rust[rng.integers(len(rust))]) * rng.uniform(0.7, 1.1)
        a = rng.uniform(0, 2 * np.pi)
        nl = 0.02 * px_per_m * rng.uniform(0.7, 1.2)
        _wrapped(cv, x, y, nl + 3, lambda X, Y: cv.leaf((X, Y), a, nl, 3.0, c, rng, shape="linear", veins=0, midrib=0.4,
                                                        dome=0.2, rough=0.75, transl=0))
    for i in range(int(10 * area)):  # small globose cones
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
    fib = tg.fft_noise((N, N), seed + 2, beta=1.0, lo=100, hi=1000, aniso=(3.0, 1.0))
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
    ppm = _ppm("BackswampOrganicMud")
    for i in range(int(125 * MATERIALS["BackswampOrganicMud"][0] ** 2)):
        x, y = rng.uniform(0, N, 2)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(0.015, 0.09) * ppm
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
    for i in range(int(1150 * MATERIALS["RiverGravel"][0] ** 2)):
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


# ----------------------------------------------------------------------------------------------- decals / macro
def trackway_decals(seed=4242):
    """Trackway decal atlas (4 cells of 0.75 x 3 m, 512 x 2048 px): theropod trackway, hadrosaur trackway, small
    tridactyl traces, trampled patch. Opacity = prints + a soft halo, so the decal lays wet, darker mud and the
    print relief (normal / height) over whatever ground layer lies below. Generic silhouettes for game reading,
    not casts of particular ichnotaxa (design assumption)."""
    rng = np.random.default_rng(seed)
    cols, rows = DECAL_GRID
    cw, ch = N // cols, N // rows
    ppm = ch / DECAL_CELL_M[1]
    Hh = np.zeros((N, N))
    Op = np.zeros((N, N))
    Wt = np.zeros((N, N))  # standing water in the deepest toe impressions
    In = np.zeros((N, N))  # inside the prints (the rims are pushed-up, drier mud)

    def stamp(cell, kind, L_m, x_m, y_m, yaw_deg, depth=1.0):
        L = L_m * ppm
        x0 = cell * cw + x_m * ppm
        y0 = ch - y_m * ppm               # y_m measured from the bottom of the cell (walking towards the top)
        sdf = _footprint_sdf(kind, L)
        rot = math.radians(yaw_deg)
        ca, sa = math.cos(rot), math.sin(rot)
        r = int(L * 1.25)
        ys = np.arange(int(y0) - r, int(y0) + r + 1)
        xs = np.arange(int(x0) - r, int(x0) + r + 1)
        ys = ys[(ys >= 0) & (ys < ch)]
        xs = xs[(xs >= cell * cw) & (xs < (cell + 1) * cw)]
        if not len(xs) or not len(ys):
            return
        X, Y = np.meshgrid(xs + 0.5 - x0, ys + 0.5 - y0)
        lx = X * ca + Y * sa
        ly = -X * sa + Y * ca
        d = sdf(lx, ly)
        wall = L * (0.10 if kind != "small" else 0.06)
        inside = tg.smoothstep(0.0, wall, -d)
        front = 0.75 + 0.5 * np.clip(-ly / L, -0.3, 0.7)
        rim = np.exp(-((d - L * 0.05) / (L * 0.055)) ** 2) * (d > -L * 0.01)
        sub = np.ix_(ys, xs)
        Hh[sub] += depth * (-inside * front + 0.3 * rim)
        Op[sub] = np.maximum(Op[sub], np.clip(inside * 1.0 + rim * 0.5 + np.exp(-np.maximum(d, 0) / (L * 0.05)) * 0.12, 0, 1))
        Wt[sub] = np.maximum(Wt[sub], inside * front * (front > 1.0))
        In[sub] = np.maximum(In[sub], inside)

    # 0: theropod trackway, slow walk (pace ~0.82 m), slightly toed-in
    for k in range(4):
        side = 1 if k % 2 else -1
        stamp(0, "theropod", 0.33 * rng.uniform(0.95, 1.05), 0.375 + side * 0.11 + rng.normal(0, 0.01), 0.28 + 0.82 * k,
              side * rng.uniform(4, 10))
    # 1: hadrosaur trackway (pace ~1 m)
    for k in range(3):
        side = 1 if k % 2 else -1
        stamp(1, "hadrosaur", 0.42 * rng.uniform(0.95, 1.05), 0.375 + side * 0.14, 0.42 + 1.0 * k, side * rng.uniform(3, 8), depth=0.8)
    # 2: small tridactyl traces along two meandering paths
    for path in range(2):
        x, y, h = rng.uniform(0.2, 0.55), rng.uniform(0.1, 0.4), rng.uniform(-20, 20)
        while y < 2.9:
            stamp(2, "small", rng.uniform(0.06, 0.09), x, y, h + rng.normal(0, 12), depth=0.45)
            h += rng.normal(0, 14)
            step = rng.uniform(0.22, 0.32)
            x = float(np.clip(x + math.sin(math.radians(h)) * step, 0.08, 0.67))
            y += math.cos(math.radians(h)) * step * 0.9 + 0.04
    # 3: trampled patch (watering point): overlapping prints of several sizes and headings
    for k in range(3):
        stamp(3, "theropod", rng.uniform(0.22, 0.36), rng.uniform(0.18, 0.57), rng.uniform(0.3, 2.7), rng.uniform(-35, 35))
    stamp(3, "hadrosaur", 0.4, rng.uniform(0.25, 0.5), rng.uniform(0.6, 2.4), rng.uniform(-20, 20), depth=0.7)
    for k in range(14):
        stamp(3, "small", rng.uniform(0.05, 0.09), rng.uniform(0.08, 0.67), rng.uniform(0.1, 2.9), rng.uniform(-180, 180), depth=0.4)
    noise = tg.fbm((N, N), seed + 1, octaves=((16, 1.0), (64, 0.5), (256, 0.25)))
    Op = np.clip(Op, 0, 1) * (0.85 + 0.15 * noise)
    Hn = Hh - Hh.min()
    Hn = Hn / max(Hn.max(), 1e-9)
    wet = tg.blur(np.clip(Wt, 0, 1), 1.0, wrap=False)
    inside = tg.blur(np.clip(In, 0, 1), 1.0, wrap=False)[..., None]
    col = (np.array([0.34, 0.30, 0.24]) * (1 - inside) + np.array([0.12, 0.10, 0.08]) * inside) * (0.85 + 0.3 * noise[..., None])
    col = col * (1 - 0.35 * wet[..., None]) + np.array([0.10, 0.10, 0.08]) * 0.35 * wet[..., None]
    rough = np.clip(0.55 - 0.45 * wet + 0.1 * noise, 0.05, 1)
    maps = dict(basecolor=col, opacity=Op, height=Hn, roughness=rough,
                normal=tg.normal_from_height(tg.blur(Hn, 0.8, wrap=False), 16.0, wrap=False))
    files = common.save_texture_set(DECAL_PREFIX, maps, ("BaseColor", "Normal", "Roughness", "Height", "Opacity"), directory=common.GROUND_DIR)
    return files


def macro_noise(seed=777, n=1024):
    """Independent low-frequency, tileable noise for the landscape material (UV = world / MACRO_TILE_M):
    R = albedo variation (features ~15-80 m), G = mask blending the near and the rotated / rescaled samples of
    each layer (anti-tiling, ~8-30 m), B = secondary tint / moisture variation."""
    r = tg.fbm((n, n), seed, octaves=((3, 1.0), (8, 0.6), (16, 0.35)), beta=1.0)
    g = tg.fbm((n, n), seed + 1, octaves=((8, 1.0), (20, 0.6), (32, 0.3)), beta=1.0)
    b = tg.fbm((n, n), seed + 2, octaves=((4, 1.0), (12, 0.5), (30, 0.3)), beta=1.0)
    p = os.path.join(common.GROUND_DIR, MACRO_NOISE)
    tg.save_png(p, np.stack([r, g, b], -1))
    return p


BUILDERS = {
    "FloodplainSilt": floodplain_silt, "FloodplainSiltDry": floodplain_silt_dry, "RiverbankMud": riverbank_mud,
    "PointBarSand": point_bar_sand, "LeafLitterBroadleaf": leaf_litter_broadleaf, "LeafLitterConifer": leaf_litter_conifer,
    "BackswampOrganicMud": backswamp_mud, "CutbankStrata": cutbank_strata, "RiverGravel": river_gravel,
}
SEEDS = {"FloodplainSilt": 1000, "FloodplainSiltDry": 1008, "RiverbankMud": 1017, "PointBarSand": 1034, "LeafLitterBroadleaf": 1051,
         "LeafLitterConifer": 1068, "BackswampOrganicMud": 1085, "CutbankStrata": 1102, "RiverGravel": 1119}


def run(names=None, force=False):
    common.ensure_dirs()
    out = {}
    for name in (names or BUILDERS):
        p = os.path.join(common.GROUND_DIR, "T_Ground_%s_BaseColor.png" % name)
        if os.path.exists(p) and not force:
            out[name] = "cached"
            continue
        t0 = time.time()
        BUILDERS[name](SEEDS[name])
        out[name] = round(time.time() - t0, 1)
        common.log("ground %s: %.1f s" % (name, out[name]))
    if force or not os.path.exists(os.path.join(common.GROUND_DIR, "T_%s_BaseColor.png" % DECAL_PREFIX)):
        t0 = time.time()
        trackway_decals()
        out["_decals"] = round(time.time() - t0, 1)
    if force or not os.path.exists(os.path.join(common.GROUND_DIR, MACRO_NOISE)):
        macro_noise()
        out["_macro"] = "ok"
    return out


def material_info():
    return {k: dict(tile_m=v[0], usage=v[1], description_fr=v[2],
                    textures={m: os.path.relpath(os.path.join(common.GROUND_DIR, "T_Ground_%s_%s.png" % (k, m)), common.ROOT)
                              for m in ("BaseColor", "Normal", "Roughness", "Height")})
            for k, v in MATERIALS.items()}


def decal_info():
    return {"textures": {m: os.path.relpath(os.path.join(common.GROUND_DIR, "T_%s_%s.png" % (DECAL_PREFIX, m)), common.ROOT)
                         for m in ("BaseColor", "Normal", "Roughness", "Height", "Opacity")},
            "grid": list(DECAL_GRID), "cell_m": list(DECAL_CELL_M),
            "meshes": (common.read_json(os.path.join(common.META_DIR, "decals.json"), {}) or {}).get("decals")
                      or [dict(ue_name=n, cell=i, description_fr=d) for i, (n, d) in enumerate(DECALS)],
            "material": "M_Noctis_Decal (Deferred Decal, DBuffer couleur + normale + rugosité) / MI_Decal_Trackways",
            "macro_noise": dict(texture=os.path.relpath(os.path.join(common.GROUND_DIR, MACRO_NOISE), common.ROOT), tile_m=MACRO_TILE_M,
                                channels={"R": "variation d'albédo", "G": "masque de mélange des échantillons proche / tourné", "B": "teinte / humidité"})}
