"""Leaf-card atlases (albedo, normal, opacity, translucency, roughness) painted procedurally.

Each painter fills an atlas of cells; every cell holds one spray / frond / leaf whose attachment point is
the bottom centre of the cell (fronds and straps: bottom centre of a tall cell). The geometry generators map
card quads onto these cells (paint.cell_uv_rect) and scale them with CELL_SIZE_M below.

Colours are sRGB albedo; they are plausible values for living foliage, not reconstructions of fossil colour
(fossil leaves do not preserve it): design assumption, recorded in each asset's metadata.
"""
from __future__ import annotations

import math

import numpy as np

from .paint import Canvas, new_atlas, paste

ATLAS_PX = 2048


def _cell_canvas(cw, ch, seed, ss=2):
    return Canvas(cw, ch, ss=ss, seed=seed)


def _curve(p0, p1, bend, n=8, rng=None, wobble=0.0):
    """Polyline from p0 to p1 bowed sideways by 'bend' (pixels)."""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    d = p1 - p0
    nrm = np.array([-d[1], d[0]]) / max(np.linalg.norm(d), 1e-6)
    t = np.linspace(0, 1, n)
    pts = p0[None] + t[:, None] * d[None] + (np.sin(math.pi * t) * bend)[:, None] * nrm[None]
    if rng is not None and wobble:
        pts += rng.normal(0, wobble, pts.shape) * np.sin(math.pi * t)[:, None]
    return pts


def _along(pts, t):
    """Point and unit tangent at fraction t of a polyline."""
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg)])
    s = t * cum[-1]
    i = int(np.clip(np.searchsorted(cum, s) - 1, 0, len(seg) - 1))
    f = (s - cum[i]) / max(seg[i], 1e-9)
    p = pts[i] + (pts[i + 1] - pts[i]) * f
    tan = (pts[i + 1] - pts[i]) / max(seg[i], 1e-9)
    return p, tan


def _ang(v):
    return math.atan2(v[1], v[0])


# ----------------------------------------------------------------------------------------------- dicot sprays
def broadleaf_spray(cv, rng, color, n_leaves=7, leaf_len=0.30, leaf_w=0.13, shape="elliptic", twig=(0.33, 0.24, 0.15),
                    spread=(40, 70), palmate=False, droop=0.0, **leaf_kw):
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    top = (W * (0.5 + rng.uniform(-0.08, 0.08)), H * (0.33 + droop * 0.12))
    stem = _curve(base, top, rng.uniform(-0.08, 0.08) * W, n=10, rng=rng, wobble=W * 0.004)
    cv.stroke(stem, W * 0.018, W * 0.007, twig, height=0.6, transl=0.05, rough=0.75)
    ts = np.linspace(0.18, 0.92, n_leaves)
    side = 1 if rng.random() < 0.5 else -1
    for i, t in enumerate(ts):
        p, tan = _along(stem, t + rng.uniform(-0.02, 0.02))
        side = -side
        a = _ang(tan) + side * math.radians(rng.uniform(*spread))
        a += math.radians(droop * 25) * side
        L = W * leaf_len * rng.uniform(0.78, 1.08) * (0.75 + 0.35 * t)
        c = _jit(color, rng)
        if palmate:
            cv.stroke([p, p + np.array([math.cos(a), math.sin(a)]) * L * 0.35], W * 0.006, W * 0.004, np.asarray(c) * 0.8)
            q = p + np.array([math.cos(a), math.sin(a)]) * L * 0.35
            cv.polar_leaf(q, a, L * 0.62, c, rng, **leaf_kw)
        else:
            cv.leaf(p, a, L, L * leaf_w / leaf_len, c, rng, shape=shape, petiole=0.12, **leaf_kw)
    # terminal leaf
    p, tan = _along(stem, 1.0)
    L = W * leaf_len * 0.9
    if palmate:
        cv.polar_leaf(p + tan * L * 0.25, _ang(tan), L * 0.55, _jit(color, rng), rng, **leaf_kw)
    else:
        cv.leaf(p, _ang(tan), L, L * leaf_w / leaf_len, _jit(color, rng), rng, shape=shape, petiole=0.1, **leaf_kw)


