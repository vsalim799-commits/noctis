"""Derivation of Data/Anatomy/tyrannosaurus_rex.json from the three.js procedural T. rex.

Input : Build/Creatures/tyrannosaurus_rex/anatomy_base_from_js.json (node Tools/CreatureFactory/export_anatomy.mjs)
Output: the factory anatomy spec (bones with heads AND tails, SDF shapes with masks for the skin
        detail stage, eye/claw/tooth placements, skin + palette parameters, documented assumptions).

What is kept from the JS sculpt: trunk, tail, neck, arms, legs and feet volumes (re-posed).
What is changed, and why (weaknesses noted on the three.js renders):
  * skull rebuilt from lofted superellipse sections (was a few ellipsoids + a flat lip cutter ->
    boxy skull, blunt snout): long tapering snout, keyhole dorsal outline (narrow snout, broad
    temporal region), convex nasal profile, deep posterior skull, sinuous lip line;
  * eye rebuilt: eyeball as a separate mesh flush with puffy upper/lower lids, a brow shelf
    (lacrimal + postorbital) overhanging it, so it no longer reads as a hole;
  * hind legs re-posed to the species hip height (acetabulum 3.1 m, sim.hip_height_m) with the
    same bone lengths (femur 1.30, tibia 1.155, metatarsus 0.61 m) and less crouch, thigh and shank
    musculature thickened (soft tissue = plausible reconstruction);
  * body raised with the hips, belly line raised slightly so the trunk reads less long/low.
"""
from __future__ import annotations

import copy
import json
import math
import os
import numpy as np

from lib import anatomy as A
from lib.anatomy import Caster, basis_to_euler, rot_between, transform_shape, scale_shape, rot_axis

SPECIES = "tyrannosaurus_rex"

JS_TO_NEW = {
    "root": "root", "pelvis": "pelvis", "spine1": "spine_01", "spine2": "spine_02", "spine3": "spine_03",
    "neck1": "neck_01", "neck2": "neck_02", "neck3": "neck_03", "skull": "head", "jaw": "jaw",
    **{f"tail{i}": f"tail_{i:02d}" for i in range(1, 11)},
    **{f"{a}{s}": f"{b}_{s.lower()}" for s in "LR" for a, b in [("thigh", "thigh"), ("shin", "shin"), ("meta", "metatarsus"),
                                                                 ("toes", "toes"), ("upperArm", "upperarm"),
                                                                 ("foreArm", "forearm"), ("hand", "hand")]},
}

# ---------------------------------------------------------------------------------------------
# Skeleton targets (metres). Bone lengths as in trex_anatomy.js (femur 1.30, tibia 1.155,
# metatarsus III 0.61 m: adult Sue-sized proportions); acetabulum at sim.hip_height_m = 3.1 m.
HIP_Y = 3.1
DY = 0.10                      # everything above the legs is raised with the acetabulum (3.0 -> 3.1)
FEMUR, TIBIA, META = 1.30, 1.155, 0.61
ANG_FEMUR, ANG_TIBIA, ANG_META = math.radians(20), math.radians(15), math.radians(20)


def leg_chain(z_hip=0.45):
    hip = np.array([0.0, HIP_Y, z_hip])
    knee = hip + FEMUR * np.array([math.sin(ANG_FEMUR), -math.cos(ANG_FEMUR), 0]) + [0, 0, 0.03]
    ankle = knee + TIBIA * np.array([-math.sin(ANG_TIBIA), -math.cos(ANG_TIBIA), 0]) + [0, 0, -0.04]
    mtp = ankle + META * np.array([math.sin(ANG_META), -math.cos(ANG_META), 0]) + [0, 0, -0.01]
    return hip, knee, ankle, mtp


# Head frame: origin at the jaw joint (quadrate-articular), u forward along the skull, v up, w = z.
JAW = np.array([3.47, 3.58, 0.0])
PITCH = 0.11                   # head carried ~6 deg nose-down (plausible relaxed carriage)
FWD = np.array([math.cos(PITCH), -math.sin(PITCH), 0.0])
UP = np.array([math.sin(PITCH), math.cos(PITCH), 0.0])
ZZ = np.array([0.0, 0.0, 1.0])


