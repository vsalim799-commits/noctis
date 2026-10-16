"""Deadwood: fallen log, broken stump, root wad (upturned root plate), branch litter.

Material slots: 0 = bark (asset 'bark'), 1 = bare dead wood (catalog.DEADWOOD_INNER), 2 = soil (root wad clod).
Pivot at ground level; logs lie along +X and are sunk by ~20 % of their radius.
"""
from __future__ import annotations

import math

import numpy as np

from . import texgen as tg
from .meshkit import Mesh, normalize, convex_hull_mesh, prism_points
from .treegen import grow_polyline

UP = np.array([0.0, 0.0, 1.0])


def _angular_noise(rng, k_lo=2, k_hi=7, amp=0.12):
    """Smooth periodic radial noise r(theta, s) for irregular cross sections."""
    ks = np.arange(k_lo, k_hi)
    ph = rng.uniform(0, 2 * np.pi, (len(ks), 2))
    am = amp / ks ** 0.9 * rng.uniform(0.5, 1.0, len(ks))
    drift = rng.uniform(0.2, 0.8, len(ks))

    def fn(th, s):
        out = np.ones((len(s), len(th)))
        for k, (p0, p1), a, dr in zip(ks, ph, am, drift):
            out += a * np.cos(k * th[None, :] + p0 + dr * s[:, None] + p1 * np.sin(s[:, None] * 0.7))
        return out
    return fn


def _bark_mask(rng, frac):
    """Faces where bark has fallen off -> material 1 (bare wood)."""
    noise = tg.fft_noise((128, 128), int(rng.integers(1 << 30)), beta=2.2, lo=2, hi=20)
    thr = np.quantile(noise, 1 - frac)

    def fn(centers, ii, jj):
        u = (jj * 8) % 128  # around the stem
        v = (ii * 3) % 128  # along it
        return np.where(noise[v.astype(int), u.astype(int)] > thr, 1, 0)
    return fn


def _splinters(m, center, axis, radius, rng, n, length, mat, sides=3):
    axis = normalize(axis)
    perp = normalize(np.cross(axis, UP)) if abs(axis[2]) < 0.95 else np.array([1.0, 0, 0])
    perp2 = np.cross(axis, perp)
    for _ in range(n):
        a = rng.uniform(0, 2 * np.pi)
        r = radius * math.sqrt(rng.random()) * 0.85
        p = center + (math.cos(a) * perp + math.sin(a) * perp2) * r
        L = length * rng.uniform(0.3, 1.0)
        d = normalize(axis + rng.normal(0, 0.15, 3))
        pts = np.array([p - axis * 0.02, p + d * L * 0.5, p + d * L])
        w = radius * rng.uniform(0.08, 0.2)
        m.tube(pts, np.array([w, w * 0.6, 0.002]), sides, mat, uv_scale=(0.3, 1.0), cap=False, height_norm=2.0)


