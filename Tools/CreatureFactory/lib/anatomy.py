"""Anatomy spec helpers: load/save Data/Anatomy/<species>.json, rigid re-posing of shapes with their
bones, Euler conversions (three.js 'XYZ' convention, same as the preview), ray casting on the SDF.
"""
from __future__ import annotations

import copy
import json
import math
import os
import numpy as np

from .sdf import Compiled, euler_xyz_matrix

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def anatomy_path(species):
    return os.path.join(REPO, "Data", "Anatomy", f"{species}.json")


def load(species):
    with open(anatomy_path(species)) as f:
        return json.load(f)


def save(species, doc):
    p = anatomy_path(species)
    os.makedirs(os.path.dirname(p), exist_ok=True)

    def rnd(o):
        if isinstance(o, float):
            return round(o, 5)
        if isinstance(o, list):
            return [rnd(v) for v in o]
        if isinstance(o, dict):
            return {k: rnd(v) for k, v in o.items()}
        return o
    with open(p, "w") as f:
        json.dump(rnd(doc), f, indent=1, ensure_ascii=False)
    return p


def mat_to_euler(m):
    m = np.asarray(m)
    y = math.asin(max(-1.0, min(1.0, m[0, 2])))
    if abs(m[0, 2]) < 0.9999999:
        return [math.atan2(-m[1, 2], m[2, 2]), y, math.atan2(-m[0, 1], m[0, 0])]
    return [math.atan2(m[2, 1], m[1, 1]), y, 0.0]


def basis_to_euler(ax, ay, az):
    return mat_to_euler(np.stack([ax, ay, az], 1))


def rot_between(a, b):
    """Minimal rotation matrix taking direction a to direction b."""
    a = np.asarray(a, float) / np.linalg.norm(a)
    b = np.asarray(b, float) / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(a @ b)
    if np.linalg.norm(v) < 1e-12:
        return np.eye(3) if c > 0 else -np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx * (1.0 / (1.0 + c))


def rot_axis(axis, ang):
    axis = np.asarray(axis, float) / np.linalg.norm(axis)
    x, y, z = axis
    c, s = math.cos(ang), math.sin(ang)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def transform_shape(s, R, h0, h1):
    """Rigidly move a shape: p' = R (p - h0) + h1."""
    s = copy.deepcopy(s)
    R = np.asarray(R, float)
    h0 = np.asarray(h0, float)
    h1 = np.asarray(h1, float)
    f = lambda p: list(R @ (np.asarray(p, float) - h0) + h1)
    if s["kind"] == "ellipsoid":
        s["center"] = f(s["center"])
        s["rotation"] = mat_to_euler(R @ euler_xyz_matrix(s.get("rotation", [0, 0, 0])))
    elif s["kind"] in ("roundCone", "capsule"):
        s["a"] = f(s["a"])
        s["b"] = f(s["b"])
    elif s["kind"] == "loft":
        s["origin"] = f(s["origin"])
        s["rotation"] = mat_to_euler(R @ euler_xyz_matrix(s.get("rotation", [0, 0, 0])))
    return s


def scale_shape(s, f, about=None):
    """Thicken a shape by factor f (radii only, keeps its axis/length)."""
    s = copy.deepcopy(s)
    if s["kind"] == "ellipsoid":
        s["radii"] = [r * f for r in s["radii"]]
    elif s["kind"] in ("roundCone", "capsule"):
        s["ra"] = s.get("ra", s.get("r", 0.1)) * f
        if "rb" in s:
            s["rb"] = s["rb"] * f
    return s


class Caster:
    """Ray casting / normals on the SDF of a list of shapes (used to attach surface details)."""

    def __init__(self, shapes, bone_names):
        self.c = Compiled(shapes, bone_names, band=0.2)

    def f(self, p):
        return float(self.c.field(np.asarray([p], float))[0])

    def normal(self, p):
        g = self.c.gradient(np.asarray([p], float), eps=2e-3)[0]
        return g / (np.linalg.norm(g) + 1e-12)

    def cast(self, o, d, maxt=2.0, step=0.003):
        o = np.asarray(o, float)
        d = np.asarray(d, float) / np.linalg.norm(d)
        ts = np.arange(0.0, maxt, step)
        pts = o[None] + ts[:, None] * d[None]
        v = self.c.field(pts)
        idx = np.nonzero(v < 0)[0]
        if len(idx) == 0:
            raise RuntimeError(f"ray from {o} dir {d} missed")
        i = idx[0]
        if i == 0:
            return o
        t0, t1, f0, f1 = ts[i - 1], ts[i], v[i - 1], v[i]
        t = t0 + (t1 - t0) * f0 / (f0 - f1)
        return o + d * t

    def cast_at(self, target, d, back=0.6):
        d = np.asarray(d, float) / np.linalg.norm(d)
        return self.cast(np.asarray(target, float) - d * back, d)