def H(u, v, w=0.0):
    return JAW + u * FWD + v * UP + w * ZZ


def HD(d):
    return d[0] * FWD + d[1] * UP + d[2] * ZZ


HEAD_ROT = basis_to_euler(FWD, UP, ZZ)


def HR(r=(0, 0, 0)):
    return A.mat_to_euler(np.stack([FWD, UP, ZZ], 1) @ A.euler_xyz_matrix(r))


def E(bone, region, c, radii, rot=(0, 0, 0), blend=0.1, **kw):
    return dict(bone=bone, kind="ellipsoid", center=[float(v) for v in c], radii=[float(v) for v in radii],
                rotation=[float(v) for v in rot], blend=float(blend), region=region, **kw)


def RC(bone, region, a, b, ra, rb, blend=0.05, **kw):
    return dict(bone=bone, kind="roundCone", a=[float(v) for v in a], b=[float(v) for v in b], ra=float(ra), rb=float(rb),
                blend=float(blend), region=region, **kw)


def CAP(bone, region, a, b, r, blend=0.02, **kw):
    return RC(bone, region, a, b, r, r, blend, **kw)


def mirror_shape(s):
    s = copy.deepcopy(s)
    if s["kind"] == "ellipsoid":
        s["center"][2] *= -1
        r = s.get("rotation", [0, 0, 0])
        s["rotation"] = [-r[0], -r[1], r[2]]
    elif s["kind"] in ("roundCone", "capsule"):
        s["a"][2] *= -1
        s["b"][2] *= -1
    elif s["kind"] == "loft":
        s["origin"][2] *= -1
        r = s.get("rotation", [0, 0, 0])
        s["rotation"] = [-r[0], -r[1], r[2]]
    if s["bone"].endswith("_r"):
        s["bone"] = s["bone"][:-2] + "_l"
    elif s["bone"].endswith("_l"):
        s["bone"] = s["bone"][:-2] + "_r"
    return s


