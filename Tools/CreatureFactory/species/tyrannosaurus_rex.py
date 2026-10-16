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

JS_SOURCE = "Tools/Preview/src/creature/trex_anatomy.js"

# ---------------------------------------------------------------------------------------------
# Skin detail parameters (read by lib/detail.py). Sizes are DESIGN values chosen for readability at
# game resolution: Bell et al. 2017 only establish small, non-imbricate (pebbly) scales on the neck,
# pelvis and tail of T. rex; exact diameters / patterns used here are not taken from the paper.
SKIN = {
    "status": "basement scales: strongly_supported (bell2017_tyrannosaur_integument); sizes, folds, scutes, pads: design / phylogenetic bracket",
    "scale_layers_m": [0.010, 0.017, 0.027],          # Worley cell size of the fine / medium / large basement-scale layers
    "scale_height_ratio": 0.17,                       # dome height / cell size
    "groove_ratio": 0.06,                             # groove half-width / cell size
    "region_layers": {                                # weights of the three layers per region
        "head": [0.75, 0.25, 0.0], "jaw": [0.85, 0.15, 0.0], "neck": [0.25, 0.6, 0.15], "torso": [0.0, 0.45, 0.55],
        "belly": [0.45, 0.55, 0.0], "tail": [0.1, 0.5, 0.4], "thigh": [0.05, 0.45, 0.5], "shin": [0.3, 0.6, 0.1],
        "foot": [0.7, 0.3, 0.0], "arm": [0.7, 0.3, 0.0]},
    "dorsal_coarsen": 0.35,                           # dorsal surfaces shift weight towards the large layer
    "ventral_refine": 0.45,                           # ventral surfaces shift weight towards the fine layer
    "feature_scales": {"cell_m": 0.075, "density": 0.16, "radius_ratio": 0.42, "height_m": 0.0055,
                       "regions": {"head": 0.6, "neck": 1.0, "torso": 0.7, "tail": 0.6, "thigh": 0.5},
                       "status": "design assumption (not documented for T. rex)"},
    "neck_folds": {"wavelength_m": 0.09, "amplitude_m": 0.013, "status": "design (soft tissue, plausible)"},
    "throat_folds": {"wavelength_m": 0.07, "amplitude_m": 0.008},
    "joint_folds": {"wavelength_m": 0.05, "amplitude_m": 0.007},
    "flank_wrinkles": {"amplitude_m": 0.0025, "scale_m": 0.11},
    "scutes": {"meta_length_m": 0.055, "toe_length_m": 0.042, "height_m": 0.0075,
               "status": "inferred by phylogenetic bracket (scutate scales of birds) - not preserved in T. rex"},
    "pads": {"cell_m": 0.022, "height_m": 0.006},
    "rugose": {"cell_m": 0.03, "height_m": 0.009, "status": "cornified skin over rugose skull bones (nasal, lacrimal, postorbital, jugal): plausible, design"},
    "lip_scales_m": 0.008,
    "lid_scales_m": 0.006,
}

