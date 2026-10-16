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
# HEAD. Head frame: origin at the jaw joint (quadrate-articular), u forward along the skull axis, v up,
# w = +z (animal right). Values = skull + soft tissue (2-4 cm) of an adult Sue/Stan-sized skull
# (~1.40-1.45 m long, ~0.9 m across the quadratojugals: Brochu 2003). Shape cues kept from the
# skeleton: narrow snout and broad temporal region ('keyhole' outline in dorsal view) so that the
# orbits face forward-laterally (Stevens 2006: ~55 deg binocular field), deep posterior skull and
# mandible (surangular), dorsal profile highest over the lacrimal-postorbital region, tall premaxilla,
# convex upper tooth-row margin. Lips covering the teeth at rest: Cullen et al. 2023 (inferred).
# Upper head sections: u, v_top (dorsal profile), v_lip (upper-lip margin), hw (max half-width),
# fr (level of max width, fraction of the height above the lip), tw (half-width at the top / hw:
# < 1 gives the tapering 'gothic arch' section, narrow nasals over sloping maxillae), nt, nb.
HEAD_UP = [
    # u     v_top  v_lip  hw     fr    tw    nt   nb
    (-0.14, 0.50, 0.25, 0.30, 0.40, 0.75, 2.0, 2.2),
    (-0.02, 0.60, 0.20, 0.42, 0.28, 0.68, 2.0, 2.5),
    (0.10, 0.665, 0.17, 0.455, 0.24, 0.64, 2.1, 2.8),
    (0.22, 0.695, 0.155, 0.45, 0.24, 0.62, 2.1, 2.9),
    (0.34, 0.705, 0.145, 0.42, 0.25, 0.6, 2.2, 3.0),
    (0.45, 0.698, 0.135, 0.37, 0.26, 0.6, 2.3, 3.0),
    (0.56, 0.68, 0.12, 0.315, 0.27, 0.62, 2.4, 3.1),
    (0.70, 0.65, 0.105, 0.285, 0.28, 0.66, 2.5, 3.1),
    (0.85, 0.615, 0.095, 0.265, 0.29, 0.68, 2.5, 3.1),
    (1.00, 0.578, 0.09, 0.248, 0.30, 0.7, 2.5, 3.0),
    (1.15, 0.54, 0.09, 0.232, 0.31, 0.72, 2.5, 2.9),
    (1.27, 0.5, 0.095, 0.212, 0.33, 0.74, 2.5, 2.8),
    (1.37, 0.455, 0.105, 0.182, 0.35, 0.76, 2.4, 2.6),
    (1.44, 0.4, 0.12, 0.14, 0.38, 0.8, 2.3, 2.4),
]
HEAD_CAPS = [0.12, 0.07]
# lower jaw (dentary + surangular/angular + skin): u, v_bot, hw (max), bw (bottom taper)
HEAD_LO = [
    (-0.18, -0.07, 0.30, 0.80),
    (-0.06, -0.19, 0.40, 0.75),
    (0.08, -0.265, 0.43, 0.72),
    (0.22, -0.295, 0.42, 0.70),
    (0.38, -0.28, 0.375, 0.70),
    (0.55, -0.24, 0.32, 0.70),
    (0.72, -0.2, 0.275, 0.72),
    (0.90, -0.17, 0.245, 0.74),
    (1.06, -0.145, 0.225, 0.76),
    (1.20, -0.125, 0.205, 0.78),
    (1.30, -0.105, 0.185, 0.80),
    (1.37, -0.08, 0.16, 0.85),
]
LIP_INSET = 0.004            # the upper lip overhangs the mandible laterally (varanid-like lips, design)
RICTUS_U = 0.34             # mouth corner, under the front half of the orbit
EYE_U, EYE_V = 0.46, 0.49
R_EYE = 0.052


def _up_sec(u):
    """Upper-head section parameters at u (linear interpolation of HEAD_UP)."""
    us = [s[0] for s in HEAD_UP]
    vt, vl, hw, fr, tw, nt, nb = [float(np.interp(u, us, [s[i] for s in HEAD_UP])) for i in range(1, 8)]
    cy = vl + fr * (vt - vl)
    return dict(vt=vt, vl=vl, hw=hw, cy=cy, top=vt - cy, bot=cy - vl, tw=tw, nt=nt, nb=nb)


def _up_interp(u):
    s = _up_sec(u)
    return [s["vt"], s["vl"], s["hw"], 0.0, s["nt"], s["nb"]]