# ---------------------------------------------------------------------------------------------
def head_shapes():
    """Upper head (bone 'head') and lower jaw (bone 'jaw') as lofts + muscles. Values in head frame.

    Dorsal outline v_top(u), lip line v_lip(u) and half-width hw(u) are 'plausible' soft-tissue
    outlines over an adult skull of ~1.45 m (Sue/Stan-like proportions), with the widest point
    of each section low (cheeks / upper lip) so the snout is an arch, not a box.
    """
    S = []
    # u, v_top, v_lip, hw, (cy fraction above the lip), n_top, n_bot
    up = [
        (-0.16, 0.66, 0.26, 0.33, 0.40, 2.0, 2.6),
        (-0.04, 0.78, 0.22, 0.42, 0.36, 2.1, 2.8),
        (0.12, 0.83, 0.20, 0.455, 0.33, 2.2, 3.0),
        (0.28, 0.83, 0.185, 0.44, 0.30, 2.2, 3.0),
        (0.44, 0.795, 0.165, 0.385, 0.27, 2.15, 3.0),
        (0.60, 0.745, 0.145, 0.315, 0.24, 2.05, 3.0),
        (0.76, 0.685, 0.13, 0.262, 0.22, 2.0, 3.0),
        (0.92, 0.625, 0.122, 0.228, 0.21, 1.95, 3.0),
        (1.08, 0.565, 0.125, 0.205, 0.21, 1.95, 3.0),
        (1.22, 0.505, 0.14, 0.188, 0.22, 1.95, 3.0),
        (1.34, 0.445, 0.165, 0.168, 0.24, 2.0, 3.0),
        (1.43, 0.385, 0.19, 0.135, 0.28, 2.0, 2.8),
        (1.49, 0.33, 0.215, 0.085, 0.35, 2.0, 2.5),
    ]
    secs = []
    for u, vt, vl, hw, fr, nt, nb in up:
        cy = vl + fr * (vt - vl)
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vl, hw=hw, nt=nt, nb=nb))
    S.append(dict(bone="head", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, blend=0.05,
                  region="head", tag="cranium"))
    # snout tip: rounds the premaxilla (narrow U in dorsal view)
    S.append(E("head", "head", H(1.455, 0.285), [0.06, 0.1, 0.105], HR((0, 0, 0.15)), 0.07, tag="premaxilla"))
    # lower jaw: dentary + surangular/angular + intermandibular soft tissue
    lo = [
        (-0.20, 0.12, -0.10, 0.30, 2.0),
        (-0.06, 0.20, -0.21, 0.40, 2.1),
        (0.12, 0.195, -0.27, 0.415, 2.2),
        (0.30, 0.18, -0.265, 0.385, 2.2),
        (0.48, 0.152, -0.225, 0.325, 2.2),
        (0.66, 0.128, -0.17, 0.27, 2.2),
        (0.84, 0.112, -0.115, 0.226, 2.2),
        (1.02, 0.112, -0.065, 0.196, 2.25),
        (1.18, 0.125, -0.02, 0.172, 2.3),
        (1.31, 0.152, 0.03, 0.148, 2.3),
        (1.41, 0.18, 0.085, 0.11, 2.3),
        (1.46, 0.195, 0.125, 0.06, 2.2),
    ]
    secs = []
    for u, vt, vb, hw, nb in lo:
        cy = vt - 0.055
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vb, hw=hw, nt=3.2, nb=nb))
    S.append(dict(bone="jaw", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, blend=0.035,
                  region="jaw", tag="mandible"))
    S.append(E("jaw", "jaw", H(1.41, 0.12), [0.08, 0.07, 0.11], HR(), 0.05, tag="chin"))
    for sg in (1, -1):
        m = lambda c: [c[0], c[1], c[2] * sg]
        w = lambda c: H(c[0], c[1], c[2] * sg)
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        # jaw-closing muscles bulging over the temporal region (M. adductor mandibulae externus)
        S.append(E("head", "head", w((0.2, 0.62, 0.25)), [0.3, 0.19, 0.17], mr(HR((0.25, 0.15, -0.05))), 0.12, tag="adductor"))
        # cheek: jugal + quadratojugal flare (broad back of the head in dorsal view)
        S.append(E("head", "head", w((0.38, 0.34, 0.31)), [0.32, 0.13, 0.1], mr(HR((0, 0.3, 0.04))), 0.1, tag="jugal"))
        S.append(E("head", "head", w((0.06, 0.34, 0.39)), [0.17, 0.19, 0.1], mr(HR((0, 0.25, 0))), 0.1, tag="quadratojugal"))
        # jowls: M. pterygoideus ventralis wrapping under the back of the lower jaw
        S.append(E("jaw", "jaw", w((0.05, -0.13, 0.3)), [0.22, 0.19, 0.14], mr(HR((0, 0.2, 0.15))), 0.12, tag="pterygoideus",
                   masks=["jowl"]))
        # surangular shelf above the jaw angle
        S.append(E("jaw", "jaw", w((0.36, 0.0, 0.33)), [0.3, 0.13, 0.08], mr(HR((0, 0.33, 0.08))), 0.08, tag="surangular"))
    # throat / gular skin between the rami, blending into the neck
    S.append(E("jaw", "jaw", H(0.32, -0.19), [0.5, 0.13, 0.25], HR((0, 0, 0.06)), 0.14, tag="throat", masks=["throat"]))
    return S


