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
        (-0.10, 0.70, 0.30, 0.30, 0.45, 2.0, 2.5),
        (0.02, 0.80, 0.24, 0.40, 0.38, 2.1, 2.8),
        (0.16, 0.84, 0.205, 0.44, 0.34, 2.2, 3.0),
        (0.30, 0.845, 0.185, 0.43, 0.31, 2.2, 3.0),
        (0.45, 0.82, 0.165, 0.38, 0.28, 2.15, 3.0),
        (0.60, 0.78, 0.15, 0.31, 0.26, 2.05, 3.0),
        (0.76, 0.735, 0.14, 0.26, 0.25, 2.0, 3.0),
        (0.92, 0.69, 0.135, 0.23, 0.25, 2.0, 3.0),
        (1.08, 0.645, 0.14, 0.212, 0.26, 2.0, 3.0),
        (1.22, 0.60, 0.15, 0.198, 0.27, 2.05, 3.0),
        (1.34, 0.545, 0.168, 0.18, 0.29, 2.1, 3.0),
        (1.43, 0.48, 0.19, 0.152, 0.32, 2.15, 2.8),
        (1.49, 0.40, 0.215, 0.10, 0.38, 2.2, 2.5),
    ]
    secs = []
    for u, vt, vl, hw, fr, nt, nb in up:
        cy = vl + fr * (vt - vl)
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vl, hw=hw, nt=nt, nb=nb))
    S.append(dict(bone="head", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, blend=0.05,
                  region="head", tag="cranium"))
    # snout tip: rounds the premaxilla (narrow U in dorsal view)
    S.append(E("head", "head", H(1.45, 0.33), [0.065, 0.14, 0.115], HR((0, 0, 0.12)), 0.08, tag="premaxilla"))
    # lower jaw: dentary + surangular/angular + intermandibular soft tissue
    lo = [
        (-0.20, 0.13, -0.10, 0.30, 2.0),
        (-0.06, 0.21, -0.22, 0.40, 2.1),
        (0.12, 0.205, -0.28, 0.41, 2.2),
        (0.30, 0.19, -0.28, 0.38, 2.2),
        (0.48, 0.17, -0.25, 0.325, 2.2),
        (0.66, 0.16, -0.21, 0.27, 2.2),
        (0.84, 0.155, -0.165, 0.225, 2.2),
        (1.02, 0.158, -0.12, 0.197, 2.25),
        (1.18, 0.168, -0.075, 0.175, 2.3),
        (1.31, 0.185, -0.025, 0.152, 2.3),
        (1.41, 0.205, 0.035, 0.117, 2.3),
        (1.47, 0.22, 0.09, 0.072, 2.2),
    ]
    secs = []
    for u, vt, vb, hw, nb in lo:
        cy = vt - 0.055
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vb, hw=hw, nt=3.2, nb=nb))
    S.append(dict(bone="jaw", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, blend=0.015,
                  region="jaw", tag="mandible"))
    S.append(E("jaw", "jaw", H(1.41, 0.12), [0.08, 0.07, 0.11], HR(), 0.05, tag="chin"))
    for sg in (1, -1):
        m = lambda c: [c[0], c[1], c[2] * sg]
        w = lambda c: H(c[0], c[1], c[2] * sg)
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        # jaw-closing muscles bulging over the temporal region (M. adductor mandibulae externus)
        S.append(E("head", "head", w((0.22, 0.64, 0.22)), [0.28, 0.17, 0.15], mr(HR((0.25, 0.15, -0.05))), 0.12, tag="adductor"))
        # cheek: jugal + quadratojugal flare (broad back of the head in dorsal view)
        S.append(E("head", "head", w((0.38, 0.34, 0.31)), [0.32, 0.13, 0.1], mr(HR((0, 0.3, 0.04))), 0.1, tag="jugal"))
        S.append(E("head", "head", w((0.1, 0.36, 0.34)), [0.15, 0.16, 0.09], mr(HR((0, 0.25, 0))), 0.1, tag="quadratojugal"))
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
        eye_dir = HD(np.array([0.42, 0.04, 0.905 * sg]))
        eye_dir /= np.linalg.norm(eye_dir)
        surf = cast.cast_at(H(0.575, 0.6, 0.24 * sg), -eye_dir, back=0.8)
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
        # ---------------- brow: lacrimal boss in front, postorbital boss above-behind -------------
        po = cast.cast_at(H(0.47, 0.76, 0.2 * sg), HD(np.array([-0.15, -0.75, -0.65 * sg])))
        la = cast.cast_at(H(0.74, 0.72, 0.14 * sg), HD(np.array([0.0, -0.85, -0.5 * sg])))
        npo, nla = cast.normal(po), cast.normal(la)
        S.append(E("head", "head", po - npo * 0.035, [0.13, 0.07, 0.1], mr(HR((0, 0.35, 0.12))), 0.05, masks=["rugose", "brow"], tag="postorbital_boss"))
        S.append(E("head", "head", la - nla * 0.03, [0.12, 0.05, 0.065], mr(HR((0, 0.25, -0.15))), 0.05, masks=["rugose", "brow"], tag="lacrimal_boss"))
        # brow shelf over the eye (between the bosses), slightly lateral so it overhangs the lids
        b0 = la - nla * 0.03 + np.array([0, 0, 0.03 * sg])
        b1 = po - npo * 0.035 + np.array([0, 0, 0.035 * sg])
        S.append(CAP("head", "head", b0, b1, 0.055, 0.05, masks=["brow"], tag="brow_shelf"))
        # brow overhang right above the upper lid
        S.append(E("head", "head", ec + by * 0.085 + bz * 0.035 + bx * 0.01, [0.1, 0.035, 0.05], lid_rot, 0.04, masks=["brow"], tag="brow_overhang"))
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