def _jit(c, rng, dv=0.12, dh=0.04):
    c = np.asarray(c, float)
    v = rng.uniform(1 - dv, 1 + dv)
    tint = np.array([1 + rng.uniform(-dh, dh) * 1.5, 1.0, 1 + rng.uniform(-dh, dh)])
    return np.clip(c * v * tint, 0, 1)


# ----------------------------------------------------------------------------------------------- conifer sprays
def metasequoia_spray(cv, rng, color, shoots=13, needle_len=0.042, needle_w=0.0065, axis_col=(0.42, 0.30, 0.20)):
    """Feathery distichous spray: main axis, opposite lateral shoots, two ranks of flat linear leaves."""
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    top = (W * (0.5 + rng.uniform(-0.06, 0.06)), H * 0.11)
    axis = _curve(base, top, rng.uniform(-0.1, 0.1) * W, n=10, rng=rng, wobble=W * 0.003)
    cv.stroke(axis, W * 0.012, W * 0.004, axis_col, height=0.6, transl=0.1)
    for k, t in enumerate(np.linspace(0.12, 0.93, shoots)):
        p, tan = _along(axis, t)
        for side in (-1, 1):
            if rng.random() < 0.08:
                continue
            a = _ang(tan) + side * math.radians(rng.uniform(48, 64))
            L = W * (0.33 * (1 - 0.65 * t) + 0.07) * rng.uniform(0.85, 1.05)
            d = np.array([math.cos(a), math.sin(a)])
            end = p + d * L
            shoot = _curve(p, end, side * rng.uniform(-0.04, 0.06) * L, n=6)
            cv.stroke(shoot, W * 0.005, W * 0.0025, np.asarray(color) * 0.75, height=0.4, transl=0.4)
            npairs = max(4, int(L / (W * 0.011)))
            c = _jit(color, rng, dv=0.08)
            for j in range(npairs):
                tt = (j + 0.5) / npairs
                q, tq = _along(shoot, tt)
                nl = W * needle_len * (0.55 + 0.45 * math.sin(math.pi * min(1.0, tt * 1.15))) * rng.uniform(0.9, 1.1)
                for s2 in (-1, 1):
                    aa = _ang(tq) + s2 * math.radians(rng.uniform(62, 80))
                    cv.leaf(q, aa, nl, W * needle_w, c, rng, shape="linear", veins=0, midrib=0.6, dome=0.3,
                            transl=0.85, rough=0.5, edge_dark=0.05)
        # terminal needles on the axis
    q, tq = _along(axis, 1.0)
    cv.leaf(q, _ang(tq), W * needle_len, W * needle_w, color, rng, shape="linear", veins=0, midrib=0.6, dome=0.3)