def log(spec, seed, lod, bark_tile, inner_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    L, R = spec["length"], spec["radius"]
    seg = (0.08, 0.6, 1.5)[lod]
    sides = (40, 9, 6)[lod]
    n = max(3, int(L / seg))
    x = np.linspace(-L * 0.5, L * 0.5, n + 1)
    bend = rng.normal(0, 0.08)
    pts = np.stack([x, bend * (x / L) ** 2 * L, np.full_like(x, R * 0.8) + 0.04 * np.sin(x * 0.8)], 1)
    rad = R * (1.15 - 0.45 * (x + L * 0.5) / L)
    rad[0] *= 1.05
    noise = _angular_noise(rng, amp=0.10 if lod < 2 else 0.05)
    m.tube(pts, rad, sides, 0, uv_scale=bark_tile, radial_fn=noise, cap=False, height_norm=2.0,
           mat_fn=_bark_mask(rng, 0.45) if lod < 2 else None)
    # broken ends: inner wood cap pushed in, jagged splinters
    for end, sgn in ((0, -1), (-1, 1)):
        c = pts[end]
        ax = np.array([sgn, 0.0, 0.0])
        k = sides
        th = np.linspace(0, 2 * math.pi, k, endpoint=False)
        r = rad[end] * noise(th, np.array([0.0 if end == 0 else L]))[0]
        ring = c + np.stack([np.zeros(k), np.cos(th) * r, np.sin(th) * r], 1)
        jag = ring.copy()
        jag[:, 0] += sgn * rng.uniform(0.0, 0.25 * R, k)
        uvr = np.stack([0.5 + 0.45 * np.cos(th), 0.5 + 0.45 * np.sin(th)], 1)
        m.fan(c + ax * 0.05 * R, jag, (0.5, 0.5), uvr, 1, height_norm=2.0)
        if lod == 0:
            _splinters(m, c, ax, rad[end], rng, 7, R * 0.9, 1)
    if lod < 2:
        for k in range(5 if lod == 0 else 3):  # broken branch stubs
            t = rng.uniform(0.15, 0.85)
            i = int(t * n)
            a = rng.uniform(0.3, 2 * math.pi - 0.3)
            d = normalize(np.array([rng.uniform(0.3, 0.8), math.cos(a), math.sin(a) * 0.8 + 0.2]))
            base = pts[i] + np.array([0, math.cos(a), math.sin(a)]) * rad[i] * 0.6
            sl = rng.uniform(0.3, 1.3)
            sp = grow_polyline(base, d, sl, 3, rng, noise=0.1)
            sr = rad[i] * rng.uniform(0.12, 0.22)
            m.tube(sp, np.linspace(sr, sr * 0.7, len(sp)), (6, 4, 3)[lod], 0, uv_scale=bark_tile, cap=True, cap_mat=1, height_norm=2.0)
    hulls = []
    if lod == 0:
        V, _ = m.arrays()
        for a, b in ((-0.5, -0.17), (-0.17, 0.17), (0.17, 0.5)):
            sel = (V[:, 0] >= a * L - 0.05) & (V[:, 0] <= b * L + 0.05) & (np.abs(V[:, 1]) < R * 1.6)
            hulls.append(convex_hull_mesh(V[sel]))
    return m, dict(length_m=L), hulls


def stump(spec, seed, lod, bark_tile, inner_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    H, R = spec["height"], spec["radius"]
    sides = (48, 10, 7)[lod]
    nz = (28, 5, 3)[lod]
    th = np.linspace(0, 2 * math.pi, sides + 1)
    z = np.linspace(-0.25, H, nz + 1)
    jag = H * (0.72 + 0.28 * np.abs(np.sin(th * 2.5 + 0.7)) ** 3 + rng.uniform(-0.05, 0.05, sides + 1) * (lod < 2))
    jag[-1] = jag[0]
    noise = _angular_noise(rng, amp=0.14)
    rr = R * (1 + 0.5 * np.exp(-(z + 0.25) / 0.35))[:, None] * noise(th, z)
    zz = np.minimum(z[:, None] * np.ones_like(th)[None, :], jag[None, :])
    zz = np.where(z[:, None] >= H - 1e-6, jag[None, :], zz)
    P = np.stack([rr * np.cos(th)[None, :], rr * np.sin(th)[None, :], zz], -1)
    ureps = max(1, round(2 * math.pi * R / bark_tile[0]))
    UV = np.stack([np.broadcast_to(th[None, :] / (2 * math.pi) * ureps, zz.shape), zz / bark_tile[1]], -1)
    m.surface(P, UV, 0, height_norm=2.0)
    # broken top: inner wood
    ring = P[-1, :-1] * np.array([0.92, 0.92, 1.0])
    c = np.array([0.0, 0.0, H * 0.8])
    uvr = np.stack([0.5 + 0.45 * np.cos(th[:-1]), 0.5 + 0.45 * np.sin(th[:-1])], 1)
    m.fan(c, ring, (0.5, 0.5), uvr, 1, height_norm=2.0)
    if lod == 0:
        _splinters(m, np.array([0, 0, H * 0.85]), UP, R * 0.9, rng, 10, H * 0.35, 1)
    for k in range((6, 5, 4)[lod]):  # buttress roots running into the soil
        a = 2 * math.pi * k / 6 + rng.normal(0, 0.2)
        d = np.array([math.cos(a), math.sin(a), -0.25])
        start = np.array([math.cos(a) * R * 0.6, math.sin(a) * R * 0.6, 0.25])
        pts = grow_polyline(start, d, rng.uniform(1.0, 1.8), (6, 3, 2)[lod], rng, up=-0.05, noise=0.15)
        m.tube(pts, np.linspace(R * 0.35, 0.03, len(pts)), (7, 5, 4)[lod], 0, uv_scale=bark_tile, cap=False, height_norm=2.0)
    hulls = []
    if lod == 0:
        V, _ = m.arrays()
        hulls.append(convex_hull_mesh(V[np.hypot(V[:, 0], V[:, 1]) < R * 1.3]))
    return m, dict(height_m=H), hulls


def rootwad(spec, seed, lod, bark_tile, inner_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    Rp, Ll, Rl = spec["radius"], spec["log_length"], spec["log_radius"]
    # log lying along -X from the root plate
    x = np.linspace(0, -Ll, (36, 5, 3)[lod] + 1)
    pts = np.stack([x, np.zeros_like(x), np.full_like(x, Rl * 0.85) + 0.15 * np.exp(x / 1.0)], 1)
    noise = _angular_noise(rng, amp=0.08)
    m.tube(pts, np.linspace(Rl * 1.4, Rl, len(x)), (32, 8, 6)[lod], 0, uv_scale=bark_tile, radial_fn=noise, cap=True,
           cap_mat=1, height_norm=3.0, mat_fn=_bark_mask(rng, 0.3) if lod < 2 else None)
    # root plate: flattened lumpy disc standing vertically (normal = +X), with a soil clod
    center = np.array([0.25, 0.0, Rp * 0.8])
    nu, nv = (24, 12, 8)[lod], (6, 3, 2)[lod]
    th = np.linspace(0, 2 * math.pi, nu + 1)
    rr = np.linspace(0.05, 1.0, nv + 1)
    TH, RR = np.meshgrid(th, rr, indexing="ij")
    lump = 1 + 0.18 * np.sin(3 * TH + 1.3) + 0.1 * np.sin(7 * TH + 0.4)
    y = RR * Rp * lump * np.cos(TH)
    z = RR * Rp * 0.8 * lump * np.sin(TH)
    lump = lump + 0.07 * np.sin(13 * TH + 2.1) * RR
    y = RR * Rp * lump * np.cos(TH)
    z = RR * Rp * 0.8 * lump * np.sin(TH)
    for face, sx in ((1.0, 0.5), (-1.0, -0.3)):
        xx = sx * (1 - RR ** 2) * Rp * 0.6
        P = center + np.stack([xx, y, z], -1)
        P[..., 2] = np.maximum(P[..., 2], -0.1)
        UV = np.stack([y / 1.5, z / 1.5], -1)
        if face < 0:
            P = P[:, ::-1]
            UV = UV[:, ::-1]
        m.surface(P, UV, 2, height_norm=3.0)
    # roots: thick tapering main roots curving out of the plate rim and its torn face, drooping under their
    # weight, with secondary roots; a few short broken stubs on the face (torn when the tree fell)
    for k in range((20, 11, 6)[lod]):
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.6, 1.0)
        start = center + np.array([rng.uniform(0.0, 0.25), math.cos(a) * Rp * s * 0.85, math.sin(a) * Rp * 0.8 * s * 0.85])
        d = normalize(np.array([rng.uniform(0.15, 0.8), math.cos(a), math.sin(a) * 0.8]))
        L = rng.uniform(0.35, 0.9) * Rp
        pts = grow_polyline(start, d, L, (8, 4, 2)[lod], rng, up=-0.3, noise=0.25)
        pts[:, 2] = np.maximum(pts[:, 2], -0.05)
        r0 = rng.uniform(0.05, 0.13)
        m.tube(pts, r0 * np.linspace(1.0, 0.12, len(pts)) ** 1.3, (6, 4, 3)[lod], 0, uv_scale=bark_tile, cap=False, height_norm=3.0)
        if lod == 0:
            for j in range(int(rng.integers(1, 3))):
                q = pts[int(rng.integers(2, len(pts) - 2))]
                d2 = normalize(d + rng.normal(0, 0.7, 3))
                sp = grow_polyline(q, d2, rng.uniform(0.2, 0.5) * Rp, 4, rng, up=-0.3, noise=0.3)
                sp[:, 2] = np.maximum(sp[:, 2], -0.05)
                m.tube(sp, r0 * 0.45 * np.linspace(1.0, 0.1, len(sp)), 4, 0, uv_scale=bark_tile, cap=False, height_norm=3.0)
    for k in range((10, 5, 0)[lod]):
        a = rng.uniform(0, 2 * math.pi)
        s = rng.uniform(0.1, 0.7)
        start = center + np.array([0.2, math.cos(a) * Rp * s, math.sin(a) * Rp * 0.8 * s])
        d = normalize(np.array([1.0, rng.normal(0, 0.4), rng.normal(0, 0.4)]))
        pts = grow_polyline(start, d, rng.uniform(0.12, 0.45), 2, rng, noise=0.2)
        m.tube(pts, np.linspace(rng.uniform(0.04, 0.09), 0.03, len(pts)), (6, 4, 3)[lod], 0, uv_scale=bark_tile, cap=True,
               cap_mat=1, height_norm=3.0)
    hulls = []
    if lod == 0:
        V, _ = m.arrays()
        hulls.append(convex_hull_mesh(V[V[:, 0] < -0.2]))
        plate = V[(V[:, 0] >= -0.3)]
        hulls.append(convex_hull_mesh(plate))
    return m, dict(radius_m=Rp), hulls


def branches(spec, seed, lod, bark_tile, inner_tile):
    rng = np.random.default_rng(seed)
    m = Mesh()
    half = spec["area"] * 0.5
    for i in range(spec["count"] if lod < 2 else max(3, spec["count"] // 2)):
        p = np.array([rng.uniform(-half, half), rng.uniform(-half, half), 0.03])
        a = rng.uniform(0, 2 * math.pi)
        d = np.array([math.cos(a), math.sin(a), rng.uniform(-0.02, 0.06)])
        L = rng.uniform(0.8, 2.6)
        r = rng.uniform(0.018, 0.05)
        pts = grow_polyline(p, d, L, (6, 3, 2)[lod], rng, noise=0.25)
        pts[:, 2] = np.clip(pts[:, 2], r * 0.6, 0.2)
        m.tube(pts, np.linspace(r, r * 0.4, len(pts)), (6, 4, 3)[lod], 1, uv_scale=(0.2, 0.8), cap=True, height_norm=1.0)
        if lod < 2:
            for k in range(rng.integers(1, 4)):
                t = rng.uniform(0.2, 0.8)
                q = pts[int(t * (len(pts) - 1))]
                side = rng.choice([-1, 1])
                d2 = normalize(d + np.array([-d[1], d[0], 0]) * side * rng.uniform(0.6, 1.2))
                d2[2] = rng.uniform(-0.05, 0.25)
                sp = grow_polyline(q, d2, rng.uniform(0.2, 0.7), 3, rng, noise=0.3)
                sp[:, 2] = np.maximum(sp[:, 2], 0.01)
                m.tube(sp, np.linspace(r * 0.5, r * 0.2, len(sp)), 3, 1, uv_scale=(0.2, 0.8), cap=False, height_norm=1.0)
    return m, dict(count=spec["count"]), []


BUILDERS = dict(log=log, stump=stump, rootwad=rootwad, branches=branches)
