# -*- coding: utf-8 -*-
"""Noctis — landscape material and paint layers of the Hell Creek valley (UE 5.4+), from Tools/EnvironmentFactory.

Creates (or refreshes with --overwrite):
  /Game/Noctis/Landscape/M_NoctisLandscape     the path build_level.py assigns to the landscape (LANDSCAPE_MATERIAL)
  /Game/Noctis/Landscape/Layers/LI_<Layer>     LandscapeLayerInfoObject per paint layer (weight-blended)
  /Game/Noctis/Landscape/T_LandscapeMasks      RGBA world mask: R static wetness, G predicted corridors,
                                              B flood risk, A water (Build/Environment/landscape/landscape_masks.png)
  /Game/Noctis/Landscape/T_LandscapeMasks2     RGBA: R ground cover (small plants), G shore wetness band,
                                              B, A reserved (Build/Environment/landscape/landscape_masks2.png)
and, when a landscape exists in the open level, assigns the material and tries to import the 9 paint layers
(Build/Environment/landscape/layer_<Layer>.png, 2017 x 2017 = heightmap resolution, row 0 = north edge).

Material (Docs/ENVIRONMENT.md §3):
  * 9 layers from the simulation habitats: Gravel (channel), Mud (riverbank), Sand (sandbar), Backswamp,
    LitterBroadleaf (riparian forest + levee), Silt (damp floodplain + levee), SiltDry (desiccation cracks, driest
    open-floodplain hollows only), LitterConifer (upland, speculative habitat), Strata (cutbank faces, slopes > 20-28°).
    Each layer = one ground texture set (2K, tileable, 4-6 m tiles), shared wrap samplers (no 16-sampler limit),
    height-blended Landscape Layer Blend for colour, normal and roughness.
  * Anti-tiling (every layer but Strata): two samples per map, the second rotated by a per-layer angle and scaled by
    1.618, blended by an independent low-frequency noise (T_Ground_MacroNoise.G); the albedo also fades to a third,
    4.3x larger sample with the camera distance (15-60 m). No discrete feature (tracks, big cracks) is baked in the
    tiling sets any more: trackways are mesh decals (import_environment.py), strong cracks are the SiltDry layer.
  * Strata: biplanar world projection, U = world X or Y (the horizontal axis most parallel to the face, from the
    landscape normal), V = -world Z / tile: beds follow the elevation (contours), not a top-down projection.
  * Moisture: wetness = saturate(R * StaticWetness + MPC_NoctisWeather.GroundWetness * (0.3 + 0.7 R)
    + B * MPC.Flooding + 0.6 * ShoreBand) darkens the albedo and lowers roughness (ShoreBand = masks2.G).
  * Trails: max(G * StaticTrails, RT_NoctisTrails.R (runtime wear, rect MPC.TrailMaskRect)) blends towards the
    Mud layer (trampled mud; footprints come from the trackway decals).
  * Far ground cover: the culled small plants (ferns, horsetails, herbs, shrubs: masks2.R) still colour the ground
    with a green-brown tint that grows with the camera distance (25-65 m).
  * Macro variation: independent low-frequency noise (T_Ground_MacroNoise.R, 240 m tile) modulates the albedo.

Run inside the editor:  py "Content/Python/setup_landscape_material.py" [--overwrite] [--skip layers,assign]
Run outside Unreal:     python Content/Python/setup_landscape_material.py --dry-run
Order: setup_project.py -> import_environment.py (ground textures) -> setup_landscape_material.py -> build_level.py.
Lines marked  # VERIFY  could not be checked against a running editor; each failure is logged with a manual fallback.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

try:
    import unreal  # noqa: F401
    IN_UNREAL = True
except ImportError:
    unreal = None
    IN_UNREAL = False

LANDSCAPE_ROOT = "/Game/Noctis/Landscape"
MATERIAL_PATH = LANDSCAPE_ROOT + "/M_NoctisLandscape"   # == build_level.LANDSCAPE_MATERIAL
LAYER_ROOT = LANDSCAPE_ROOT + "/Layers"
MASKS_PATH = LANDSCAPE_ROOT + "/T_LandscapeMasks"
MASKS2_PATH = LANDSCAPE_ROOT + "/T_LandscapeMasks2"
MACRO_NOISE_PATH = "/Game/Environment/Textures/Ground/T_Ground_MacroNoise"  # import_environment.py
GROUND_TEX_ROOT = "/Game/Environment/Textures/Ground"   # import_environment.py
MPC_WEATHER = "/Game/Environment/MPC_NoctisWeather"
RT_TRAILS = "/Game/Environment/Vegetation/RT_NoctisTrails"

LOG = []


def log(msg, level="INFO"):
    line = "[NoctisLandscape] %s %s" % (level, msg)
    LOG.append(line)
    if IN_UNREAL:
        {"INFO": unreal.log, "WARN": unreal.log_warning, "ERROR": unreal.log_error}[level](line)
    else:
        print(line)


def repo_root():
    if IN_UNREAL:
        return os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def build_level_constants():
    """Reads LANDSCAPE_MATERIAL from build_level.py (same folder) so both scripts agree on the path."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import build_level  # noqa: F401  (module import is side-effect free: main() is guarded)
        return getattr(build_level, "LANDSCAPE_MATERIAL", MATERIAL_PATH)
    except Exception as exc:
        log("build_level.py non importable (%s) : chemin par défaut %s" % (exc, MATERIAL_PATH), "WARN")
        return MATERIAL_PATH


