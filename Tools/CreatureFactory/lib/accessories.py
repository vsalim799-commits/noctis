"""Separate rigid meshes attached to one bone each: claws (meshed from small SDF chains), teeth (lathed
cones, hidden by the lips at rest) and eyeballs (UV spheres with an iris UV layout).
All in anatomy space (metres, +x forward, +y up, +z animal right)."""
from __future__ import annotations

import math
import numpy as np

from .sdf import Compiled
from .mesher import mesh_sdf


def claw_meshes(doc, h=0.0025, log=print):
    """One mesh per claw from its centre-line (doc['claws']): a chain of laterally compressed round
    cones, curved downward, with a sharpened tip."""
    out = []
    bone_names = [b["name"] for b in doc["bones"]]
    for i, c in enumerate(doc["claws"]):
        P = [np.asarray(p, float) for p in c["points"]]
        R = list(c["radii"])
        R[-1] = max(0.004, min(R[-1], 0.35 * R[0]))
        shapes = []
        for k in range(len(P) - 1):
            shapes.append(dict(bone=c["bone"], kind="roundCone", a=list(P[k]), b=list(P[k + 1]), ra=R[k], rb=R[k + 1],
                               scale=[1.0, 1.0, 0.78], blend=0.004, region="foot"))
        comp = Compiled(shapes, bone_names, band=0.02, cell=0.05)
        V, F = mesh_sdf(comp, h, B=12, log=lambda *a: None, min_component_faces=20)
        out.append(dict(name=f"claw_{i:02d}_{c['bone']}", bone=c["bone"], V=V, F=F, base=P[0], tip=P[-1]))
    log(f"  claws: {len(out)} meshes, {sum(len(o['F']) for o in out)} tris before decimation")
    return out


def tooth_mesh(root, tip, radius, sides=6, rings=5, recurve=0.12, flat=0.82):
    root = np.asarray(root, float)
    tip = np.asarray(tip, float)
    ax = tip - root
    L = np.linalg.norm(ax)
    d = ax / L
    # recurve towards the back of the jaw (-x) and labio-lingual axis ~ z
    back = np.array([-1.0, 0.0, 0.0])
    back = back - (back @ d) * d
    back /= np.linalg.norm(back) + 1e-12
    side = np.cross(d, back)
    V = []
    for j in range(rings):
        t = j / rings
        c = root + ax * t + back * (recurve * L * t * t)
        r = radius * (1 - t) ** 0.75
        for k in range(sides):
            a = 2 * math.pi * k / sides
            V.append(c + back * (r * math.cos(a)) + side * (r * flat * math.sin(a)))
    V.append(tip + back * (recurve * L))
    F = []
    for j in range(rings - 1):
        for k in range(sides):
            a = j * sides + k
            b = j * sides + (k + 1) % sides
            F += [(a, b, b + sides), (a, b + sides, a + sides)]
    apex = len(V) - 1
    for k in range(sides):
        a = (rings - 1) * sides + k
        b = (rings - 1) * sides + (k + 1) % sides
        F.append((a, b, apex))
    # cap the root
    rc = len(V)
    V.append(root)
    for k in range(sides):
        F.append((rc, (k + 1) % sides, k))
    return np.array(V), np.array(F, np.int32)


def teeth_meshes(doc):
    out = []
    for i, t in enumerate(doc.get("teeth", [])):
        V, F = tooth_mesh(t["root"], t["tip"], t["radius"])
        # orient faces outward (rings go root->tip, check with the centroid)
        out.append(dict(name=f"tooth_{i:02d}", bone="head" if t["jaw"] == "upper" else "jaw", V=V, F=F))
    return out


def eye_mesh(center, radius, direction, nu=32, nv=20):
    """UV sphere whose pole (+direction) is the cornea centre; UV: u = longitude, v = polar angle/pi,
    the iris occupies v < ~0.28 (see bake/render eye material)."""
    c = np.asarray(center, float)
    d = np.asarray(direction, float)
    d /= np.linalg.norm(d)
    a = np.cross(d, [0, 1, 0])
    if np.linalg.norm(a) < 1e-6:
        a = np.cross(d, [1, 0, 0])
    a /= np.linalg.norm(a)
    b = np.cross(d, a)
    V, UV = [], []
    for j in range(nv + 1):
        th = math.pi * j / nv
        for i in range(nu + 1):
            ph = 2 * math.pi * i / nu
            n = d * math.cos(th) + (a * math.cos(ph) + b * math.sin(ph)) * math.sin(th)
            V.append(c + radius * n)
            UV.append((0.5 + 0.5 * (th / math.pi) * math.cos(ph), 0.5 + 0.5 * (th / math.pi) * math.sin(ph)))
    F = []
    W = nu + 1
    for j in range(nv):
        for i in range(nu):
            p = j * W + i
            if j == 0:
                F.append((p, p + W + 1, p + W))
            elif j == nv - 1:
                F.append((p, p + 1, p + W))
            else:
                F.append((p, p + 1, p + W + 1, p + W))
    return np.array(V), F, np.array(UV)
