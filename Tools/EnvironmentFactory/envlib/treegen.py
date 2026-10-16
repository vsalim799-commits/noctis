"""Procedural woody plants: recursive branching skeleton -> bark tubes + alpha leaf cards, with LODs.

A species is a dict (see catalog.py):
  trunk:   height, radius, tip_radius, lean_deg, sinuosity, flare (amp, height_m), buttress (lobes, amp, height_m),
           sink (m below the pivot), count / splay_deg (multi-stem), sides, crown_split (decurrent top)
  levels:  list of child-level dicts (level 1, 2, ...):
           per_m | count, range (t0, t1) on the parent, down (deg at t0, deg at t1), down_var, rot ('spiral'|'planar'),
           length (m), shape ('conical'|'flat_top'|'dome'|'flame'|'columnar'|'uniform'|'taper'), len_var,
           rad (ratio to parent radius at the attachment), up (gravitropism, + up / - droop), noise, sides
  envelope: optional crown ellipsoid (cx, cy, cz, rx, ry, rz) clipping branch lengths (levels >= 2)
  leaves:  atlas, cells, levels, spacing (m), start (fraction of the twig), scale, tilt_deg, spread_deg, bend,
           terminal (extra card at twig tips)
LOD settings (lod.py) thin out levels, ring sides and cards, enlarging the remaining cards to keep the crown's
coverage.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .meshkit import Mesh, normalize, any_perp, prism_points, convex_hull_mesh

UP = np.array([0.0, 0.0, 1.0])


@dataclass
class Branch:
    pts: np.ndarray
    rad: np.ndarray
    level: int
    parent: int
    t_on_parent: float
    phase: float
    length: float


def shape_factor(shape, x):
    x = min(max(x, 0.0), 1.0)
    if shape == "conical":
        return 0.08 + 0.92 * (1 - x) ** 0.85
    if shape == "flat_top":
        return 0.45 + 0.55 * math.sin(math.pi * (0.15 + 0.6 * x))
    if shape == "dome":
        return 0.3 + 0.7 * math.sin(math.pi * (0.2 + 0.75 * x))
    if shape == "flame":
        return 0.15 + 0.85 * math.sin(math.pi * (0.08 + 0.85 * x)) ** 0.7 * (1 - 0.5 * x)
    if shape == "columnar":
        return 0.35 + 0.65 * (1 - x ** 2.5)
    if shape == "taper":
        return 1.0 - 0.7 * x
    return 1.0


def grow_polyline(start, direction, length, segs, rng, up=0.0, noise=0.05, droop_end=0.0):
    """Random-walk polyline with gravitropism. Returns (pts (segs+1, 3))."""
    d = normalize(np.asarray(direction, float))
    p = np.asarray(start, float).copy()
    pts = [p.copy()]
    step = length / segs
    wander = rng.normal(size=3) * noise
    for i in range(segs):
        f = (i + 1) / segs
        wander = 0.7 * wander + 0.3 * rng.normal(size=3) * noise
        g = up + droop_end * f
        d = normalize(d + UP * g * step + wander * step)
        p = p + d * step
        pts.append(p.copy())
    return np.array(pts)


def _envelope_len(env, p, d, default):
    if env is None:
        return default
    c = np.array(env[:3])
    r = np.array(env[3:6])
    q = (p - c) / r
    dd = d / r
    a = dd @ dd
    b = 2 * q @ dd
    cc = q @ q - 1
    disc = b * b - 4 * a * cc
    if cc > 0 or disc < 0:
        return min(default, 0.35)
    L = (-b + math.sqrt(disc)) / (2 * a)
    return max(0.25, min(default, L))


def _point_on(br, t):
    s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(br.pts, axis=0), axis=1))])
    target = t * s[-1]
    i = int(np.clip(np.searchsorted(s, target) - 1, 0, len(s) - 2))
    f = (target - s[i]) / max(s[i + 1] - s[i], 1e-9)
    p = br.pts[i] + (br.pts[i + 1] - br.pts[i]) * f
    r = br.rad[i] + (br.rad[i + 1] - br.rad[i]) * f
    tan = normalize(br.pts[i + 1] - br.pts[i])
    return p, r, tan


def build_skeleton(spec, rng, max_level=None):
    tr = spec["trunk"]
    branches = []
    n_trunks = tr.get("count", 1)
    for k in range(n_trunks):
        if n_trunks > 1:
            az = 2 * math.pi * k / n_trunks + rng.uniform(-0.4, 0.4)
            tilt = math.radians(tr.get("splay_deg", 15) * rng.uniform(0.5, 1.2))
        else:
            az = rng.uniform(0, 2 * math.pi)
            tilt = math.radians(tr.get("lean_deg", 0.0))
        d0 = np.array([math.sin(tilt) * math.cos(az), math.sin(tilt) * math.sin(az), math.cos(tilt)])
        H = tr["height"] * (rng.uniform(0.85, 1.0) if n_trunks > 1 else 1.0)
        segs = max(6, int(H / tr.get("seg_m", 0.8)))
        start = np.array([rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 0.0]) * (n_trunks > 1) - np.array([0, 0, tr.get("sink", 0.25)])
        pts = grow_polyline(start, d0, H + tr.get("sink", 0.25), segs, rng, up=tr.get("up", 0.02), noise=tr.get("sinuosity", 0.03))
        s = np.linspace(0, 1, len(pts))
        r0 = tr["radius"] * (rng.uniform(0.7, 1.0) if n_trunks > 1 else 1.0)
        rad = r0 + (tr.get("tip_radius", 0.02) - r0) * s ** tr.get("taper_pow", 0.9)
        fl = tr.get("flare")
        if fl:
            z = pts[:, 2]
            rad = rad * (1 + fl[0] * np.exp(-np.clip(z, 0, None) / fl[1]))
        branches.append(Branch(pts, rad, 0, -1, 0.0, rng.random(), H))
    levels = spec["levels"][: (max_level if max_level is not None else len(spec["levels"]))]
    env = spec.get("envelope")
    parents = list(range(len(branches)))
    for li, lv in enumerate(levels, start=1):
        new = []
        for pi in parents:
            par = branches[pi]
            t0, t1 = lv.get("range", (0.2, 1.0))
            if "count" in lv:
                cnt = lv["count"]
                n = int(rng.integers(cnt[0], cnt[1] + 1)) if isinstance(cnt, (tuple, list)) else int(cnt)
            else:
                n = int(round(lv["per_m"] * par.length * (t1 - t0) * rng.uniform(0.85, 1.15)))
            if n <= 0:
                continue
            ts = np.sort(t0 + (t1 - t0) * (np.arange(n) + rng.uniform(0.15, 0.85, n)) / n)
            az = rng.uniform(0, 2 * math.pi)
            side = 1
            for j, t in enumerate(ts):
                p, r_par, tan = _point_on(par, t)
                x = (t - t0) / max(t1 - t0, 1e-6)
                down = math.radians(lv["down"][0] + (lv["down"][1] - lv["down"][0]) * x + rng.normal(0, lv.get("down_var", 8)))
                rot = lv.get("rot", "spiral")
                if rot == "planar":
                    side = -side
                    bh = np.cross(tan, UP)
                    if np.linalg.norm(bh) < 1e-3:
                        bh = any_perp(tan)
                    bh = normalize(bh) * side
                    tilt = math.radians(rng.normal(lv.get("plane_tilt", 0), 15))
                    perp = normalize(bh * math.cos(tilt) + np.cross(bh, tan) * math.sin(tilt))
                else:
                    az += math.radians(lv.get("rot_deg", 137.5)) + rng.normal(0, 0.25)
                    b1 = any_perp(tan) if abs(tan[2]) < 0.95 else normalize(np.cross(tan, np.array([1.0, 0, 0])))
                    b1 = normalize(b1 - (b1 @ tan) * tan)
                    b2 = np.cross(tan, b1)
                    perp = math.cos(az) * b1 + math.sin(az) * b2
                d = normalize(math.cos(down) * tan + math.sin(down) * perp)
                L = lv["length"] * shape_factor(lv.get("shape", "uniform"), x) * rng.uniform(1 - lv.get("len_var", 0.2), 1 + lv.get("len_var", 0.2) * 0.5)
                if lv.get("len_rel_parent"):
                    L *= par.length / max(levels[li - 2]["length"] if li >= 2 else par.length, 1e-6)
                if li >= 2:
                    L = _envelope_len(env, p, d, L)
                segs = max(2, int(math.ceil(L / lv.get("seg_m", 0.5))))
                pts = grow_polyline(p - d * r_par * 0.5, d, L, segs, rng, up=lv.get("up", 0.0), noise=lv.get("noise", 0.08),
                                    droop_end=lv.get("droop_end", 0.0))
                rb = max(lv.get("min_rad", 0.004), r_par * lv.get("rad", 0.5) * rng.uniform(0.85, 1.1))
                s = np.linspace(0, 1, len(pts))
                rad = rb + (rb * lv.get("tip", 0.25) - rb) * s
                new.append(Branch(pts, rad, li, pi, t, rng.random(), L))
        start = len(branches)
        branches.extend(new)
        parents = list(range(start, len(branches)))
    return branches


def sides_for(r, level, lod):
    table = {0: (6, 10, 16), 1: (4, 6, 9), 2: (3, 4, 6)}[lod]
    if r > 0.25:
        return table[2] if level == 0 else table[1]
    if r > 0.06:
        return table[1]
    if r > 0.015:
        return max(3, table[0] - 1)
    return 3


def build_tree(spec, seed, lod=0, bark_tile=(0.9, 1.4), atlas_layout=None, uv_rects=None):
    """Returns (Mesh, info). Material slots: 0 bark, 1 leaves."""
    rng = np.random.default_rng(seed)
    lods = spec.get("lod", {})
    L = [dict(max_level=None, card_keep=1.0, card_scale=1.0, min_rad=0.0),
         dict(max_level=lods.get("lod1_levels", len(spec["levels"]) - 1), card_keep=0.42, card_scale=1.4, min_rad=0.012),
         dict(max_level=lods.get("lod2_levels", 1), card_keep=0.12, card_scale=2.3, min_rad=0.05)][lod]
    branches = build_skeleton(spec, rng)  # same skeleton for every LOD (same seed), then filtered
    height_norm = max(spec["trunk"]["height"], 1.0)
    maxlev = max(b.level for b in branches)
    m = Mesh()
    keep_level = L["max_level"] if L["max_level"] is not None else maxlev
    tr = spec["trunk"]
    bt = tr.get("buttress")
    for bi, b in enumerate(branches):
        if b.level > keep_level or (b.level > 0 and b.rad[0] < L["min_rad"]):
            continue
        if b.level > 0 and not spec["levels"][b.level - 1].get("geom", True):
            continue  # finest twigs are painted on the cards: anchors only
        sides = sides_for(b.rad[0], b.level, lod)
        flex0 = min(1.0, b.level / max(maxlev, 1) * 0.8)
        flex1 = min(1.0, (b.level + 1) / max(maxlev, 1) * 0.8) if b.level > 0 else 0.15
        radial = None
        if b.level == 0 and bt and lod < 2:
            lobes, amp, hb = bt
            ph = rng.uniform(0, 6.28)
            z = b.pts[:, 2]

            def radial(th, s, z=z, lobes=lobes, amp=amp, hb=hb, ph=ph):
                fall = np.exp(-np.clip(z, 0, None) / hb)[:, None]
                return 1 + amp * fall * (np.maximum(np.cos(lobes * th + ph)[None, :], -0.3) + 0.15 * np.cos(3 * lobes * th + 2 * ph)[None, :])
        if b.level == 0 and lod < 2:
            sides = max(sides, 12 if bt else sides)
        m.tube(b.pts, b.rad, sides, 0, uv_scale=bark_tile, wind=(flex0, flex1, b.phase), cap=b.level > 0,
               radial_fn=radial, height_norm=height_norm)
    # leaves
    lv = spec.get("leaves")
    n_cards = 0
    if lv and uv_rects:
        cs = atlas_layout["cell_size_m"]
        cells = lv.get("cells", list(range(len(uv_rects))))
        for b in branches:
            if b.level not in lv["levels"]:
                continue
            seg = np.linalg.norm(np.diff(b.pts, axis=0), axis=1)
            total = seg.sum()
            n = max(1, int(total * (1 - lv.get("start", 0.3)) / lv["spacing"]))
            ts = lv.get("start", 0.3) + (1 - lv.get("start", 0.3)) * (np.arange(n) + rng.uniform(0.2, 0.8, n)) / n
            if lv.get("terminal", True):
                ts = np.append(ts, 1.0)
            ts = np.repeat(ts, lv.get("cards_per", 1))
            for t in ts:
                if rng.random() > L["card_keep"]:
                    continue
                p, r, tan = _point_on(b, min(t, 1.0))
                cell = int(rng.choice(cells))
                sc = rng.uniform(*lv.get("scale", (0.85, 1.1))) * L["card_scale"]
                w, h = cs[0] * sc, cs[1] * sc
                # card direction: along the twig, rotated sideways by 'spread', lifted by 'tilt'
                side_h = np.cross(tan, UP)
                if np.linalg.norm(side_h) < 1e-3:
                    side_h = any_perp(tan)
                side_h = normalize(side_h)
                spread = math.radians(rng.normal(0, lv.get("spread_deg", 35)))
                tilt = math.radians(lv.get("tilt_deg", 10) + rng.normal(0, 12))
                if t >= 1.0:
                    spread *= 0.3
                up_dir = normalize(tan * math.cos(spread) + side_h * math.sin(spread))
                up_dir = normalize(up_dir + UP * math.sin(tilt) - UP * lv.get("droop", 0.0))
                horiz = np.cross(up_dir, UP)
                if np.linalg.norm(horiz) < 1e-3:
                    horiz = any_perp(up_dir)
                horiz = normalize(horiz)
                roll = math.radians(rng.normal(0, lv.get("roll_deg", 30)))
                nrm0 = normalize(np.cross(horiz, up_dir))
                side = normalize(horiz * math.cos(roll) + nrm0 * math.sin(roll))
                m.card(p, up_dir, side, w, h, uv_rects[cell], 1, bend=lv.get("bend", 0.08) * h,
                       wind_phase=rng.random(), flex=(0.75, 1.0), segments=lv.get("segments", 1), height_norm=height_norm)
                n_cards += 1
    info = dict(branches=len([b for b in branches if b.level <= keep_level]), cards=n_cards, levels=maxlev)
    return m, info, branches


def trunk_collision(branches, max_z=4.0, sides=8):
    """Convex hull of the trunk(s) below max_z (UCX proxy). Returns list of (V, F)."""
    hulls = []
    for b in branches:
        if b.level != 0:
            continue
        sel = b.pts[:, 2] <= max_z
        if sel.sum() < 2:
            sel[:2] = True
        P = []
        for p, r in zip(b.pts[sel], b.rad[sel]):
            P.append(prism_points(p, UP, r * 1.05, 0.001, sides)[:sides])
        P = np.concatenate(P)
        hulls.append(convex_hull_mesh(P))
    return hulls