def glyptostrobus_spray(cv, rng, color, shoots=10):
    """Dimorphic foliage: cord-like shoots with appressed scale leaves plus a few shoots of short linear leaves."""
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    top = (W * (0.5 + rng.uniform(-0.08, 0.08)), H * 0.07)
    axis = _curve(base, top, rng.uniform(-0.12, 0.12) * W, n=10, rng=rng, wobble=W * 0.004)
    cv.stroke(axis, W * 0.013, W * 0.005, (0.40, 0.29, 0.20), height=0.6)
    for t in np.linspace(0.1, 0.95, shoots):
        p, tan = _along(axis, t)
        side = 1 if rng.random() < 0.5 else -1
        for s in (side, -side) if rng.random() < 0.6 else (side,):
            a = _ang(tan) + s * math.radians(rng.uniform(30, 55))
            L = W * (0.34 * (1 - 0.55 * t) + 0.1) * rng.uniform(0.8, 1.1)
            end = p + np.array([math.cos(a), math.sin(a)]) * L
            shoot = _curve(p, end, rng.uniform(-0.15, 0.15) * L, n=7, rng=rng, wobble=W * 0.002)
            c = _jit(color, rng, dv=0.1)
            if rng.random() < 0.3:
                cv.stroke(shoot, W * 0.004, W * 0.002, np.asarray(c) * 0.8)
                for j in range(int(L / (W * 0.012))):
                    q, tq = _along(shoot, (j + 0.5) * W * 0.012 / L)
                    for s2 in (-1, 1):
                        cv.leaf(q, _ang(tq) + s2 * math.radians(rng.uniform(40, 60)), W * 0.026, W * 0.005, c, rng,
                                shape="linear", veins=0, midrib=0.5, dome=0.3)
            else:
                # scale-leaf cord: overlapping small ellipses along the shoot, with short side branchlets
                n = int(L / (W * 0.006))
                for j in range(n):
                    q, tq = _along(shoot, (j + 0.5) / n)
                    w = W * 0.0075 * (1 - 0.4 * j / n)
                    cv.ellipse(q, w * 1.6, w, _ang(tq) + rng.uniform(-0.3, 0.3), _jit(c, rng, dv=0.06), height=0.6, transl=0.5, rough=0.5)
                    if j % 9 == 4 and j < n - 5:
                        a2 = _ang(tq) + rng.choice([-1, 1]) * math.radians(rng.uniform(35, 55))
                        L2 = W * rng.uniform(0.05, 0.09)
                        for jj in range(int(L2 / (W * 0.006))):
                            q2 = q + np.array([math.cos(a2), math.sin(a2)]) * jj * W * 0.006
                            cv.ellipse(q2, w * 1.3, w * 0.8, a2, _jit(c, rng, dv=0.06), height=0.5, transl=0.5)


# ----------------------------------------------------------------------------------------------- fronds
def pinnate_frond(cv, rng, color, pinnae=26, bipinnate=False, pinna_w=0.22, max_len=0.42, rachis=(0.30, 0.32, 0.14),
                  stipe=0.18, lobed=0.0, curl=0.0, necrosis=0.0, stiff=False, leaflet_shape="lanceolate"):
    """Frond in a tall cell: stipe from the bottom, lanceolate outline (pinnae shortest at base and tip)."""
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.995)
    top = (W * (0.5 + rng.uniform(-0.04, 0.04)), H * 0.02)
    ax = _curve(base, top, rng.uniform(-0.03, 0.03) * W, n=14, rng=rng, wobble=W * 0.002)
    cv.stroke(ax, W * 0.022, W * 0.006, rachis, height=0.7, transl=0.2, rough=0.6)
    for i in range(pinnae):
        t = stipe + (1 - stipe) * (i + 0.5) / pinnae
        p, tan = _along(ax, t)
        tl = (t - stipe) / (1 - stipe)
        env = math.sin(math.pi * min(1.0, 0.12 + tl * 0.95)) ** 0.7  # widest below the middle
        L = W * max_len * (0.25 + 0.75 * env) * rng.uniform(0.92, 1.05)
        for side in (-1, 1):
            a = _ang(tan) + side * math.radians(rng.uniform(58, 72) if not stiff else rng.uniform(55, 62))
            c = _jit(color, rng, dv=0.06)
            pp = p + tan * side * W * 0.004
            if bipinnate:
                end = pp + np.array([math.cos(a), math.sin(a)]) * L
                sub = _curve(pp, end, -side * L * 0.08, n=6)
                cv.stroke(sub, W * 0.005, W * 0.002, np.asarray(rachis) * 1.05, height=0.4)
                npin = max(3, int(L / (W * 0.03)))
                for j in range(npin):
                    q, tq = _along(sub, (j + 0.6) / (npin + 0.4))
                    ll = W * 0.05 * (1 - 0.6 * j / npin) * (0.6 + 0.4 * env)
                    for s2 in (-1, 1):
                        cv.leaf(q, _ang(tq) + s2 * math.radians(60), ll, ll * 0.42, c, rng, shape="ovate", peak=0.35,
                                veins=3, vein_angle=50, lobes=2 if lobed else 0, lobe_depth=lobed * 0.5, transl=0.85,
                                necrosis=necrosis, dome=0.35)
            else:
                cv.leaf(pp, a, L, L * pinna_w, c, rng, shape=leaflet_shape, peak=0.25, tip_power=1.3,
                        veins=max(6, int(L / (W * 0.012))), vein_angle=62, lobes=int(L / (W * 0.025)) if lobed else 0,
                        lobe_depth=lobed, curl=curl * side, transl=0.85, necrosis=necrosis, dome=0.4,
                        midrib=0.8, teeth=0, edge_dark=0.08)