def upper_width(u, v):
    """Half-width of the upper-head loft at (u, v) (mirrors the kernel's tapered superellipse)."""
    s = _up_sec(u)
    yy = v - s["cy"]
    if yy >= 0:
        t = min(1.0, yy / s["top"])
        hwe = s["hw"] * (1 - (1 - s["tw"]) * t)
        return hwe * max(0.0, 1 - t ** s["nt"]) ** (1 / s["nt"])
    t = min(1.0, -yy / s["bot"])
    return s["hw"] * max(0.0, 1 - t ** s["nb"]) ** (1 / s["nb"])


def jaw_hw(u):
    us = [s[0] for s in HEAD_LO]
    hw = float(np.interp(u, us, [s[2] for s in HEAD_LO]))
    vl = _up_sec(min(max(u, -0.14), 1.44))["vl"]
    return min(hw, upper_width(u, vl + 0.025) - LIP_INSET) if u > RICTUS_U - 0.1 else hw


def lip_v(u):
    """Upper-lip margin height; rises behind the rictus into the cheek."""
    v = _up_sec(min(u, 1.44))["vl"]
    if u < RICTUS_U + 0.12:
        v += 0.10 * ((RICTUS_U + 0.12 - u) / 0.2) ** 1.6
    return v


def head_shapes():
    """Upper head (bone 'head') and lower jaw (bone 'jaw') as lofts + muscles. Values in head frame."""
    S = []
    secs = []
    for u, vt, vl, hw, fr, tw, nt, nb in HEAD_UP:
        cy = vl + fr * (vt - vl)
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vl, hw=hw, nt=nt, nb=nb, tw=tw))
    S.append(dict(bone="head", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, caps=HEAD_CAPS, blend=0.2,
                  region="head", tag="cranium"))
    secs = []
    for u, vb, hw, bw in HEAD_LO:
        vl = _up_sec(min(max(u, -0.14), 1.44))["vl"]
        vt = vl - 0.012
        if u > RICTUS_U - 0.1:
            hw = min(hw, upper_width(u, vl + 0.025) - LIP_INSET)
        cy = vt - 0.3 * (vt - vb)
        secs.append(dict(u=u, cy=cy, top=vt - cy, bot=cy - vb, hw=hw, nt=2.8, nb=2.2, bw=bw))
    S.append(dict(bone="jaw", kind="loft", origin=list(JAW), rotation=HEAD_ROT, sections=secs, caps=[0.1, 0.05], blend=0.022,
                  region="jaw", tag="mandible"))
    for sg in (1, -1):
        w = lambda c: H(c[0], c[1], c[2] * sg)
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        # jaw-closing muscles (M. adductor mandibulae externus) bulging out of the supratemporal
        # fenestrae on each side of the sagittal crest: broad, rounded skull top behind the orbits
        S.append(E("head", "head", w((0.08, 0.58, 0.2)), [0.25, 0.1, 0.15], mr(HR((0.25, 0.1, -0.08))), 0.12, tag="adductor"))
        # cheek over the jugal / quadratojugal: covers the back of the mouth line behind the rictus
        S.append(E("head", "head", w((0.1, 0.2, 0.3)), [0.3, 0.16, 0.1], mr(HR((0.0, 0.22, 0.12))), 0.14, tag="cheek"))
        # jowl: M. pterygoideus ventralis wrapping the back of the lower jaw (rounded, deep)
        S.append(E("jaw", "jaw", w((-0.02, -0.16, 0.2)), [0.22, 0.14, 0.12], mr(HR((0, 0.25, 0.3))), 0.15, tag="pterygoideus",
                   masks=["jowl"]))
    # throat / gular skin between the rami, blending into the neck
    S.append(E("jaw", "jaw", H(0.28, -0.2), [0.5, 0.1, 0.22], HR((0, 0, 0.12)), 0.14, tag="throat", masks=["throat"]))
    # detail mask (no volume): rugose cornified skin over the nasals
    S.append(E("head", "head", H(0.95, 0.56), [0.52, 0.11, 0.12], HR((0, 0, -0.3)), 0.05, mask_only=True, masks=["rugose"], tag="nasal_rugose"))
    return S


