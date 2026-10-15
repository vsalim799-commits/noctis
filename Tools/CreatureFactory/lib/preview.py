"""Fast sphere-traced previews of an SDF anatomy (no meshing): orthographic side / top / front and a
3/4 perspective, clay-shaded with SDF ambient occlusion. Used to iterate on proportions in seconds.
"""
from __future__ import annotations

import math
import numpy as np
from numba import njit, prange
from PIL import Image, ImageDraw

from .sdf import field_at


@njit(cache=False, fastmath=True)
def _trace_one(ox, oy, oz, dx, dy, dz, maxt, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    t = 0.0
    for it in range(400):
        d = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, ox + dx * t, oy + dy * t, oz + dz * t)
        if d < 4e-4:
            return t
        t += max(d * 0.8, 2e-4)
        if t > maxt:
            return -1.0
    return -1.0


@njit(parallel=True, cache=False, fastmath=True)
def _trace(ro, rd, out_depth, out_n, out_ao, maxt, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems):
    n = ro.shape[0]
    for j in prange(n):
        t = _trace_one(ro[j, 0], ro[j, 1], ro[j, 2], rd[j, 0], rd[j, 1], rd[j, 2], maxt, kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems)
        out_depth[j] = t
        if t < 0:
            continue
        hx = ro[j, 0] + rd[j, 0] * t
        hy = ro[j, 1] + rd[j, 1] * t
        hz = ro[j, 2] + rd[j, 2] * t
        e = 1e-3
        gx = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx + e, hy, hz) - field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx - e, hy, hz)
        gy = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx, hy + e, hz) - field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx, hy - e, hz)
        gz = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx, hy, hz + e) - field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx, hy, hz - e)
        gl = math.sqrt(gx * gx + gy * gy + gz * gz) + 1e-12
        nx = gx / gl
        ny = gy / gl
        nz = gz / gl
        out_n[j, 0] = nx
        out_n[j, 1] = ny
        out_n[j, 2] = nz
        occ = 0.0
        for s in range(1, 6):
            hh = 0.006 * s * s
            dd = field_at(kind, sign, k, P, LT, SW, g0, cell, dims, cstart, citems, hx + nx * hh, hy + ny * hh, hz + nz * hh)
            occ += max(0.0, hh - dd) / hh / s
        out_ao[j] = max(0.0, 1.0 - 0.9 * occ)


def _shade(depth, nrm, ao, W, H, light=(0.35, 0.85, 0.4), view=None):
    L = np.asarray(light, np.float64)
    L /= np.linalg.norm(L)
    hit = depth > 0
    V = np.asarray(view if view is not None else (0, 0, 1.0), np.float64)
    lam = np.clip((nrm @ L) * 0.8 + 0.2, 0, 1) ** 1.3
    F2 = np.array([V[0] * 0.7, 0.25, V[2] * 0.7])
    F2 /= np.linalg.norm(F2)
    fill = np.clip(nrm @ F2, 0, 1) * 0.18
    R = np.array([-V[0], 0.5, -V[2]])
    R /= np.linalg.norm(R)
    rim = np.clip(nrm @ R, 0, 1) ** 2 * np.clip(1 - np.abs(nrm @ V), 0, 1) ** 1.5 * 0.6
    H_ = (L + V) / np.linalg.norm(L + V)
    spec = np.clip(nrm @ H_, 0, 1) ** 24 * 0.12
    c = (0.04 + 0.85 * lam + fill) * (0.25 + 0.75 * ao) + rim + spec
    col = np.stack([c * 0.62, c * 0.57, c * 0.5], 1)
    yy = np.repeat(np.linspace(0, 1, H), W)
    bg = np.stack([0.035 + 0.02 * (1 - yy)] * 3, 1)
    img = np.where(hit[:, None], col, bg)
    img = np.clip(img, 0, 1) ** (1 / 2.2)
    return (img.reshape(H, W, 3) * 255).astype(np.uint8)


def ortho_box(comp, view, box, W=900):
    """Orthographic view of a sub-box: view in side/top/front, box = (u0,u1,v0,v1) in view axes."""
    if view == "side":
        ax_u, ax_v, dax, sgn = 0, 1, 2, 1.0
    elif view == "top":
        ax_u, ax_v, dax, sgn = 0, 2, 1, 1.0
    else:
        ax_u, ax_v, dax, sgn = 2, 1, 0, 1.0
    u0, u1, v0, v1 = box
    Hh = int(W * (v1 - v0) / (u1 - u0))
    us = np.linspace(u0, u1, W)
    vs = np.linspace(v1, v0, Hh)
    if view == "side":
        us = us[::-1]
    if view == "front":
        us = us[::-1]
    UU, VV = np.meshgrid(us, vs)
    ro = np.zeros((W * Hh, 3))
    ro[:, ax_u] = UU.reshape(-1)
    ro[:, ax_v] = VV.reshape(-1)
    ro[:, dax] = sgn * 20.0
    rd = np.zeros((W * Hh, 3))
    rd[:, dax] = -sgn
    depth = np.empty(W * Hh)
    nrm = np.zeros((W * Hh, 3))
    ao = np.ones(W * Hh)
    _trace(ro, rd, depth, nrm, ao, 40.0, *comp.args())
    light = (0.3, 0.9, 0.5) if view != "top" else (0.3, 0.6, 0.6)
    return Image.fromarray(_shade(depth, nrm, ao, W, Hh, light=light, view=-rd[0])), (us, vs)


