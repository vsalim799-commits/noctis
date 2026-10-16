# -*- coding: utf-8 -*-
"""Noctis — landscape material and paint layers of the Hell Creek valley (UE 5.4+), from Tools/EnvironmentFactory.

Creates (or refreshes with --overwrite):
  /Game/Noctis/Landscape/M_NoctisLandscape     the path build_level.py assigns to the landscape (LANDSCAPE_MATERIAL)
  /Game/Noctis/Landscape/Layers/LI_<Layer>     LandscapeLayerInfoObject per paint layer (weight-blended)
  /Game/Noctis/Landscape/T_LandscapeMasks      RGBA world mask: R static wetness, G predicted corridors,
                                              B flood risk, A water (Build/Environment/landscape/landscape_masks.png)
and, when a landscape exists in the open level, assigns the material and tries to import the 8 paint layers
(Build/Environment/landscape/layer_<Layer>.png, 2017 x 2017 = heightmap resolution, row 0 = north edge).

Material (Docs/ENVIRONMENT.md §3):
  * 8 layers from the simulation habitats: Gravel (channel), MudTracks (riverbank), Sand (sandbar), Backswamp,
    LitterBroadleaf (riparian forest + levee), Silt (floodplain + levee), LitterConifer (upland, speculative habitat),
    Strata (cutbank cliffs). Each layer = one ground texture set (2K, tileable), world-aligned UVs (tile size from the
    manifest), shared wrap samplers (no 16-sampler limit), height-blended Landscape Layer Blend for colour, normal
    and roughness.
  * Moisture: wetness = saturate(R * StaticWetness + MPC_NoctisWeather.GroundWetness * (0.3 + 0.7 R)
    + B * MPC.Flooding) darkens the albedo and lowers roughness.
  * Trails: max(G * StaticTrails, RT_NoctisTrails.R (runtime wear, rect MPC.TrailMaskRect)) blends towards the
    MudTracks layer (trampled, footprinted mud).
  * Macro variation: a very low-frequency sample of the silt height map modulates the albedo (anti-tiling).

Run inside the editor:  py "Content/Python/setup_landscape_material.py" [--overwrite] [--skip layers,assign]
Run outside Unreal:     python Content/Python/setup_landscape_material.py --dry-run
Order: setup_project.py -> import_environment.py (ground textures) -> setup_landscape_material.py -> build_level.py.
Lines marked  # VERIFY  could not be checked against a running editor; each failure is logged with a manual fallback.
"""
from __future__ import annotations