# Colour is unknown for every non-avian dinosaur: muted, countershaded palette, SPÉCULATIF.
PALETTE = {
    "status": "SPÉCULATIF",
    "note": "Couleur inconnue (science.integument : 'Couleur : inconnue'). Palette terne et contre-ombrée choisie pour la lisibilité ; aucun motif tiré d'une référence.",
    "dorsal": [0.062, 0.054, 0.038],       # linear RGB
    "flank": [0.118, 0.098, 0.068],
    "ventral": [0.30, 0.25, 0.175],
    "head_top": [0.05, 0.043, 0.032],
    "lip": [0.21, 0.165, 0.12],
    "mottle_dark": [0.032, 0.028, 0.021],
    "band_dark": [0.036, 0.031, 0.024],
    "scute": [0.045, 0.04, 0.034],
    "pad": [0.1, 0.085, 0.065],
    "rugose": [0.085, 0.07, 0.052],
    "claw": [0.035, 0.03, 0.027],
    "claw_tip": [0.16, 0.14, 0.11],
    "tooth": [0.62, 0.56, 0.45],
    "iris": [0.42, 0.26, 0.07],
    "gum": [0.32, 0.12, 0.10],
}

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
    knee = hip + FEMUR * np.array([math.sin(ANG_FEMUR), -math.cos(ANG_FEMUR), 0]) + [0, 0, -0.02]
    ankle = knee + TIBIA * np.array([-math.sin(ANG_TIBIA), -math.cos(ANG_TIBIA), 0]) + [0, 0, -0.07]
    mtp = ankle + META * np.array([math.sin(ANG_META), -math.cos(ANG_META), 0]) + [0, 0, -0.02]
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
# Skull + soft-tissue outline of the upper head, head frame (u forward from the jaw joint, v up).
# Adult skull ~1.45 m (Sue / Stan-like): broad temporal region (~0.9 m across the jugals and the
# jaw muscles), narrow snout (~0.42 m), dorsal profile highest over the postorbitals and sloping
# down to a tall premaxilla, convex (sinuous) tooth-row margin. Soft tissue = plausible thickness.
#  u,    v_top, v_lip, hw,    fr (widest level), nt,  nb
HEAD_UP = [
    (-0.10, 0.62, 0.25, 0.36, 0.34, 1.9, 2.4),
    (0.00, 0.70, 0.20, 0.435, 0.30, 1.9, 2.7),
    (0.12, 0.74, 0.195, 0.465, 0.28, 1.9, 3.0),
    (0.24, 0.76, 0.195, 0.46, 0.27, 2.0, 3.1),
    (0.36, 0.765, 0.19, 0.425, 0.27, 2.1, 3.2),
    (0.48, 0.75, 0.175, 0.37, 0.28, 2.25, 3.3),
    (0.60, 0.72, 0.16, 0.32, 0.29, 2.4, 3.4),
    (0.75, 0.68, 0.145, 0.282, 0.30, 2.5, 3.5),
    (0.90, 0.635, 0.137, 0.255, 0.31, 2.6, 3.6),
    (1.05, 0.59, 0.135, 0.236, 0.32, 2.6, 3.6),
    (1.20, 0.55, 0.14, 0.222, 0.33, 2.6, 3.5),
    (1.32, 0.51, 0.15, 0.206, 0.34, 2.5, 3.3),
    (1.42, 0.47, 0.165, 0.18, 0.36, 2.4, 3.0),
]
# lower jaw (dentary + surangular/angular + skin): u, v_top, v_bot, hw, nb
HEAD_LO = [
    (-0.16, 0.13, -0.15, 0.34, 2.1),
    (-0.04, 0.17, -0.26, 0.41, 2.2),
    (0.12, 0.18, -0.315, 0.425, 2.3),
    (0.30, 0.17, -0.32, 0.395, 2.3),
    (0.48, 0.155, -0.29, 0.345, 2.3),
    (0.66, 0.145, -0.25, 0.292, 2.3),
    (0.84, 0.137, -0.205, 0.255, 2.3),
    (1.02, 0.135, -0.165, 0.228, 2.35),
    (1.18, 0.14, -0.125, 0.208, 2.4),
    (1.30, 0.15, -0.09, 0.19, 2.4),
    (1.39, 0.162, -0.055, 0.165, 2.4),
]
EYE_U, EYE_V = 0.43, 0.62
R_EYE = 0.055


def _up_interp(u):
    us = [s[0] for s in HEAD_UP]
    return [float(np.interp(u, us, [s[i] for s in HEAD_UP])) for i in range(1, 7)]