def head_details(cast, base_shapes):
    """Eyes, lids, brow, nostrils, lip line, rugosities: placed on the surface by ray casting."""
    S = []
    eyes = []
    teeth = []
    rng = np.random.default_rng(1905)
    for sg in (1, -1):
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        side = "r" if sg > 0 else "l"
        # ---------------- eye --------------------------------------------------------------------
        # Orbit high on the skull, eye looking forward-outward (binocular field ~55 deg, Stevens 2006).
        eye_dir = HD(np.array([0.42, 0.06, 0.905 * sg]))
        eye_dir /= np.linalg.norm(eye_dir)
        surf = cast.cast_at(H(0.575, 0.585, 0.24 * sg), -eye_dir, back=0.8)
        n = cast.normal(surf)
        R_EYE = 0.052
        # eyeball centre: front of the cornea sits ~1 cm below the original surface
        ec = surf - eye_dir * (R_EYE - 0.012)
        eyes.append(dict(side=side, center=list(ec), radius=R_EYE, direction=list(eye_dir), bone=f"eye_{side}"))
        # lid frame: x along the fissure (forward, slightly up), z = eye direction
        fwd = HD(np.array([1.0, 0.1, 0.0]))
        bz = eye_dir
        by = np.cross(bz, fwd)
        by /= np.linalg.norm(by)
        if by[1] < 0:
            by = -by
        bx = np.cross(by, bz)
        lid_rot = basis_to_euler(bx, by, bz)
        # puffy lid ring around the eye (adds), then the palpebral opening (subtract) and the eye pit
        S.append(E("head", "head", ec + bz * 0.012, [0.105, 0.078, 0.055], lid_rot, 0.035, masks=["lid"], tag="lid_ring"))
        S.append(E("head", "head", ec + by * 0.03 + bz * 0.006, [0.085, 0.035, 0.05], lid_rot, 0.025, masks=["lid"], tag="upper_lid"))
        S.append(E("head", "head", ec - by * 0.034 + bz * 0.002, [0.08, 0.028, 0.045], lid_rot, 0.025, masks=["lid"], tag="lower_lid"))
        # opening: almond 2 x 7.2 cm x 2 x 3.4 cm, deep enough to expose the eyeball
        S.append(E("head", "head", ec + bz * 0.05, [0.064, 0.03, 0.075], lid_rot, 0.012, sign=-1, tag="palpebral_opening"))
        S.append(E("head", "head", ec, [R_EYE + 0.006] * 3, (0, 0, 0), 0.006, sign=-1, tag="eye_pit"))
        # crease under the lower lid and above the upper lid
        S.append(E("head", "head", ec - by * 0.078 + bz * 0.03, [0.075, 0.012, 0.03], lid_rot, 0.02, sign=-1, tag="lid_crease"))
        # ---------------- brow: lacrimal boss in front, postorbital boss above-behind -------------
        po = cast.cast_at(H(0.47, 0.76, 0.2 * sg), HD(np.array([-0.15, -0.75, -0.65 * sg])))
        la = cast.cast_at(H(0.74, 0.72, 0.14 * sg), HD(np.array([0.0, -0.85, -0.5 * sg])))
        npo, nla = cast.normal(po), cast.normal(la)
        S.append(E("head", "head", po - npo * 0.035, [0.13, 0.07, 0.1], mr(HR((0, 0.35, 0.12))), 0.05, masks=["rugose", "brow"], tag="postorbital_boss"))
        S.append(E("head", "head", la - nla * 0.03, [0.12, 0.05, 0.065], mr(HR((0, 0.25, -0.15))), 0.05, masks=["rugose", "brow"], tag="lacrimal_boss"))
        # brow shelf over the eye (between the bosses), slightly lateral so it overhangs the lids
        b0 = la - nla * 0.035 + np.array([0, 0, 0.02 * sg])
        b1 = po - npo * 0.04 + np.array([0, 0, 0.025 * sg])
        S.append(CAP("head", "head", b0, b1, 0.048, 0.05, masks=["brow"], tag="brow_shelf"))
        # jugal 'horn' (cornual process) below-behind the orbit, and the cheekbone ridge
        jh = cast.cast_at(H(0.36, 0.3, 0.3 * sg), np.array([0, 0, -sg]))
        S.append(E("head", "head", jh - cast.normal(jh) * 0.03, [0.09, 0.05, 0.06], mr(HR((0, 0.3, 0))), 0.04, masks=["rugose"], tag="jugal_horn"))
        j0 = cast.cast_at(H(0.7, 0.36, 0.25 * sg), np.array([0, 0, -sg]))
        S.append(CAP("head", "head", j0 - cast.normal(j0) * 0.035, jh - cast.normal(jh) * 0.04, 0.04, 0.05, tag="cheek_ridge"))
        # ---------------- nostril: external naris high at the front of the snout -----------------
        na = cast.cast_at(H(1.355, 0.395, 0.1 * sg), HD(np.array([0.45, 0.15, -0.88 * sg])))
        nn = cast.normal(na)
        nf = HD(np.array([1.0, -0.15, 0.0]))
        ny = np.cross(nn, nf)
        ny /= np.linalg.norm(ny)
        if ny[1] < 0:
            ny = -ny
        nx = np.cross(ny, nn)
        nrot = basis_to_euler(nx, ny, nn)
        S.append(E("head", "head", na - nn * 0.016, [0.075, 0.045, 0.035], nrot, 0.035, masks=["nostril"], tag="narial_swelling"))
        S.append(E("head", "head", na + nn * 0.012 + nx * 0.008, [0.045, 0.019, 0.032], nrot, 0.012, sign=-1, masks=["nostril"], tag="naris"))
        # ---------------- lip line: groove where the upper lip overhangs the lower one ----------
        lip = [(1.47, 0.205), (1.4, 0.19), (1.3, 0.165), (1.16, 0.142), (1.0, 0.128), (0.84, 0.124), (0.68, 0.132), (0.54, 0.148), (0.44, 0.17), (0.37, 0.195)]
        hits = []
        for u, v in lip:
            o = H(u, v, 0.8 * sg)
            hits.append(cast.cast(o, np.array([0, 0, -sg])))
        for k in range(len(hits) - 1):
            a, b = hits[k], hits[k + 1]
            r = 0.012 if k < len(hits) - 3 else 0.009
            na_, nb_ = cast.normal(a), cast.normal(b)
            S.append(CAP("head", "head", a + na_ * (r - 0.007), b + nb_ * (r - 0.007), r, 0.01, sign=-1, masks=["lip"], tag="lip_groove"))
        # rictus: soft fold at the corner of the mouth
        rc = hits[-1]
        S.append(E("jaw", "jaw", rc - cast.normal(rc) * 0.03 + HD(np.array([-0.04, -0.02, 0])), [0.07, 0.05, 0.04], mr(HR((0, 0.3, 0.4))), 0.04, tag="rictal_fold"))
        # labial bulge along the upper lip (supralabial scale row sits here)
        for k in range(1, len(hits) - 2):
            p = hits[k]
            S.append(E("head", "head", p - cast.normal(p) * 0.02 + UP * 0.03, [0.07, 0.03, 0.03], mr(HR((0, 0.1 * sg, 0))), 0.03, masks=["lip"], tag="supralabial"))
        # ---------------- rugose patches: nasal ridge, around the orbit, jugal --------------------
        for k in range(10):
            u = 0.82 + 0.6 * rng.random()
            p = cast.cast_at(H(u, 0.75, (0.025 + 0.085 * rng.random()) * sg), HD(np.array([0, -1, 0])))
            r = 0.016 + 0.012 * rng.random()
            S.append(E("head", "head", p - cast.normal(p) * 0.62 * r, [r * 1.3, r, r * 1.1], (0, 0, 0), 0.016, masks=["rugose"], tag="nasal_rugosity"))
        for k in range(7):
            a = math.pi * (0.05 + 1.1 * rng.random())
            p = cast.cast_at(ec + HD(np.array([math.cos(a), math.sin(a), 0])) * 0.15 + eye_dir * 0.1, -eye_dir, back=0.5)
            r = 0.013 + 0.008 * rng.random()
            S.append(E("head", "head", p - cast.normal(p) * 0.6 * r, [r * 1.2, r, r * 1.1], (0, 0, 0), 0.014, masks=["rugose"], tag="orbital_rugosity"))
        # ---------------- teeth (hidden by the lips at rest, Cullen et al. 2023) -------------------
        # upper: 4 premaxillary + 11 maxillary; lower: 13 dentary (counts within the range reported for
        # adult T. rex; exact count varies by specimen).  Only used when the mouth opens.
        for j in range(15):
            t = j / 14.0
            u = 1.40 - 1.0 * t
            v_lip = np.interp(u, [x[0] for x in lip[::-1]], [x[1] for x in lip[::-1]])
            length = 0.06 + 0.07 * math.sin(math.pi * min(1.0, 0.15 + 0.9 * t)) if j >= 4 else 0.055
            hwj = np.interp(u, [-0.04, 0.44, 0.76, 1.08, 1.34, 1.49], [0.36, 0.33, 0.22, 0.18, 0.15, 0.06]) - 0.035
            root = H(u, v_lip + 0.06, hwj * sg)
            tip = H(u - 0.01, v_lip + 0.06 - length, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="upper", side=side, root=list(root), tip=list(tip), radius=0.018 + 0.012 * (length / 0.13)))
        for j in range(13):
            t = j / 12.0
            u = 1.36 - 0.95 * t
            v_lip = np.interp(u, [x[0] for x in lip[::-1]], [x[1] for x in lip[::-1]]) - 0.02
            length = 0.05 + 0.05 * math.sin(math.pi * min(1.0, 0.2 + 0.8 * t))
            hwj = np.interp(u, [-0.06, 0.48, 0.84, 1.18, 1.41], [0.36, 0.29, 0.19, 0.14, 0.08]) - 0.04
            root = H(u, v_lip - 0.07, hwj * sg)
            tip = H(u + 0.01, v_lip - 0.07 + length, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="lower", side=side, root=list(root), tip=list(tip), radius=0.015 + 0.01 * (length / 0.1)))
    return S, eyes, teeth