def strap_leaf(cv, rng, color, width=0.16, twist=0.0, tip_dry=0.0):
    """Long linear (strap) leaf with parallel venation, in a tall cell."""
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.998)
    a = -math.pi / 2 + rng.uniform(-0.03, 0.03)
    L = H * 0.985
    cv.leaf(base, a, L, W * width, _jit(color, rng, dv=0.06), rng, shape="strap", veins=0, midrib=0.4,
            curl=twist, transl=0.75, rough=0.45, dome=0.25, necrosis=tip_dry, edge_dark=0.05)
    # parallel veins as faint strokes
    for k in np.linspace(-0.35, 0.35, 5):
        x = W * 0.5 + k * W * width
        cv.stroke([(x, H * 0.98), (W * 0.5 + k * W * width * 0.15, H * 0.05)], 2.0, 1.0,
                  np.asarray(color) * 1.12, height=0.15, transl=0.6, rough=0.45, noise_amp=0.05)


# ----------------------------------------------------------------------------------------------- atlas builders
def build_atlas(kind, seed):
    """Returns (maps, layout). layout = {grid: (cols, rows), cells: [ {size_m: (w, h)} ... ]}."""
    rng = np.random.default_rng(seed)
    spec = ATLASES[kind]
    cols, rows = spec["grid"]
    size = (ATLAS_PX, ATLAS_PX)
    atlas = new_atlas(size, spec["grid"])
    cw, ch = size[0] // cols, size[1] // rows
    for cell in range(cols * rows):
        cv = _cell_canvas(cw, ch, seed + 31 * cell, ss=spec.get("ss", 2))
        spec["paint"](cv, np.random.default_rng(seed * 1000 + cell), cell)
        maps = cv.resolve(normal_strength=spec.get("normal_strength", 5.0))
        paste(atlas, maps, cell, spec["grid"])
    layout = dict(grid=spec["grid"], cell_size_m=spec["cell_size_m"], description=spec["description"])
    return atlas, layout


def _p_metaseq(cv, rng, cell):
    col = [(0.40, 0.52, 0.17), (0.36, 0.49, 0.16), (0.42, 0.53, 0.19), (0.47, 0.50, 0.18)][cell]
    metasequoia_spray(cv, rng, col, shoots=12 + cell)


def _p_glypto(cv, rng, cell):
    col = [(0.27, 0.38, 0.17), (0.25, 0.36, 0.18), (0.30, 0.40, 0.17), (0.33, 0.40, 0.19)][cell]
    glyptostrobus_spray(cv, rng, col, shoots=9 + cell)


def _p_broad_entire(cv, rng, cell):
    col = [(0.25, 0.37, 0.12), (0.22, 0.34, 0.11), (0.28, 0.39, 0.13), (0.30, 0.38, 0.12)][cell]
    broadleaf_spray(cv, rng, col, n_leaves=7 + cell % 2, leaf_len=0.30, leaf_w=0.13, shape="elliptic", peak=0.45,
                    tip_power=1.25, veins=9, vein_angle=52, damage=0.35, necrosis=0.15 if cell == 3 else 0.0,
                    transl=0.75, rough=0.42, dome=0.5)


def _p_broad_lobed(cv, rng, cell):
    col = [(0.27, 0.38, 0.13), (0.25, 0.36, 0.12), (0.29, 0.40, 0.14), (0.33, 0.39, 0.13)][cell]
    _broad_lobed_impl(cv, rng, col, cell)