def set_props(obj, **props):
    for k, v in props.items():
        try:
            obj.set_editor_property(k, v)
        except Exception as exc:
            log("%s.%s non appliqué (%s) — VERIFY" % (obj.get_name(), k, exc), "WARN")


def eal():
    return unreal.EditorAssetLibrary


def ensure_dir(p):
    if not eal().does_directory_exist(p):
        eal().make_directory(p)


# ----------------------------------------------------------------------------------------------- material graph
GOLDEN = 1.618
FAR_SCALE = 4.3


def build_material(mat, man, size_cm):
    mel = unreal.MaterialEditingLibrary
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)  # noqa: E731
    mel.delete_all_material_expressions(mat)
    layers = man["landscape"]["layers"]
    grounds = man["ground_materials"]
    trail_layer = man["landscape"].get("trail_layer", "Mud")
    shared = unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS

    def op(cls, a, b, x, y, pa="", pb=""):
        n = E(cls, x, y)
        if isinstance(a, float):
            n.set_editor_property("const_a", a)
        else:
            mel.connect_material_expressions(a, pa, n, "A")
        if isinstance(b, float):
            n.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(b, pb, n, "B")
        return n

    def mask(src, x, y, r=False, g=False, b=False, a=False, pin=""):
        n = E(unreal.MaterialExpressionComponentMask, x, y)
        set_props(n, r=r, g=g, b=b, a=a)
        mel.connect_material_expressions(src, pin, n, "")
        return n

    def const2(u, v, x, y):
        n = E(unreal.MaterialExpressionConstant2Vector, x, y)
        set_props(n, r=u, g=v)
        return n

    def scalar(name, v, x, y):
        n = E(unreal.MaterialExpressionScalarParameter, x, y)
        set_props(n, parameter_name=name, default_value=v)
        return n

    def lerp(a, b, alpha, x, y, pa="", pb="", palpha=""):
        n = E(unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, pa, n, "A")
        mel.connect_material_expressions(b, pb, n, "B")
        mel.connect_material_expressions(alpha, palpha, n, "Alpha")
        return n

    wp = E(unreal.MaterialExpressionWorldPosition, -4200, 0)
    wpxy = mask(wp, -4050, 0, r=True, g=True)
    wpz = mask(wp, -4050, 120, b=True)
    # camera distance (cm) for the far blends
    cam = E(unreal.MaterialExpressionCameraPositionWS, -4200, 250)
    dist = E(unreal.MaterialExpressionDistance, -4050, 250)
    mel.connect_material_expressions(wp, "", dist, "A")
    mel.connect_material_expressions(cam, "", dist, "B")
    far_t = E(unreal.MaterialExpressionSaturate, -3700, 250)
    mel.connect_material_expressions(op(unreal.MaterialExpressionDivide, op(unreal.MaterialExpressionSubtract, dist, 1500.0, -3900, 250),
                                        4500.0, -3800, 250), "", far_t, "")
    # independent macro noise (R albedo variation, G anti-tiling mix, B tint)
    macro = E(unreal.MaterialExpressionTextureSampleParameter2D, -3700, -3200)
    set_props(macro, parameter_name="MacroNoise", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, sampler_source=shared)
    if eal().does_asset_exist(MACRO_NOISE_PATH):
        macro.set_editor_property("texture", eal().load_asset(MACRO_NOISE_PATH))
    else:
        log("Texture absente : %s (lancer import_environment.py)" % MACRO_NOISE_PATH, "WARN")
    macro_tile = float((man.get("decals") or {}).get("macro_noise", {}).get("tile_m", 240.0)) * 100.0
    mel.connect_material_expressions(op(unreal.MaterialExpressionDivide, wpxy, macro_tile, -3900, -3200), "", macro, "UVs")
    mixmask = E(unreal.MaterialExpressionSmoothStep, -3450, -3150)  # VERIFY node (UMaterialExpressionSmoothStep, 5.0+)
    set_props(mixmask, const_min=0.35, const_max=0.65)
    mel.connect_material_expressions(macro, "G", mixmask, "Value")
    # landscape normal for the biplanar strata projection
    vn = E(unreal.MaterialExpressionVertexNormalWS, -4200, 500)
    nx = E(unreal.MaterialExpressionAbs, -4000, 500)
    mel.connect_material_expressions(mask(vn, -4100, 500, r=True), "", nx, "")
    ny = E(unreal.MaterialExpressionAbs, -4000, 600)
    mel.connect_material_expressions(mask(vn, -4100, 600, g=True), "", ny, "")
    bi_w = E(unreal.MaterialExpressionSaturate, -3700, 550)
    mel.connect_material_expressions(op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply,
                                                                          op(unreal.MaterialExpressionSubtract, nx, ny, -3900, 550), 4.0, -3800, 550),
                                        0.5, -3750, 550), "", bi_w, "")
    negz = op(unreal.MaterialExpressionMultiply, wpz, -1.0, -3900, 700)

    blends = {}
    for k, prop in (("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR), ("Normal", unreal.MaterialProperty.MP_NORMAL),
                    ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS)):
        lb = E(unreal.MaterialExpressionLandscapeLayerBlend, -600, {"BaseColor": -600, "Normal": 0, "Roughness": 600}[k])
        entries = []
        for L in layers:
            e = unreal.LayerBlendInput()  # VERIFY struct name (FLayerBlendInput)
            set_props(e, layer_name=L["name"], blend_type=unreal.LandscapeLayerBlendType.LB_HEIGHT_BLEND,
                      preview_weight=1.0 if L["name"] == "Silt" else 0.0)
            entries.append(e)
        lb.set_editor_property("layers", entries)
        blends[k] = (lb, prop)
    MAPS = (("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "RGB"),
            ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "RGB"),
            ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "R"),
            ("Height", unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE, "R"))
    samples = {}  # (layer, map) -> (node, pin) of the final (blended) value
    for i, L in enumerate(layers):
        g = grounds[L["ground_material"]]
        tile = float(g["tile_m"]) * 100.0
        y = -3000 + i * 1100
        tex_path = lambda mp: "%s/T_Ground_%s_%s" % (GROUND_TEX_ROOT, L["ground_material"], mp)  # noqa: E731

        def sample(mp, st, uv, x, yy):
            t = E(unreal.MaterialExpressionTextureSampleParameter2D, x, yy)
            # same parameter name on every sample of a map: one texture parameter per layer and map
            set_props(t, parameter_name="%s_%s" % (L["name"], mp), sampler_type=st, sampler_source=shared)
            if eal().does_asset_exist(tex_path(mp)):
                t.set_editor_property("texture", eal().load_asset(tex_path(mp)))
            else:
                log("Texture absente : %s (lancer import_environment.py)" % tex_path(mp), "WARN")
            mel.connect_material_expressions(uv, "", t, "UVs")
            return t

        if L["ground_material"] == "CutbankStrata":
            # biplanar: (Y, -Z) on faces looking along X, (X, -Z) on faces looking along Y; V follows the elevation
            wpx = mask(wp, -3300, y, r=True)
            wpy = mask(wp, -3300, y + 80, g=True)
            ax = E(unreal.MaterialExpressionAppendVector, -3150, y)
            mel.connect_material_expressions(wpy, "", ax, "A")
            mel.connect_material_expressions(negz, "", ax, "B")
            ay = E(unreal.MaterialExpressionAppendVector, -3150, y + 120)
            mel.connect_material_expressions(wpx, "", ay, "A")
            mel.connect_material_expressions(negz, "", ay, "B")
            uv_x = op(unreal.MaterialExpressionDivide, ax, tile, -3000, y)
            uv_y = op(unreal.MaterialExpressionDivide, ay, tile, -3000, y + 120)
            for j, (mp, st, pin) in enumerate(MAPS):
                sx = sample(mp, st, uv_x, -2700, y + j * 200)
                sy = sample(mp, st, uv_y, -2500, y + j * 200)
                samples[(L["name"], mp)] = (lerp(sy, sx, bi_w, -2200, y + j * 200, pin, pin), "")
            continue
        # anti-tiling: A = world XY / tile ; B = rotated by a per-layer angle, scaled by 1.618 ; far = x 4.3
        th = 0.6 + 0.9 * i
        c_, s_ = math.cos(th), math.sin(th)
        uv_a = op(unreal.MaterialExpressionDivide, wpxy, tile, -3300, y)
        du = E(unreal.MaterialExpressionDotProduct, -3300, y + 100)
        mel.connect_material_expressions(wpxy, "", du, "A")
        mel.connect_material_expressions(const2(c_, -s_, -3450, y + 100), "", du, "B")
        dv = E(unreal.MaterialExpressionDotProduct, -3300, y + 180)
        mel.connect_material_expressions(wpxy, "", dv, "A")
        mel.connect_material_expressions(const2(s_, c_, -3450, y + 180), "", dv, "B")
        rot = E(unreal.MaterialExpressionAppendVector, -3150, y + 140)
        mel.connect_material_expressions(du, "", rot, "A")
        mel.connect_material_expressions(dv, "", rot, "B")
        uv_b = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionDivide, rot, tile * GOLDEN, -3000, y + 140),
                  const2(0.37 * i, 0.61 * i, -3000, y + 240), -2900, y + 160)
        uv_f = op(unreal.MaterialExpressionDivide, wpxy, tile * FAR_SCALE, -3000, y + 300)
        for j, (mp, st, pin) in enumerate(MAPS):
            sa = sample(mp, st, uv_a, -2700, y + j * 200)
            sb = sample(mp, st, uv_b, -2500, y + j * 200)
            near = lerp(sa, sb, mixmask, -2200, y + j * 200, pin, pin)
            if mp == "BaseColor":
                sf = sample(mp, st, uv_f, -2500, y + 900)
                ft = op(unreal.MaterialExpressionMultiply, far_t, 0.6, -2350, y + 900)
                near = lerp(near, sf, ft, -2050, y + 850, "", pin)
            samples[(L["name"], mp)] = (near, "")
    for L in layers:
        for k, (lb, _) in blends.items():
            node, pin = samples[(L["name"], k)]
            mel.connect_material_expressions(node, pin, lb, "Layer " + L["name"])  # VERIFY pin names
            hn, hp = samples[(L["name"], "Height")]
            mel.connect_material_expressions(hn, hp, lb, "Height " + L["name"])
    # world masks (wetness, corridors, flood, water) and masks2 (ground cover, shore band, sand variation)
    rect = E(unreal.MaterialExpressionVectorParameter, -2650, 3400)
    set_props(rect, parameter_name="LandscapeRectCm", default_value=unreal.LinearColor(0.0, -size_cm, size_cm, size_cm))
    rxy = mask(rect, -2450, 3400, r=True, g=True)
    rzw = mask(rect, -2450, 3500, b=True, a=True)
    uv = op(unreal.MaterialExpressionDivide, op(unreal.MaterialExpressionSubtract, wpxy, rxy, -2300, 3400), rzw, -2150, 3400)

    def world_mask(name, path, yy):
        t = E(unreal.MaterialExpressionTextureSampleParameter2D, -1950, yy)
        set_props(t, parameter_name=name, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        if eal().does_asset_exist(path):
            t.set_editor_property("texture", eal().load_asset(path))
        mel.connect_material_expressions(uv, "", t, "UVs")
        return t
    masks = world_mask("LandscapeMasks", MASKS_PATH, 3400)
    masks2 = world_mask("LandscapeMasks2", MASKS2_PATH, 3650)
    mpc = eal().load_asset(MPC_WEATHER) if eal().does_asset_exist(MPC_WEATHER) else None

    def mpc_param(name, x, y):
        if mpc is None:
            return E(unreal.MaterialExpressionConstant, x, y)
        n = E(unreal.MaterialExpressionCollectionParameter, x, y)
        set_props(n, collection=mpc, parameter_name=name)  # VERIFY
        return n

    # wetness = saturate(R * StaticWetness + GroundWetness * (0.3 + 0.7 R) + B * Flooding + ShoreWetness * masks2.G)
    sw = scalar("StaticWetness", 0.8, -1700, 3600)
    w1 = op(unreal.MaterialExpressionMultiply, masks, sw, -1550, 3600, "R")
    gw = mpc_param("GroundWetness", -1700, 3750)
    r07b = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, masks, 0.7, -1700, 3850, "R"), 0.3, -1550, 3850)
    w2 = op(unreal.MaterialExpressionMultiply, gw, r07b, -1400, 3750)
    fl = mpc_param("Flooding", -1700, 3950)
    w3 = op(unreal.MaterialExpressionMultiply, masks, fl, -1550, 3950, "B")
    w4 = op(unreal.MaterialExpressionMultiply, masks2, scalar("ShoreWetness", 0.6, -1700, 4050), -1550, 4050, "G")
    wsum = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionAdd, w1, w2, -1250, 3650), w3, -1150, 3700),
              w4, -1050, 3750)
    wet = E(unreal.MaterialExpressionSaturate, -950, 3700)
    mel.connect_material_expressions(wsum, "", wet, "")
    # trails = max(G * StaticTrails, RT_NoctisTrails.R)
    st = scalar("StaticTrails", 0.6, -1700, 4250)
    trail = op(unreal.MaterialExpressionMultiply, masks, st, -1550, 4250, "G")
    if eal().does_asset_exist(RT_TRAILS) and mpc is not None:
        tr_rect = mpc_param("TrailMaskRect", -2300, 4400)
        a_xy = mask(tr_rect, -2150, 4400, r=True, g=True)
        a_zw = mask(tr_rect, -2150, 4500, b=True, a=True)
        tuv = op(unreal.MaterialExpressionDivide, op(unreal.MaterialExpressionSubtract, wpxy, a_xy, -2000, 4400), a_zw, -1850, 4400)
        rt = E(unreal.MaterialExpressionTextureSampleParameter2D, -1700, 4400)
        set_props(rt, parameter_name="TrailRuntime", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rt.set_editor_property("texture", eal().load_asset(RT_TRAILS))  # VERIFY: render target as sampler texture
        mel.connect_material_expressions(tuv, "", rt, "UVs")
        trail = op(unreal.MaterialExpressionMax, trail, rt, -1400, 4300, "", "R")
    trail_s = E(unreal.MaterialExpressionSaturate, -1250, 4300)
    mel.connect_material_expressions(trail, "", trail_s, "")
    # combine: trail mud, wetness, far ground cover, macro variation
    outs = {}
    for k, (lb, prop) in blends.items():
        yy = {"BaseColor": -600, "Normal": 0, "Roughness": 600}[k]
        lerp_n = E(unreal.MaterialExpressionLinearInterpolate, -300, yy)
        mel.connect_material_expressions(lb, "", lerp_n, "A")
        if (trail_layer, k) in samples:
            node, pin = samples[(trail_layer, k)]
            mel.connect_material_expressions(node, pin, lerp_n, "B")
        mel.connect_material_expressions(trail_s, "", lerp_n, "Alpha")
        outs[k] = lerp_n
    dark = E(unreal.MaterialExpressionLinearInterpolate, -100, -600)
    dark.set_editor_property("const_a", 1.0)
    dark.set_editor_property("const_b", 0.55)
    mel.connect_material_expressions(wet, "", dark, "Alpha")
    bc = op(unreal.MaterialExpressionMultiply, outs["BaseColor"], dark, 50, -600)
    # far ground cover: tint = masks2.R * lerp(0.25, 0.85, saturate((dist - 2500) / 4000))
    gct = E(unreal.MaterialExpressionVectorParameter, -300, -900)
    set_props(gct, parameter_name="GroundCoverTint", default_value=unreal.LinearColor(0.055, 0.07, 0.028, 1.0))
    gd = E(unreal.MaterialExpressionSaturate, -500, -1000)
    mel.connect_material_expressions(op(unreal.MaterialExpressionDivide, op(unreal.MaterialExpressionSubtract, dist, 2500.0, -700, -1000),
                                        4000.0, -600, -1000), "", gd, "")
    gdl = E(unreal.MaterialExpressionLinearInterpolate, -350, -1000)
    gdl.set_editor_property("const_a", 0.25)
    gdl.set_editor_property("const_b", 0.85)
    mel.connect_material_expressions(gd, "", gdl, "Alpha")
    gamt = op(unreal.MaterialExpressionMultiply, masks2, gdl, -200, -1000, "R")
    bc_gc = lerp(bc, gct, gamt, 120, -800)
    # macro variation: independent low-frequency noise
    mv = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, macro, 0.36, -3450, -3000, "R"), 0.82, -3300, -3000)
    bc2 = op(unreal.MaterialExpressionMultiply, bc_gc, mv, 250, -600)
    mel.connect_material_property(bc2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(outs["Normal"], "", unreal.MaterialProperty.MP_NORMAL)
    rwet = E(unreal.MaterialExpressionLinearInterpolate, 100, 600)
    mel.connect_material_expressions(outs["Roughness"], "", rwet, "A")
    rwet.set_editor_property("const_b", 0.3)  # wet mud is satin, not a mirror (0.12 read as a grey-blue sheet of sky)
    mel.connect_material_expressions(op(unreal.MaterialExpressionMultiply, wet, 0.7, -100, 700), "", rwet, "Alpha")
    mel.connect_material_property(rwet, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)


# ----------------------------------------------------------------------------------------------- stages
def import_masks(path, asset_path=MASKS_PATH):
    t = unreal.AssetImportTask()
    for k, v in (("filename", path), ("destination_path", LANDSCAPE_ROOT), ("destination_name", asset_path.rsplit("/", 1)[1]),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = eal().load_asset(asset_path)
    set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
              mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS, address_x=unreal.TextureAddress.TA_CLAMP,
              address_y=unreal.TextureAddress.TA_CLAMP)
    eal().save_asset(asset_path, only_if_is_dirty=False)
    return tex


def layer_infos(man):
    ensure_dir(LAYER_ROOT)
    out = {}
    for L in man["landscape"]["layers"]:
        p = "%s/LI_%s" % (LAYER_ROOT, L["name"])
        li = eal().load_asset(p) if eal().does_asset_exist(p) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "LI_" + L["name"], LAYER_ROOT, unreal.LandscapeLayerInfoObject, None)  # VERIFY: no factory needed
        if li is None:
            log("LI_%s : à créer depuis le panneau Paint (Target Layers > +)" % L["name"], "WARN")
            continue
        set_props(li, layer_name=L["name"])
        eal().save_loaded_asset(li)
        out[L["name"]] = li
    return out