# Vertebral centre-line (x, y) from the tail tip to the occipital condyle, and the soft-tissue
# section around it: skin height above the centra (top), depth below (bot), half-width (hw) at the
# widest level (fraction fw of the depth from the top). Plausible values over a Sue-sized skeleton:
# deep tail base (M. caudofemoralis, Persons & Currie 2011), deep ribcage with the gastral line from
# the coracoids to the pubic boot, short thick S-shaped neck.
BODY_KEYS = [
    # x,     y,    top,   bot,   hw,    fw,  nt,  nb
    (-7.05, 2.78, 0.012, 0.016, 0.012, 0.40, 2.0, 2.0),
    (-6.40, 2.87, 0.050, 0.072, 0.046, 0.38, 2.0, 1.9),
    (-5.60, 2.97, 0.100, 0.150, 0.092, 0.36, 2.0, 1.9),
    (-4.70, 3.08, 0.155, 0.250, 0.148, 0.34, 2.05, 1.9),
    (-3.80, 3.18, 0.210, 0.360, 0.210, 0.33, 2.1, 1.9),
    (-2.90, 3.28, 0.260, 0.480, 0.275, 0.33, 2.1, 1.9),
    (-2.00, 3.37, 0.310, 0.610, 0.345, 0.34, 2.15, 1.95),
    (-1.20, 3.44, 0.350, 0.740, 0.410, 0.36, 2.2, 2.0),
    (-0.50, 3.49, 0.370, 0.870, 0.460, 0.40, 2.3, 2.0),
    (0.20, 3.51, 0.370, 1.060, 0.485, 0.42, 2.3, 2.0),
    (0.90, 3.48, 0.385, 1.360, 0.505, 0.45, 2.3, 2.0),
    (1.60, 3.40, 0.405, 1.370, 0.515, 0.45, 2.3, 2.0),
    (2.20, 3.28, 0.420, 1.120, 0.475, 0.45, 2.3, 2.0),
    (2.62, 3.21, 0.430, 0.900, 0.430, 0.45, 2.25, 2.0),
    (2.96, 3.34, 0.420, 0.720, 0.385, 0.45, 2.2, 2.0),
    (3.26, 3.57, 0.380, 0.580, 0.345, 0.45, 2.2, 2.0),
    (3.50, 3.82, 0.300, 0.470, 0.305, 0.45, 2.1, 2.0),
    (3.64, 3.93, 0.220, 0.360, 0.255, 0.45, 2.0, 2.0),
]
TAIL_X0, TAIL_TIP = -0.85, -7.05


