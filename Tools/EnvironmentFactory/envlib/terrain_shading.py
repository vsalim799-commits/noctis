"""Cycles terrain and water materials for the valley renders: the same layer logic as the UE5 landscape
material (Docs/ENVIRONMENT.md), with the habitat masks of the crop as weights, height-weighted blending,
wetness darkening, trail mud and macro colour variation.
"""
from __future__ import annotations

import os

import bpy

from . import common
from .materials import _img

# ground material -> list of (mask name, weight)
LAYERS = [
    ("RiverGravel", [("channel", 1.0)]),
    ("RiverbankMudTracks", [("riverbank", 1.0), ("trails", 0.7)]),
    ("PointBarSand", [("sandbar", 1.0)]),
    ("BackswampOrganicMud", [("backswamp", 1.0)]),
    ("LeafLitterBroadleaf", [("riparian_forest", 1.0), ("levee_woodland", 0.6)]),
    ("FloodplainSilt", [("floodplain_open", 1.0), ("levee_woodland", 0.4)]),
    ("LeafLitterConifer", [("upland_conifer_forest", 1.0)]),
    ("CutbankStrata", [("cutbank_cliff", 1.0)]),
]


class _NT:
    def __init__(self, nt):
        self.nt = nt
        self.y = 0

    def node(self, kind, x=0, y=None, **props):
        n = self.nt.nodes.new(kind)
        n.location = (x, self.y if y is None else y)
        if y is None:
            self.y -= 60
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def link(self, a, b):
        self.nt.links.new(a, b)

    def math(self, op, a, b=None, x=0, val_b=None):
        n = self.node("ShaderNodeMath", x, operation=op)
        self._in(n.inputs[0], a)
        if b is not None:
            self._in(n.inputs[1], b)
        return n.outputs[0]

    def vmath(self, op, a, b=None, x=0, scale=None):
        n = self.node("ShaderNodeVectorMath", x, operation=op)
        self._in(n.inputs[0], a)
        if b is not None:
            self._in(n.inputs[1], b)
        if scale is not None:
            self._in(n.inputs[3], scale)
        return n.outputs[0] if op != "DOT_PRODUCT" else n.outputs[1]

    def _in(self, sock, v):
        if isinstance(v, (int, float)):
            sock.default_value = v
        elif isinstance(v, tuple):
            sock.default_value = v
        else:
            self.link(v, sock)


