"""Narrow-band block marching cubes of an SDF anatomy -> welded triangle mesh (numpy arrays).

The bounding box is split into blocks of B^3 voxels; a block is sampled only when the coarse field at
its centre says the surface may cross it. Each active block is sampled on its (B+1)^3 lattice (shared
faces are evaluated identically by both neighbours), polygonised with scikit-image's Lewiner
marching cubes, and the duplicated face vertices are welded on an integer lattice key.
"""
from __future__ import annotations

import math
import time
import numpy as np
from skimage.measure import marching_cubes
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components

from .sdf import eval_blocks


def mesh_sdf(comp, h, B=None, batch=1500, log=print, min_component_faces=200, bbox=None):
    t0 = time.time()
    if B is None:
        # the culled field is clamped at 0.1 m (lib/sdf.field_at): blocks must be small enough for
        # the centre test |d| < 1.6 half-diagonal + 2h to stay below the clamp, or nothing is culled
        B = int(max(4, min(16, (0.095 - 2 * h) / (1.6 * 0.866 * h))))
    lo = comp.box[:, :3].min(0) - 3 * h
    hi = comp.box[:, 3:].max(0) + 3 * h
    if bbox is not None:
        lo = np.maximum(lo, bbox[0])
        hi = np.minimum(hi, bbox[1])
    bs = B * h
    nb = np.ceil((hi - lo) / bs).astype(int)
    ix, iy, iz = np.meshgrid(np.arange(nb[0]), np.arange(nb[1]), np.arange(nb[2]), indexing="ij")
    idx = np.stack([ix.ravel(), iy.ravel(), iz.ravel()], 1)
    centers = lo + (idx + 0.5) * bs
    d = comp.field(centers)
    half = math.sqrt(3) * bs / 2
    active = idx[np.abs(d) < 1.6 * half + 2 * h]
    log(f"  blocks: {len(idx)} total, {len(active)} active (h={h*1000:.1f} mm, B={B})")
    V, F = [], []
    nv = 0
    n1 = B + 1
    for s in range(0, len(active), batch):
        a = active[s:s + batch]
        origins = np.ascontiguousarray(lo + a * bs, dtype=np.float64)
        vol = np.empty((len(a), n1, n1, n1), np.float64)
        eval_blocks(origins, B, h, vol, *comp.args())
        for i in range(len(a)):
            v = vol[i]
            if v.min() >= 0 or v.max() <= 0:
                continue
            verts, faces, _, _ = marching_cubes(v, 0.0, spacing=(h, h, h), gradient_direction="descent", allow_degenerate=False)
            V.append(verts + origins[i])
            F.append(faces + nv)
            nv += len(verts)
        log(f"  {min(s + batch, len(active))}/{len(active)} blocks, {nv} raw verts, {time.time() - t0:.1f}s")
    V = np.concatenate(V, 0)
    F = np.concatenate(F, 0).astype(np.int64)
    # weld on a lattice key (vertices lie on lattice edges: two integer coordinates + one fraction)
    key = np.round((V - lo) / h * 4096.0).astype(np.int64)
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.ravel()
    Vw = np.zeros((len(uniq), 3))
    np.add.at(Vw, inv, V)
    cnt = np.bincount(inv, minlength=len(uniq))
    Vw /= cnt[:, None]
    F = inv[F]
    ok = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])
    F = F[ok]
    # remove duplicate faces (can appear at welded degenerate spots)
    Fs = np.sort(F, 1)
    _, ui = np.unique(Fs, axis=0, return_index=True)
    F = F[np.sort(ui)]
    # components
    n = len(Vw)
    e = np.concatenate([F[:, [0, 1]], F[:, [1, 2]]], 0)
    g = coo_matrix((np.ones(len(e)), (e[:, 0], e[:, 1])), shape=(n, n))
    ncomp, lab = connected_components(g, directed=False)
    fl = lab[F[:, 0]]
    fc = np.bincount(fl, minlength=ncomp)
    keep_c = fc >= min_component_faces
    F = F[keep_c[fl]]
    used = np.zeros(n, bool)
    used[F.ravel()] = True
    remap = -np.ones(n, np.int64)
    remap[used] = np.arange(used.sum())
    Vw = Vw[used]
    F = remap[F]
    # outward orientation (positive signed volume)
    vol, _ = mesh_volume_area(Vw, F)
    if vol < 0:
        F = F[:, ::-1]
    log(f"  mesh: {len(Vw)} verts, {len(F)} tris, {int(keep_c.sum())} components kept of {ncomp}, {time.time() - t0:.1f}s")
    return Vw, np.ascontiguousarray(F, dtype=np.int32)


def vertex_normals(V, F):
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    vn = np.zeros_like(V)
    for k in range(3):
        np.add.at(vn, F[:, k], fn)
    return vn / (np.linalg.norm(vn, axis=1, keepdims=True) + 1e-20)


def mesh_volume_area(V, F):
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    vol = np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6.0
    area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1).sum()
    return vol, area


def adjacency(F, n):
    e = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]], 0)
    e = np.concatenate([e, e[:, ::-1]], 0)
    A = coo_matrix((np.ones(len(e), np.float32), (e[:, 0], e[:, 1])), shape=(n, n)).tocsr()
    A.data[:] = 1.0
    return A


def smooth_attr(A, X, iters=5, alpha=0.5):
    deg = np.asarray(A.sum(1)).ravel()
    deg[deg == 0] = 1
    for _ in range(iters):
        X = (1 - alpha) * X + alpha * (A @ X) / deg[:, None] if X.ndim == 2 else (1 - alpha) * X + alpha * (A @ X) / deg
    return X