def head_shapes():
    """Upper head (bone 'head') and lower jaw (bone 'jaw') as lofts + muscles. Values in head frame.
    Rounded caps close both ends of each loft (occiput, snout tip, chin)."""
    S = []
    secs = []
    for u, vt, vl, hw, fr, nt, nb in HEAD_UP:
        cy = vl + fr * (vt - vl)
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vl, hw=hw, nt=nt, nb=nb))
    S.append(dict(bone="head", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, caps=[0.14, 0.08], blend=0.2,
                  region="head", tag="cranium"))

    def upper_width(u, v):
        vt, vl, hw, fr, nt, nb = _up_interp(u)
        cy = vl + fr * (vt - vl)
        bot = cy - vl
        t = min(1.0, max(0.0, (cy - v) / bot))
        return hw * (1 - t ** nb) ** (1 / nb)

    secs = []
    for u, vt, vb, hw, nb in HEAD_LO:
        vl = _up_interp(u)[1]
        if u > 0.4:
            vt = max(vt, vl + 0.01)
            # the upper lip overlaps the dentary (varanid-like, Cullen et al. 2023): jaw ~2 cm inside it
            hw = min(hw, upper_width(u, vl + 0.03) - 0.012)
        cy = vt - 0.06
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vb, hw=hw, nt=3.4, nb=nb))
    S.append(dict(bone="jaw", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, caps=[0.12, 0.09], blend=0.012,
                  region="jaw", tag="mandible"))
    for sg in (1, -1):
        w = lambda c: H(c[0], c[1], c[2] * sg)
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        # jaw-closing muscles (M. adductor mandibulae externus) bulging out of the supratemporal
        # fenestrae on each side of the sagittal crest
        S.append(E("head", "head", w((0.12, 0.62, 0.2)), [0.25, 0.12, 0.15], mr(HR((0.3, 0.12, -0.06))), 0.12, tag="adductor"))
        # jugal ridge under the orbit (the jugal is deep in tyrannosaurids) and quadratojugal flare
        S.append(E("head", "head", w((0.38, 0.35, 0.29)), [0.32, 0.075, 0.07], mr(HR((0.05, 0.36, 0.06))), 0.12, tag="jugal"))
        S.append(E("head", "head", w((0.08, 0.32, 0.36)), [0.14, 0.13, 0.08], mr(HR((0, 0.22, 0))), 0.1, tag="quadratojugal"))
        # jowls: M. pterygoideus ventralis wrapping the back of the lower jaw
        S.append(E("jaw", "jaw", w((-0.02, -0.1, 0.22)), [0.22, 0.13, 0.09], mr(HR((0, 0.25, 0.25))), 0.25, tag="pterygoideus",
                   masks=["jowl"]))
        # surangular shelf above the jaw angle
        S.append(E("jaw", "jaw", w((0.34, -0.02, 0.31)), [0.27, 0.1, 0.05], mr(HR((0, 0.3, 0.06))), 0.1, tag="surangular"))
        # antorbital region: shallow skin-covered depression in front of the orbit (design)
        S.append(E("head", "head", w((0.8, 0.44, 0.30)), [0.2, 0.075, 0.04], mr(HR((0, 0.3, -0.05))), 0.06, sign=-1, tag="antorbital_fossa"))
    # throat / gular skin between the rami, blending into the neck
    S.append(E("jaw", "jaw", H(0.28, -0.2), [0.5, 0.13, 0.25], HR((0, 0, 0.08)), 0.15, tag="throat", masks=["throat"]))
    # nasal ridge (paired rugose nasals) along the top of the snout
    S.append(RC("head", "head", H(1.36, 0.43), H(0.6, 0.69), 0.05, 0.075, 0.07, scale=[1, 1, 1.35], masks=["rugose"], tag="nasal_ridge"))
    # detail masks (no volume): rugose cornified skin over the nasals
    S.append(E("head", "head", H(1.0, 0.62), [0.5, 0.12, 0.14], HR((0, 0, -0.3)), 0.05, mask_only=True, masks=["rugose"], tag="nasal_rugose"))
    return S


def _arc_chain(points, radii, blend, sign=1, masks=None, tag=""):
    out = []
    for k in range(len(points) - 1):
        kw = dict(tag=tag)
        if sign < 0:
            kw["sign"] = -1
        if masks:
            kw["masks"] = masks
        out.append(RC("head", "head", points[k], points[k + 1], radii[k], radii[k + 1], blend, **kw))
    return out


