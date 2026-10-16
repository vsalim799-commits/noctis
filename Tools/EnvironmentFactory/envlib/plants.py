"""Non-branching plants: fan palms, fern clumps, horsetail stands, aquatic emergents, floating rosettes,
cycadophyte and low herbs. Each builder returns (Mesh, info) for one LOD (0, 1, 2).

Material slots: 0 = stem/trunk texture (asset 'bark'), 1 = leaf atlas, 2 = green stem (petioles, rachises).
Atlas cells are mapped with their attachment point at the bottom centre of the cell (fronds: tall cells).
"""
from __future__ import annotations

import math

import numpy as np

from .meshkit import Mesh, normalize, any_perp

UP = np.array([0.0, 0.0, 1.0])


def _cell_xy_to_uv(rect, cell_m, x, y, anchor=(0.5, 0.03)):
    """Physical in-plane coords (x sideways, y forward, metres) -> atlas UV of a cell."""
    u0, v0, u1, v1 = rect
    u = u0 + (anchor[0] + x / cell_m[0]) * (u1 - u0)
    v = v0 + (anchor[1] + y / cell_m[1]) * (v1 - v0)
    return np.stack([u, v], -1)


def _frond(m, base, d0, L, width, rect, mat, rng, segs, arch, fold=0.25, twist=0.0, phase=None, height_norm=2.0, droop_tip=0.0):
    """Arching ribbon (frond, strap leaf) with a V fold along its midrib. d0 = initial direction (unit)."""
    d0 = normalize(np.asarray(d0, float))
    horiz = np.array([d0[0], d0[1], 0.0])
    if np.linalg.norm(horiz) < 1e-3:
        horiz = any_perp(d0)
    horiz = normalize(horiz)
    t = np.linspace(0, 1, segs + 1)
    # direction rotates down with gravity: blend initial dir towards horizontal / downward
    pts = [np.asarray(base, float)]
    p = pts[0].copy()
    for i in range(segs):
        f = (i + 0.5) / segs
        d = normalize(d0 * (1 - f * arch) + horiz * f * arch * 0.6 - UP * (f * arch * 0.9 + droop_tip * f ** 3))
        p = p + d * L / segs
        pts.append(p.copy())
    P = np.array(pts)
    T = np.gradient(P, axis=0)
    T = normalize(T)
    side0 = normalize(np.cross(T[0], UP)) if abs(T[0][2]) < 0.98 else any_perp(T[0])
    S = []
    for i in range(len(P)):
        sd = side0 - (side0 @ T[i]) * T[i]
        a = twist * t[i]
        nrm = np.cross(T[i], normalize(sd))
        sd = normalize(normalize(sd) * math.cos(a) + nrm * math.sin(a))
        S.append(sd)
    S = np.array(S)
    Nn = normalize(np.cross(S, T))  # roughly up
    w = width * np.ones(len(P))
    rows = np.stack([P - S * w[:, None] * 0.5 - Nn * fold * w[:, None] * 0.5, P + Nn * 0.0, P + S * w[:, None] * 0.5 - Nn * fold * w[:, None] * 0.5], 1)
    u0, v0, u1, v1 = rect
    U = np.stack([np.broadcast_to(np.array([u0, (u0 + u1) * 0.5, u1])[None, :], (len(P), 3)),
                  np.broadcast_to((v0 + (v1 - v0) * t)[:, None], (len(P), 3))], -1)
    flex = np.broadcast_to((0.3 + 0.7 * t)[:, None], (len(P), 3))
    m.surface(rows, U, mat, flex=flex, phase=rng.random() if phase is None else phase, height_norm=height_norm)
    return P