def ortho_view(comp, view, W=900, pad=0.3):
    """view: 'side' (right side, head to the left), 'left', 'top', 'front'."""
    lo, hi = comp.box[:, :3].min(0), comp.box[:, 3:].max(0)
    lo = np.minimum(lo, [-99, 0, -99])
    if view in ("side", "left"):
        ax_u, ax_v, depth_axis, sgn = 0, 1, 2, 1.0 if view == "side" else -1.0
    elif view == "top":
        ax_u, ax_v, depth_axis, sgn = 0, 2, 1, 1.0
    else:
        ax_u, ax_v, depth_axis, sgn = 2, 1, 0, 1.0
    u0, u1 = comp.box[:, ax_u].min() - pad, comp.box[:, 3 + ax_u].max() + pad
    v0, v1 = min(comp.box[:, ax_v].min(), 0) - pad, comp.box[:, 3 + ax_v].max() + pad
    if view == "top":
        v0 = comp.box[:, ax_v].min() - pad
    H = int(W * (v1 - v0) / (u1 - u0))
    us = np.linspace(u0, u1, W)
    vs = np.linspace(v1, v0, H)
    if view in ("side",):
        us = us[::-1]  # head (+x) on the left
    if view == "top":
        vs = vs[::-1]
    if view == "front":
        us = us[::-1]
    UU, VV = np.meshgrid(us, vs)
    ro = np.zeros((W * H, 3))
    ro[:, ax_u] = UU.reshape(-1)
    ro[:, ax_v] = VV.reshape(-1)
    far = 20.0
    ro[:, depth_axis] = sgn * far
    rd = np.zeros((W * H, 3))
    rd[:, depth_axis] = -sgn
    depth = np.empty(W * H)
    nrm = np.zeros((W * H, 3))
    ao = np.ones(W * H)
    _trace(ro, rd, depth, nrm, ao, 2 * far, *comp.args())
    img = _shade(depth, nrm, ao, W, H, view=-rd[0])
    im = Image.fromarray(img)
    return im, (us, vs)


def persp_view(comp, eye, target, W=900, H=506, fov=30.0, light=(0.35, 0.85, 0.4)):
    eye = np.asarray(eye, float)
    target = np.asarray(target, float)
    f = target - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    t = math.tan(math.radians(fov) / 2)
    xs = np.linspace(-t, t, W)
    ys = np.linspace(t * H / W, -t * H / W, H)
    X, Y = np.meshgrid(xs, ys)
    rd = f[None] + X.reshape(-1, 1) * r[None] + Y.reshape(-1, 1) * u[None]
    rd /= np.linalg.norm(rd, axis=1, keepdims=True)
    ro = np.repeat(eye[None], W * H, 0)
    depth = np.empty(W * H)
    nrm = np.zeros((W * H, 3))
    ao = np.ones(W * H)
    _trace(ro, rd, depth, nrm, ao, 60.0, *comp.args())
    return Image.fromarray(_shade(depth, nrm, ao, W, H, light=light, view=-f))


def grid_overlay(im, us, vs, step=1.0, color=(90, 90, 90)):
    d = ImageDraw.Draw(im)
    W, H = im.size
    for val in np.arange(math.floor(min(us)), math.ceil(max(us)) + 1e-6, step):
        px = np.argmin(np.abs(us - val))
        d.line([(px, 0), (px, H)], fill=color, width=1)
    for val in np.arange(math.floor(min(vs)), math.ceil(max(vs)) + 1e-6, step):
        py = np.argmin(np.abs(vs - val))
        d.line([(0, py), (W, py)], fill=color, width=1)
    return im


def contact(images, cols=2, bg=(30, 30, 32)):
    w = max(i.size[0] for i in images)
    rows = math.ceil(len(images) / cols)
    hs = [max(images[r * cols + c].size[1] for c in range(cols) if r * cols + c < len(images)) for r in range(rows)]
    sheet = Image.new("RGB", (w * cols, sum(hs)), bg)
    y = 0
    for r in range(rows):
        for c in range(cols):
            k = r * cols + c
            if k < len(images):
                sheet.paste(images[k], (c * w, y))
        y += hs[r]
    return sheet
