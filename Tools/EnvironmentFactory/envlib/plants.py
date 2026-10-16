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
# Atlas 'palm_segments' cells (foliage._p_palm_seg)
SEG_GREEN, SEG_OLD, SEG_DEAD, SEG_PAIR, SEG_GROUP, SEG_GROUP_DEAD = (0, 1, 2), 3, 4, 5, 6, 7


def _palm_blade(m, H, f, s, n, spec, rng, lod, rects, age, dead, hn, phase):
    """One costapalmate blade as real 3D geometry (Sabal-type leaf).

    Polar layout around the hastula H: segments fan over 'spread_deg'; each segment springs from the costa (the
    rachis extension running into the blade over costa x R), so central segments start far along the costa and
    lateral ones near the hastula. Both halves of the blade rise from the costa (V fold), the costa arches down,
    segments are fused over the inner part and free over the outer 'split' fraction, where they narrow and their
    tips droop independently. LOD0: every segment is folded along its middle (induplicate V section);
    LOD1: segment pairs, flat; LOD2: groups of four, flat."""
    R = spec["blade_r"] * rng.uniform(0.85, 1.08) * (0.85 if dead else 1.0)
    K0 = spec["segments"]
    group = (1, 2, 4)[lod]
    K = max(4, int(round(K0 / group)))
    spread = math.radians(spec.get("spread_deg", 230.0) * (0.62 if dead else rng.uniform(0.9, 1.05)))
    costa = spec.get("costa", 0.5)
    ts = 1.0 - spec.get("split", 0.42)
    fold = spec.get("fold", 0.45) * (1.15 - 0.5 * age) * (0.35 if dead else 1.0)
    arch = spec.get("arch", 0.35) * (0.7 + 0.8 * age)
    droop = spec.get("tip_droop", 0.35) * (0.8 + 0.7 * age) * (1.6 if dead else 1.0)
    rows = (5, 3, 2)[lod]
    tsamp = np.unique(np.concatenate([np.linspace(0, ts, max(2, rows // 2) + 1), np.linspace(ts, 1, rows - rows // 2 + 1)]))
    dth = spread / K
    plic = spec.get("plication", 0.30) if lod == 0 else 0.0

    def inplane(theta, t):
        ra = costa * (1.0 - np.minimum(np.abs(theta) / (spread * 0.5), 1.0))
        rm = R * (0.80 + 0.20 * np.cos(theta * 0.85))
        ax, ay = 0.0, ra * R
        mx, my = rm * np.sin(theta), rm * np.cos(theta)
        return ax + (mx - ax) * t, ay + (my - ay) * t

    def lift(x, y, t, drp):
        z = fold * np.abs(x) - arch * (y / R) ** 2 * R - arch * 0.25 * (np.abs(x) / R) ** 2 * R
        z = z - drp * R * np.clip((t - ts) / max(1 - ts, 1e-6), 0, 1) ** 2
        return z

    for k in range(K):
        th = -spread * 0.5 + (k + 0.5) * dth
        drp = droop * rng.uniform(0.7, 1.35)
        if dead:
            cell = SEG_GROUP_DEAD if lod == 2 else SEG_DEAD
        elif lod == 0:
            cell = SEG_OLD if (age > 0.85 and rng.random() < 0.5) else int(rng.choice(SEG_GREEN, p=[0.45, 0.3, 0.25]))
        else:
            cell = SEG_PAIR if lod == 1 else SEG_GROUP
        u0, v0, u1, v1 = rects[cell]
        cols = 3 if plic > 0 else 2
        P = np.zeros((len(tsamp), cols, 3))
        UV = np.zeros((len(tsamp), cols, 2))
        for r_i, t in enumerate(tsamp):
            free = max(0.0, (t - ts) / max(1 - ts, 1e-6))
            half = dth * 0.5 * (1.0 - 0.45 * free)   # free part narrows (the texture tapers it further)
            ths = [th - half, th, th + half] if cols == 3 else [th - half, th + half]
            us = [u0, 0.5 * (u0 + u1), u1] if cols == 3 else [u0, u1]
            for c_i, (tt, uu) in enumerate(zip(ths, us)):
                x, y = inplane(np.array(tt), t)
                z = lift(x, y, t, drp)
                if cols == 3 and c_i == 1:
                    # crease of the induplicate fold: below the segment edges by plic x half-width
                    rr = math.hypot(float(x), float(y))
                    z = z - plic * rr * half
                P[r_i, c_i] = H + float(x) * s + float(y) * f + float(z) * n
                UV[r_i, c_i] = (uu, v0 + (v1 - v0) * t)
        flex = np.broadcast_to((0.55 + 0.45 * tsamp)[:, None], (len(tsamp), cols))
        m.surface(P, UV, 1, flex=flex, phase=phase, height_norm=hn)


def palm(spec, seed, lod, rects, layout, bark_tile):
    """Sabal-type fan palm: stipe with persistent leaf bases, spherical crown of costapalmate leaves (young ones
    erect and folded, old ones drooping), optional skirt of dead hanging leaves; acaulescent variant (trunk_h ~ 0):
    leaves rise from the ground on long erect petioles, blades held more upright and less costapalmate."""
    rng = np.random.default_rng(seed)
    m = Mesh()
    H = spec["trunk_h"]
    hn = H + spec["petiole"] + spec["blade_r"]
    sides = (12, 8, 5)[lod]
    n = max(4, int(H / 0.25)) if lod == 0 else max(3, int(H / 0.6))
    lean = np.array([rng.normal(0, 0.05), rng.normal(0, 0.05), 1.0])
    pts = np.array([[lean[0] * z * z / max(H, 0.1) * 0.4, lean[1] * z * z / max(H, 0.1) * 0.4, z] for z in np.linspace(-0.25, H, n + 1)])
    rad = spec["trunk_r"] * (1.0 + 0.25 * np.exp(-(pts[:, 2] + 0.25) / 0.4) + 0.18 * np.clip((pts[:, 2] - H + 1.0), 0, 1))
    m.tube(pts, rad, sides, 0, uv_scale=bark_tile, wind=(0.0, 0.2, rng.random()), cap=True, height_norm=hn)
    top = pts[-1]
    acaul = H < 1.0
    nl = spec["leaves"] if lod == 0 else max(6, int(spec["leaves"] * (0.75 if lod == 1 else 0.5)))
    nd = spec.get("dead_leaves", 0) if lod == 0 else int(spec.get("dead_leaves", 0) * (0.6 if lod == 1 else 0.4))
    golden = math.radians(137.5)
    leaves = [(i / max(nl - 1, 1), False) for i in range(nl)] + [(1.0, True)] * nd
    for i, (age, dead) in enumerate(leaves):
        az = i * golden + rng.normal(0, 0.12)
        if dead:  # skirt: petiole snapped down along the stipe, blade hanging
            elev = math.radians(rng.uniform(-82, -68))
        elif acaul:
            elev = math.radians(78 - 38 * age + rng.normal(0, 5))
        else:  # spherical crown: from erect spear leaves to old leaves below the horizontal
            elev = math.radians(74 - 96 * age + rng.normal(0, 6))
        d = np.array([math.cos(az) * math.cos(elev), math.sin(az) * math.cos(elev), math.sin(elev)])
        pl = spec["petiole"] * rng.uniform(0.85, 1.1) * (0.75 if dead else 1.0)
        segs = (5, 3, 2)[lod]
        r0 = spec["trunk_r"] * 0.8
        base = top + np.array([math.cos(az) * r0, math.sin(az) * r0, -0.15 - (0.35 * age if not acaul else 0.0) - (0.4 if dead else 0.0)])
        if acaul:
            base = np.array([math.cos(az) * r0 * 0.6, math.sin(az) * r0 * 0.6, -0.05])
        ppts = [base]
        p = base.copy()
        dd = d.copy()
        for k in range(segs):
            dd = normalize(dd - UP * spec.get("petiole_sag", 0.06) * (1 + age))
            p = p + dd * pl / segs
            ppts.append(p.copy())
        ppts = np.array(ppts)
        wdt = spec.get("petiole_w", 0.03)
        m.tube(ppts, np.linspace(wdt * 1.6, wdt * 0.7, len(ppts)), (5, 4, 3)[lod], 2 if not dead else 0, uv_scale=(0.1, 0.5),
               wind=(0.25, 0.6, rng.random()), cap=False, height_norm=hn)
        # blade frame: forward axis tilted below the petiole (blades are held flatter than their petioles)
        tilt = math.radians(spec.get("blade_tilt_deg", 25.0) * (0.5 + 0.6 * age))
        if dead:
            fwd = normalize(dd * 0.3 - UP)
        else:
            fwd = normalize(dd - UP * math.tan(tilt))
        side = normalize(np.cross(fwd, UP)) if abs(fwd[2]) < 0.97 else any_perp(fwd)
        nrm = normalize(np.cross(side, fwd))
        if nrm[2] < 0 and not dead:
            nrm = -nrm
            side = -side
        # small roll of the blade around its axis
        roll = rng.normal(0, 0.15)
        side, nrm = side * math.cos(roll) + nrm * math.sin(roll), nrm * math.cos(roll) - side * math.sin(roll)
        _palm_blade(m, ppts[-1], fwd, side, nrm, spec, rng, lod, rects, age, dead, hn, rng.random())
    return m, dict(leaves=nl, dead_leaves=nd, segments_per_leaf=spec["segments"])


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
        dry = cell in spec.get("dry_cells", (6, 7))
        if dry:
            L *= 0.85
        scale = L / cs[1]
        r0 = spec["rosette_r"] + spread * math.sqrt(rng.random())
        base = np.array([math.cos(az) * r0 * rng.uniform(0.3, 1.0), math.sin(az) * r0 * rng.uniform(0.3, 1.0), -0.03])
        arch = spec["arch"] * rng.uniform(0.8, 1.2) * (1.6 if dry else 1.0)
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
def _lathe(m, axis_pts, radii, sides, rect, mat, flex, phase, height_norm):
    """Surface of revolution along a polyline with UVs mapped into an atlas rectangle (u around, v along)."""
    axis_pts = np.asarray(axis_pts, float)
    T = normalize(np.gradient(axis_pts, axis=0))
    N0 = any_perp(T[0])
    B0 = np.cross(T[0], N0)
    th = np.linspace(0, 2 * math.pi, sides + 1)
    P = axis_pts[:, None, :] + np.asarray(radii)[:, None, None] * (np.cos(th)[None, :, None] * N0 + np.sin(th)[None, :, None] * B0)
    u0, v0, u1, v1 = rect
    t = np.linspace(0, 1, len(axis_pts))
    UV = np.stack([np.broadcast_to(u0 + (u1 - u0) * th / (2 * math.pi), P.shape[:2]),
                   np.broadcast_to((v0 + (v1 - v0) * t)[:, None], P.shape[:2])], -1)
    m.surface(P, UV, mat, flex=np.full(P.shape[:2], flex), phase=phase, height_norm=height_norm)


def horsetail(spec, seed, lod, rects, layout, bark_tile):
    """Equisetum-type stand: erect, nearly parallel jointed stems rising from a creeping rhizome (bases scattered
    over the clump area, not splayed from one point), closed stem tips; elongate, apiculate strobili (cones) on a
    few fertile stems; optional whorls of branches from ~0.3 m up (branched form)."""
    from .foliage import STROBILUS_PATCH
    rng = np.random.default_rng(seed)
    m = Mesh()
    ns = spec["stems"] if lod == 0 else int(spec["stems"] * (0.6 if lod == 1 else 0.35))
    rscale = (1.0, 1.25, 1.7)[lod]  # thinner LODs keep their coverage with thicker stems
    hmax = spec["height"][1]
    cs = layout["cell_size_m"] if layout else (0.26, 0.26)
    R = spec["radius"]
    lv = spec.get("lean", 0.035)
    # rhizome: a few runners; stems emerge along them (gives elongated, open clumps instead of a bouquet)
    runners = []
    for k in range(spec.get("runners", 3)):
        a = rng.uniform(0, 2 * math.pi)
        c0 = rng.normal(0, R * 0.25, 2)
        runners.append((c0, np.array([math.cos(a), math.sin(a)])))
    srect = None
    if rects:
        u0, v0, u1, v1 = rects[3]
        srect = (u0, v1 - STROBILUS_PATCH * (v1 - v0), u0 + STROBILUS_PATCH * (u1 - u0), v1)
    for i in range(ns):
        c0, rd = runners[i % len(runners)]
        along = rng.uniform(-R, R)
        off = rng.normal(0, R * 0.18)
        bxy = c0 + rd * along + np.array([-rd[1], rd[0]]) * off
        base = np.array([bxy[0], bxy[1], -0.05])
        dist = min(1.0, float(np.linalg.norm(bxy)) / max(R, 1e-6))
        h = rng.uniform(*spec["height"]) * (1 - 0.15 * dist)
        d = normalize(np.array([rng.normal(0, lv), rng.normal(0, lv), 1.0]))
        inter = rng.uniform(*spec["internode"])
        nseg = max(2, int(h / (inter * (2.0 if lod == 0 else 4.0))))
        z = np.linspace(0, 1, nseg + 1)
        bow = normalize(np.array([d[0], d[1], 0]) + 1e-6)
        pts = base + (z * h)[:, None] * d + (z ** 2 * h * 0.03)[:, None] * bow
        r = rng.uniform(*spec["stem_r"]) * rscale
        rad = r * (1 - 0.35 * z)
        fertile = spec.get("strobili", 0.0) > 0 and rng.random() < spec["strobili"] and lod < 2 and srect is not None
        m.tube(pts, rad, (6, 4, 3)[lod], 0, uv_scale=(2 * math.pi * r, inter), wind=(0.1, 1.0, rng.random()),
               cap=not fertile, height_norm=hmax)
        if fertile:
            # cone: ellipsoidal-cylindrical, max radius ~1.3 x the stem tip, length ~2.8 x its diameter, apiculate tip
            rt = rad[-1]
            rmax = 1.3 * rt
            L = 2.8 * 2 * rmax
            sz = np.linspace(0, 1, 9)
            prof = np.where(sz < 0.35, 1.0 + 0.3 * np.sin(0.5 * math.pi * sz / 0.35), 1.3 * np.clip((1 - sz) / 0.65, 0, 1) ** 0.55)
            prof[-1] = 0.0
            sp = pts[-1] + (sz * L)[:, None] * d
            _lathe(m, sp, rt * prof, 6 if lod == 0 else 4, srect, 1, 1.0, rng.random(), hmax)
        if spec.get("branched") and rects and lod < 2:
            nn = int(h / inter)
            for k in range(2, nn):
                if lod == 1 and k % 2:
                    continue
                f = k * inter / h
                zc = base[2] + d[2] * k * inter
                if f < 0.35 or f > 0.97 or zc < 0.30:
                    continue  # no whorls on the lowest nodes: nothing lies on the ground
                c = base + d * k * inter
                Rw = cs[0] * 0.5 * (1.15 - 0.6 * f) * rng.uniform(0.85, 1.1) * spec.get("whorl_scale", 1.0)
                kk = 8 if lod == 0 else 5
                th = np.linspace(0, 2 * math.pi, kk, endpoint=False) + rng.uniform(0, 1)
                dz = -Rw * spec.get("whorl_droop", 0.35) + Rw * 0.6 * (1 - f)
                dz = max(dz, 0.12 - zc)
                ring = c + np.stack([np.cos(th) * Rw, np.sin(th) * Rw, np.full(kk, dz)], 1)
                cell = int(rng.integers(0, len(rects)))
                u0, v0, u1, v1 = rects[cell]
                uc = ((u0 + u1) / 2, (v0 + v1) / 2)
                # the painted whorl fills ~0.47 of the cell around its centre, whatever the branch length
                uvr = np.stack([uc[0] + np.cos(th) * 0.47 * (u1 - u0), uc[1] + np.sin(th) * 0.47 * (v1 - v0)], 1)
                m.fan(c, ring, uc, uvr, 1, flex=0.8, phase=rng.random(), height_norm=hmax)
    return m, dict(stems=ns)


# ----------------------------------------------------------------------------------------------- aquatic
def emergent(spec, seed, lod, rects, layout, bark_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    if spec["kind"] == "rosette":
        # semi-emergent rosette (Stratiotes-like habit, SPECULATIVE): thick toothed strap leaves radiating from a
        # sunken centre, arching outwards; neither a grass tuft nor an iris fan
        n = spec["leaves"] if lod == 0 else max(6, int(spec["leaves"] * (0.65 if lod == 1 else 0.45)))
        hmax = spec["length"][1]
        golden = math.radians(137.5)
        for i in range(n):
            age = i / max(n - 1, 1)
            az = i * golden + rng.normal(0, 0.15)
            rise = math.radians(76 - 40 * age + rng.normal(0, 5))
            d0 = np.array([math.cos(az) * math.cos(rise), math.sin(az) * math.cos(rise), math.sin(rise)])
            L = rng.uniform(*spec["length"]) * (0.75 + 0.35 * age)
            cell = int(rng.choice([0, 1, 2, 3], p=[0.32, 0.32, 0.26, 0.10]))
            w = cs[0] * (L / cs[1]) * (1.4 if lod == 2 else 1.0)
            base = np.array([math.cos(az) * 0.03, math.sin(az) * 0.03, -0.08])
            _frond(m, base, d0, L, w, rects[cell], 1, rng, (6, 4, 2)[lod], arch=0.22 + 0.3 * age, fold=0.45,
                   twist=rng.normal(0, 0.15), height_norm=hmax, droop_tip=0.15 * age)
        return m, dict(leaves=n)
    if spec["kind"] == "strap" and spec.get("fans"):
        # Equitant fans (iris / bur-reed-like habit): each shoot is a flat fan of overlapping strap leaves in one
        # plane, so the clump reads as a few broad fans rather than a grass tuft.
        nf = spec["fans"] if lod < 2 else max(2, spec["fans"] // 2)
        per = spec["leaves_per_fan"] if lod == 0 else max(3, spec["leaves_per_fan"] - (2 if lod == 1 else 3))
        hmax = spec["length"][1]
        ws = spec.get("width_scale", 1.0)
        n = 0
        for j in range(nf):
            phi = rng.uniform(0, math.pi)
            r0 = spec["radius"] * math.sqrt(rng.random())
            ba = rng.uniform(0, 2 * math.pi)
            base0 = np.array([math.cos(ba) * r0, math.sin(ba) * r0, -0.1])
            plane = np.array([math.cos(phi), math.sin(phi), 0.0])
            for k in range(per):
                u = (k - (per - 1) / 2) / max(1, (per - 1) / 2)
                ang = math.radians(spec.get("fan_spread_deg", 24)) * u + rng.normal(0, 0.04)
                d0 = normalize(plane * math.sin(ang) + np.array([0, 0, math.cos(ang)]) + rng.normal(0, 0.03, 3))
                L = rng.uniform(*spec["length"]) * (1.0 - 0.28 * abs(u))
                base = base0 + plane * u * 0.025
                cell = int(rng.choice([0, 1, 2, 3], p=[0.3, 0.3, 0.3, 0.1]))
                w = cs[0] * (L / cs[1]) * ws * (1.4 if lod == 2 else 1.0)
                stiff = 0.35 if spec.get("stiff") else 1.0  # sword-like, upright leaves
                _frond(m, base, d0, L, w, rects[cell], 1, rng, (6, 4, 2)[lod], arch=(0.12 + 0.3 * abs(u)) * stiff, fold=0.3,
                       twist=rng.normal(0, 0.25) * stiff, height_norm=hmax,
                       droop_tip=(rng.uniform(0, 0.25) + 0.3 * abs(u)) * stiff)
                n += 1
        return m, dict(leaves=n, fans=nf)
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
    """Low dicot herb, no showy flowers: basal rosette of petiolate leaves plus a few leafy stems reaching
    spec['height'] (angio_herb height in the project data: 0.5 m)."""
    rng = np.random.default_rng(seed)
    m = Mesh()
    cs = layout["cell_size_m"]
    hmax = spec.get("height", 0.45)
    n = spec["leaves"] if lod == 0 else int(spec["leaves"] * (0.6 if lod == 1 else 0.35))
    for i in range(n):
        az = rng.uniform(0, 2 * math.pi)
        r0 = spec["radius"] * math.sqrt(rng.random()) * 0.4
        base = np.array([math.cos(az) * r0, math.sin(az) * r0, -0.02])
        tilt = math.radians(rng.uniform(25, 60))
        fwd = normalize(np.array([math.cos(az) * math.cos(tilt), math.sin(az) * math.cos(tilt), math.sin(tilt)]))
        side = normalize(np.cross(fwd, UP))
        sc_ = rng.uniform(0.75, 1.1) * (1.3 if lod == 2 else 1.0)
        cell = int(rng.integers(0, 4))
        m.card(base, fwd, side, cs[0] * sc_, cs[1] * sc_, rects[cell], 1, bend=0.08 * sc_, segments=(2, 1, 1)[lod],
               flex=(0.5, 1.0), wind_phase=rng.random(), height_norm=hmax)
    ns = spec.get("stems", 3) if lod < 2 else 1
    for k in range(ns):
        az = rng.uniform(0, 2 * math.pi)
        h = hmax * rng.uniform(0.75, 1.0)
        top = np.array([math.cos(az) * 0.06, math.sin(az) * 0.06, h])
        pts = np.array([[0, 0, -0.02], top * 0.5 + np.array([0, 0, 0.0]), top])
        if lod < 2:
            m.tube(pts, np.array([0.004, 0.0035, 0.002]), (4, 3, 3)[lod], 2, uv_scale=(0.05, 0.3), wind=(0.2, 0.9, rng.random()),
                   height_norm=hmax)
        nl = (5, 3, 2)[lod]
        for j in range(nl):
            t = 0.3 + 0.7 * (j + 0.5) / nl
            p = pts[0] + (top - pts[0]) * t
            a = az + j * math.radians(137.5)
            tilt = math.radians(rng.uniform(15, 40))
            fwd = normalize(np.array([math.cos(a) * math.cos(tilt), math.sin(a) * math.cos(tilt), math.sin(tilt)]))
            side = normalize(np.cross(fwd, UP))
            sc_ = rng.uniform(0.45, 0.65) * (1 - 0.3 * t) * (1.3 if lod == 2 else 1.0)
            m.card(p, fwd, side, cs[0] * sc_, cs[1] * sc_, rects[int(rng.integers(0, 4))], 1, bend=0.05 * sc_,
                   segments=1, flex=(0.6, 1.0), wind_phase=rng.random(), height_norm=hmax)
    return m, dict(leaves=n, stems=ns)


BUILDERS = dict(palm=palm, fern=fern, horsetail=horsetail, emergent=emergent, floating=floating, cycad=cycad, herb=herb)
