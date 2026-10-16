"""Cycles terrain and water materials for the valley renders, mirroring M_NoctisLandscape
(Content/Python/setup_landscape_material.py, Docs/ENVIRONMENT.md §3.2):
  * same 9 layers and habitat weights (envlib/landscape.LAYERS, incl. oxbow_lake in Backswamp and the SiltDry hollows),
    Strata only on slopes > 20-28 deg, height-weighted blending;
  * same anti-tiling: second sample rotated by the per-layer angle 0.6 + 0.9 i rad and scaled by 1.618, mixed by
    T_Ground_MacroNoise.G, albedo faded to a 4.3x larger sample with the camera distance (15-60 m);
  * Strata in biplanar projection (U = world X or Y, V = -Z: beds follow the elevation);
  * same wetness (0.8 x static wetness + 0.6 x shore band; MPC GroundWetness / Flooding at their defaults, 0),
    same trail overlay (corridors x 0.6 -> Mud layer), same far ground-cover tint and macro noise.
Render-only differences (documented in Docs/ENVIRONMENT.md §3.3): optional canopy_shade (aerial), procedural
micro-relief in the geometry, crop masks resampled at the crop resolution, Cycles water shader.
"""
from __future__ import annotations

import os

import bpy

from . import common
from .materials import _img

GOLDEN = 1.618       # same constants as setup_landscape_material.py
FAR_SCALE = 4.3
STATIC_WETNESS = 0.8
SHORE_WETNESS = 0.6
STATIC_TRAILS = 0.6
GROUND_COVER_TINT = (0.055, 0.07, 0.028)


def _layers():
    """[(layer, ground material, [(mask, weight)])] from envlib.landscape.LAYERS ('_dry' -> crop mask 'dry')."""
    from . import landscape as LS
    out = []
    for name, (g, habs) in LS.LAYERS.items():
        w = [("dry" if h == "_dry" else h, k) for h, k in habs.items()]
        if name == "Silt":
            w.append(("dry", -1.0))
        out.append((name, g, w))
    return out


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


# Forest habitats and their canopy closure, for the optional understorey shade of the render material.
CANOPY = {"riparian_forest": 1.0, "levee_woodland": 0.7, "upland_conifer_forest": 1.0, "backswamp": 0.5}