import argparse
import json
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
def build_material(mat, man, size_cm):
    mel = unreal.MaterialEditingLibrary
    E = lambda cls, x, y: mel.create_material_expression(mat, cls, x, y)  # noqa: E731
    mel.delete_all_material_expressions(mat)
    layers = man["landscape"]["layers"]
    grounds = man["ground_materials"]
    wp = E(unreal.MaterialExpressionWorldPosition, -3000, 0)
    wpxy = E(unreal.MaterialExpressionComponentMask, -2850, 0)
    set_props(wpxy, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(wp, "", wpxy, "")
    shared = unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS
    blends = {}
    for k, prop in (("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR), ("Normal", unreal.MaterialProperty.MP_NORMAL),
                    ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS)):
        lb = E(unreal.MaterialExpressionLandscapeLayerBlend, -600, {"BaseColor": -600, "Normal": 0, "Roughness": 600}[k])
        entries = []
        for i, L in enumerate(layers):
            e = unreal.LayerBlendInput()  # VERIFY struct name (FLayerBlendInput)
            set_props(e, layer_name=L["name"], blend_type=unreal.LandscapeLayerBlendType.LB_HEIGHT_BLEND,
                      preview_weight=1.0 if L["name"] == "Silt" else 0.0)
            entries.append(e)
        lb.set_editor_property("layers", entries)
        blends[k] = (lb, prop)
    samples = {}
    for i, L in enumerate(layers):
        g = grounds[L["ground_material"]]
        y = -2400 + i * 700
        scale = E(unreal.MaterialExpressionDivide, -2650, y)
        mel.connect_material_expressions(wpxy, "", scale, "A")
        scale.set_editor_property("const_b", float(g["tile_m"]) * 100.0)
        for j, (mp, st) in enumerate((("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
                                      ("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                                      ("Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS),
                                      ("Height", unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE))):
            t = E(unreal.MaterialExpressionTextureSampleParameter2D, -2300, y + j * 160)
            set_props(t, parameter_name="%s_%s" % (L["name"], mp), sampler_type=st, sampler_source=shared)
            tp = "%s/T_Ground_%s_%s" % (GROUND_TEX_ROOT, L["ground_material"], mp)
            if eal().does_asset_exist(tp):
                t.set_editor_property("texture", eal().load_asset(tp))
            else:
                log("Texture absente : %s (lancer import_environment.py)" % tp, "WARN")
            mel.connect_material_expressions(scale, "", t, "UVs")
            samples[(L["name"], mp)] = t
        for k, (lb, _) in blends.items():
            out = "RGB" if k != "Roughness" else "R"
            mel.connect_material_expressions(samples[(L["name"], k)], out, lb, "Layer " + L["name"])  # VERIFY pin names
            mel.connect_material_expressions(samples[(L["name"], "Height")], "R", lb, "Height " + L["name"])
    # world mask (wetness, corridors, flood, water)
    rect = E(unreal.MaterialExpressionVectorParameter, -2650, 3400)
    set_props(rect, parameter_name="LandscapeRectCm", default_value=unreal.LinearColor(0.0, -size_cm, size_cm, size_cm))
    rxy = E(unreal.MaterialExpressionComponentMask, -2450, 3400)
    set_props(rxy, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(rect, "", rxy, "")
    rzw = E(unreal.MaterialExpressionComponentMask, -2450, 3500)
    set_props(rzw, r=False, g=False, b=True, a=True)
    mel.connect_material_expressions(rect, "", rzw, "")
    sub = E(unreal.MaterialExpressionSubtract, -2300, 3400)
    mel.connect_material_expressions(wpxy, "", sub, "A")
    mel.connect_material_expressions(rxy, "", sub, "B")
    uv = E(unreal.MaterialExpressionDivide, -2150, 3400)
    mel.connect_material_expressions(sub, "", uv, "A")
    mel.connect_material_expressions(rzw, "", uv, "B")
    masks = E(unreal.MaterialExpressionTextureSampleParameter2D, -1950, 3400)
    set_props(masks, parameter_name="LandscapeMasks", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    if eal().does_asset_exist(MASKS_PATH):
        masks.set_editor_property("texture", eal().load_asset(MASKS_PATH))
    mel.connect_material_expressions(uv, "", masks, "UVs")
    mpc = eal().load_asset(MPC_WEATHER) if eal().does_asset_exist(MPC_WEATHER) else None

    def mpc_param(name, x, y):
        if mpc is None:
            c = E(unreal.MaterialExpressionConstant, x, y)
            return c
        n = E(unreal.MaterialExpressionCollectionParameter, x, y)
        set_props(n, collection=mpc, parameter_name=name)  # VERIFY
        return n

    def scalar(name, v, x, y):
        n = E(unreal.MaterialExpressionScalarParameter, x, y)
        set_props(n, parameter_name=name, default_value=v)
        return n

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

    # wetness = saturate(R * StaticWetness + GroundWetness * (0.3 + 0.7 R) + B * Flooding)
    sw = scalar("StaticWetness", 0.8, -1700, 3600)
    w1 = op(unreal.MaterialExpressionMultiply, masks, sw, -1550, 3600, "R")
    gw = mpc_param("GroundWetness", -1700, 3750)
    r07 = op(unreal.MaterialExpressionMultiply, masks, 0.7, -1700, 3850, "R")
    r07b = op(unreal.MaterialExpressionAdd, r07, 0.3, -1550, 3850)
    w2 = op(unreal.MaterialExpressionMultiply, gw, r07b, -1400, 3750)
    fl = mpc_param("Flooding", -1700, 3950)
    w3 = op(unreal.MaterialExpressionMultiply, masks, fl, -1550, 3950, "B")
    wsum = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionAdd, w1, w2, -1250, 3650), w3, -1100, 3700)
    wet = E(unreal.MaterialExpressionSaturate, -950, 3700)
    mel.connect_material_expressions(wsum, "", wet, "")
    # trails = max(G * StaticTrails, RT_NoctisTrails.R)
    st = scalar("StaticTrails", 0.6, -1700, 4150)
    t1 = op(unreal.MaterialExpressionMultiply, masks, st, -1550, 4150, "G")
    trail = t1
    if eal().does_asset_exist(RT_TRAILS) and mpc is not None:
        tr_rect = mpc_param("TrailMaskRect", -2300, 4300)
        a_xy = E(unreal.MaterialExpressionComponentMask, -2150, 4300)
        set_props(a_xy, r=True, g=True, b=False, a=False)
        mel.connect_material_expressions(tr_rect, "", a_xy, "")
        a_zw = E(unreal.MaterialExpressionComponentMask, -2150, 4400)
        set_props(a_zw, r=False, g=False, b=True, a=True)
        mel.connect_material_expressions(tr_rect, "", a_zw, "")
        tuv = op(unreal.MaterialExpressionDivide, op(unreal.MaterialExpressionSubtract, wpxy, a_xy, -2000, 4300), a_zw, -1850, 4300)
        rt = E(unreal.MaterialExpressionTextureSampleParameter2D, -1700, 4300)
        set_props(rt, parameter_name="TrailRuntime", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rt.set_editor_property("texture", eal().load_asset(RT_TRAILS))  # VERIFY: render target as sampler texture
        mel.connect_material_expressions(tuv, "", rt, "UVs")
        trail = op(unreal.MaterialExpressionMax, t1, rt, -1400, 4200, "", "R")
    trail_s = E(unreal.MaterialExpressionSaturate, -1250, 4200)
    mel.connect_material_expressions(trail, "", trail_s, "")
    # combine: trail mud, wetness, macro variation
    outs = {}
    for k, (lb, prop) in blends.items():
        y = {"BaseColor": -600, "Normal": 0, "Roughness": 600}[k]
        pin = "RGB" if k != "Roughness" else "R"
        lerp = E(unreal.MaterialExpressionLinearInterpolate, -300, y)
        mel.connect_material_expressions(lb, "", lerp, "A")
        if ("MudTracks", k) in samples:
            mel.connect_material_expressions(samples[("MudTracks", k)], pin, lerp, "B")
        mel.connect_material_expressions(trail_s, "", lerp, "Alpha")
        outs[k] = lerp
    dark = E(unreal.MaterialExpressionLinearInterpolate, -100, -600)
    dark.set_editor_property("const_a", 1.0)
    dark.set_editor_property("const_b", 0.55)
    mel.connect_material_expressions(wet, "", dark, "Alpha")
    bc = op(unreal.MaterialExpressionMultiply, outs["BaseColor"], dark, 50, -600)
    # macro variation from the silt height at 1/150 scale
    macro_uv = op(unreal.MaterialExpressionDivide, wpxy, 15000.0, -2300, -3000)
    macro = E(unreal.MaterialExpressionTextureSampleParameter2D, -2100, -3000)
    set_props(macro, parameter_name="MacroVariation", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE, sampler_source=shared)
    tp = GROUND_TEX_ROOT + "/T_Ground_FloodplainSilt_Height"
    if eal().does_asset_exist(tp):
        macro.set_editor_property("texture", eal().load_asset(tp))
    mel.connect_material_expressions(macro_uv, "", macro, "UVs")
    mv = op(unreal.MaterialExpressionAdd, op(unreal.MaterialExpressionMultiply, macro, 0.3, -1900, -3000, "R"), 0.85, -1750, -3000)
    bc2 = op(unreal.MaterialExpressionMultiply, bc, mv, 200, -600)
    mel.connect_material_property(bc2, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(outs["Normal"], "", unreal.MaterialProperty.MP_NORMAL)
    rwet = E(unreal.MaterialExpressionLinearInterpolate, 100, 600)
    mel.connect_material_expressions(outs["Roughness"], "", rwet, "A")
    rwet.set_editor_property("const_b", 0.12)
    rw = op(unreal.MaterialExpressionMultiply, wet, 0.8, -100, 700)
    mel.connect_material_expressions(rw, "", rwet, "Alpha")
    mel.connect_material_property(rwet, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)


# ----------------------------------------------------------------------------------------------- stages
def import_masks(path):
    t = unreal.AssetImportTask()
    for k, v in (("filename", path), ("destination_path", LANDSCAPE_ROOT), ("destination_name", "T_LandscapeMasks"),
                 ("automated", True), ("replace_existing", True), ("save", False)):
        t.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = eal().load_asset(MASKS_PATH)
    set_props(tex, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
              mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS, address_x=unreal.TextureAddress.TA_CLAMP,
              address_y=unreal.TextureAddress.TA_CLAMP)
    eal().save_asset(MASKS_PATH, only_if_is_dirty=False)
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
        log("[simulation] matériau %s : Landscape Layer Blend (hauteur) x3, humidité (R + MPC.GroundWetness + B*MPC.Flooding), "
            "corridors (G, RT_NoctisTrails), variation macro" % target)
        return 0
    ensure_dir(LANDSCAPE_ROOT)
    mpath = os.path.join(root, lay["masks"]["file"])
    if os.path.exists(mpath):
        import_masks(mpath)
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