def head_details(cast, base_shapes):
    """Eyes, lids, brow, nostrils, lip line, teeth: placed on the surface by ray casting."""
    S = []
    eyes = []
    teeth = []
    for sg in (1, -1):
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        side = "r" if sg > 0 else "l"
        # ---------------- eye --------------------------------------------------------------------
        # Orbit high on the skull, the eye looking forward-outward: optical axis ~57 deg from the
        # sagittal plane gives the ~55 deg binocular overlap of Stevens (2006) for a ~170 deg field.
        eye_dir = HD(np.array([0.53, 0.07, 0.845 * sg]))
        eye_dir /= np.linalg.norm(eye_dir)
        surf = cast.cast_at(H(EYE_U, EYE_V, 0.25 * sg), -eye_dir, back=0.8)
        ec = surf - eye_dir * (R_EYE - 0.008)          # cornea ~0.8 cm proud of the smooth head surface
        eyes.append(dict(side=side, center=list(ec), radius=R_EYE, direction=list(eye_dir), bone=f"eye_{side}"))
        fwd = HD(np.array([1.0, 0.12, 0.0]))
        bz = eye_dir
        by = np.cross(bz, fwd)
        by /= np.linalg.norm(by)
        if by[1] < 0:
            by = -by
        bx = np.cross(by, bz)
        if bx @ FWD < 0:
            bx = -bx
        lid_rot = basis_to_euler(bx, by, bz)
        L = lambda x, y, z: ec + bx * x + by * y + bz * z
        # orbit mound (soft tissue around the eyeball)
        S.append(E("head", "head", L(0.0, 0.0, -0.04), [0.13, 0.095, 0.055], lid_rot, 0.06, masks=["lid"], tag="orbit_mound"))
        # pit for the eyeball (separate mesh)
        S.append(E("head", "head", ec, [R_EYE + 0.003] * 3, (0, 0, 0), 0.004, sign=-1, tag="eye_pit"))
        # lids: capsule arcs along an almond palpebral opening, hugging the eyeball
        a_, bu, bd = 0.052, 0.03, 0.024
        for upper in (True, False):
            pts, rad = [], []
            r0 = 0.022 if upper else 0.017
            for th in np.linspace(0.0, math.pi, 9):
                b = bu if upper else bd
                x = a_ * math.cos(th)
                y = (b * math.sin(th)) * (1 if upper else -1) + (0.003 if upper else 0.0)
                q = np.array([x, y, math.sqrt(max(1e-6, R_EYE ** 2 - x * x - y * y))])
                q /= np.linalg.norm(q)
                inplane = np.array([x / a_, y / max(b, 1e-3), 0.0])
                inplane /= np.linalg.norm(inplane) + 1e-9
                taper = 0.5 + 0.5 * math.sin(th)
                rr = r0 * taper
                c = q * (R_EYE + 0.3 * rr) + inplane * rr * 0.85
                pts.append(L(*c))
                rad.append(rr)
            S += _arc_chain(pts, rad, 0.02, masks=["lid"], tag="upper_lid" if upper else "lower_lid")
        # crease above the upper lid
        S += _arc_chain([L(0.06, 0.04, 0.012), L(0.0, 0.068, 0.012), L(-0.064, 0.044, 0.008)], [0.006, 0.008, 0.006], 0.008, sign=-1, tag="lid_crease")
        # ---------------- brow: lacrimal horn in front, postorbital boss above-behind ----------------
        # (rugose bosses of adult T. rex; the brow shelf overhangs and shades the eye)
        S.append(E("head", "head", L(0.0, 0.092, -0.03), [0.16, 0.042, 0.06], lid_rot, 0.05, masks=["brow"], tag="brow_ridge"))
        S.append(E("head", "head", L(-0.12, 0.1, -0.05), [0.09, 0.065, 0.065], lid_rot, 0.05, masks=["rugose", "brow"], tag="postorbital_boss"))
        S.append(E("head", "head", L(0.18, 0.085, -0.055), [0.085, 0.042, 0.05], lid_rot, 0.05, masks=["rugose", "brow"], tag="lacrimal_horn"))
        # jugal 'horn' (cornual process) below-behind the orbit
        jh = cast.cast_at(H(0.3, 0.3, 0.3 * sg), np.array([0, 0, -sg]))
        S.append(E("head", "head", jh - cast.normal(jh) * 0.025, [0.075, 0.04, 0.045], mr(HR((0, 0.3, 0))), 0.05, masks=["rugose"], tag="jugal_horn"))
        # ---------------- nostril: external naris high at the front of the snout -----------------
        na = cast.cast_at(H(1.3, 0.405, 0.15 * sg), HD(np.array([0.3, 0.1, -0.95 * sg])))
        nn = cast.normal(na)
        nf = HD(np.array([1.0, -0.3, 0.0]))
        nf = nf - (nf @ nn) * nn
        nf /= np.linalg.norm(nf)
        ny = np.cross(nn, nf)
        nx = nf
        nrot = basis_to_euler(nx, ny, nn)
        S.append(E("head", "head", na - nn * 0.02, [0.085, 0.045, 0.035], nrot, 0.04, masks=["nostril"], tag="narial_swelling"))
        S.append(E("head", "head", na + nn * 0.012 + nx * 0.012, [0.045, 0.017, 0.032], nrot, 0.01, sign=-1, masks=["nostril"], tag="naris"))
        # ---------------- lip line: shallow continuous groove at the upper-lip margin -------------
        lip_u = [1.45, 1.4, 1.32, 1.2, 1.05, 0.9, 0.75, 0.62, 0.52, 0.45, 0.4]
        hits = []
        for u in lip_u:
            vl = _up_interp(min(u, 1.42))[1] + (0.012 if u > 1.42 else 0.0)
            if u < 0.55:   # mouth corner rising under the jugal (rictus)
                vl += 0.11 * ((0.55 - u) / 0.15) ** 1.4
            hits.append(cast.cast(H(u, vl, 0.8 * sg), np.array([0, 0, -sg])))
        for k in range(len(hits) - 1):
            a, b = hits[k], hits[k + 1]
            r = 0.01 if k < len(hits) - 3 else 0.007
            S.append(CAP("head", "head", a + cast.normal(a) * (r - 0.0045), b + cast.normal(b) * (r - 0.0045), r, 0.007, sign=-1, tag="lip_groove"))
            S.append(CAP("head", "head", a, b, 0.045, 0.02, mask_only=True, masks=["lip"], tag="lip_mask"))
        rc = hits[-1]
        S.append(E("jaw", "jaw", rc - cast.normal(rc) * 0.035 + HD(np.array([-0.05, -0.03, 0])), [0.08, 0.06, 0.045], mr(HR((0, 0.3, 0.4))), 0.05, tag="rictal_fold"))
        # ---------------- teeth (covered by the lips at rest, Cullen et al. 2023) ------------------
        # upper: 4 premaxillary + 11 maxillary; lower: 13 dentary (within the range reported for
        # adult T. rex; exact counts vary). Kept inside the closed head volume at rest.
        for j in range(15):
            t = j / 14.0
            u = 1.38 - 0.86 * t
            vl = _up_interp(u)[1]
            length = 0.05 + 0.06 * math.sin(math.pi * min(1.0, 0.15 + 0.9 * t)) if j >= 4 else 0.045
            hwj = upper_width(u, vl + 0.06) - 0.06
            root = H(u, vl + 0.035 + length, hwj * sg)
            tip = H(u - 0.01, vl + 0.035, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="upper", side=side, root=list(root), tip=list(tip), radius=0.016 + 0.01 * (length / 0.11)))
        for j in range(13):
            t = j / 12.0
            u = 1.3 - 0.8 * t
            vl = _up_interp(u)[1]
            length = 0.04 + 0.04 * math.sin(math.pi * min(1.0, 0.2 + 0.8 * t))
            hwj = upper_width(u, vl + 0.06) - 0.075
            root = H(u, vl - 0.02 - length, hwj * sg)
            tip = H(u + 0.01, vl - 0.02, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="lower", side=side, root=list(root), tip=list(tip), radius=0.014 + 0.008 * (length / 0.08)))
    return S, eyes, teeth