def _broad_lobed_impl(cv, rng, col, cell):
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    top = (W * (0.5 + rng.uniform(-0.08, 0.08)), H * 0.30)
    stem = _curve(base, top, rng.uniform(-0.08, 0.08) * W, n=10, rng=rng, wobble=W * 0.004)
    cv.stroke(stem, W * 0.018, W * 0.008, (0.36, 0.27, 0.18), height=0.6)
    side = 1
    for i, t in enumerate(np.linspace(0.2, 1.0, 5)):
        p, tan = _along(stem, min(t, 1.0))
        side = -side
        a = _ang(tan) + (side * math.radians(rng.uniform(30, 60)) if t < 0.99 else 0.0)
        L = W * rng.uniform(0.26, 0.32)
        d = np.array([math.cos(a), math.sin(a)])
        q = p + d * L * 0.45
        cv.stroke([p, q], W * 0.007, W * 0.005, (0.34, 0.33, 0.15))
        cv.polar_leaf(q, a, L * 0.62, _jit(col, rng), rng, kind="palmate", spread=150, lobes=5 if cell % 2 == 0 else 3,
                      lobe_width=0.26, sinus=0.42, teeth=34, tooth_depth=0.05, damage=0.35,
                      necrosis=0.12 if cell == 3 else 0.0, transl=0.75)


def _p_broad_lance(cv, rng, cell):
    col = [(0.30, 0.40, 0.15), (0.28, 0.38, 0.14), (0.33, 0.42, 0.16), (0.36, 0.40, 0.15)][cell]
    broadleaf_spray(cv, rng, col, n_leaves=15, leaf_len=0.32, leaf_w=0.075, shape="lanceolate", peak=0.3,
                    tip_power=1.1, veins=14, vein_angle=60, spread=(25, 45), droop=0.6, damage=0.25,
                    necrosis=0.1 if cell == 3 else 0.0, transl=0.8, rough=0.48, dome=0.35, teeth=40, tooth_depth=0.03)


def _p_shrub_ovate(cv, rng, cell):
    col = [(0.24, 0.37, 0.13), (0.27, 0.40, 0.14), (0.22, 0.34, 0.12), (0.30, 0.38, 0.13)][cell]
    broadleaf_spray(cv, rng, col, n_leaves=8, leaf_len=0.26, leaf_w=0.15, shape="ovate", peak=0.32, tip_power=1.6,
                    veins=7, vein_angle=48, damage=0.4, teeth=30 if cell % 2 else 0, tooth_depth=0.05, transl=0.75,
                    rough=0.4, dome=0.55)


def _p_shrub_small(cv, rng, cell):
    col = [(0.26, 0.36, 0.14), (0.24, 0.33, 0.13), (0.29, 0.38, 0.15), (0.31, 0.36, 0.14)][cell]
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    stem = _curve(base, (W * 0.5, H * 0.1), rng.uniform(-0.1, 0.1) * W, n=10, rng=rng, wobble=W * 0.004)
    cv.stroke(stem, W * 0.014, W * 0.005, (0.33, 0.26, 0.18), height=0.6)
    for t in np.linspace(0.15, 0.9, 5):
        p, tan = _along(stem, t)
        a = _ang(tan) + rng.choice([-1, 1]) * math.radians(rng.uniform(30, 55))
        end = p + np.array([math.cos(a), math.sin(a)]) * W * rng.uniform(0.18, 0.3)
        br = _curve(p, end, rng.uniform(-0.05, 0.05) * W, n=6)
        cv.stroke(br, W * 0.007, W * 0.003, (0.35, 0.28, 0.19))
        for tt in np.linspace(0.15, 1.0, 7):
            q, tq = _along(br, tt)
            s = rng.choice([-1, 1])
            cv.leaf(q, _ang(tq) + s * math.radians(rng.uniform(25, 60)), W * rng.uniform(0.08, 0.11), W * 0.045,
                    _jit(col, rng), rng, shape="elliptic", peak=0.5, tip_power=1.0, veins=5, vein_angle=50, petiole=0.08,
                    damage=0.3, transl=0.7, rough=0.45, dome=0.5)


