"""Signed-distance 'sculpt' evaluation for the creature factory (numba-accelerated).

A creature is a list of primitive volumes attached to bones (Data/Anatomy/<species>.json), merged in
array order with polynomial smooth-min unions and smooth subtractions (same convention as
Tools/Preview/src/creature/sdf_mesher.js, so the three.js preview and the factory agree).

Primitive kinds
  ellipsoid  centre, radii, rotation (three.js XYZ Euler)        -> Quilez 'improved' ellipsoid bound
  roundCone  a, b, ra, rb, optional per-axis scale about a        -> Quilez round cone
  capsule    roundCone with ra == rb
  loft       generalized cylinder in its own frame: sections {u, cy, top, bot, hw, nt, nb} (two
             half-superellipses, upper/lower) interpolated along u (PCHIP), used for the head and
             any form that ellipsoids render too 'blobby'. Distance = first-order estimate
             (r - 1) / |grad r| including the variation of the section along u.

Everything is in anatomy space: metres, +x forward, +y up, +z the animal's right, ground y = 0.

Culling: a uniform grid of cells lists, in array order, the shapes whose AABB (expanded by a
conservative margin = blend radius + narrow band) touches the cell. Inside the narrow band the
culled field is identical to the full field (skipped shapes contribute exactly zero to the smooth
min), outside it only the sign is guaranteed, which is all marching cubes needs.
"""
from __future__ import annotations

import math
import numpy as np
from numba import njit, prange

STRIDE = 24
KIND = {"ellipsoid": 0, "roundCone": 1, "capsule": 1, "loft": 2, "sweep": 3}
REGIONS = ["head", "jaw", "neck", "torso", "belly", "tail", "thigh", "shin", "foot", "arm"]
FEATURES = ["skin", "eye", "claw"]


# ----------------------------------------------------------------------------------------------
# Python-side compilation
def euler_xyz_matrix(r):
    """Rotation matrix (columns = local axes in model space) of a three.js 'XYZ' Euler."""
    x, y, z = r
    a, b, c, d, e, f = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    ae, af, be, bf = a * e, a * f, b * e, b * f
    return np.array([[c * e, -c * f, d],
                     [af + be * d, ae - bf * d, -b * c],
                     [bf - ae * d, be + af * d, a * c]], dtype=np.float64)


def _loft_table(sections, nsamp=128):
    """Resample loft key sections (dicts) to a dense uniform-u table with monotone cubic interp."""
    from scipy.interpolate import PchipInterpolator
    keys = sorted(sections, key=lambda s: s["u"])
    u = np.array([s["u"] for s in keys], dtype=np.float64)
    cols = ["cy", "top", "bot", "hw", "nt", "nb"]
    vals = np.array([[s.get(c, s.get("n", 2.0)) if c in ("nt", "nb") else s[c] for c in cols] for s in keys])
    uu = np.linspace(u[0], u[-1], nsamp)
    if len(u) >= 3:
        tab = np.stack([PchipInterpolator(u, vals[:, j])(uu) for j in range(6)], axis=1)
    else:
        tab = np.stack([np.interp(uu, u, vals[:, j]) for j in range(6)], axis=1)
    tab[:, 1:4] = np.maximum(tab[:, 1:4], 1e-3)
    tab[:, 4:6] = np.clip(tab[:, 4:6], 1.2, 8.0)
    return u[0], (u[-1] - u[0]) / (nsamp - 1), tab


SW_COLS = 22   # px py pz | tx ty tz | ux uy uz | sx sy sz | cy top bot hw nt nb | b0 b1 bf | region


