"""Numpy mesh accumulator: tubes (parallel-transport frames), cards, convex hulls; conversion to Blender.

Units: metres, Z up, pivot at the base of the plant (0, 0, 0). Faces are polygons (tris or quads) with a
material slot index, per-corner UVs and a per-vertex wind colour (R = flexibility 0 trunk .. 1 leaf tip,
G = per-element random phase, B = normalised height), see README "Vent".
"""
from __future__ import annotations

import math

import numpy as np


def normalize(v, axis=-1):
    n = np.linalg.norm(v, axis=axis, keepdims=True)
    return v / np.maximum(n, 1e-12)


def any_perp(t):
    a = np.array([0.0, 0.0, 1.0]) if abs(t[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    p = np.cross(t, a)
    return p / max(np.linalg.norm(p), 1e-12)


class Mesh:
    def __init__(self):
        self.V = []      # list of (n, 3)
        self.C = []      # list of (n, 3) wind colours
        self.polys = []  # list of (indices array (m, k), uv array (m, k, 2), material int)
        self.nv = 0

    # --------------------------------------------------------------------------------- primitives
    def add_verts(self, V, col):
        V = np.asarray(V, float)
        base = self.nv
        self.V.append(V)
        col = np.asarray(col, float)
        if col.ndim == 1:
            col = np.repeat(col[None], len(V), 0)
        self.C.append(col)
        self.nv += len(V)
        return base

    def add_faces(self, F, UV, mat):
        F = np.asarray(F, np.int64)
        UV = np.asarray(UV, float)
        if len(F):
            self.polys.append((F, UV, int(mat)))

    def tube(self, pts, radii, sides, mat, uv_scale=(1.0, 1.0), wind=(0.0, 0.0), cap=True, radial_fn=None,
             frame0=None, v_offset=0.0, height_norm=30.0, mat_fn=None, cap_mat=None, offset_fn=None):
        """Generalised cylinder along a polyline. uv_scale = tile size in metres (u around, v along).
        radial_fn(theta (k,), s (n,)) -> multiplicative radius modulation (n, k) for buttresses / irregular logs.
        wind = (flex at start, flex at end)."""
        pts = np.asarray(pts, float)
        radii = np.asarray(radii, float)
        n = len(pts)
        if n < 2:
            return
        seg = np.diff(pts, axis=0)
        segl = np.linalg.norm(seg, axis=1)
        T = np.zeros_like(pts)
        T[0] = seg[0]
        T[-1] = seg[-1]
        T[1:-1] = seg[:-1] / np.maximum(segl[:-1, None], 1e-9) + seg[1:] / np.maximum(segl[1:, None], 1e-9)
        T = normalize(T)
        N = np.zeros_like(pts)
        N[0] = frame0 if frame0 is not None else any_perp(T[0])
        for i in range(1, n):
            v = N[i - 1] - np.dot(N[i - 1], T[i]) * T[i]
            ln = np.linalg.norm(v)
            N[i] = v / ln if ln > 1e-9 else any_perp(T[i])
        B = np.cross(T, N)
        k = int(sides)
        th = np.linspace(0, 2 * math.pi, k + 1)  # seam vertex duplicated for UVs
        ct, st = np.cos(th), np.sin(th)
        s = np.concatenate([[0], np.cumsum(segl)])
        R = np.repeat(radii[:, None], k + 1, 1)
        if radial_fn is not None:
            R = R * radial_fn(th, s)
        ring = pts[:, None, :] + R[..., None] * (ct[None, :, None] * N[:, None, :] + st[None, :, None] * B[:, None, :])
        if offset_fn is not None:
            ring = ring + offset_fn(th, s, T, N, B)
        Vr = ring.reshape(-1, 3)
        flex = np.interp(s, [0, s[-1] if s[-1] > 0 else 1], [wind[0], wind[1]])
        phase = wind[2] if len(wind) > 2 else np.random.default_rng(int(abs(pts[0, 0] * 1e4 + pts[0, 1] * 1e3)) % 2**31).random()
        col = np.stack([np.repeat(flex, k + 1), np.full(n * (k + 1), phase),
                        np.clip(Vr[:, 2] / height_norm, 0, 1)], 1)
        base = self.add_verts(Vr, col)
        ii, jj = np.meshgrid(np.arange(n - 1), np.arange(k), indexing="ij")
        a = base + ii * (k + 1) + jj
        F = np.stack([a, a + 1, a + k + 2, a + k + 1], -1).reshape(-1, 4)
        circ = 2 * math.pi * float(np.mean(radii))
        ureps = max(1, round(circ / uv_scale[0]))
        u = th / (2 * math.pi) * ureps
        v = (s + v_offset) / uv_scale[1]
        U = np.stack([np.broadcast_to(u[None, :], (n, k + 1)), np.broadcast_to(v[:, None], (n, k + 1))], -1)
        UVq = np.stack([U[ii, jj], U[ii, jj + 1], U[ii + 1, jj + 1], U[ii + 1, jj]], -2).reshape(-1, 4, 2)
        if mat_fn is not None:
            centers = Vr[F].mean(1)
            mats = np.asarray(mat_fn(centers, ii.ravel(), jj.ravel()), int)
            for mm in np.unique(mats):
                sel = mats == mm
                self.add_faces(F[sel], UVq[sel], int(mm))
        else:
            self.add_faces(F, UVq, mat)
        if cap and radii[-1] > 0.004:
            c = self.add_verts(pts[-1:] + T[-1:] * radii[-1] * 0.3, np.array([flex[-1], phase, min(1, pts[-1, 2] / height_norm)]))
            last = base + (n - 1) * (k + 1)
            F = np.stack([np.full(k, c), last + np.arange(k), last + np.arange(k) + 1], -1)
            uvc = np.stack([np.full((k, 2), 0.5), np.stack([0.5 + 0.4 * ct[:-1], 0.5 + 0.4 * st[:-1]], -1),
                            np.stack([0.5 + 0.4 * ct[1:], 0.5 + 0.4 * st[1:]], -1)], 1)
            self.add_faces(F, uvc, mat if cap_mat is None else cap_mat)
        return base

    def card(self, base, up, side, w, h, uv_rect, mat, bend=0.0, normal=None, wind_phase=0.0, flex=(0.7, 1.0),
             segments=1, height_norm=30.0, width_taper=1.0):
        """Alpha card attached at the middle of its bottom edge. bend: sag of the tip along -normal (metres)."""
        base = np.asarray(base, float)
        up = np.asarray(up, float)
        side = np.asarray(side, float)
        nrm = np.cross(side, up) if normal is None else np.asarray(normal, float)
        u0, v0, u1, v1 = uv_rect
        rows = segments + 1
        verts, uvs = [], []
        for r in range(rows):
            t = r / segments
            sag = bend * t * t
            ww = w * (1 - (1 - width_taper) * t)
            c = base + up * h * t - nrm * sag
            for sgn in (-0.5, 0.5):
                verts.append(c + side * ww * sgn)
                uvs.append((u0 + (u1 - u0) * (sgn + 0.5), v0 + (v1 - v0) * t))
        verts = np.array(verts)
        fl = np.repeat(np.linspace(flex[0], flex[1], rows), 2)
        col = np.stack([fl, np.full(len(verts), wind_phase), np.clip(verts[:, 2] / height_norm, 0, 1)], 1)
        b = self.add_verts(verts, col)
        F, UV = [], []
        uvs = np.array(uvs)
        for r in range(segments):
            a = b + 2 * r
            F.append([a, a + 1, a + 3, a + 2])
            UV.append(uvs[[2 * r, 2 * r + 1, 2 * r + 3, 2 * r + 2]])
        self.add_faces(np.array(F), np.array(UV), mat)

    def strip(self, P, side_dirs, widths, uv_rect, mat, wind_phase=0.0, flex=(0.3, 1.0), height_norm=30.0):
        """Ribbon along points P (n,3) with per-point side directions and widths; UV along the cell (bottom -> top)."""
        P = np.asarray(P, float)
        n = len(P)
        S = normalize(np.asarray(side_dirs, float))
        w = np.asarray(widths, float)[:, None]
        L = np.concatenate([P - S * w * 0.5, P + S * w * 0.5], 0)  # left row then right row
        u0, v0, u1, v1 = uv_rect
        s = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))])
        t = s / max(s[-1], 1e-9)
        fl = np.interp(t, [0, 1], flex)
        col = np.stack([np.concatenate([fl, fl]), np.full(2 * n, wind_phase), np.clip(L[:, 2] / height_norm, 0, 1)], 1)
        b = self.add_verts(L, col)
        i = np.arange(n - 1)
        F = np.stack([b + i, b + n + i, b + n + i + 1, b + i + 1], -1)
        vv = v0 + (v1 - v0) * t
        UV = np.stack([np.stack([np.full(n - 1, u0), vv[:-1]], -1), np.stack([np.full(n - 1, u1), vv[:-1]], -1),
                       np.stack([np.full(n - 1, u1), vv[1:]], -1), np.stack([np.full(n - 1, u0), vv[1:]], -1)], 1)
        self.add_faces(F, UV, mat)

    def surface(self, P, UV, mat, flex=None, phase=0.0, height_norm=30.0, close_u=False):
        """Quads over a (rows, cols, 3) grid of points with (rows, cols, 2) UVs."""
        P = np.asarray(P, float)
        n, m = P.shape[:2]
        V = P.reshape(-1, 3)
        fl = np.zeros(len(V)) if flex is None else np.asarray(flex, float).reshape(-1)
        col = np.stack([fl, np.full(len(V), phase), np.clip(V[:, 2] / height_norm, 0, 1)], 1)
        b = self.add_verts(V, col)
        ii, jj = np.meshgrid(np.arange(n - 1), np.arange(m - 1), indexing="ij")
        a = b + ii * m + jj
        F = np.stack([a, a + 1, a + m + 1, a + m], -1).reshape(-1, 4)
        UV = np.asarray(UV, float)
        UVq = np.stack([UV[ii, jj], UV[ii, jj + 1], UV[ii + 1, jj + 1], UV[ii + 1, jj]], -2).reshape(-1, 4, 2)
        self.add_faces(F, UVq, mat)
        return b

    def fan(self, center, ring, uv_center, uv_ring, mat, flex=0.0, phase=0.0, height_norm=30.0):
        """Triangle fan (caps, whorls): center (3,), ring (k, 3) closed implicitly."""
        ring = np.asarray(ring, float)
        k = len(ring)
        V = np.concatenate([np.asarray(center, float)[None], ring])
        col = np.stack([np.full(k + 1, flex), np.full(k + 1, phase), np.clip(V[:, 2] / height_norm, 0, 1)], 1)
        b = self.add_verts(V, col)
        i = np.arange(k)
        F = np.stack([np.full(k, b), b + 1 + i, b + 1 + (i + 1) % k], -1)
        uvr = np.asarray(uv_ring, float)
        UV = np.stack([np.repeat(np.asarray(uv_center, float)[None], k, 0), uvr, uvr[(i + 1) % k]], 1)
        self.add_faces(F, UV, mat)

    def merge(self, other, offset=None, rot=None, scale=1.0):
        if not other.V:
            return
        V = np.concatenate(other.V) * scale
        if rot is not None:
            V = V @ np.asarray(rot).T
        if offset is not None:
            V = V + np.asarray(offset)
        base = self.add_verts(V, np.concatenate(other.C))
        for F, UV, m in other.polys:
            self.add_faces(F + base, UV, m)

    def displace(self, maps):
        """Displaces vertices along their normals by tileable height maps sampled at their UVs.
        maps: {material index: (height array (rows, cols) in [0, 1], amplitude m)}. Used on Nanite-ready LOD0s."""
        V, C = self.arrays()
        if not len(V):
            return
        N = np.zeros_like(V)
        vid, uvs, mats = [], [], []
        for F, UV, m in self.polys:
            P = V[F]
            if F.shape[1] == 4:
                fn = np.cross(P[:, 2] - P[:, 0], P[:, 3] - P[:, 1])
            else:
                fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
            for k in range(F.shape[1]):
                np.add.at(N, F[:, k], fn)
            vid.append(F.ravel())
            uvs.append(UV.reshape(-1, 2))
            mats.append(np.full(F.size, m))
        N = normalize(N)
        vid = np.concatenate(vid)
        uvs = np.concatenate(uvs)
        mats = np.concatenate(mats)
        first = np.unique(vid, return_index=True)[1]
        d = np.zeros(len(V))
        for m, (H, amp) in maps.items():
            sel = first[mats[first] == m]
            if not len(sel):
                continue
            u = np.mod(uvs[sel, 0], 1.0)
            v = np.mod(uvs[sel, 1], 1.0)
            r = ((1 - v) * (H.shape[0] - 1)).astype(int)
            c = (u * (H.shape[1] - 1)).astype(int)
            d[vid[sel]] = (H[r, c] - 0.5) * amp
        V = V + N * d[:, None]
        self.V = [V]
        self.C = [C]

    # --------------------------------------------------------------------------------- queries
    def arrays(self):
        V = np.concatenate(self.V) if self.V else np.zeros((0, 3))
        C = np.concatenate(self.C) if self.C else np.zeros((0, 3))
        return V, C

    def tri_count(self):
        return int(sum(len(F) * (F.shape[1] - 2) for F, _, _ in self.polys))

    def bounds(self):
        V, _ = self.arrays()
        return V.min(0), V.max(0)

    # --------------------------------------------------------------------------------- blender
    def to_blender(self, name, materials, collection=None):
        import bpy
        V, C = self.arrays()
        me = bpy.data.meshes.new(name)
        me.vertices.add(len(V))
        me.vertices.foreach_set("co", V.astype(np.float32).ravel())
        sizes, loops, uvs, mats = [], [], [], []
        for F, UV, m in self.polys:
            sizes.append(np.full(len(F), F.shape[1]))
            loops.append(F.ravel())
            uvs.append(UV.reshape(-1, 2))
            mats.append(np.full(len(F), m))
        sizes = np.concatenate(sizes) if sizes else np.zeros(0, int)
        loops = np.concatenate(loops) if loops else np.zeros(0, int)
        me.loops.add(len(loops))
        me.loops.foreach_set("vertex_index", loops.astype(np.int32))
        me.polygons.add(len(sizes))
        starts = np.concatenate([[0], np.cumsum(sizes)[:-1]]) if len(sizes) else np.zeros(0)
        me.polygons.foreach_set("loop_start", starts.astype(np.int32))
        me.polygons.foreach_set("loop_total", sizes.astype(np.int32))
        me.polygons.foreach_set("material_index", np.concatenate(mats).astype(np.int32) if mats else np.zeros(0, np.int32))
        uv = me.uv_layers.new(name="UVMap")
        if len(uvs):
            uv.data.foreach_set("uv", np.concatenate(uvs).astype(np.float32).ravel())
        attr = me.color_attributes.new("Wind", "FLOAT_COLOR", "POINT")
        rgba = np.concatenate([C, np.ones((len(C), 1))], 1).astype(np.float32)
        attr.data.foreach_set("color", rgba.ravel())
        me.update(calc_edges=True)
        me.validate(clean_customdata=False)
        for mat in materials:
            me.materials.append(mat)
        me.polygons.foreach_set("use_smooth", np.ones(len(me.polygons), bool))
        ob = bpy.data.objects.new(name, me)
        (collection or bpy.context.scene.collection).objects.link(ob)
        return ob


def convex_hull_mesh(points):
    """Convex hull of points -> (V, F tris) for UCX collision proxies."""
    from scipy.spatial import ConvexHull
    P = np.asarray(points, float)
    h = ConvexHull(P)
    used = np.unique(h.simplices)
    remap = -np.ones(len(P), int)
    remap[used] = np.arange(len(used))
    F = remap[h.simplices]
    V = P[used]
    # Outward winding
    c = V.mean(0)
    for i, f in enumerate(F):
        n = np.cross(V[f[1]] - V[f[0]], V[f[2]] - V[f[0]])
        if np.dot(n, V[f[0]] - c) < 0:
            F[i] = f[[0, 2, 1]]
    return V, F


def prism_points(center_bottom, axis, radius, length, sides=8):
    c = np.asarray(center_bottom, float)
    a = normalize(np.asarray(axis, float))
    n = any_perp(a)
    b = np.cross(a, n)
    th = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    ring = radius * (np.cos(th)[:, None] * n + np.sin(th)[:, None] * b)
    return np.concatenate([c + ring, c + a * length + ring])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def rot_axis(axis, ang):
    a = normalize(np.asarray(axis, float))
    x, y, z = a
    c, s = math.cos(ang), math.sin(ang)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])