def _p_ginkgo(cv, rng, cell):
    col = [(0.36, 0.47, 0.17), (0.33, 0.45, 0.16), (0.39, 0.49, 0.18), (0.50, 0.50, 0.17)][cell]
    W, H = cv.W, cv.H
    base = (W * 0.5, H * 0.985)
    stem = _curve(base, (W * (0.5 + rng.uniform(-0.1, 0.1)), H * 0.15), rng.uniform(-0.06, 0.06) * W, n=10, rng=rng)
    cv.stroke(stem, W * 0.02, W * 0.012, (0.42, 0.36, 0.30), height=0.7, rough=0.8)
    for t in np.linspace(0.15, 1.0, 5):
        p, tan = _along(stem, t)
        cv.ellipse(p, W * 0.016, W * 0.012, _ang(tan), (0.38, 0.32, 0.26), height=0.8)  # spur shoot
        for k in range(rng.integers(3, 6)):
            a = _ang(tan) + rng.uniform(-1.4, 1.4)
            L = W * rng.uniform(0.17, 0.24)
            d = np.array([math.cos(a), math.sin(a)])
            q = p + d * L * 0.55
            cv.stroke([p, q], W * 0.004, W * 0.003, np.asarray(col) * 0.8)
            cv.polar_leaf(q, a, L * 0.48, _jit(col, rng), rng, kind="fan", spread=rng.uniform(110, 150),
                          segments=60, split=0.0, notch=0.35 if rng.random() < 0.6 else 0.0, plication=0.15,
                          transl=0.8, edge_dark=0.05, necrosis=0.3 if cell == 3 else 0.0)


def _p_palm(cv, rng, cell):
    col = [(0.28, 0.39, 0.17), (0.30, 0.41, 0.19), (0.36, 0.40, 0.18), (0.50, 0.40, 0.22)][cell]
    W, H = cv.W, cv.H
    base = np.array([W * 0.5, H * 0.97])
    cv.polar_leaf(base, -math.pi / 2, W * 0.47, col, rng, kind="fan", spread=230, segments=34,
                  split=[0.5, 0.58, 0.62, 0.66][cell], costa=0.5, plication=1.0, transl=0.6, rough=0.5,
                  necrosis=[0.05, 0.12, 0.3, 0.9][cell], edge_dark=0.15, damage=0.2)
    cv.stroke([base, base + np.array([0, -W * 0.18])], W * 0.02, W * 0.008, np.asarray(col) * 0.9, height=0.5)


def _p_fern(cv, rng, cell):
    if cell == 0:
        pinnate_frond(cv, rng, (0.32, 0.47, 0.16), pinnae=26, pinna_w=0.18, max_len=0.42, lobed=0.25)
    elif cell == 1:
        pinnate_frond(cv, rng, (0.30, 0.45, 0.17), pinnae=18, bipinnate=True, max_len=0.44, lobed=0.3)
    elif cell == 2:
        pinnate_frond(cv, rng, (0.36, 0.48, 0.18), pinnae=30, pinna_w=0.12, max_len=0.30, stipe=0.25)
    else:
        pinnate_frond(cv, rng, (0.50, 0.40, 0.20), pinnae=24, pinna_w=0.16, max_len=0.40, lobed=0.2, necrosis=0.9, curl=0.08)


def _p_cycad(cv, rng, cell):
    col = [(0.20, 0.32, 0.13), (0.22, 0.34, 0.14), (0.24, 0.35, 0.15), (0.46, 0.40, 0.22)][cell]
    pinnate_frond(cv, rng, col, pinnae=36, pinna_w=0.07, max_len=0.40, stipe=0.15, stiff=True,
                  rachis=(0.34, 0.33, 0.18), leaflet_shape="strap", necrosis=0.85 if cell == 3 else 0.0)


def _p_strap(cv, rng, cell):
    col = [(0.33, 0.45, 0.18), (0.30, 0.43, 0.17), (0.36, 0.46, 0.19), (0.48, 0.44, 0.22)][cell]
    strap_leaf(cv, rng, col, width=[0.30, 0.26, 0.34, 0.28][cell], twist=0.02 * (cell - 1.5), tip_dry=[0.1, 0.2, 0.05, 0.6][cell])