def sweep_table(shape, bone_index, spacing=0.03):
    """Dense samples of a sweep: centre-line (PCHIP through the key points, resampled by arc length),
    parallel frames (T forward, U up-ish, S = T x U = animal right) and section parameters."""
    from scipy.interpolate import PchipInterpolator
    keys = shape["points"]
    P = np.array([k["p"] for k in keys], np.float64)
    seg = np.linalg.norm(np.diff(P, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    L = s[-1]
    n = max(8, int(math.ceil(L / spacing)) + 1)
    ss = np.linspace(0, L, n)
    C = np.stack([PchipInterpolator(s, P[:, j])(ss) for j in range(3)], 1)
    cols = ["cy", "top", "bot", "hw", "nt", "nb"]
    defaults = dict(cy=0.0, nt=2.0, nb=2.0)
    vals = np.array([[k.get(c, defaults.get(c, 0.0)) for c in cols] for k in keys], np.float64)
    sec = np.stack([PchipInterpolator(s, vals[:, j])(ss) for j in range(6)], 1)
    sec[:, 1:4] = np.maximum(sec[:, 1:4], 2e-3)
    sec[:, 4:6] = np.clip(sec[:, 4:6], 1.2, 8.0)
    T = np.gradient(C, axis=0)
    T /= np.linalg.norm(T, axis=1, keepdims=True)
    up = np.asarray(shape.get("up", [0, 1, 0]), np.float64)
    U = up[None] - (T @ up)[:, None] * T
    U /= np.linalg.norm(U, axis=1, keepdims=True)
    S = np.cross(T, U)
    # bones along the chain: two nearest bone segments, inverse-distance blend
    chain = shape.get("chain", [])
    b0 = np.zeros(n)
    b1 = np.zeros(n)
    bf = np.zeros(n)
    if chain:
        D = []
        for c in chain:
            a = np.asarray(c["head"], float)
            b = np.asarray(c["tail"], float)
            ab = b - a
            t = np.clip(((C - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
            D.append(np.linalg.norm(C - (a + t[:, None] * ab), axis=1))
        D = np.stack(D, 1)
        o = np.argsort(D, 1)
        i0, i1 = o[:, 0], o[:, 1]
        d0 = D[np.arange(n), i0]
        d1 = D[np.arange(n), i1]
        w0 = 1 / (d0 + 0.02) ** 4
        w1 = 1 / (d1 + 0.02) ** 4
        b0 = np.array([bone_index[chain[i]["bone"]] for i in i0], float)
        b1 = np.array([bone_index[chain[i]["bone"]] for i in i1], float)
        bf = w1 / (w0 + w1)
    reg = np.zeros(n)
    rr = shape.get("regions", [])   # [[s_from_head_start_fraction..]] -> list of {"x_max": .., "region": ..}
    for r in rr:
        m = C[:, 0] <= r["x_max"]
        reg[m & (reg == 0)] = REGIONS.index(r["region"]) + 1
    reg = np.where(reg == 0, REGIONS.index(shape.get("region", "torso")) + 1, reg) - 1
    tab = np.concatenate([C, T, U, S, sec, b0[:, None], b1[:, None], bf[:, None], reg[:, None]], 1)
    return tab


def loft_frame(shape):
    """World->local matrix and origin of a loft (local u = forward axis, v = up, w = side)."""
    o = np.asarray(shape["origin"], dtype=np.float64)
    R = euler_xyz_matrix(shape.get("rotation", [0, 0, 0]))
    return o, R


class Compiled:
    """Flat arrays describing the shapes, ready for the numba kernels."""

    def __init__(self, shapes, bone_names, mask_names=None, band=0.08, cell=0.2):
        self.shapes = shapes
        n = len(shapes)
        self.n = n
        self.bone_names = list(bone_names)
        bidx = {b: i for i, b in enumerate(self.bone_names)}
        self.kind = np.zeros(n, np.int8)
        self.sign = np.ones(n, np.int8)
        self.bone = np.zeros(n, np.int32)
        self.region = np.zeros(n, np.int32)
        self.feature = np.zeros(n, np.int32)
        self.k = np.zeros(n, np.float64)
        self.P = np.zeros((n, STRIDE), np.float64)
        self.box = np.zeros((n, 6), np.float64)
        self.mask_names = list(mask_names or sorted({m for s in shapes for m in s.get("masks", [])}))
        midx = {m: i for i, m in enumerate(self.mask_names)}
        self.mask = -np.ones((n, 3), np.int32)
        tables = []
        toff = 0
        swt = []
        swoff = 0
        for i, s in enumerate(shapes):
            kind = KIND[s["kind"]]
            self.kind[i] = kind
            self.sign[i] = -1 if s.get("sign", 1) == -1 else 1
            if s["bone"] not in bidx:
                raise ValueError(f"shape {i} references unknown bone {s['bone']}")
            self.bone[i] = bidx[s["bone"]]
            self.region[i] = REGIONS.index(s["region"]) if s.get("region") in REGIONS else 3
            self.feature[i] = FEATURES.index(s.get("feature", "skin")) if s.get("feature", "skin") in FEATURES else 0
            self.k[i] = max(float(s.get("blend", 0.05)), 1e-4)
            for j, m in enumerate(s.get("masks", [])[:3]):
                self.mask[i, j] = midx[m]
            P = self.P[i]
            if kind == 0:
                c = np.asarray(s["center"], np.float64)
                r = np.asarray(s["radii"], np.float64)
                R = euler_xyz_matrix(s.get("rotation", [0, 0, 0]))
                P[0:3] = c
                P[3:12] = R.T.reshape(-1)          # local = R^T (p - c)
                P[12:15] = 1.0 / r
                P[15] = r.min()
                h = np.abs(R) @ r
                self.box[i, :3] = c - h
                self.box[i, 3:] = c + h
            elif kind == 1:
                A = np.asarray(s["a"], np.float64)
                B = np.asarray(s["b"], np.float64)
                r1 = float(s.get("ra", s.get("r", 0.1)))
                r2 = r1 if s["kind"] == "capsule" else float(s.get("rb", r1))
                sc = np.asarray(s.get("scale", [1, 1, 1]), np.float64)
                ba = (B - A) / sc
                l2 = max(float(ba @ ba), 1e-8)
                l = math.sqrt(l2)
                if abs(r1 - r2) > 0.98 * l:
                    if r1 > r2:
                        r2 = r1 - 0.98 * l
                    else:
                        r1 = r2 - 0.98 * l
                rr = r1 - r2
                P[0:3] = A
                P[3:6] = ba
                P[6] = l2
                P[7] = rr
                P[8] = l2 - rr * rr
                P[9] = 1.0 / l2
                P[10] = r1
                P[11] = r2
                P[12:15] = 1.0 / sc
                P[15] = sc.min()
                self.box[i, :3] = np.minimum(A - r1 * sc, B - r2 * sc)
                self.box[i, 3:] = np.maximum(A + r1 * sc, B + r2 * sc)
            elif kind == 3:
                tab = sweep_table(s, bidx)
                P[0] = swoff
                P[1] = len(tab)
                P[2] = max(2, min(8, len(tab) // 16))
                swt.append(tab)
                swoff += len(tab)
                ext = np.maximum(tab[:, 13:16].max(1), 0) + np.abs(tab[:, 12])
                self.box[i, :3] = (tab[:, 0:3] - ext[:, None]).min(0)
                self.box[i, 3:] = (tab[:, 0:3] + ext[:, None]).max(0)
            else:
                o, R = loft_frame(s)
                u0, du, tab = _loft_table(s["sections"])
                P[0:3] = o
                P[3:12] = R.T.reshape(-1)
                P[12] = u0
                P[13] = du
                P[14] = len(tab)
                P[15] = toff
                tables.append(tab)
                toff += len(tab)
                # conservative box: union of the section boxes in local space, rotated
                us = u0 + du * np.arange(len(tab))
                lo = np.array([us.min(), (tab[:, 0] - tab[:, 2]).min(), -tab[:, 3].max()])
                hi = np.array([us.max(), (tab[:, 0] + tab[:, 1]).max(), tab[:, 3].max()])
                corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
                wc = corners @ R.T + o
                self.box[i, :3] = wc.min(0)
                self.box[i, 3:] = wc.max(0)
        self.LT = np.concatenate(tables, 0) if tables else np.zeros((1, 6))
        self.SW = np.ascontiguousarray(np.concatenate(swt, 0)) if swt else np.zeros((2, SW_COLS))
        self.band = band
        self.build_cells(cell)

    def build_cells(self, cell):
        margin = 1.6 * self.k + self.band + 0.03
        lo = self.box[:, :3] - margin[:, None]
        hi = self.box[:, 3:] + margin[:, None]
        g0 = lo.min(0) - cell
        g1 = hi.max(0) + cell
        dims = np.ceil((g1 - g0) / cell).astype(np.int64)
        cells_of = []
        for i in range(self.n):
            a = np.floor((lo[i] - g0) / cell).astype(np.int64)
            b = np.floor((hi[i] - g0) / cell).astype(np.int64)
            ix, iy, iz = np.meshgrid(np.arange(a[0], b[0] + 1), np.arange(a[1], b[1] + 1), np.arange(a[2], b[2] + 1), indexing="ij")
            c = (ix * dims[1] + iy) * dims[2] + iz
            cells_of.append(np.stack([c.reshape(-1), np.full(c.size, i)], 1))
        cs = np.concatenate(cells_of, 0)
        order = np.lexsort((cs[:, 1], cs[:, 0]))
        cs = cs[order]
        ncell = int(dims.prod())
        start = np.zeros(ncell + 1, np.int64)
        np.add.at(start, cs[:, 0] + 1, 1)
        start = np.cumsum(start)
        self.g0 = g0.astype(np.float64)
        self.cell = float(cell)
        self.dims = dims.astype(np.int64)
        self.cstart = start
        self.citems = cs[:, 1].astype(np.int32)
        self.bounds = (g0, g0 + dims * cell)

    # convenience wrappers -------------------------------------------------------------------
    def args(self):
        return (self.kind, self.sign, self.k, self.P, self.LT, self.SW, self.g0, self.cell, self.dims, self.cstart, self.citems)

    def field(self, pts):
        pts = np.ascontiguousarray(pts, dtype=np.float64)
        out = np.empty(len(pts), np.float64)
        eval_points(pts, out, *self.args())
        return out

    def gradient(self, pts, eps=1e-3):
        pts = np.ascontiguousarray(pts, dtype=np.float64)
        g = np.empty((len(pts), 3), np.float64)
        grad_points(pts, g, eps, *self.args())
        return g

    def project(self, pts, iters=3, max_step=None):
        """Newton-project points onto the zero iso-surface."""
        pts = np.array(pts, dtype=np.float64)
        for _ in range(iters):
            d = self.field(pts)
            g = self.gradient(pts)
            gl2 = np.maximum((g * g).sum(1), 1e-12)
            step = d / gl2
            if max_step is not None:
                step = np.clip(step, -max_step, max_step)
            pts -= g * step[:, None]
        return pts

    def attributes(self, pts, sig_scale=0.3, sig_add=0.02):
        """Per-point region scores, bone weights, feature, masks (see sdf_mesher.js weights)."""
        pts = np.ascontiguousarray(pts, dtype=np.float64)
        n = len(pts)
        nreg = len(REGIONS)
        nb = len(self.bone_names)
        nm = max(1, len(self.mask_names))
        reg = np.zeros((n, nreg), np.float32)
        bw = np.zeros((n, nb), np.float32)
        feat = np.zeros((n, len(FEATURES)), np.float32)
        masks = np.zeros((n, nm), np.float32)
        attr_points(pts, reg, bw, feat, masks, self.bone, self.region, self.feature, self.mask, sig_scale, sig_add, *self.args())
        return reg, bw, feat, masks[:, :len(self.mask_names)]


# ----------------------------------------------------------------------------------------------
# numba kernels
@njit(cache=True, fastmath=True)
def _se_d2(yy, w, top, bot, hw, nt, nbt):
    """First-order distance to a section made of two half-superellipses (upper/lower)."""
    if yy > 0:
        b = top
        ne = nt
    else:
        b = bot
        ne = nbt
    X = abs(w) / hw
    Y = abs(yy) / b
    if X < 1e-9 and Y < 1e-9:
        return -min(hw, b)
    m = max(X, Y)
    F = (X / m) ** ne + (Y / m) ** ne
    r = m * F ** (1.0 / ne)
    gx = (X / r) ** (ne - 1.0) / hw
    gy = (Y / r) ** (ne - 1.0) / b
    g = math.sqrt(gx * gx + gy * gy)
    d = (r - 1.0) / max(g, 1e-6)
    lo = -min(hw, b)
    if d < lo:
        d = lo
    return d


@njit(cache=True, fastmath=True)
def _sweep_sec(SW, r0, f, v, w):
    cy = SW[r0, 12] * (1 - f) + SW[r0 + 1, 12] * f
    top = SW[r0, 13] * (1 - f) + SW[r0 + 1, 13] * f
    bot = SW[r0, 14] * (1 - f) + SW[r0 + 1, 14] * f
    hw = SW[r0, 15] * (1 - f) + SW[r0 + 1, 15] * f
    nt = SW[r0, 16] * (1 - f) + SW[r0 + 1, 16] * f
    nb = SW[r0, 17] * (1 - f) + SW[r0 + 1, 17] * f
    return _se_d2(v - cy, w, top, bot, hw, nt, nb)


@njit(cache=True, fastmath=True)
def sweep_eval(SW, off, n, step, x, y, z):
    """Distance to a sweep + the (fractional) sample index of the nearest centre-line point."""
    # coarse search over every `step`-th sample, then exact projection on the segments around it
    best = 1e30
    bi = 0
    for j in range(0, n, step):
        dx = x - SW[off + j, 0]
        dy = y - SW[off + j, 1]
        dz = z - SW[off + j, 2]
        dd = dx * dx + dy * dy + dz * dz
        if dd < best:
            best = dd
            bi = j
    lo = max(0, bi - step - 1)
    hi = min(n - 2, bi + step + 1)
    best = 1e30
    bs = 0
    bt = 0.0
    for j in range(lo, hi + 1):
        r = off + j
        ax = SW[r + 1, 0] - SW[r, 0]
        ay = SW[r + 1, 1] - SW[r, 1]
        az = SW[r + 1, 2] - SW[r, 2]
        px = x - SW[r, 0]
        py = y - SW[r, 1]
        pz = z - SW[r, 2]
        l2 = ax * ax + ay * ay + az * az
        t = (px * ax + py * ay + pz * az) / max(l2, 1e-12)
        if j == 0 and t < 0:
            tt = t
        elif j == n - 2 and t > 1:
            tt = t
        else:
            tt = min(1.0, max(0.0, t))
        qx = px - ax * tt
        qy = py - ay * tt
        qz = pz - az * tt
        dd = qx * qx + qy * qy + qz * qz
        if dd < best:
            best = dd
            bs = j
            bt = tt
    r0 = off + bs
    f = min(1.0, max(0.0, bt))
    cx = SW[r0, 0] * (1 - f) + SW[r0 + 1, 0] * f
    cyy = SW[r0, 1] * (1 - f) + SW[r0 + 1, 1] * f
    cz = SW[r0, 2] * (1 - f) + SW[r0 + 1, 2] * f
    ux = SW[r0, 6] * (1 - f) + SW[r0 + 1, 6] * f
    uy = SW[r0, 7] * (1 - f) + SW[r0 + 1, 7] * f
    uz = SW[r0, 8] * (1 - f) + SW[r0 + 1, 8] * f
    sx = SW[r0, 9] * (1 - f) + SW[r0 + 1, 9] * f
    sy = SW[r0, 10] * (1 - f) + SW[r0 + 1, 10] * f
    sz = SW[r0, 11] * (1 - f) + SW[r0 + 1, 11] * f
    tx = SW[r0, 3] * (1 - f) + SW[r0 + 1, 3] * f
    ty = SW[r0, 4] * (1 - f) + SW[r0 + 1, 4] * f
    tz = SW[r0, 5] * (1 - f) + SW[r0 + 1, 5] * f
    ul = math.sqrt(ux * ux + uy * uy + uz * uz) + 1e-12
    sl = math.sqrt(sx * sx + sy * sy + sz * sz) + 1e-12
    tl = math.sqrt(tx * tx + ty * ty + tz * tz) + 1e-12
    px = x - cx
    py = y - cyy
    pz = z - cz
    v = (px * ux + py * uy + pz * uz) / ul
    w = (px * sx + py * sy + pz * sz) / sl
    a = (px * tx + py * ty + pz * tz) / tl
    d2 = _sweep_sec(SW, r0, f, v, w)
    if abs(d2) < 0.3:
        # slope of the section along the curve (neighbour samples), first-order correction
        jm = max(0, bs - 1)
        jp = min(n - 2, bs + 1)
        dp = _sweep_sec(SW, off + jp, f, v, w)
        dm = _sweep_sec(SW, off + jm, f, v, w)
        ds = 0.0
        for q in range(3):
            ds += (SW[off + jp, q] - SW[off + jm, q]) ** 2
        ds = math.sqrt(ds) + 1e-9
        s = (dp - dm) / ds
        d2 = d2 / math.sqrt(1.0 + s * s)
    out = 0.0
    if bs == 0 and bt < 0:
        out = -a
    elif bs == n - 2 and bt > 1:
        out = a
    if out > 0:
        if d2 > 0:
            d2 = math.sqrt(d2 * d2 + out * out)
        else:
            d2 = max(d2, out)
    return d2, bs + f


@njit(cache=True, fastmath=True)
def _loft_d2(LT, toff, n, u0, du, u, v, w):
    t = (u - u0) / du
    if t < 0.0:
        t = 0.0
    if t > n - 1.0:
        t = n - 1.0
    i = int(t)
    if i > n - 2:
        i = n - 2
    f = t - i
    r0 = toff + i
    cy = LT[r0, 0] * (1 - f) + LT[r0 + 1, 0] * f
    top = LT[r0, 1] * (1 - f) + LT[r0 + 1, 1] * f
    bot = LT[r0, 2] * (1 - f) + LT[r0 + 1, 2] * f
    hw = LT[r0, 3] * (1 - f) + LT[r0 + 1, 3] * f
    nt = LT[r0, 4] * (1 - f) + LT[r0 + 1, 4] * f
    nbt = LT[r0, 5] * (1 - f) + LT[r0 + 1, 5] * f
    yy = v - cy
    if yy > 0:
        b = top
        ne = nt
    else:
        b = bot
        ne = nbt
    X = abs(w) / hw
    Y = abs(yy) / b
    if X < 1e-9 and Y < 1e-9:
        return -min(hw, b)
    # r = (X^n + Y^n)^(1/n), computed scale-safely
    m = max(X, Y)
    xn = (X / m) ** ne
    ynn = (Y / m) ** ne
    F = xn + ynn
    r = m * F ** (1.0 / ne)
    # dr/dX = (X/r)^(n-1)
    gx = (X / r) ** (ne - 1.0) / hw
    gy = (Y / r) ** (ne - 1.0) / b
    g = math.sqrt(gx * gx + gy * gy)
    d = (r - 1.0) / max(g, 1e-6)
    lo = -min(hw, b)
    if d < lo:
        d = lo
    return d


@njit(cache=True, fastmath=True)
def shape_dist(kind, P, LT, SW, i, x, y, z):
    if kind == 0:
        dx = x - P[i, 0]
        dy = y - P[i, 1]
        dz = z - P[i, 2]
        lx = P[i, 3] * dx + P[i, 4] * dy + P[i, 5] * dz
        ly = P[i, 6] * dx + P[i, 7] * dy + P[i, 8] * dz
        lz = P[i, 9] * dx + P[i, 10] * dy + P[i, 11] * dz
        qx = lx * P[i, 12]
        qy = ly * P[i, 13]
        qz = lz * P[i, 14]
        k0 = math.sqrt(qx * qx + qy * qy + qz * qz)
        ux = qx * P[i, 12]
        uy = qy * P[i, 13]
        uz = qz * P[i, 14]
        k1 = math.sqrt(ux * ux + uy * uy + uz * uz)
        if k1 < 1e-9:
            return -P[i, 15]
        return k0 * (k0 - 1.0) / k1
    elif kind == 1:
        pax = (x - P[i, 0]) * P[i, 12]
        pay = (y - P[i, 1]) * P[i, 13]
        paz = (z - P[i, 2]) * P[i, 14]
        bax = P[i, 3]
        bay = P[i, 4]
        baz = P[i, 5]
        l2 = P[i, 6]
        rr = P[i, 7]
        a2 = P[i, 8]
        il2 = P[i, 9]
        yy = pax * bax + pay * bay + paz * baz
        zz = yy - l2
        wx = pax * l2 - bax * yy
        wy = pay * l2 - bay * yy
        wz = paz * l2 - baz * yy
        x2 = wx * wx + wy * wy + wz * wz
        y2 = yy * yy * l2
        z2 = zz * zz * l2
        sr = 1.0 if rr > 0 else (-1.0 if rr < 0 else 0.0)
        k = sr * rr * rr * x2
        ms = P[i, 15]
        szz = 1.0 if zz > 0 else (-1.0 if zz < 0 else 0.0)
        syy = 1.0 if yy > 0 else (-1.0 if yy < 0 else 0.0)
        if szz * a2 * z2 > k:
            return (math.sqrt(x2 + z2) * il2 - P[i, 11]) * ms
        if syy * a2 * y2 < k:
            return (math.sqrt(x2 + y2) * il2 - P[i, 10]) * ms
        return ((math.sqrt(x2 * a2 * il2) + yy * rr) * il2 - P[i, 10]) * ms
    elif kind == 3:
        d, sf = sweep_eval(SW, int(P[i, 0]), int(P[i, 1]), int(P[i, 2]), x, y, z)
        return d
    else:
        dx = x - P[i, 0]
        dy = y - P[i, 1]
        dz = z - P[i, 2]
        u = P[i, 3] * dx + P[i, 4] * dy + P[i, 5] * dz
        v = P[i, 6] * dx + P[i, 7] * dy + P[i, 8] * dz
        w = P[i, 9] * dx + P[i, 10] * dy + P[i, 11] * dz
        u0 = P[i, 12]
        du = P[i, 13]
        n = int(P[i, 14])
        toff = int(P[i, 15])
        u1 = u0 + du * (n - 1)
        d2 = _loft_d2(LT, toff, n, u0, du, u, v, w)
        # account for the section changing along u (first-order distance correction)
        if abs(d2) < 0.25:
            h = 0.01
            dp = _loft_d2(LT, toff, n, u0, du, u + h, v, w)
            dm = _loft_d2(LT, toff, n, u0, du, u - h, v, w)
            s = (dp - dm) / (2 * h)
            d2 = d2 / math.sqrt(1.0 + s * s)
        out = 0.0
        if u < u0:
            out = u0 - u
        elif u > u1:
            out = u - u1
        if out > 0:
            if d2 > 0:
                return math.sqrt(d2 * d2 + out * out)
            return max(d2, out)
        return d2


@njit(cache=True, fastmath=True)
def _cell_of(g0, cell, dims, x, y, z):
    ix = int(math.floor((x - g0[0]) / cell))
    iy = int(math.floor((y - g0[1]) / cell))
    iz = int(math.floor((z - g0[2]) / cell))
    if ix < 0 or iy < 0 or iz < 0 or ix >= dims[0] or iy >= dims[1] or iz >= dims[2]:
        return -1
    return (ix * dims[1] + iy) * dims[2] + iz


@njit(cache=True, fastmath=True)
def field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, x, y, z):
    c = _cell_of(g0, cell, dims, x, y, z)
    if c < 0:
        # outside the grid: distance to the grid box is a safe positive lower bound
        ex = max(g0[0] - x, 0.0, x - (g0[0] + dims[0] * cell))
        ey = max(g0[1] - y, 0.0, y - (g0[1] + dims[1] * cell))
        ez = max(g0[2] - z, 0.0, z - (g0[2] + dims[2] * cell))
        return math.sqrt(ex * ex + ey * ey + ez * ez) + 0.25
    d = 1e9
    for q in range(cstart[c], cstart[c + 1]):
        i = citems[q]
        di = shape_dist(kind[i], P, LT, SW, i, x, y, z)
        kk = k[i]
        if sign[i] > 0:
            h = kk - abs(d - di)
            d = min(d, di) - (h * h * 0.25 / kk if h > 0 else 0.0)
        else:
            nd = -di
            h = kk - abs(d - nd)
            d = max(d, nd) + (h * h * 0.25 / kk if h > 0 else 0.0)
    if d > 1e8:
        d = 0.25
    return d


@njit(parallel=True, cache=True, fastmath=True)
def eval_points(pts, out, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    for j in prange(pts.shape[0]):
        out[j] = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, pts[j, 0], pts[j, 1], pts[j, 2])


@njit(parallel=True, cache=True, fastmath=True)
def grad_points(pts, g, eps, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    # tetrahedral central differences: 4 samples
    for j in prange(pts.shape[0]):
        x = pts[j, 0]
        y = pts[j, 1]
        z = pts[j, 2]
        a = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, x + eps, y - eps, z - eps)
        b = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, x - eps, y - eps, z + eps)
        c = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, x - eps, y + eps, z - eps)
        d = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, x + eps, y + eps, z + eps)
        s = 1.0 / (4.0 * eps)
        g[j, 0] = (a - b - c + d) * s
        g[j, 1] = (-a - b + c + d) * s
        g[j, 2] = (-a + b - c + d) * s


@njit(parallel=True, cache=True, fastmath=True)
def eval_blocks(origins, B, h, out, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    """Sample (B+1)^3 grids at block origins (block-local index i,j,l -> origin + (i,j,l)*h)."""
    nb = origins.shape[0]
    n1 = B + 1
    for bi in prange(nb):
        ox = origins[bi, 0]
        oy = origins[bi, 1]
        oz = origins[bi, 2]
        for i in range(n1):
            for j in range(n1):
                for l in range(n1):
                    out[bi, i, j, l] = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems,
                                                ox + i * h, oy + j * h, oz + l * h)


@njit(parallel=True, cache=True, fastmath=True)
def attr_points(pts, reg, bw, feat, masks, bone, region, feature, mask, sig_scale, sig_add,
                kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    for j in prange(pts.shape[0]):
        x = pts[j, 0]
        y = pts[j, 1]
        z = pts[j, 2]
        c = _cell_of(g0, cell, dims, x, y, z)
        if c < 0:
            bw[j, 0] = 1.0
            continue
        tot = 0.0
        best = -1.0
        bi = -1
        for q in range(cstart[c], cstart[c + 1]):
            i = citems[q]
            if sign[i] < 0:
                continue
            di = shape_dist(kind[i], P, LT, SW, i, x, y, z)
            sig = sig_scale * k[i] + sig_add
            w = math.exp(-max(di, 0.0) / sig)
            if w < 1e-3:
                continue
            sc = math.exp(-abs(di) / sig)
            if kind[i] == 3:
                dd, sf = sweep_eval(SW, int(P[i, 0]), int(P[i, 1]), int(P[i, 2]), x, y, z)
                r = int(P[i, 0]) + min(int(P[i, 1]) - 1, int(sf + 0.5))
                b0 = int(SW[r, 18])
                b1 = int(SW[r, 19])
                bf = SW[r, 20]
                bw[j, b0] += w * (1 - bf)
                bw[j, b1] += w * bf
                rg = int(SW[r, 21])
                reg[j, rg] = max(reg[j, rg], sc)
            else:
                bw[j, bone[i]] += w
                reg[j, region[i]] = max(reg[j, region[i]], sc)
            tot += w
            feat[j, feature[i]] = max(feat[j, feature[i]], sc)
            for m in range(3):
                mi = mask[i, m]
                if mi >= 0:
                    masks[j, mi] = max(masks[j, mi], sc)
            if sc > best:
                best = sc
                bi = i
        if tot > 0:
            for q in range(bw.shape[1]):
                bw[j, q] /= tot
        else:
            bw[j, 0] = 1.0