def find_landscape():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() == "Landscape":
            return a
    return None


def assign_and_import(man, root, mat, infos):
    land = find_landscape()
    if land is None:
        log("Aucun paysage dans le niveau : build_level.py assignera %s ; couches à importer ensuite :" % MATERIAL_PATH, "WARN")
        print_layer_steps(man, root)
        return
    land.set_editor_property("landscape_material", mat)
    log("Matériau assigné au paysage %s" % land.get_actor_label())
    helper = getattr(unreal, "NoctisEditorLibrary", None)
    if helper is not None and hasattr(helper, "import_landscape_weightmap"):
        # Project helper (same pattern as build_level's import_landscape_from_heightmap): expected signature
        # import_landscape_weightmap(landscape, layer_info, png_path) -> bool — not implemented in the C++ module yet.
        ok = 0
        for L in man["landscape"]["layers"]:
            try:
                ok += bool(helper.import_landscape_weightmap(land, infos.get(L["name"]), os.path.join(root, L["weightmap"])))
            except Exception as exc:
                log("Couche %s : %s" % (L["name"], exc), "WARN")
        log("%d/%d couches importées par NoctisEditorLibrary" % (ok, len(man["landscape"]["layers"])))
        if ok == len(man["landscape"]["layers"]):
            return
    # UE 5.4 exposes no stable Python call that imports a weightmap file into a paint layer: manual steps.
    print_layer_steps(man, root)