def body_y(x):
    xs = [k[0] for k in BODY_KEYS]
    ys = [k[1] for k in BODY_KEYS]
    from scipy.interpolate import PchipInterpolator
    return float(PchipInterpolator(xs, ys)(x))


def make_bones(js):
    """Final skeleton (names, heads, tails) for the re-posed animal."""
    nh = {}
    nh["root"] = np.zeros(3)
    nh["pelvis"] = np.array([0.0, HIP_Y, 0.0])
    for n, x in (("spine_01", 0.70), ("spine_02", 1.45), ("spine_03", 2.15), ("neck_01", 2.62), ("neck_02", 2.96), ("neck_03", 3.26)):
        nh[n] = np.array([x, body_y(x), 0.0])
    nh["head"] = H(0.04, 0.3)
    nh["jaw"] = JAW.copy()
    seg = (TAIL_TIP - TAIL_X0) / 10
    for i in range(1, 11):
        x = TAIL_X0 + seg * (i - 1)
        nh[f"tail_{i:02d}"] = np.array([x, body_y(x), 0.0])
    legs = {}
    for s, sg in (("l", -1), ("r", 1)):
        hip, knee, ankle, mtp = [np.array([p[0], p[1], p[2] * sg]) for p in leg_chain()]
        legs[s] = (hip, knee, ankle, mtp)
        nh[f"thigh_{s}"], nh[f"shin_{s}"], nh[f"metatarsus_{s}"], nh[f"toes_{s}"] = hip, knee, ankle, mtp
    jb = {b["name"]: np.array(b["head"], float) for b in js["bones"]}
    for s, S in (("l", "L"), ("r", "R")):
        for a, b in (("upperarm", "upperArm"), ("forearm", "foreArm"), ("hand", "hand")):
            nh[f"{a}_{s}"] = jb[f"{b}{S}"] + [0, DY, 0]
    parents = {"root": None, "pelvis": "root", "spine_01": "pelvis", "spine_02": "spine_01", "spine_03": "spine_02",
               "neck_01": "spine_03", "neck_02": "neck_01", "neck_03": "neck_02", "head": "neck_03", "jaw": "head",
               "tail_01": "pelvis", **{f"tail_{i:02d}": f"tail_{i - 1:02d}" for i in range(2, 11)}}
    for s in "lr":
        parents.update({f"thigh_{s}": "pelvis", f"shin_{s}": f"thigh_{s}", f"metatarsus_{s}": f"shin_{s}",
                        f"toes_{s}": f"metatarsus_{s}", f"upperarm_{s}": "spine_03", f"forearm_{s}": f"upperarm_{s}",
                        f"hand_{s}": f"forearm_{s}"})
    chain_child = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03", "spine_03": "neck_01",
                   "neck_01": "neck_02", "neck_02": "neck_03", "neck_03": "head", **{f"tail_{i:02d}": f"tail_{i + 1:02d}" for i in range(1, 10)}}
    for s in "lr":
        chain_child.update({f"thigh_{s}": f"shin_{s}", f"shin_{s}": f"metatarsus_{s}", f"metatarsus_{s}": f"toes_{s}",
                            f"upperarm_{s}": f"forearm_{s}", f"forearm_{s}": f"hand_{s}"})
    tails = {n: nh[c] for n, c in chain_child.items()}
    tails["root"] = np.array([0.6, 0.0, 0.0])
    tails["head"] = H(0.95, 0.42)
    tails["jaw"] = H(1.25, 0.02)
    tails["tail_10"] = np.array([TAIL_TIP, body_y(TAIL_TIP), 0.0])
    for s in "lr":
        tails[f"toes_{s}"] = nh[f"toes_{s}"] + [0.42, -0.08, 0.0]
        tails[f"hand_{s}"] = nh[f"hand_{s}"] + [0.10, -0.12, 0.0]
    order = ["root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "neck_02", "neck_03", "head", "jaw"] + \
            [f"tail_{i:02d}" for i in range(1, 11)] + \
            [f"{b}_{s}" for s in "lr" for b in ("thigh", "shin", "metatarsus", "toes")] + \
            [f"{b}_{s}" for s in "lr" for b in ("upperarm", "forearm", "hand")]
    inv = {v: k for k, v in JS_TO_NEW.items()}
    bones = [dict(name=n, parent=parents[n], head=list(map(float, nh[n])), tail=list(map(float, tails[n])), deform=True,
                  js_name=inv.get(n)) for n in order]
    return bones, nh, legs


def body_shapes(bones, legs):
    bd = {b["name"]: b for b in bones}
    S = []
    pts = []
    for (x, y, top, bot, hw, fw, nt, nb) in BODY_KEYS:
        c = top - fw * (top + bot)          # widest level relative to the centre-line
        pts.append(dict(p=[x, y, 0.0], cy=c, top=top - c, bot=bot + c, hw=hw, nt=nt, nb=nb))
    chain = [dict(bone=n, head=bd[n]["head"], tail=bd[n]["tail"]) for n in
             [f"tail_{i:02d}" for i in range(10, 0, -1)] + ["spine_01", "spine_02", "spine_03", "neck_01", "neck_02", "neck_03"]]
    # the pelvis bone lies at the acetabulum: give the sacral stretch of the column to it
    chain.append(dict(bone="pelvis", head=[-0.85, body_y(-0.85), 0.0], tail=[0.7, body_y(0.7), 0.0]))
    S.append(dict(bone="pelvis", kind="sweep", region="torso", points=pts, chain=chain, blend=0.05, tag="body",
                  regions=[dict(x_max=-0.95, region="tail"), dict(x_max=2.45, region="torso"), dict(x_max=99, region="neck")]))
    # ----- pelvis: pubis + boot, ischium, hip musculature ---------------------------------------
    S.append(RC("pelvis", "belly", [0.1, 2.85, 0], [0.42, 2.02, 0], 0.25, 0.15, 0.22, scale=[1, 1, 1.25], tag="pubis"))
    S.append(E("pelvis", "belly", [0.44, 1.98, 0], [0.36, 0.105, 0.19], (0, 0, -0.06), 0.2, tag="pubic_boot"))
    S.append(E("pelvis", "torso", [-0.55, 2.82, 0], [0.48, 0.3, 0.3], (0, 0, 0.35), 0.2, tag="ischium"))
    # ventral line: gastralia from the boot to the chest (slightly convex), chest floor
    S.append(E("spine_02", "belly", [1.25, 2.2, 0], [0.95, 0.24, 0.42], (0, 0, 0.03), 0.25, tag="gastral_belly"))
    S.append(E("spine_03", "belly", [2.22, 2.3, 0], [0.42, 0.22, 0.34], (0, 0, 0.15), 0.2, tag="chest_floor"))
    for s, sg in (("r", 1), ("l", -1)):
        m = lambda p: [p[0], p[1], p[2] * sg]
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        hip, knee, ankle, mtp = legs[s]
        # hip musculature over the ilium (M. iliofemoralis region)
        S.append(E("pelvis", "torso", m([-0.05, 3.42, 0.3]), [0.85, 0.3, 0.2], mr([0, 0, 0.02]), 0.16, tag="ilium_muscle"))
        # ribcage flank and shoulder girdle
        S.append(E("spine_02", "torso", m([1.35, 2.8, 0.2]), [0.95, 0.55, 0.3], mr([0, 0, 0.03]), 0.2, tag="ribcage_flank"))
        S.append(E("spine_03", "torso", m([2.12, 3.08, 0.33]), [0.5, 0.42, 0.15], mr([0, 0, -0.55]), 0.16, tag="scapular_muscles"))
        S.append(E("spine_03", "torso", m([2.38, 2.48, 0.25]), [0.3, 0.34, 0.19], mr([0, 0, 0.1]), 0.16, tag="pectoralis"))
        # neck: lateral muscles (folds), nape
        S.append(E("neck_01", "neck", m([2.72, 3.2, 0.18]), [0.4, 0.45, 0.24], mr([0, 0, 0.75]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        S.append(E("neck_02", "neck", m([3.05, 3.45, 0.16]), [0.36, 0.42, 0.22], mr([0, 0, 0.8]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        S.append(E("neck_03", "neck", m([3.32, 3.65, 0.15]), [0.3, 0.38, 0.2], mr([0, 0, 0.7]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        # ---------------- hind limb ----------------------------------------------------------------
        fd = (knee - hip) / np.linalg.norm(knee - hip)
        ang = math.atan2(-fd[0], -fd[1])          # rotation about z bringing local +y onto (hip - knee)
        # thigh: long teardrop along the femur, broadest proximally
        S.append(E(f"thigh_{s}", "thigh", hip + 0.4 * (knee - hip) + [0.02, 0, 0.1 * sg], [0.47, 0.8, 0.25], mr([0, 0, ang]), 0.12, tag="thigh_main"))
        S.append(E(f"thigh_{s}", "thigh", hip + [0.02, 0.1, 0.07 * sg], [0.78, 0.42, 0.24], mr([0, 0, 0.08]), 0.14, tag="thigh_iliotibialis"))
        S.append(RC(f"thigh_{s}", "thigh", hip + 0.55 * (knee - hip) + [0, 0, 0.06 * sg], knee + [0.0, 0.04, 0.0], 0.31, 0.19, 0.1, tag="thigh_lower"))
        # M. caudofemoralis longus: tail base -> 4th trochanter, bulging under the tail
        S.append(RC(f"thigh_{s}", "thigh", m([-1.9, 3.0, 0.2]), hip + 0.35 * (knee - hip) + [-0.22, 0, -0.03 * sg], 0.27, 0.27, 0.22, tag="caudofemoralis"))
        S.append(E(f"shin_{s}", "shin", knee + [0.04, -0.02, 0], [0.2, 0.2, 0.19], (0, 0, 0), 0.09, masks=["joint_folds"], tag="knee"))
        sd = (ankle - knee) / np.linalg.norm(ankle - knee)
        post = np.array([sd[1], -sd[0], 0.0]) * (-1 if sd[1] < 0 else 1)
        post = post if post[0] < 0 else -post
        sang = math.atan2(-sd[0], -sd[1])
        S.append(RC(f"shin_{s}", "shin", knee + [0.0, -0.06, 0], ankle + [0, 0.03, 0], 0.205, 0.118, 0.08, scale=[1, 1, 0.92], tag="shank"))
        S.append(E(f"shin_{s}", "shin", knee + 0.32 * (ankle - knee) + post * 0.1, [0.21, 0.44, 0.18], mr([0, 0, sang]), 0.09, tag="gastrocnemius"))
        S.append(E(f"shin_{s}", "shin", knee + 0.4 * (ankle - knee) - post * 0.09, [0.11, 0.4, 0.12], mr([0, 0, sang]), 0.07, tag="tibialis"))
    return S


def build(js):
    bones, nh, legs = make_bones(js)
    jb = {b["name"]: np.array(b["head"], float) for b in js["bones"]}
    old_child = {"meta": "toes"}
    shapes = body_shapes(bones, legs)
    # ----- re-used JS volumes: metatarsus + toes (re-posed), arms (raised) -------------------------
    for s0 in js["shapes"]:
        b = s0["bone"]
        if not b.startswith(("meta", "toes", "upperArm", "foreArm", "hand")):
            continue
        s = {k: v for k, v in s0.items() if k not in ("id", "folds")}
        masks = classify_js_masks(s)
        S = b[-1]
        if b.startswith("meta"):
            d0 = jb["toes" + S] - jb["meta" + S]
            d1 = nh["toes_" + S.lower()] - nh["metatarsus_" + S.lower()]
            s = transform_shape(s, rot_between(d0, d1), jb["meta" + S], nh["metatarsus_" + S.lower()])
            if s.get("feature") != "claw":
                s = scale_shape(s, 1.05)
        elif b.startswith("toes"):
            s = transform_shape(s, np.eye(3), jb["toes" + S], nh["toes_" + S.lower()])
        else:
            s = transform_shape(s, np.eye(3), np.zeros(3), np.array([0, DY, 0.0]))
        s["bone"] = JS_TO_NEW[b]
        if masks:
            s["masks"] = masks
        shapes.append(s)
    # ----- head ------------------------------------------------------------------------------------
    shapes += head_shapes()
    bone_names = [b["name"] for b in bones]
    cast = Caster(shapes, bone_names)
    det, eyes, teeth = head_details(cast, shapes)
    shapes += det
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