def _p_emergent_broad(cv, rng, cell):
    col = [(0.27, 0.40, 0.15), (0.30, 0.43, 0.16), (0.25, 0.38, 0.15), (0.40, 0.42, 0.18)][cell]
    W, H = cv.W, cv.H
    cv.leaf((W * 0.5, H * 0.96), -math.pi / 2 + rng.uniform(-0.05, 0.05), H * 0.9, W * 0.62, col, rng, shape="cordate",
            peak=0.3, tip_power=1.4, veins=7, vein_angle=40, damage=0.4, necrosis=0.4 if cell == 3 else 0.05,
            transl=0.7, rough=0.35, dome=0.5, midrib=1.4)


def _p_herb(cv, rng, cell):
    col = [(0.28, 0.42, 0.15), (0.26, 0.39, 0.14), (0.31, 0.44, 0.17), (0.35, 0.41, 0.16)][cell]
    W, H = cv.W, cv.H
    shape = ["ovate", "elliptic", "ovate", "elliptic"][cell]
    cv.leaf((W * 0.5, H * 0.985), -math.pi / 2, H * 0.92, W * [0.55, 0.38, 0.6, 0.42][cell], col, rng, shape=shape,
            peak=0.35, tip_power=1.3, veins=8, vein_angle=55, petiole=0.25, teeth=[0, 30, 22, 0][cell],
            tooth_depth=0.06, damage=0.5, necrosis=0.2 if cell == 3 else 0.0, transl=0.75, rough=0.5, dome=0.5)


def _p_floating(cv, rng, cell):
    """Floating rosettes of small rhombic toothed leaves, seen from above (cells 0-2) and loose leaves (cell 3)."""
    W, H = cv.W, cv.H
    c = np.array([W * 0.5, H * 0.5])
    col = np.array([(0.26, 0.36, 0.13), (0.30, 0.38, 0.14), (0.24, 0.33, 0.12), (0.36, 0.36, 0.14)][cell])
    if cell < 3:
        n = [26, 18, 34][cell]
        for k in range(n):
            ring = k / n
            a = k * 2.39996 + rng.uniform(-0.1, 0.1)
            r0 = W * (0.05 + 0.32 * math.sqrt(ring)) * 0.55
            L = W * (0.13 + 0.06 * ring) * rng.uniform(0.85, 1.1)
            p = c + np.array([math.cos(a), math.sin(a)]) * r0
            cv.stroke([c, p], W * 0.006, W * 0.004, col * 0.8 + 0.08, height=0.2)
            cv.leaf(p, a, L, L * 0.95, _jit(col * (0.92 + 0.15 * ring), rng), rng, shape="ovate", peak=0.55, tip_power=1.0,
                    veins=4, vein_angle=40, teeth=14, tooth_depth=0.12, damage=0.2, transl=0.4, rough=0.25, dome=0.3,
                    necrosis=0.25 if rng.random() < 0.15 else 0.0)
    else:
        for k in range(14):
            p = rng.uniform(0.15, 0.85, 2) * np.array([W, H])
            L = W * rng.uniform(0.1, 0.16)
            cv.leaf(p, rng.uniform(0, 6.28), L, L * 0.95, _jit(col, rng), rng, shape="ovate", peak=0.55, tip_power=1.0,
                    veins=4, teeth=14, tooth_depth=0.12, transl=0.4, rough=0.25, necrosis=0.3)


def _p_whorl(cv, rng, cell):
    """Horsetail branch whorl seen from above: thin jointed branches radiating from the node."""
    W, H = cv.W, cv.H
    c = np.array([W * 0.5, H * 0.5])
    n = [16, 18, 20, 15][cell]
    col = np.array([0.24, 0.38, 0.13])
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-0.12, 0.12)
        L = W * rng.uniform(0.40, 0.49)
        end = c + np.array([math.cos(a), math.sin(a)]) * L
        pts = _curve(c, end, rng.uniform(-0.03, 0.03) * W, n=6)
        # branches drawn thick enough to survive mip-mapping (the whorl must read at 5-10 m)
        cv.stroke(pts, W * 0.02, W * 0.011, _jit(col, rng, dv=0.08), height=0.6, transl=0.5, rough=0.5, noise_amp=0.1)
        for j in range(1, 4):  # small dark joints
            q = c + (end - c) * j / 4
            cv.ellipse(q, W * 0.007, W * 0.005, a, (0.16, 0.16, 0.10), height=0.7)