def print_layer_steps(man, root):
    log("Landscape Mode > Paint > Target Layers : créer chaque couche avec son LayerInfo (%s/LI_<Couche>), puis clic droit > Import from file :" % LAYER_ROOT, "WARN")
    for L in man["landscape"]["layers"]:
        log("  %-16s <- %s   (%s)" % (L["name"], os.path.join(root, L["weightmap"]), L["ground_material"]), "WARN")


def main(argv=None):
    root = repo_root()
    p = argparse.ArgumentParser(description="Noctis landscape material and paint layers.")
    p.add_argument("--manifest", default=os.path.join(root, "Build", "Environment", "manifest.json"))
    p.add_argument("--overwrite", action="store_true")
    p.add_argument("--skip", default="")
    p.add_argument("--dry-run", action="store_true")
    a = p.parse_args(sys.argv[1:] if argv is None else argv)
    skip = set(x for x in a.skip.split(",") if x)
    if not IN_UNREAL:
        a.dry_run = True
    if not os.path.exists(a.manifest):
        log("Manifeste absent : %s (Tools/EnvironmentFactory/factory.py --stages manifest)" % a.manifest, "ERROR")
        return 1
    with open(a.manifest, encoding="utf-8") as f:
        man = json.load(f)
    target = build_level_constants()
    if target != MATERIAL_PATH:
        log("build_level.LANDSCAPE_MATERIAL = %s diffère de %s : le chemin de build_level.py est utilisé" % (target, MATERIAL_PATH), "WARN")
    size_cm = float(man["simulation"]["terrain"]["size_m"]) * 100.0 if man.get("simulation", {}).get("terrain") else 819200.0
    lay = man["landscape"]
    missing = [L["weightmap"] for L in lay["layers"] if not os.path.exists(os.path.join(root, L["weightmap"]))]
    log("Couches : %s ; masques %s ; taille %.0f cm%s" % (", ".join("%s<-%s" % (L["name"], L["ground_material"]) for L in lay["layers"]),
                                                       lay["masks"]["file"], size_cm, ("  MANQUANT: %s" % missing) if missing else ""))
    if a.dry_run:
        for L in lay["layers"]:
            g = man["ground_materials"][L["ground_material"]]
            log("[simulation] couche %-16s habitats %s, tuile %.1f m, textures %s" % (L["name"], L["habitats"], g["tile_m"], ", ".join(g["textures"])))
        log("[simulation] matériau %s : Landscape Layer Blend (hauteur) x3 ; anti-répétition (2e échantillon tourné / x1,618 mélangé "
            "par T_Ground_MacroNoise.G, albédo lointain x4,3) ; Strata en projection biplanaire (V = altitude) ; humidité "
            "(R + MPC.GroundWetness + B*MPC.Flooding + bande de rive masks2.G) ; corridors (G, RT_NoctisTrails) -> couche %s ; "
            "teinte de couvert bas lointain (masks2.R) ; variation macro indépendante (MacroNoise.R)" % (target, lay.get("trail_layer", "Mud")))
        for L in lay["layers"]:
            if L.get("rule"):
                log("[simulation] couche %s : règle %s" % (L["name"], L["rule"]))
        return 0
    ensure_dir(LANDSCAPE_ROOT)
    mpath = os.path.join(root, lay["masks"]["file"])
    if os.path.exists(mpath):
        import_masks(mpath)
    m2 = (lay.get("masks2") or {}).get("file")
    if m2 and os.path.exists(os.path.join(root, m2)):
        import_masks(os.path.join(root, m2), MASKS2_PATH)
    existing = eal().load_asset(target) if eal().does_asset_exist(target) else None
    if existing is None or a.overwrite:
        folder, name = target.rsplit("/", 1)
        ensure_dir(folder)
        mat = existing or unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        try:
            build_material(mat, man, size_cm)
            log("Matériau construit : %s" % target)
        except Exception as exc:
            import traceback
            log("Construction du graphe interrompue (%s) — compléter à la main selon Docs/ENVIRONMENT.md §3" % exc, "ERROR")
            unreal.log_error(traceback.format_exc())
        eal().save_loaded_asset(mat)
    else:
        mat = existing
        log("Matériau existant conservé : %s (--overwrite pour le reconstruire)" % target)
    infos = layer_infos(man) if "layers" not in skip else {}
    if "assign" not in skip:
        assign_and_import(man, root, mat, infos)
    return 0 if not any(" ERROR " in l for l in LOG) else 1


if __name__ == "__main__":
    status = main()
    if not IN_UNREAL:
        sys.exit(status)