def upper_width(u, v):
    vt, vl, hw, fr, nt, nb = _up_interp(u)
    cy = vl + fr * (vt - vl)
    bot = cy - vl
    if v >= cy:
        t = min(1.0, (v - cy) / (vt - cy))
        return hw * (1 - t ** nt) ** (1 / nt)
    t = min(1.0, (cy - v) / bot)
    return hw * (1 - t ** nb) ** (1 / nb)


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
    (-7.05, 2.79, 0.030, 0.034, 0.028, 0.40, 2.0, 2.0),
    (-6.40, 2.87, 0.066, 0.088, 0.060, 0.38, 2.0, 1.9),
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
    (2.62, 3.21, 0.430, 0.900, 0.400, 0.45, 2.25, 2.0),
    (2.96, 3.34, 0.450, 0.800, 0.370, 0.45, 2.2, 2.0),
    (3.26, 3.57, 0.430, 0.680, 0.340, 0.45, 2.2, 2.0),
    (3.50, 3.82, 0.360, 0.560, 0.320, 0.45, 2.1, 2.0),
    (3.64, 3.93, 0.280, 0.450, 0.290, 0.45, 2.0, 2.0),
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
        S.append(E("neck_01", "neck", m([2.72, 3.2, 0.16]), [0.4, 0.45, 0.2], mr([0, 0, 0.75]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        S.append(E("neck_02", "neck", m([3.05, 3.45, 0.14]), [0.36, 0.42, 0.19], mr([0, 0, 0.8]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        S.append(E("neck_03", "neck", m([3.32, 3.65, 0.13]), [0.3, 0.38, 0.17], mr([0, 0, 0.7]), 0.16, masks=["neck_folds"], tag="neck_lateral"))
        # ---------------- hind limb ----------------------------------------------------------------
        fd = (knee - hip) / np.linalg.norm(knee - hip)
        ang = math.atan2(-fd[0], -fd[1])          # rotation about z bringing local +y onto (hip - knee)
        # thigh: long teardrop along the femur, broadest proximally
        S.append(E(f"thigh_{s}", "thigh", hip + 0.4 * (knee - hip) + [0.02, 0, 0.1 * sg], [0.47, 0.8, 0.25], mr([0, 0, ang]), 0.12, tag="thigh_main"))
        S.append(E(f"thigh_{s}", "thigh", hip + [0.02, 0.1, 0.07 * sg], [0.78, 0.42, 0.24], mr([0, 0, 0.08]), 0.14, tag="thigh_iliotibialis"))
        S.append(RC(f"thigh_{s}", "thigh", hip + 0.55 * (knee - hip) + [0, 0, 0.06 * sg], knee + [0.0, 0.04, 0.0], 0.34, 0.22, 0.1, tag="thigh_lower"))
        # M. caudofemoralis longus: tail base -> 4th trochanter, bulging under the tail
        S.append(RC("pelvis", "thigh", m([-1.7, 3.04, 0.17]), hip + 0.35 * (knee - hip) + [-0.22, 0, -0.03 * sg], 0.24, 0.27, 0.3, tag="caudofemoralis"))
        S.append(E(f"shin_{s}", "shin", knee + [0.01, -0.02, 0], [0.19, 0.19, 0.19], (0, 0, 0), 0.09, masks=["joint_folds"], tag="knee"))
        sd = (ankle - knee) / np.linalg.norm(ankle - knee)
        post = np.array([sd[1], -sd[0], 0.0]) * (-1 if sd[1] < 0 else 1)
        post = post if post[0] < 0 else -post
        sang = math.atan2(-sd[0], -sd[1])
        S.append(RC(f"shin_{s}", "shin", knee + [0.0, -0.06, 0], ankle + [0, 0.03, 0], 0.235, 0.145, 0.08, scale=[1, 1, 0.92], tag="shank"))
        S.append(E(f"shin_{s}", "shin", knee + 0.32 * (ankle - knee) + post * 0.1, [0.235, 0.5, 0.2], mr([0, 0, sang]), 0.09, tag="gastrocnemius"))
        S.append(E(f"shin_{s}", "shin", knee + 0.4 * (ankle - knee) - post * 0.09, [0.13, 0.42, 0.13], mr([0, 0, sang]), 0.07, tag="tibialis"))
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
        "skin": SKIN,
        "palette": PALETTE,
        "bones": bones,
        "shapes": shapes,
        "eyes": eyes,
        "claws": claws,
        "teeth": teeth,
    }
    return doc