def _arc_chain(points, radii, blend, sign=1, masks=None, tag="", bone="head"):
    out = []
    for k in range(len(points) - 1):
        kw = dict(tag=tag)
        if sign < 0:
            kw["sign"] = -1
        if masks:
            kw["masks"] = masks
        out.append(RC(bone, "head", points[k], points[k + 1], radii[k], radii[k + 1], blend, **kw))
    return out


def head_details(cast, base_shapes):
    """Eyes, sockets, lids, brow, nostrils, lip line, teeth: placed on the surface by ray casting."""
    S = []
    eyes = []
    teeth = []
    for sg in (1, -1):
        mr = lambda r: r if sg > 0 else [-r[0], -r[1], r[2]]
        side = "r" if sg > 0 else "l"
        out = lambda u, v: cast.cast(H(u, v, 1.2 * sg), np.array([0, 0, -sg]))   # lateral surface point
        # ---------------- jugal ridge (cheekbone line under the orbit) and antorbital fossa --------
        jg = out(0.36, 0.31)
        S.append(E("head", "head", jg - cast.normal(jg) * 0.035, [0.24, 0.065, 0.055], mr(HR((0, 0.25, -0.04))), 0.08,
                   masks=["rugose"], tag="jugal_swelling"))
        fo = out(0.86, 0.36)
        S.append(E("head", "head", fo + cast.normal(fo) * 0.008, [0.16, 0.065, 0.02], mr(HR((0, 0.28, -0.06))), 0.06, sign=-1,
                   tag="antorbital_fossa"))
        # ---------------- eye ------------------------------------------------------------------------
        # optical axis forward-outward (~55 deg binocular overlap, Stevens 2006), slightly upward
        eye_dir = HD(np.array([0.46, 0.04, 0.89 * sg]))
        eye_dir /= np.linalg.norm(eye_dir)
        surf = cast.cast_at(H(EYE_U, EYE_V, 0.12 * sg), -eye_dir, back=1.0)
        fwd = HD(np.array([1.0, 0.08, 0.0]))
        bz = eye_dir
        by = np.cross(bz, fwd)
        by /= np.linalg.norm(by)
        if by[1] < 0:
            by = -by
        bx = np.cross(by, bz)
        if bx @ FWD < 0:
            bx = -bx
        lid_rot = basis_to_euler(bx, by, bz)
        ec = surf - eye_dir * (R_EYE - 0.008)          # cornea ~8 mm proud of the smooth head surface
        eyes.append(dict(side=side, center=list(ec), radius=R_EYE, direction=list(eye_dir), bone=f"eye_{side}"))
        L = lambda x, y, z: ec + bx * x + by * y + bz * z
        zs = R_EYE - 0.008                              # z of the smooth surface in the eye frame
        # palpebral mask (fine scales around the eye), no volume
        S.append(E("head", "head", L(0.0, 0.0, zs), [0.09, 0.07, 0.05], lid_rot, 0.03, mask_only=True, masks=["lid"], tag="orbit_mask"))
        # pit for the eyeball (separate mesh)
        S.append(E("head", "head", ec, [R_EYE + 0.003] * 3, (0, 0, 0), 0.004, sign=-1, tag="eye_pit"))
        # lids: capsule arcs along an almond palpebral opening, hugging the eyeball
        a_, bu, bd = 0.04, 0.022, 0.019
        for upper in (True, False):
            pts, rad = [], []
            r0 = 0.017 if upper else 0.013
            for th in np.linspace(-0.15, math.pi + 0.15, 11):
                b = bu if upper else bd
                x = a_ * math.cos(th)
                y = (b * max(0.0, math.sin(th))) * (1 if upper else -1)
                q = np.array([x, y, math.sqrt(max(1e-6, R_EYE ** 2 - x * x - y * y))])
                q /= np.linalg.norm(q)
                inplane = np.array([x / a_, (y / b) if abs(y) > 1e-6 else (0.02 if upper else -0.02), 0.0])
                inplane /= np.linalg.norm(inplane) + 1e-9
                taper = 0.5 + 0.5 * math.sin(min(max(th, 0.0), math.pi))
                rr = r0 * taper
                c = q * (R_EYE + 0.15 * rr) + inplane * rr * 0.95
                pts.append(L(*c))
                rad.append(rr)
            S += _arc_chain(pts, rad, 0.016, masks=["lid"], tag="upper_lid" if upper else "lower_lid")
        # crease above the upper lid
        S += _arc_chain([L(0.06, 0.042, zs - 0.004), L(0.0, 0.058, zs - 0.002), L(-0.062, 0.04, zs - 0.006)], [0.005, 0.007, 0.005], 0.007, sign=-1, tag="lid_crease")
        # ---------------- brow: continuous shelf from the lacrimal horn to the postorbital boss, ---------
        # overhanging the eye (rugose bosses of adult tyrannosaurids; soft-tissue thickness = design)
        S.append(E("head", "head", L(0.01, 0.07, zs - 0.022), [0.14, 0.032, 0.04], lid_rot, 0.045, masks=["brow", "rugose"], tag="brow_ridge"))
        S.append(E("head", "head", L(0.19, 0.06, zs - 0.04), [0.075, 0.04, 0.045], lid_rot, 0.05, masks=["rugose", "brow"], tag="lacrimal_horn"))
        S.append(E("head", "head", L(-0.15, 0.075, zs - 0.045), [0.085, 0.055, 0.05], lid_rot, 0.05, masks=["rugose", "brow"], tag="postorbital_boss"))
        # jugal 'horn' (cornual process) below-behind the orbit
        jh = out(0.27, 0.31)
        S.append(E("head", "head", jh - cast.normal(jh) * 0.02, [0.06, 0.035, 0.04], mr(HR((0, 0.3, 0))), 0.04, masks=["rugose"], tag="jugal_horn"))
        # ---------------- nostril: external naris high at the front of the snout -----------------
        na = cast.cast_at(H(1.32, 0.35, 0.12 * sg), HD(np.array([0.35, 0.05, -0.94 * sg])))
        nn = cast.normal(na)
        nf = HD(np.array([1.0, -0.25, 0.0]))
        nf = nf - (nf @ nn) * nn
        nf /= np.linalg.norm(nf)
        ny = np.cross(nn, nf)
        nx = nf
        nrot = basis_to_euler(nx, ny, nn)
        S.append(E("head", "head", na - nn * 0.022 - nx * 0.02, [0.1, 0.05, 0.04], nrot, 0.04, masks=["nostril"], tag="narial_swelling"))
        S.append(E("head", "head", na + nn * 0.01 + nx * 0.01, [0.042, 0.016, 0.03], nrot, 0.008, sign=-1, masks=["nostril"], tag="naris"))
        # ---------------- lip margin: mask for the supralabial / infralabial scale rows -------------
        lip_u = [1.44, 1.4, 1.34, 1.22, 1.08, 0.94, 0.8, 0.66, 0.54, 0.44, RICTUS_U]
        hits = []
        for u in lip_u:
            hits.append(cast.cast(H(u, lip_v(min(u, 1.43)) + 0.01, 0.8 * sg), np.array([0, 0, -sg])))
        for k in range(len(hits) - 1):
            a, b = hits[k], hits[k + 1]
            S.append(CAP("head", "head", a, b, 0.05, 0.02, mask_only=True, masks=["lip"], tag="lip_mask"))
        # ---------------- teeth (covered by the lips at rest, Cullen et al. 2023) ------------------
        # upper: 4 premaxillary + 11 maxillary; lower: 13 dentary (within the range reported for
        # adult T. rex). Kept inside the closed head volume at rest.
        for j in range(15):
            t = j / 14.0
            u = 1.38 - 0.86 * t
            vl = _up_sec(u)["vl"]
            length = 0.05 + 0.06 * math.sin(math.pi * min(1.0, 0.15 + 0.9 * t)) if j >= 4 else 0.045
            hwj = upper_width(u, vl + 0.06) - 0.06
            root = H(u, vl + 0.035 + 0.7 * length, hwj * sg)
            tip = H(u - 0.01, vl + 0.035, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="upper", side=side, root=list(root), tip=list(tip), radius=0.016 + 0.01 * (length / 0.11)))
        for j in range(13):
            t = j / 12.0
            u = 1.3 - 0.8 * t
            vl = _up_sec(u)["vl"]
            length = 0.04 + 0.04 * math.sin(math.pi * min(1.0, 0.2 + 0.8 * t))
            hwj = min(upper_width(u, vl + 0.06) - 0.08, jaw_hw(u) - 0.06)
            root = H(u, vl - 0.03 - 0.7 * length, hwj * sg)
            tip = H(u + 0.01, vl - 0.03, (hwj - 0.004) * sg)
            teeth.append(dict(jaw="lower", side=side, root=list(root), tip=list(tip), radius=0.014 + 0.008 * (length / 0.08)))
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