def terrain_material(masks, name="M_NoctisTerrain_Render", ground_dir=None, tile_scale=1.0, sharp=4.0, canopy_shade=0.0):
    """masks: dict mask name -> (png path, channel letter) from terrain.crop_masks().
    canopy_shade (0..1, render-only approximation): darkens the forest floor where the habitats are wooded. Alpha-card
    LODs are far more porous than a real closed canopy, so from above the sun- and sky-lit litter shows through the
    crowns as bright specks; real understorey light under a closed canopy is a few % of open light."""
    from . import ground as groundmod
    from . import landscape as LS
    ground_dir = ground_dir or common.GROUND_DIR
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    T = _NT(nt)
    out = T.node("ShaderNodeOutputMaterial", 2600, 0)
    bsdf = T.node("ShaderNodeBsdfPrincipled", 2300, 0)
    T.link(bsdf.outputs[0], out.inputs["Surface"])
    tc = T.node("ShaderNodeTexCoord", -3000, 0)
    uvm = T.node("ShaderNodeUVMap", -3000, -200)
    uvm.uv_map = "MaskUV"
    geo = T.node("ShaderNodeNewGeometry", -3000, -400)
    camd = T.node("ShaderNodeCameraData", -3000, -600)
    # mask channels
    seps, chan = {}, {}
    for mname, (path, ch) in masks.items():
        if path not in seps:
            tex = T.node("ShaderNodeTexImage", -2700)
            tex.image = _img(path, "Non-Color")
            tex.image.alpha_mode = "CHANNEL_PACKED"
            tex.extension = "EXTEND"
            T.link(uvm.outputs["UV"], tex.inputs["Vector"])
            sep = T.node("ShaderNodeSeparateColor", -2500)
            T.link(tex.outputs["Color"], sep.inputs["Color"])
            seps[path] = (sep, tex)
        sep, tex = seps[path]
        chan[mname] = tex.outputs["Alpha"] if ch == "A" else sep.outputs[{"R": "Red", "G": "Green", "B": "Blue"}[ch]]
    # macro noise (world / 240 m): R albedo, G anti-tiling mix
    mn = T.node("ShaderNodeTexImage", -2700, 900)
    mn.image = _img(os.path.join(ground_dir, groundmod.MACRO_NOISE), "Non-Color")
    T.link(T.vmath("SCALE", tc.outputs["Object"], x=-2900, scale=1.0 / groundmod.MACRO_TILE_M), mn.inputs["Vector"])
    mn_sep = T.node("ShaderNodeSeparateColor", -2500, 900)
    T.link(mn.outputs["Color"], mn_sep.inputs["Color"])
    mix_ab = T.node("ShaderNodeMapRange", -2300, 900)
    mix_ab.interpolation_type = "SMOOTHSTEP"
    mix_ab.inputs["From Min"].default_value = 0.35
    mix_ab.inputs["From Max"].default_value = 0.65
    T.link(mn_sep.outputs["Green"], mix_ab.inputs["Value"])
    dist = camd.outputs["View Distance"]
    far_t = T.node("ShaderNodeMapRange", -2300, 700)
    far_t.inputs["From Min"].default_value = 15.0
    far_t.inputs["From Max"].default_value = 60.0
    far_t.inputs["To Max"].default_value = 0.6
    T.link(dist, far_t.inputs["Value"])
    # biplanar weight (|Nx| > |Ny| -> project on the YZ plane) and steepness of the faces for Strata
    nsep = T.node("ShaderNodeSeparateXYZ", -2700, -400)
    T.link(geo.outputs["Normal"], nsep.inputs["Vector"])
    ax = T.math("ABSOLUTE", nsep.outputs["X"], x=-2550)
    ay = T.math("ABSOLUTE", nsep.outputs["Y"], x=-2550)
    bi = T.math("ADD", T.math("MULTIPLY", T.math("SUBTRACT", ax, ay, x=-2450), 4.0, x=-2350), 0.5, x=-2250)
    bi = T.math("MINIMUM", T.math("MAXIMUM", bi, 0.0, x=-2150), 1.0, x=-2050)
    steep = T.node("ShaderNodeMapRange", -2300, -600)
    steep.interpolation_type = "SMOOTHSTEP"
    import math as _m
    steep.inputs["From Min"].default_value = _m.cos(_m.radians(LS.STRATA_SLOPE_DEG[0]))
    steep.inputs["From Max"].default_value = _m.cos(_m.radians(LS.STRATA_SLOPE_DEG[1]))
    T.link(nsep.outputs["Z"], steep.inputs["Value"])
    osep = T.node("ShaderNodeSeparateXYZ", -2700, -200)
    T.link(tc.outputs["Object"], osep.inputs["Vector"])

    def mix_col(a, b, fac, x):
        n = T.node("ShaderNodeMix", x)
        n.data_type = "RGBA"
        T._in(n.inputs[0], fac)
        T.link(a, n.inputs[6])
        T.link(b, n.inputs[7])
        return n.outputs[2]

    def mix_f(a, b, fac, x):
        n = T.node("ShaderNodeMix", x)
        n.data_type = "FLOAT"
        T._in(n.inputs[0], fac)
        T._in(n.inputs[2], a)
        T._in(n.inputs[3], b)
        return n.outputs[0]

    layer_out = {}
    acc_c = acc_n = acc_r = acc_w = None
    for i, (lname, gname, weights) in enumerate(_layers()):
        tile = groundmod.MATERIALS[gname][0] * tile_scale
        T.y = -500 * i
        paths = {mp: os.path.join(ground_dir, "T_Ground_%s_%s.png" % (gname, mp)) for mp in ("BaseColor", "Normal", "Roughness", "Height")}
        cs = {"BaseColor": "sRGB", "Normal": "Non-Color", "Roughness": "Non-Color", "Height": "Non-Color"}

        def tex(mp, vec, x):
            t = T.node("ShaderNodeTexImage", x)
            t.image = _img(paths[mp], cs[mp])
            T.link(vec, t.inputs["Vector"])
            return t.outputs["Color"]

        res = {}
        if gname == "CutbankStrata":
            negz = T.math("MULTIPLY", osep.outputs["Z"], -1.0, x=-2200)
            vx = T.node("ShaderNodeCombineXYZ", -2100)
            T.link(osep.outputs["Y"], vx.inputs["X"])
            T.link(negz, vx.inputs["Y"])
            vy = T.node("ShaderNodeCombineXYZ", -2100)
            T.link(osep.outputs["X"], vy.inputs["X"])
            T.link(negz, vy.inputs["Y"])
            uvx = T.vmath("SCALE", vx.outputs["Vector"], x=-1950, scale=1.0 / tile)
            uvy = T.vmath("SCALE", vy.outputs["Vector"], x=-1950, scale=1.0 / tile)
            for mp in paths:
                res[mp] = mix_col(tex(mp, uvy, -1700), tex(mp, uvx, -1700), bi, -1400)
        else:
            uva = T.vmath("SCALE", tc.outputs["Object"], x=-2100, scale=1.0 / tile)
            mp_b = T.node("ShaderNodeMapping", -2000)
            mp_b.inputs["Rotation"].default_value = (0.0, 0.0, 0.6 + 0.9 * i)
            mp_b.inputs["Scale"].default_value = (1.0 / (tile * GOLDEN),) * 3
            mp_b.inputs["Location"].default_value = (0.37 * i, 0.61 * i, 0.0)
            T.link(tc.outputs["Object"], mp_b.inputs["Vector"])
            uvf = T.vmath("SCALE", tc.outputs["Object"], x=-2100, scale=1.0 / (tile * FAR_SCALE))
            for mp in paths:
                near = mix_col(tex(mp, uva, -1700), tex(mp, mp_b.outputs["Vector"], -1700), mix_ab.outputs["Result"], -1400)
                if mp == "BaseColor":
                    near = mix_col(near, tex(mp, uvf, -1700), far_t.outputs["Result"], -1300)
                res[mp] = near
        layer_out[lname] = res
        # weight = sum(mask * k) (* steepness for Strata, flat strata -> Silt) * (0.25 + height)^sharp
        w = None
        for mname, k in weights:
            if mname not in chan:
                continue
            term = T.math("MULTIPLY", chan[mname], float(k), x=-1200)
            w = term if w is None else T.math("ADD", w, term, x=-1100)
        if lname == "Strata" and w is not None:
            w = T.math("MULTIPLY", w, steep.outputs["Result"], x=-1050)
        if lname == "Silt" and "cutbank_cliff" in chan:
            flat = T.math("MULTIPLY", chan["cutbank_cliff"], T.math("SUBTRACT", 1.0, steep.outputs["Result"], x=-1150), x=-1100)
            w = flat if w is None else T.math("ADD", w, flat, x=-1050)
        if w is None:
            continue
        w = T.math("MAXIMUM", w, 0.0, x=-1000)
        hsep = T.node("ShaderNodeSeparateColor", -1150)
        T.link(res["Height"], hsep.inputs["Color"])
        hb = T.math("POWER", T.math("ADD", hsep.outputs["Red"], 0.25, x=-1000), sharp, x=-950)
        w = T.math("MULTIPLY", w, hb, x=-900)
        c = T.vmath("SCALE", res["BaseColor"], x=-700, scale=w)
        nrm = T.vmath("SCALE", res["Normal"], x=-700, scale=w)
        rsep = T.node("ShaderNodeSeparateColor", -800)
        T.link(res["Roughness"], rsep.inputs["Color"])
        r = T.math("MULTIPLY", rsep.outputs["Red"], w, x=-700)
        acc_c = c if acc_c is None else T.vmath("ADD", acc_c, c, x=-500)
        acc_n = nrm if acc_n is None else T.vmath("ADD", acc_n, nrm, x=-500)
        acc_r = r if acc_r is None else T.math("ADD", acc_r, r, x=-500)
        acc_w = w if acc_w is None else T.math("ADD", acc_w, w, x=-500)
    T.y = 600
    inv = T.math("DIVIDE", 1.0, T.math("MAXIMUM", acc_w, 1e-4, x=-300), x=-200)
    col = T.vmath("SCALE", acc_c, x=0, scale=inv)
    ncol = T.vmath("SCALE", acc_n, x=0, scale=inv)
    rough = T.math("MULTIPLY", acc_r, inv, x=0)
    # trails -> Mud layer (overlay after the blend, like the UE material)
    trail_layer = layer_out.get(LS.TRAIL_LAYER)
    if trail_layer and "trails" in chan:
        ta = T.math("MINIMUM", T.math("MULTIPLY", chan["trails"], STATIC_TRAILS, x=100), 1.0, x=150)
        col = mix_col(col, trail_layer["BaseColor"], ta, 200)
        ncol = mix_col(ncol, trail_layer["Normal"], ta, 200)
        tsep = T.node("ShaderNodeSeparateColor", 150)
        T.link(trail_layer["Roughness"], tsep.inputs["Color"])
        rough = mix_f(rough, tsep.outputs["Red"], ta, 200)
    # wetness = saturate(0.8 R + 0.6 shore): albedo x lerp(1, 0.55, wet), roughness -> 0.3 with 0.7 wet
    wet = None
    if "wetness" in chan:
        wet = T.math("MULTIPLY", chan["wetness"], STATIC_WETNESS, x=300)
    if "shore" in chan:
        sh = T.math("MULTIPLY", chan["shore"], SHORE_WETNESS, x=300)
        wet = sh if wet is None else T.math("ADD", wet, sh, x=350)
    if wet is not None:
        wet = T.math("MINIMUM", wet, 1.0, x=400)
        dark = T.math("SUBTRACT", 1.0, T.math("MULTIPLY", wet, 0.45, x=450), x=500)
        col = T.vmath("SCALE", col, x=550, scale=dark)
        rough = mix_f(rough, 0.3, T.math("MULTIPLY", wet, 0.7, x=500), 600)
    # far ground cover tint (culled small plants)
    if "groundcover" in chan:
        gdist = T.node("ShaderNodeMapRange", 600, 900)
        gdist.inputs["From Min"].default_value = 25.0
        gdist.inputs["From Max"].default_value = 65.0
        gdist.inputs["To Min"].default_value = 0.25
        gdist.inputs["To Max"].default_value = 0.85
        T.link(dist, gdist.inputs["Value"])
        gamt = T.math("MULTIPLY", chan["groundcover"], gdist.outputs["Result"], x=700)
        tint = T.node("ShaderNodeRGB", 700, 1000)
        tint.outputs[0].default_value = (*GROUND_COVER_TINT, 1.0)
        col = mix_col(col, tint.outputs[0], gamt, 800)
    # understorey shade (render-only, see docstring)
    if canopy_shade > 0:
        cw = None
        for hname, k in CANOPY.items():
            if hname in chan:
                term = T.math("MULTIPLY", chan[hname], float(k), x=850)
                cw = term if cw is None else T.math("ADD", cw, term, x=900)
        if cw is not None:
            cw = T.math("MINIMUM", cw, 1.0, x=950)
            shade = T.math("SUBTRACT", 1.0, T.math("MULTIPLY", cw, float(canopy_shade), x=1000), x=1050)
            col = T.vmath("SCALE", col, x=1100, scale=shade)
            # no sky sheen on the shaded litter: seen at grazing angles through porous crowns, a glossy floor
            # mirrors the bright horizon and sparkles (the real canopy would occlude that sky)
            rough = T.math("MAXIMUM", rough, cw, x=1150)
            spec_k = T.math("MULTIPLY", 0.4, shade, x=1150)
            T.link(spec_k, bsdf.inputs["Specular IOR Level"])
    # macro variation: independent low-frequency noise (0.82 + 0.36 R), as in the UE material
    mv = T.math("ADD", T.math("MULTIPLY", mn_sep.outputs["Red"], 0.36, x=1200), 0.82, x=1250)
    col = T.vmath("SCALE", col, x=1300, scale=mv)
    T.link(col, bsdf.inputs["Base Color"])
    T.link(rough, bsdf.inputs["Roughness"])
    # normal: DX -> GL (flip green) then normal map
    sep = T.node("ShaderNodeSeparateColor", 1400, 300)
    T.link(ncol, sep.inputs["Color"])
    g = T.math("SUBTRACT", 1.0, sep.outputs["Green"], x=1500)
    comb = T.node("ShaderNodeCombineColor", 1650, 300)
    T.link(sep.outputs["Red"], comb.inputs["Red"])
    T.link(g, comb.inputs["Green"])
    T.link(sep.outputs["Blue"], comb.inputs["Blue"])
    nm = T.node("ShaderNodeNormalMap", 1850, 300)
    nm.inputs["Strength"].default_value = 1.0
    T.link(comb.outputs["Color"], nm.inputs["Color"])
    T.link(nm.outputs["Normal"], bsdf.inputs["Normal"])
    if not bsdf.inputs["Specular IOR Level"].is_linked:
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