# grid: (cols, rows); cell_size_m: physical size (width, height) of one cell.
ATLASES = {
    "metasequoia": dict(paint=_p_metaseq, grid=(2, 2), cell_size_m=(0.75, 0.75),
                        description="rameaux distiques de feuilles linéaires plates (habitus de type Metasequoia)"),
    "glyptostrobus": dict(paint=_p_glypto, grid=(2, 2), cell_size_m=(0.65, 0.65),
                          description="rameaux dimorphes : pousses à feuilles écailleuses + pousses à feuilles linéaires"),
    "broadleaf_entire": dict(paint=_p_broad_entire, grid=(2, 2), cell_size_m=(0.8, 0.8),
                             description="rameaux de feuilles elliptiques à marge entière, nervation pennée, dégâts d'insectes"),
    "broadleaf_lobed": dict(paint=_p_broad_lobed, grid=(2, 2), cell_size_m=(0.8, 0.8),
                            description="rameaux de feuilles palmatilobées dentées (morphotype générique)"),
    "broadleaf_lanceolate": dict(paint=_p_broad_lance, grid=(2, 2), cell_size_m=(0.7, 0.7),
                                 description="rameaux retombants de feuilles lancéolées finement dentées"),
    "shrub_ovate": dict(paint=_p_shrub_ovate, grid=(2, 2), cell_size_m=(0.55, 0.55),
                        description="rameaux de feuilles ovées acuminées (arbuste)"),
    "shrub_small": dict(paint=_p_shrub_small, grid=(2, 2), cell_size_m=(0.45, 0.45),
                        description="rameaux denses de petites feuilles elliptiques (arbuste)"),
    "ginkgo": dict(paint=_p_ginkgo, grid=(2, 2), cell_size_m=(0.5, 0.5),
                   description="rameaux à rameaux courts portant des feuilles en éventail (Ginkgo)"),
    "palm_fan": dict(paint=_p_palm, grid=(2, 2), cell_size_m=(1.6, 1.6), normal_strength=7.0,
                     description="limbes costapalmés plissés à segments fendus (type Sabalites), dont un sec"),
    "fern": dict(paint=_p_fern, grid=(4, 1), cell_size_m=(0.5, 2.0), ss=2,
                 description="frondes : pennée lobée, bipennée, pennée étroite, fronde sèche"),
    "cycad": dict(paint=_p_cycad, grid=(4, 1), cell_size_m=(0.45, 1.8),
                  description="feuilles pennées rigides à folioles linéaires (cycadophyte générique), dont une sèche"),
    "strap": dict(paint=_p_strap, grid=(4, 1), cell_size_m=(0.12, 1.4),
                  description="feuilles rubanées à nervation parallèle (émergente monocotylédone générique)"),
    "emergent_broad": dict(paint=_p_emergent_broad, grid=(2, 2), cell_size_m=(0.35, 0.35),
                           description="limbes cordés à ovés d'émergente aquatique (générique)"),
    "herb": dict(paint=_p_herb, grid=(2, 2), cell_size_m=(0.22, 0.3),
                 description="feuilles pétiolées d'herbacée dicotylédone basse (générique)"),
    "horsetail_whorl": dict(paint=_p_whorl, grid=(2, 2), cell_size_m=(0.26, 0.26),
                            description="verticilles de rameaux de prêle vus de dessus"),
    "floating": dict(paint=_p_floating, grid=(2, 2), cell_size_m=(0.45, 0.45),
                     description="rosettes flottantes de petites feuilles rhomboïdales dentées (inspiration Quereuxia)"),
}