def terrain_material(masks, name="M_NoctisTerrain_Render", ground_dir=None, tile_scale=1.0, sharp=4.0):
    """masks: dict mask name -> (png path, channel letter) from terrain.crop_masks()."""
    from . import ground as groundmod
    ground_dir = ground_dir or common.GROUND_DIR
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    T = _NT(nt)
    out = T.node("ShaderNodeOutputMaterial", 2400, 0)
    bsdf = T.node("ShaderNodeBsdfPrincipled", 2100, 0)
    T.link(bsdf.outputs[0], out.inputs["Surface"])
    tc = T.node("ShaderNodeTexCoord", -2400, 0)
    uvm = T.node("ShaderNodeUVMap", -2400, -200)
    uvm.uv_map = "MaskUV"
    # mask channels
    seps = {}
    chan = {}
    for mname, (path, ch) in masks.items():
        if path not in seps:
            tex = T.node("ShaderNodeTexImage", -2100)
            tex.image = _img(path, "Non-Color")
            tex.image.alpha_mode = "CHANNEL_PACKED"
            tex.extension = "EXTEND"
            T.link(uvm.outputs["UV"], tex.inputs["Vector"])
            sep = T.node("ShaderNodeSeparateColor", -1900)
            T.link(tex.outputs["Color"], sep.inputs["Color"])
            seps[path] = (sep, tex)
        sep, tex = seps[path]
        chan[mname] = tex.outputs["Alpha"] if ch == "A" else sep.outputs[{"R": "Red", "G": "Green", "B": "Blue"}[ch]]
    # per-layer samples
    acc_c = acc_n = acc_r = acc_w = None
    for i, (gname, weights) in enumerate(LAYERS):
        tile = groundmod.MATERIALS[gname][0] * tile_scale
        T.y = -400 * i
        sc = T.vmath("SCALE", tc.outputs["Object"], x=-1700, scale=1.0 / tile)
        texs = {}
        for mp, cs in (("BaseColor", "sRGB"), ("Normal", "Non-Color"), ("Roughness", "Non-Color"), ("Height", "Non-Color")):
            p = os.path.join(ground_dir, "T_Ground_%s_%s.png" % (gname, mp))
            t = T.node("ShaderNodeTexImage", -1500)
            t.image = _img(p, cs)
            if gname == "CutbankStrata":
                t.projection = "BOX"
                t.projection_blend = 0.25
            T.link(sc, t.inputs["Vector"])
            texs[mp] = t
        # weight = sum(mask * k) * (0.25 + height)^sharp
        w = None
        for mname, k in weights:
            if mname not in chan:
                continue
            term = T.math("MULTIPLY", chan[mname], float(k), x=-1200)
            w = term if w is None else T.math("ADD", w, term, x=-1100)
        if w is None:
            continue
        hb = T.math("ADD", texs["Height"].outputs["Color"], 0.25, x=-1100)
        hb = T.math("POWER", hb, sharp, x=-1000)
        w = T.math("MULTIPLY", w, hb, x=-900)
        c = T.vmath("SCALE", texs["BaseColor"].outputs["Color"], x=-700, scale=w)
        nrm = T.vmath("SCALE", texs["Normal"].outputs["Color"], x=-700, scale=w)
        r = T.math("MULTIPLY", texs["Roughness"].outputs["Color"], w, x=-700)
        acc_c = c if acc_c is None else T.vmath("ADD", acc_c, c, x=-500)
        acc_n = nrm if acc_n is None else T.vmath("ADD", acc_n, nrm, x=-500)
        acc_r = r if acc_r is None else T.math("ADD", acc_r, r, x=-500)
        acc_w = w if acc_w is None else T.math("ADD", acc_w, w, x=-500)
    T.y = 600
    inv = T.math("DIVIDE", 1.0, T.math("MAXIMUM", acc_w, 1e-4, x=-300), x=-200)
    col = T.vmath("SCALE", acc_c, x=0, scale=inv)
    ncol = T.vmath("SCALE", acc_n, x=0, scale=inv)
    rough = T.math("MULTIPLY", acc_r, inv, x=0)
    # wetness: darker, glossier
    wet = chan.get("wetness")
    if wet is not None:
        wk = T.math("MULTIPLY", T.math("SUBTRACT", wet, 0.55, x=100), 1.8, x=200)
        wk = T.math("MINIMUM", T.math("MAXIMUM", wk, 0.0, x=300), 1.0, x=400)
        dark = T.math("SUBTRACT", 1.0, T.math("MULTIPLY", wk, 0.35, x=500), x=600)
        col = T.vmath("SCALE", col, x=700, scale=dark)
        rough = T.math("MULTIPLY", rough, T.math("SUBTRACT", 1.0, T.math("MULTIPLY", wk, 0.45, x=500), x=600), x=700)
    # macro variation
    noise = T.node("ShaderNodeTexNoise", 600, 900)
    noise.inputs["Scale"].default_value = 0.012
    noise.inputs["Detail"].default_value = 4.0
    T.link(tc.outputs["Object"], noise.inputs["Vector"])
    mv = T.math("ADD", T.math("MULTIPLY", noise.outputs["Fac"], 0.35, x=800), 0.83, x=900)
    col = T.vmath("SCALE", col, x=1000, scale=mv)
    T.link(col, bsdf.inputs["Base Color"])
    T.link(rough, bsdf.inputs["Roughness"])
    # normal: DX -> GL (flip green) then normal map
    sep = T.node("ShaderNodeSeparateColor", 1100, 300)
    T.link(ncol, sep.inputs["Color"])
    g = T.math("SUBTRACT", 1.0, sep.outputs["Green"], x=1300)
    comb = T.node("ShaderNodeCombineColor", 1450, 300)
    T.link(sep.outputs["Red"], comb.inputs["Red"])
    T.link(g, comb.inputs["Green"])
    T.link(sep.outputs["Blue"], comb.inputs["Blue"])
    nm = T.node("ShaderNodeNormalMap", 1650, 300)
    nm.inputs["Strength"].default_value = 1.0
    T.link(comb.outputs["Color"], nm.inputs["Color"])
    T.link(nm.outputs["Normal"], bsdf.inputs["Normal"])
    bsdf.inputs["Specular IOR Level"].default_value = 0.4
    return mat


def water_material(name="M_NoctisWater_Render", color=(0.055, 0.065, 0.045), wave_scale=0.35, bump_strength=0.08):
    """Silty, opaque-looking river water: dark Principled, low roughness, wind ripples (bump)."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Roughness"].default_value = 0.035
    p.inputs["IOR"].default_value = 1.333
    p.inputs["Specular IOR Level"].default_value = 0.5
    tc = nt.nodes.new("ShaderNodeTexCoord")
    n1 = nt.nodes.new("ShaderNodeTexNoise")
    n1.inputs["Scale"].default_value = wave_scale
    n1.inputs["Detail"].default_value = 6
    map_ = nt.nodes.new("ShaderNodeMapping")
    map_.inputs["Scale"].default_value = (1.0, 3.0, 1.0)  # ripples elongated across the flow
    nt.links.new(tc.outputs["Object"], map_.inputs["Vector"])
    nt.links.new(map_.outputs["Vector"], n1.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = bump_strength
    bump.inputs["Distance"].default_value = 0.02
    nt.links.new(n1.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    return mat