# ---------------------------------------------------------------------------------------------
def classify_js_masks(s):
    """Masks for the detail stage on the re-used JS shapes."""
    m = []
    b, k, reg = s["bone"], s["kind"], s.get("region")
    if s.get("feature") == "claw":
        return ["claw"]
    if b.startswith("toes"):
        if k == "ellipsoid":
            cy = s["center"][1]
            rr = s["radii"]
            m.append("pads" if (cy < 0.1 and rr[1] < 0.75 * rr[2]) or rr[0] > 0.15 else "scutes_toe")
        else:
            m.append("scutes_toe")
    if b.startswith("meta"):
        if k == "ellipsoid" and s["radii"][0] < 0.08:
            m.append("scutes_meta")
        elif k == "roundCone" and s.get("ra", 0) > 0.08:
            m.append("scutes_meta")
        elif k == "ellipsoid":
            m.append("joint_folds")
    if b.startswith("shin") and k == "ellipsoid" and abs(s["radii"][0] - s["radii"][1]) < 0.03:
        m.append("joint_folds")      # knee
    if s.get("folds") or reg == "neck":
        m.append("neck_folds")
    if reg == "arm":
        m.append("arm")
    return m


def build(js):
    jb = {b["name"]: b for b in js["bones"]}
    old_head = {n: np.array(b["head"], float) for n, b in jb.items()}

    # ----- new skeleton (heads), JS names first --------------------------------------------------
    new_head = {}
    for n, p in old_head.items():
        if n.startswith(("thigh", "shin", "meta", "toes")):
            continue
        new_head[n] = p + [0, DY, 0] if n != "root" else p.copy()
    legs = {}
    for sfx, sg in (("L", -1), ("R", 1)):
        hip, knee, ankle, mtp = leg_chain()
        f = lambda p: np.array([p[0], p[1], p[2] * sg])
        legs[sfx] = (f(hip), f(knee), f(ankle), f(mtp))
        new_head[f"thigh{sfx}"], new_head[f"shin{sfx}"], new_head[f"meta{sfx}"], new_head[f"toes{sfx}"] = legs[sfx]
    new_head["skull"] = H(0.04, 0.3)
    new_head["jaw"] = JAW.copy()

    # ----- shapes: re-used JS body, re-posed -----------------------------------------------------
    def bone_xform(n):
        """(R, h0, h1) mapping a JS bone's shapes to the new pose."""
        if n.startswith(("thigh", "shin", "meta")):
            child = {"thigh": "shin", "shin": "meta", "meta": "toes"}[n[:-1] if n[-1] in "LR" else n]
            child = child + n[-1]
            d0 = old_head[child] - old_head[n]
            d1 = new_head[child] - new_head[n]
            return rot_between(d0, d1), old_head[n], new_head[n]
        if n.startswith("toes"):
            return np.eye(3), old_head[n], new_head[n]
        return np.eye(3), np.zeros(3), np.array([0, DY, 0.0])

    shapes = []
    for s in js["shapes"]:
        if s["bone"] in ("skull", "jaw"):
            continue
        s = {k: v for k, v in s.items() if k not in ("id", "folds")}
        masks = classify_js_masks(s)
        R, h0, h1 = bone_xform(s["bone"])
        s = transform_shape(s, R, h0, h1)
        b = s["bone"]
        # musculature: thicker thighs and shanks (plausible soft tissue; T. rex hindlimb muscles were
        # massive, Hutchinson et al. 2011; Persons & Currie 2011 for the caudofemoralis)
        if b.startswith("thigh"):
            s = scale_shape(s, 1.13)
        elif b.startswith("shin"):
            s = scale_shape(s, 1.08)
        elif b.startswith("meta") and s.get("feature") != "claw":
            s = scale_shape(s, 1.05)
        s["bone"] = JS_TO_NEW[b]
        if masks:
            s["masks"] = masks
        shapes.append(s)

    # thigh: extra lateral bulge of M. iliotibialis / femorotibialis so the drumstick stands proud
    for sfx, sg in (("r", 1), ("l", -1)):
        hip, knee, ankle, mtp = legs["R" if sg > 0 else "L"]
        mid = 0.55 * hip + 0.45 * knee
        shapes.append(E(f"thigh_{sfx}", "thigh", mid + [0.06, 0.05, 0.1 * sg], [0.5, 0.78, 0.26], (0, 0, 0.38), 0.14, tag="thigh_lateral"))
        # M. caudofemoralis longus insertion region behind the thigh
        shapes.append(E(f"thigh_{sfx}", "thigh", hip + [-0.32, -0.42, 0.04 * sg], [0.42, 0.42, 0.25], (0, 0, 0.5), 0.15, tag="thigh_caudal"))
        # gastrocnemius belly
        shapes.append(E(f"shin_{sfx}", "shin", 0.7 * knee + 0.3 * ankle + [-0.12, 0.0, 0.0], [0.2, 0.4, 0.19], (0, 0, -0.28), 0.1, tag="gastrocnemius"))

    # ----- the head -------------------------------------------------------------------------------
    hs = head_shapes()
    base = shapes + hs
    bone_names = sorted({s["bone"] for s in base})
    cast = Caster(base, bone_names)
    det, eyes, teeth = head_details(cast, base)
    shapes = base + det

    # ----- bones (final names, heads + tails) ----------------------------------------------------
    nh = {JS_TO_NEW[n]: p for n, p in new_head.items()}
    parents = {JS_TO_NEW[b["name"]]: (JS_TO_NEW[b["parent"]] if b["parent"] else None) for b in js["bones"]}
    tails = {}
    children = {}
    for n, p in parents.items():
        if p:
            children.setdefault(p, []).append(n)
    chain_child = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03", "spine_03": "neck_01",
                   "neck_01": "neck_02", "neck_02": "neck_03", "neck_03": "head", **{f"tail_{i:02d}": f"tail_{i + 1:02d}" for i in range(1, 10)}}
    for s in "lr":
        chain_child.update({f"thigh_{s}": f"shin_{s}", f"shin_{s}": f"metatarsus_{s}", f"metatarsus_{s}": f"toes_{s}",
                            f"upperarm_{s}": f"forearm_{s}", f"forearm_{s}": f"hand_{s}"})
    for n in nh:
        if n in chain_child:
            tails[n] = nh[chain_child[n]]
    tails["root"] = nh["root"] + [0.6, 0, 0]
    tails["head"] = H(0.95, 0.42)
    tails["jaw"] = H(1.25, 0.02)
    tail_tip = np.array([-7.05, nh["tail_10"][1] - 0.18, 0])
    tails["tail_10"] = tail_tip
    for s, sg in (("l", -1), ("r", 1)):
        tails[f"toes_{s}"] = nh[f"toes_{s}"] + [0.42, -0.08, 0.0]
        tails[f"hand_{s}"] = nh[f"hand_{s}"] + [0.12, -0.12, 0.0]
    bones = []
    order = ["root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "neck_02", "neck_03", "head", "jaw"] + \
            [f"tail_{i:02d}" for i in range(1, 11)] + \
            [f"{b}_{s}" for s in "lr" for b in ("thigh", "shin", "metatarsus", "toes")] + \
            [f"{b}_{s}" for s in "lr" for b in ("upperarm", "forearm", "hand")]
    inv = {v: k for k, v in JS_TO_NEW.items()}
    for n in order:
        bones.append(dict(name=n, parent=parents[n], head=list(nh[n]), tail=list(tails[n]), deform=True, js_name=inv[n]))
    for e in eyes:
        bones.append(dict(name=e["bone"], parent="head", head=e["center"], tail=list(np.array(e["center"]) + np.array(e["direction"]) * 0.08),
                          deform=True, js_name=None))
    return bones, shapes, eyes, teeth


def claws_from_shapes(shapes):
    """Claw centre-lines (for the separate low-poly claw meshes) from consecutive claw round cones."""
    out = []
    cl = [s for s in shapes if s.get("feature") == "claw"]
    i = 0
    while i < len(cl):
        s = cl[i]
        pts = [s["a"], s["b"]]
        radii = [s["ra"], s["rb"]]
        if i + 1 < len(cl) and np.allclose(cl[i + 1]["a"], s["b"]) and cl[i + 1]["bone"] == s["bone"]:
            pts.append(cl[i + 1]["b"])
            radii.append(cl[i + 1]["rb"])
            i += 1
        out.append(dict(bone=s["bone"], points=[list(map(float, p)) for p in pts], radii=[float(r) for r in radii]))
        i += 1
    return out


def make_doc(js_path):
    with open(js_path) as f:
        js = json.load(f)
    bones, shapes, eyes, teeth = build(js)
    for i, s in enumerate(shapes):
        s["id"] = i
    claws = claws_from_shapes(shapes)
    doc = {
        "schema": "noctis.anatomy/1",
        "species_id": SPECIES,
        "derived_from": {
            "file": "Tools/Preview/src/creature/trex_anatomy.js",
            "via": ["Tools/CreatureFactory/export_anatomy.mjs", "Tools/CreatureFactory/species/tyrannosaurus_rex.py"],
            "changes": [
                "skull rebuilt from lofted superellipse sections (snout longer and tapering, keyhole dorsal outline, convex nasal profile)",
                "eye: separate eyeball flush with puffy lids, brow shelf (lacrimal + postorbital bosses) overhanging it",
                "legs re-posed to acetabulum 3.1 m with unchanged bone lengths; thigh x1.13, shank x1.08 soft tissue",
                "body raised 0.10 m with the hips",
            ],
        },
        "axes": "anatomy space: metres, +x forward (snout), +y up, +z animal right, ground y = 0",
        "targets": {
            "total_length_m": 12.0, "hip_height_m": HIP_Y, "adult_mass_kg": 7000,
            "skull_length_m": 1.47, "femur_m": FEMUR, "tibia_m": TIBIA, "metatarsus_m": META,
            "sources": ["hutchinson2011_trex_mass", "persons2019_trex_scotty", "brochu2003_trex_osteology", "carr2020_trex_growth"],
            "note": "longueurs osseuses reprises de trex_anatomy.js (proportions d'un adulte de la taille de Sue) ; volumes de tissus mous = reconstruction plausible",
        },
        "bones": bones,
        "shapes": shapes,
        "eyes": eyes,
        "claws": claws,
        "teeth": teeth,
    }
    return doc