# ----------------------------------------------------------------------------------------------- palm
def palm(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    H = spec["trunk_h"]
    hn = H + spec["petiole"] + spec["blade"]
    sides = (12, 8, 5)[lod]
    n = max(4, int(H / 0.25)) if lod == 0 else max(3, int(H / 0.6))
    lean = np.array([rng.normal(0, 0.06), rng.normal(0, 0.06), 1.0])
    pts = np.array([[lean[0] * z * z / max(H, 0.1) * 0.4, lean[1] * z * z / max(H, 0.1) * 0.4, z] for z in np.linspace(-0.25, H, n + 1)])
    rad = spec["trunk_r"] * (1.0 + 0.25 * np.exp(-(pts[:, 2] + 0.25) / 0.4) + 0.15 * np.clip((pts[:, 2] - H + 1.0), 0, 1))
    m.tube(pts, rad, sides, 0, uv_scale=bark_tile, wind=(0.0, 0.25, rng.random()), cap=True, height_norm=hn)
    top = pts[-1]
    nl = spec["leaves"] if lod < 2 else max(6, int(spec["leaves"] * 0.6))
    cs = layout["cell_size_m"]
    golden = math.radians(137.5)
    for i in range(nl):
        age = i / max(nl - 1, 1)  # 0 = youngest (upright) .. 1 = oldest (drooping)
        az = i * golden + rng.normal(0, 0.1)
        elev = math.radians(70 - 95 * age + rng.normal(0, 6))
        d = np.array([math.cos(az) * math.cos(elev), math.sin(az) * math.cos(elev), math.sin(elev)])
        pl = spec["petiole"] * rng.uniform(0.85, 1.1)
        segs = (5, 3, 2)[lod]
        ppts = [top + d * 0.05]
        p = ppts[0].copy()
        dd = d.copy()
        for k in range(segs):
            dd = normalize(dd - UP * spec["droop"] * 0.25 * (1 + age))
            p = p + dd * pl / segs
            ppts.append(p.copy())
        ppts = np.array(ppts)
        m.tube(ppts, np.linspace(0.035, 0.015, len(ppts)), (5, 4, 3)[lod], 2, uv_scale=(0.1, 0.5),
               wind=(0.3, 0.7, rng.random()), cap=False, height_norm=hn)
        # Blade: costapalmate fan, plicate, sagging; old leaves use the dry cell.
        cell = 3 if age > 0.9 and rng.random() < 0.7 else int(rng.choice([0, 1, 2], p=[0.45, 0.35, 0.2]))
        fwd = normalize(dd + UP * 0.25)
        side = normalize(np.cross(fwd, UP)) if abs(fwd[2]) < 0.98 else any_perp(fwd)
        nrm = normalize(np.cross(side, fwd))
        R = 0.47 * cs[1] * (spec["blade"] / 1.6) * rng.uniform(0.9, 1.05)
        scale = spec["blade"] / 1.6
        na, nr = [(24, 5), (12, 3), (6, 2)][lod]
        spread = math.radians(210) / 2
        phis = np.linspace(-spread, spread, na + 1)
        rs = np.linspace(0.02, 1.0, nr + 1) * R
        PH, RR = np.meshgrid(phis, rs, indexing="ij")
        x = RR * np.sin(PH)
        y = RR * np.cos(PH)
        rr = RR / R
        pleat = 0.0 if lod == 2 else 0.04 * rr * np.where((np.arange(na + 1) % 2 == 0)[:, None], 1.0, -1.0)
        # costapalmate fold: both halves of the blade rise from the costa (V section), segment tips droop
        fold = 0.42 * np.abs(x) * (0.6 + 0.4 * (1 - age))
        sag = spec["droop"] * rr ** 2 * (0.4 + 0.6 * age) * R + 0.3 * R * rr ** 2 * (np.abs(PH) / spread) ** 2
        roll = rng.normal(0, 0.12)
        P = ppts[-1] + x[..., None] * side + y[..., None] * fwd + (pleat + fold - sag)[..., None] * nrm
        P = P + (x * roll)[..., None] * nrm
        UV = _cell_xy_to_uv(rects[cell], (cs[0] * scale, cs[1] * scale), x, y, anchor=(0.5, 0.03))
        flex = 0.6 + 0.4 * rr
        m.surface(P, UV, 1, flex=flex, phase=rng.random(), height_norm=hn)
    return m, dict(leaves=nl)


# ----------------------------------------------------------------------------------------------- fern
def fern(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    nf = spec["fronds"] if lod == 0 else max(5, int(spec["fronds"] * (0.7 if lod == 1 else 0.45)))
    segs = (8, 5, 3)[lod]
    spread = spec.get("spread", 0.0)
    for i in range(nf):
        az = 2 * math.pi * i / nf + rng.normal(0, 0.25)
        rise = math.radians(rng.uniform(*spec["rise"]))
        d0 = np.array([math.cos(az) * math.cos(rise), math.sin(az) * math.cos(rise), math.sin(rise)])
        L = rng.uniform(*spec["length"]) * (1.0 if lod == 0 else 1.05)
        cell = int(rng.choice(spec["cells"]))
        if cell == 3:
            L *= 0.85
        scale = L / cs[1]
        r0 = spec["rosette_r"] + spread * math.sqrt(rng.random())
        base = np.array([math.cos(az) * r0 * rng.uniform(0.3, 1.0), math.sin(az) * r0 * rng.uniform(0.3, 1.0), -0.03])
        arch = spec["arch"] * rng.uniform(0.8, 1.2) * (1.6 if cell == 3 else 1.0)
        _frond(m, base, d0, L, cs[0] * scale * (1.6 if lod == 2 else 1.0), rects[cell], 1, rng, segs, arch,
               fold=0.35, twist=rng.normal(0, 0.3), height_norm=max(spec["length"]))
    if lod == 0 and spec.get("crosiers", True):
        for k in range(3):  # young coiled fronds (crosiers) at the centre
            az = rng.uniform(0, 2 * math.pi)
            h = rng.uniform(0.12, 0.3)
            th = np.linspace(0, 1, 14)
            coil = 0.035
            pts = np.stack([np.cos(az) * (0.02 + coil * np.sin(th * 5)), np.sin(az) * (0.02 + coil * np.sin(th * 5)),
                            h * np.minimum(th * 1.3, 1) + coil * (1 - np.cos(th * 5)) * 0.5 * (th > 0.7)], 1)
            m.tube(pts, np.linspace(0.006, 0.004, len(pts)), 4, 2, uv_scale=(0.05, 0.3), wind=(0.2, 0.6, rng.random()),
                   height_norm=max(spec["length"]))
    return m, dict(fronds=nf)


# ----------------------------------------------------------------------------------------------- horsetails
def horsetail(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    ns = spec["stems"] if lod == 0 else int(spec["stems"] * (0.6 if lod == 1 else 0.3))
    hmax = spec["height"][1]
    cs = layout["cell_size_m"] if layout else (0.26, 0.26)
    for i in range(ns):
        rr = spec["radius"] * math.sqrt(rng.random()) * (0.5 + 0.5 * rng.random())
        a = rng.uniform(0, 2 * math.pi)
        base = np.array([rr * math.cos(a), rr * math.sin(a), -0.05])
        h = rng.uniform(*spec["height"]) * (1 - 0.3 * rr / spec["radius"])
        lean = np.array([rng.normal(0, 0.08), rng.normal(0, 0.08), 1.0]) + np.array([base[0], base[1], 0]) * 0.25
        inter = rng.uniform(*spec["internode"])
        nseg = max(2, int(h / (inter * (2.0 if lod == 0 else 4.0))))
        z = np.linspace(0, 1, nseg + 1)
        d = normalize(lean)
        pts = base + (z * h)[:, None] * d + (z ** 2 * h * 0.08)[:, None] * normalize(np.array([d[0], d[1], 0]) + 1e-6)
        r = rng.uniform(*spec["stem_r"])
        rad = r * (1 - 0.5 * z)
        m.tube(pts, rad, (5, 4, 3)[lod], 0, uv_scale=(2 * math.pi * r, inter), wind=(0.1, 1.0, rng.random()),
               cap=not spec["strobili"], height_norm=hmax)
        if spec.get("strobili") and rng.random() < spec["strobili"] and lod < 2:
            top = pts[-1]
            sz = np.linspace(0, 1, 6)
            sp = top + (sz * 0.035)[:, None] * d
            srad = r * (0.8 + 1.4 * np.sin(np.pi * np.clip(sz * 0.9 + 0.05, 0, 1)))
            srad[-1] = r * 0.2
            # strobilus: UV pinned on the dark sheath band of the stem texture
            m.tube(sp, srad, (6, 4, 3)[lod], 0, uv_scale=(1e3, 1e3), v_offset=0.0, wind=(1.0, 1.0, rng.random()), height_norm=hmax)
        if spec.get("branched") and rects and lod < 2:
            # whorls of branches: shallow umbrella (cone) cards at nodes above 30 % of the height
            nn = int(h / inter)
            for k in range(2, nn):
                if lod == 1 and k % 2:
                    continue
                f = k * inter / h
                if f < 0.3 or f > 0.97:
                    continue
                c = base + d * k * inter
                R = cs[0] * 0.5 * (1.15 - 0.6 * f) * rng.uniform(0.85, 1.1)
                kk = 8 if lod == 0 else 5
                th = np.linspace(0, 2 * math.pi, kk, endpoint=False) + rng.uniform(0, 1)
                ring = c + np.stack([np.cos(th) * R, np.sin(th) * R, np.full(kk, -R * 0.35 + R * 0.6 * (1 - f))], 1)
                cell = int(rng.integers(0, len(rects)))
                u0, v0, u1, v1 = rects[cell]
                uc = ((u0 + u1) / 2, (v0 + v1) / 2)
                Rf = R / cs[0]
                uvr = np.stack([uc[0] + np.cos(th) * Rf * (u1 - u0), uc[1] + np.sin(th) * Rf * (v1 - v0)], 1)
                m.fan(c, ring, uc, uvr, 1, flex=0.8, phase=rng.random(), height_norm=hmax)
    return m, dict(stems=ns)


# ----------------------------------------------------------------------------------------------- aquatic
def emergent(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    if spec["kind"] == "strap":
        n = spec["leaves"] if lod == 0 else int(spec["leaves"] * (0.6 if lod == 1 else 0.35))
        hmax = spec["length"][1]
        for i in range(n):
            az = rng.uniform(0, 2 * math.pi)
            rise = math.radians(rng.uniform(62, 86))
            d0 = np.array([math.cos(az) * math.cos(rise), math.sin(az) * math.cos(rise), math.sin(rise)])
            L = rng.uniform(*spec["length"])
            r0 = spec["radius"] * math.sqrt(rng.random())
            base = np.array([math.cos(az) * r0, math.sin(az) * r0, -0.1])
            cell = int(rng.choice([0, 1, 2, 3], p=[0.3, 0.3, 0.3, 0.1]))
            w = cs[0] * (L / cs[1]) * (1.6 if lod == 2 else 1.0)
            _frond(m, base, d0, L, w, rects[cell], 1, rng, (6, 4, 2)[lod], arch=rng.uniform(0.1, 0.45), fold=0.4,
                   twist=rng.normal(0, 0.6), height_norm=hmax, droop_tip=rng.uniform(0, 0.4))
        return m, dict(leaves=n)
    n = spec["leaves"] if lod == 0 else int(spec["leaves"] * (0.65 if lod == 1 else 0.4))
    hmax = spec["petiole"][1] + spec["blade"][1]
    for i in range(n):
        az = rng.uniform(0, 2 * math.pi)
        r0 = spec["radius"] * math.sqrt(rng.random())
        base = np.array([math.cos(az) * r0, math.sin(az) * r0, -0.15])
        pl = rng.uniform(*spec["petiole"])
        lean = math.radians(rng.uniform(5, 30))
        d = np.array([math.cos(az) * math.sin(lean), math.sin(az) * math.sin(lean), math.cos(lean)])
        segs = (4, 2, 1)[lod]
        pts = base + np.linspace(0, 1, segs + 1)[:, None] * d * pl
        pts[:, 0] += np.linspace(0, 1, segs + 1) ** 2 * math.cos(az) * 0.08
        pts[:, 1] += np.linspace(0, 1, segs + 1) ** 2 * math.sin(az) * 0.08
        if lod < 2:
            m.tube(pts, np.linspace(0.008, 0.005, len(pts)), (5, 3, 3)[lod], 2, uv_scale=(0.05, 0.4),
                   wind=(0.2, 0.7, rng.random()), cap=False, height_norm=hmax)
        bl = rng.uniform(*spec["blade"])
        cell = int(rng.choice([0, 1, 2, 3], p=[0.3, 0.3, 0.3, 0.1]))
        tilt = math.radians(rng.uniform(25, 70))
        fwd = normalize(np.array([math.cos(az) * math.cos(tilt), math.sin(az) * math.cos(tilt), math.sin(tilt)]))
        side = normalize(np.cross(fwd, UP))
        w = cs[0] * bl / cs[1] * 1.0
        m.card(pts[-1], fwd, side, w, bl, rects[cell], 1, bend=0.12 * bl, segments=(2, 1, 1)[lod], flex=(0.7, 1.0),
               wind_phase=rng.random(), height_norm=hmax)
    return m, dict(leaves=n)


def floating(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    n = spec["rosettes"] if lod == 0 else max(3, int(spec["rosettes"] * (0.7 if lod == 1 else 0.4)))
    half = spec["patch"] * 0.5
    placed = []
    for i in range(n * 4):
        if len(placed) >= n:
            break
        p = rng.uniform(-half, half, 2)
        s = rng.uniform(0.55, 1.0)
        if any(np.linalg.norm(p - q) < (s + t) * cs[0] * 0.42 for q, t in placed):
            continue
        placed.append((p, s))
    for p, s in placed:
        cell = int(rng.integers(0, 3))
        a = rng.uniform(0, 2 * math.pi)
        side = np.array([math.cos(a), math.sin(a), 0.0])
        up = np.array([-math.sin(a), math.cos(a), 0.0])
        w = cs[0] * s
        base = np.array([p[0], p[1], 0.004]) - up * w * 0.5
        m.card(base, up, side, w, w, rects[cell], 1, bend=0.0, normal=UP, flex=(0.6, 0.6), wind_phase=rng.random(), height_norm=1.0)
    if lod < 2:
        for k in range(5):
            p = rng.uniform(-half * 1.1, half * 1.1, 2)
            a = rng.uniform(0, 2 * math.pi)
            side = np.array([math.cos(a), math.sin(a), 0.0])
            up = np.array([-math.sin(a), math.cos(a), 0.0])
            w = cs[0] * 0.6
            m.card(np.array([p[0], p[1], 0.003]) - up * w * 0.5, up, side, w, w, rects[3], 1, normal=UP, flex=(0.6, 0.6),
                   height_norm=1.0)
    return m, dict(rosettes=len(placed))


# ----------------------------------------------------------------------------------------------- cycad / herb
def cycad(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    H = spec["trunk_h"]
    hn = H + spec["length"][1]
    z = np.linspace(-0.15, H, (8, 4, 2)[lod] + 1)
    pts = np.stack([np.zeros_like(z), np.zeros_like(z), z], 1)
    rad = spec["trunk_r"] * (1.15 - 0.3 * (z / H) ** 2)
    m.tube(pts, rad, (12, 8, 5)[lod], 0, uv_scale=(0.45, 0.6), wind=(0, 0.1, 0.0), cap=True, height_norm=hn)
    cs = layout["cell_size_m"]
    n = spec["leaves"] if lod == 0 else int(spec["leaves"] * (0.7 if lod == 1 else 0.45))
    for i in range(n):
        az = i * math.radians(137.5)
        age = i / max(n - 1, 1)
        rise = math.radians(65 - 55 * age + rng.normal(0, 5))
        d0 = np.array([math.cos(az) * math.cos(rise), math.sin(az) * math.cos(rise), math.sin(rise)])
        L = rng.uniform(*spec["length"])
        cell = 3 if age > 0.9 else int(rng.integers(0, 3))
        base = np.array([0, 0, H - 0.05]) + np.array([math.cos(az), math.sin(az), 0]) * spec["trunk_r"] * 0.5
        _frond(m, base, d0, L, cs[0] * L / cs[1] * (1.4 if lod == 2 else 1.0), rects[cell], 1, rng, (6, 4, 2)[lod],
               arch=0.25 + 0.4 * age, fold=0.3, height_norm=hn)
    return m, dict(leaves=n)


def herb(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    n = spec["leaves"] if lod == 0 else int(spec["leaves"] * (0.6 if lod == 1 else 0.35))
    for i in range(n):
        az = rng.uniform(0, 2 * math.pi)
        r0 = spec["radius"] * math.sqrt(rng.random()) * 0.5
        base = np.array([math.cos(az) * r0, math.sin(az) * r0, -0.02])
        tilt = math.radians(rng.uniform(25, 70))
        fwd = normalize(np.array([math.cos(az) * math.cos(tilt), math.sin(az) * math.cos(tilt), math.sin(tilt)]))
        side = normalize(np.cross(fwd, UP))
        s = rng.uniform(0.75, 1.15) * (1.3 if lod == 2 else 1.0)
        cell = int(rng.integers(0, 4))
        m.card(base, fwd, side, cs[0] * s, cs[1] * s, rects[cell], 1, bend=0.08 * s, segments=(2, 1, 1)[lod],
               flex=(0.5, 1.0), wind_phase=rng.random(), height_norm=0.5)
    return m, dict(leaves=n)


BUILDERS = dict(palm=palm, fern=fern, horsetail=horsetail, emergent=emergent, floating=floating, cycad=cycad, herb=herb)
