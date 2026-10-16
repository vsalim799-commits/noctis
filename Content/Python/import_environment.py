# -*- coding: utf-8 -*-
"""Noctis — imports the Hell Creek environment assets of Tools/EnvironmentFactory into /Game/Environment (UE 5.4+).

Reads Build/Environment/manifest.json (written by `factory.py --stages manifest`) and creates:
  /Game/Environment/Textures/{Foliage,Bark,Ground}/T_*         leaf atlases, bark / stem sets, 2K ground sets
  /Game/Environment/Materials/M_Noctis_Foliage, M_Noctis_Bark  master materials (created when absent)
  /Game/Environment/Materials/Instances/MI_<slot>              one instance per material slot of the FBX files
  /Game/Environment/Flora/<Category>/SM_<Name>                 static meshes: LOD0 (+ UCX collision) and LOD1/LOD2
  /Game/Environment/FoliageTypes/FT_<Name>                     foliage types (cull distances, scale, slope, collision)
  /Game/Environment/Decals/SM_Decal_Trackway_*                 trackway mesh decals (M_Noctis_Decal, deferred decal)
  /Game/Environment/PCG/Textures/T_PcgSpawn_*                  per-asset spawn-probability textures (envlib/pcgbake.py)
  /Game/Noctis/PCG/PCG_Noctis_Foliage                          PCG graph (only when absent or --overwrite-pcg; see below)

Run inside the editor (Python Editor Script Plugin):
    py "Content/Python/import_environment.py" [--manifest FILE] [--only conifer_metasequoia,...] [--skip pcg,foliage]
                                              [--overwrite-materials] [--overwrite-pcg]
Run outside Unreal:  python Content/Python/import_environment.py --dry-run     (checks the manifest and every file, and
                     evaluates the PCG graph as it would be built: map coverage and expected density of every asset;
                     an asset that could never spawn is an ERROR and the exit code is 1)

Order with the other editor scripts (Docs/ENVIRONMENT.md): setup_project.py -> import_environment.py ->
setup_landscape_material.py -> build_level.py (which assigns /Game/Noctis/Landscape/M_NoctisLandscape and the PCG
graphs of /Game/Noctis/PCG to the level).

FBX conventions (factory): 1 unit = 1 cm (UnitScaleFactor 1), Z up, pivot at the plant base, UCX_<mesh>_NN convex
collision in the LOD0 file, vertex colour = wind data (R flexibility, G phase, B normalised height), normal maps in
the DirectX convention. Import settings used here: uniform scale 1.0, Convert Scene on, Convert Scene Unit off,
vertex colours replaced, no automatic collision, no lightmap UVs (Lumen).

Lines marked  # VERIFY  use Unreal Python API that could not be checked against a running 5.4 editor when this script
was written. Each is wrapped: a failure is logged with the manual fallback and the rest of the import goes on.
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

ENV_ROOT = "/Game/Environment"
TEX_ROOT = ENV_ROOT + "/Textures"
MAT_ROOT = ENV_ROOT + "/Materials"
MI_ROOT = MAT_ROOT + "/Instances"
FT_ROOT = ENV_ROOT + "/FoliageTypes"
FLORA_ROOT = ENV_ROOT + "/Flora"
PCG_ROOT = "/Game/Noctis/PCG"           # same folder as build_level.PCG_ROOT
PCG_TEX_ROOT = ENV_ROOT + "/PCG/Textures"  # spawn-probability textures (envlib/pcgbake.UE_TEX_ROOT)
DECAL_ROOT = ENV_ROOT + "/Decals"
MASTER_DECAL = MAT_ROOT + "/M_Noctis_Decal"
LEVEL_TEX_ROOT = "/Game/Noctis/Level/Textures/Terrain"  # foliage_*.png / weight_*.png imported by build_level.py
MASTER_FOLIAGE = MAT_ROOT + "/M_Noctis_Foliage"
MASTER_BARK = MAT_ROOT + "/M_Noctis_Bark"
MPC_WEATHER = "/Game/Environment/MPC_NoctisWeather"  # setup_project.py
CATEGORY_FOLDER = {"tree": "Trees", "shrub": "Shrubs", "palm": "Palms", "fern": "Ferns", "horsetail": "Horsetails",
                   "aquatic": "Aquatic", "herb": "Herbs", "deadwood": "Deadwood"}
ALPHA_CLIP = 0.45  # opacity mask clip value of M_Noctis_Foliage (also the alpha-coverage threshold of the mips)

LOG_LINES = []


def log(msg, level="INFO"):
    line = "[NoctisEnvironment] %s %s" % (level, msg)
    LOG_LINES.append(line)
    if IN_UNREAL:
        {"INFO": unreal.log, "WARN": unreal.log_warning, "ERROR": unreal.log_error}[level](line)
    else:
        print(line)


def guarded(label):
    def wrap(fn):
        def run(*a, **k):
            log("=== %s ===" % label)
            try:
                return fn(*a, **k)
            except Exception as exc:  # broad on purpose: one API mismatch must not stop the import
                import traceback
                log("%s : échec (%s: %s)" % (label, type(exc).__name__, exc), "ERROR")
                if IN_UNREAL:
                    unreal.log_error(traceback.format_exc())
                return None
        return run
    return wrap


def repo_root():
    if IN_UNREAL:
        return os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def parse_args(argv):
    root = repo_root()
    p = argparse.ArgumentParser(description="Import the Noctis environment assets (Tools/EnvironmentFactory).")
    p.add_argument("--manifest", default=os.path.join(root, "Build", "Environment", "manifest.json"))
    p.add_argument("--only", default="", help="comma-separated asset ids")
    p.add_argument("--skip", default="", help="comma-separated stages: textures,materials,meshes,foliage,pcg")
    p.add_argument("--overwrite-materials", action="store_true")
    p.add_argument("--overwrite-pcg", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    a = p.parse_args(argv)
    a.only = set(x for x in a.only.split(",") if x)
    a.skip = set(x for x in a.skip.split(",") if x)
    if not IN_UNREAL:
        a.dry_run = True
    return a


def rel(root, path):
    return path if os.path.isabs(path) else os.path.join(root, path)


# ----------------------------------------------------------------------------------------------- helpers (editor)
def eal():
    return unreal.EditorAssetLibrary


def ensure_dir(path):
    if not eal().does_directory_exist(path):
        eal().make_directory(path)


def import_task(filename, dest, name=None, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", filename)
    t.set_editor_property("destination_path", dest)
    if name:
        t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    if options is not None:
        t.set_editor_property("options", options)
    return t


def set_props(obj, **props):
    for k, v in props.items():
        try:
            obj.set_editor_property(k, v)
        except Exception as exc:
            log("%s.%s non appliqué (%s) — VERIFY" % (obj.get_name(), k, exc), "WARN")


# ----------------------------------------------------------------------------------------------- textures
def load_pcg_plan(man, root):
    """Build/Environment/pcg/pcg_spawn.json (written by factory.py --stages landscape), or None."""
    p = rel(root, (man.get("pcg") or {}).get("plan", "Build/Environment/pcg/pcg_spawn.json"))
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def texture_list(man, root):
    """(file, destination folder, kind). kind: map suffix (BaseColor, Normal...) or 'Mask' (PCG data texture)."""
    out = []
    for mname, m in man["materials"].items():
        sub = "Foliage" if m["kind"] == "foliage" else "Bark"
        for mp, path in m["maps"].items():
            out.append((rel(root, path), TEX_ROOT + "/" + sub, mp))
    for g, info in man["ground_materials"].items():
        for mp, path in info["textures"].items():
            out.append((rel(root, path), TEX_ROOT + "/Ground", mp))
    dec = man.get("decals") or {}
    for mp, path in (dec.get("textures") or {}).items():
        out.append((rel(root, path), TEX_ROOT + "/Decals", mp))
    if dec.get("macro_noise"):
        out.append((rel(root, dec["macro_noise"]["texture"]), TEX_ROOT + "/Ground", "MacroNoise"))
    plan = load_pcg_plan(man, root)
    if plan:
        for ch in plan["chains"]:
            if ch.get("graph") == "PCG_Noctis_Foliage" or ch.get("graph") == "PCG_Noctis_RiverDebris":
                out.append((rel(root, ch["texture"]), PCG_TEX_ROOT, "Mask"))
        out.append((rel(root, plan["water_depth"]["texture"]), PCG_TEX_ROOT, "Mask"))
    seen, uniq = set(), []
    for t in out:
        if t[0] not in seen:
            seen.add(t[0])
            uniq.append(t)
    return uniq


TEX_KIND = {  # map suffix -> (sRGB, compression)
    "BaseColor": (True, "TC_DEFAULT"), "Normal": (False, "TC_NORMALMAP"), "Roughness": (False, "TC_MASKS"),
    "Opacity": (False, "TC_GRAYSCALE"), "Translucency": (False, "TC_GRAYSCALE"), "Height": (False, "TC_GRAYSCALE"),
    "MacroNoise": (False, "TC_MASKS"), "Mask": (False, "TC_VECTOR_DISPLACEMENTMAP"),
}


@guarded("1. Textures")
def stage_textures(args, man, root):
    items = texture_list(man, root)
    missing = [p for p, _, _ in items if not os.path.exists(p)]
    log("%d textures (%d manquantes)" % (len(items), len(missing)), "WARN" if missing else "INFO")
    for p in missing[:20]:
        log("  manquante : %s" % p, "WARN")
    if args.dry_run:
        return
    tasks = []
    for p, dest, mp in items:
        if os.path.exists(p):
            ensure_dir(dest)
            tasks.append((import_task(p, dest), dest + "/" + os.path.splitext(os.path.basename(p))[0], mp))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _, _ in tasks])
    for _, asset_path, mp in tasks:
        tex = eal().load_asset(asset_path)
        if tex is None:
            log("import échoué : %s" % asset_path, "WARN")
            continue
        srgb, comp = TEX_KIND.get(mp, (False, "TC_DEFAULT"))
        set_props(tex, srgb=srgb, compression_settings=getattr(unreal.TextureCompressionSettings, comp))
        if mp == "Normal":
            set_props(tex, flip_green_channel=False)  # factory writes DirectX normals (Unreal native)
        if mp == "Mask":
            # PCG data: linear, no mips, never streamed (same settings as build_level.import_texture)
            set_props(tex, mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS, never_stream=True)
        else:
            set_props(tex, lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD)
        if mp == "Opacity":
            # Alpha-tested cards: keep the coverage of every mip equal to the full-resolution coverage at the clip
            # value, otherwise crowns thin out and "dissolve" with distance (VERIFY property names, UE 5.1+).
            set_props(tex, do_scale_mips_for_alpha_coverage=True,
                      alpha_coverage_thresholds=unreal.Vector4(ALPHA_CLIP, ALPHA_CLIP, ALPHA_CLIP, ALPHA_CLIP))
        eal().save_asset(asset_path, only_if_is_dirty=False)
    log("%d textures importées" % len(tasks))


# ----------------------------------------------------------------------------------------------- materials
def _param_tex(mel, mat, name, x, y, sampler_type=None, default=None):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", name)
    if sampler_type is not None:
        n.set_editor_property("sampler_type", sampler_type)
    if default is not None:
        n.set_editor_property("texture", default)
    return n


def _scalar(mel, mat, name, value, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", value)
    return n


def _wind_wpo(mel, mat, x, y, amplitude_cm):
    """World position offset (wind), cheap and per instance:
        phase  = 2 pi (VertexColor.G + PerInstanceRandom)            each instance / element sways out of step
        gust   = 0.55 + 0.45 sin(dot(WorldPosition.xy, WindDir.xy) / GustWavelengthCm - Time * GustSpeed)
                 slow gust fronts travelling across the landscape (world-position term)
        offset = sin(Time * WindFrequency + phase) * VertexColor.R * WindAmplitudeCm * gust * (0.15 WindSpeed) * WindDir
    VertexColor.R = flexibility (0 trunk .. 1 leaf tip). Behind a static switch 'UseWind' (false: no WPO is compiled,
    used by the deadwood instances so that they never evaluate WPO nor invalidate Virtual Shadow Map pages).
    Pivot Painter can replace it later."""
    mpc = eal().load_asset(MPC_WEATHER) if eal().does_asset_exist(MPC_WEATHER) else None
    E = lambda cls, dx, dy: mel.create_material_expression(mat, cls, x + dx, y + dy)  # noqa: E731

    def mul(a, b, dx, dy, pa="", pb=""):
        n = E(unreal.MaterialExpressionMultiply, dx, dy)
        if isinstance(b, float):
            mel.connect_material_expressions(a, pa, n, "A")
            n.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(a, pa, n, "A")
            mel.connect_material_expressions(b, pb, n, "B")
        return n

    def add(a, b, dx, dy, pa="", pb=""):
        n = E(unreal.MaterialExpressionAdd, dx, dy)
        mel.connect_material_expressions(a, pa, n, "A")
        if isinstance(b, float):
            n.set_editor_property("const_b", b)
        else:
            mel.connect_material_expressions(b, pb, n, "B")
        return n

    vc = E(unreal.MaterialExpressionVertexColor, -1300, 0)
    time = E(unreal.MaterialExpressionTime, -1300, 150)
    freq = _scalar(mel, mat, "WindFrequency", 1.3, x - 1300, y + 250)
    amp = _scalar(mel, mat, "WindAmplitudeCm", amplitude_cm, x - 1300, y + 350)
    gust_len = _scalar(mel, mat, "GustWavelengthCm", 4000.0, x - 1300, y + 450)
    gust_spd = _scalar(mel, mat, "GustSpeed", 0.35, x - 1300, y + 550)
    pir = E(unreal.MaterialExpressionPerInstanceRandom, -1300, 650)
    # phase = 2 pi (G + PerInstanceRandom)
    ph = mul(add(vc, pir, -1100, 50, "G"), 6.2832, -950, 50)
    arg = add(mul(time, freq, -1100, 150), ph, -800, 100)
    sine = E(unreal.MaterialExpressionSine, -680, 100)
    mel.connect_material_expressions(arg, "", sine, "")
    k = mul(mul(sine, vc, -560, 100, "", "R"), amp, -440, 100)
    direction = speed = None
    if mpc is not None:
        direction = E(unreal.MaterialExpressionCollectionParameter, -800, 300)
        set_props(direction, collection=mpc, parameter_name="WindDirection")  # VERIFY property names
        speed = E(unreal.MaterialExpressionCollectionParameter, -800, 420)
        set_props(speed, collection=mpc, parameter_name="WindSpeed")
    if direction is not None:
        dmask = E(unreal.MaterialExpressionComponentMask, -650, 300)
        set_props(dmask, r=True, g=True, b=True, a=False)
        mel.connect_material_expressions(direction, "", dmask, "")
    else:
        dmask = E(unreal.MaterialExpressionConstant3Vector, -650, 300)
        dmask.set_editor_property("constant", unreal.LinearColor(1, 0, 0, 0))
    # gust: travelling along the wind direction over the landscape
    wp = E(unreal.MaterialExpressionWorldPosition, -1300, 800)
    wxy = E(unreal.MaterialExpressionComponentMask, -1150, 800)
    set_props(wxy, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(wp, "", wxy, "")
    dxy = E(unreal.MaterialExpressionComponentMask, -1150, 900)
    set_props(dxy, r=True, g=True, b=False, a=False)
    mel.connect_material_expressions(dmask, "", dxy, "")
    dot = E(unreal.MaterialExpressionDotProduct, -1000, 850)
    mel.connect_material_expressions(wxy, "", dot, "A")
    mel.connect_material_expressions(dxy, "", dot, "B")
    gdiv = E(unreal.MaterialExpressionDivide, -880, 850)
    mel.connect_material_expressions(dot, "", gdiv, "A")
    mel.connect_material_expressions(gust_len, "", gdiv, "B")
    gsub = E(unreal.MaterialExpressionSubtract, -760, 850)
    mel.connect_material_expressions(gdiv, "", gsub, "A")
    mel.connect_material_expressions(mul(time, gust_spd, -880, 950), "", gsub, "B")
    gs = E(unreal.MaterialExpressionSine, -640, 850)
    mel.connect_material_expressions(gsub, "", gs, "")
    gust = add(mul(gs, 0.45, -520, 850), 0.55, -400, 850)
    k = mul(k, gust, -300, 200)
    if speed is not None:
        k = mul(k, mul(speed, 0.15, -400, 420), -200, 250)
    vec = mul(k, dmask, -80, 250)
    sw = E(unreal.MaterialExpressionStaticSwitchParameter, 80, 250)
    set_props(sw, parameter_name="UseWind", default_value=True)
    zero = E(unreal.MaterialExpressionConstant3Vector, -80, 400)
    zero.set_editor_property("constant", unreal.LinearColor(0, 0, 0, 0))
    mel.connect_material_expressions(vec, "", sw, "True")
    mel.connect_material_expressions(zero, "", sw, "False")
    mel.connect_material_property(sw, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def create_master_foliage(overwrite):
    mel = unreal.MaterialEditingLibrary

    def configure(mat):
        mel.delete_all_material_expressions(mat)
        set_props(mat, blend_mode=unreal.BlendMode.BLEND_MASKED, two_sided=True,
                  shading_model=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, opacity_mask_clip_value=ALPHA_CLIP)
        set_props(mat, used_with_instanced_static_meshes=True)  # VERIFY flag name (bUsedWithInstancedStaticMeshes)
        bc = _param_tex(mel, mat, "BaseColor", -900, -300)
        nm = _param_tex(mel, mat, "Normal", -900, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        op = _param_tex(mel, mat, "Opacity", -900, 250, unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
        tr = _param_tex(mel, mat, "Translucency", -900, 500, unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
        rg = _param_tex(mel, mat, "Roughness", -900, 750, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        mel.connect_material_property(bc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(nm, "RGB", unreal.MaterialProperty.MP_NORMAL)
        mel.connect_material_property(op, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
        mel.connect_material_property(rg, "R", unreal.MaterialProperty.MP_ROUGHNESS)
        # Subsurface colour = base colour * translucency * tint (light through thin leaves)
        tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 600)
        set_props(tint, parameter_name="TranslucencyTint", default_value=unreal.LinearColor(0.9, 1.0, 0.55, 1))
        m1 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 500)
        mel.connect_material_expressions(bc, "RGB", m1, "A")
        mel.connect_material_expressions(tr, "R", m1, "B")
        m2 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 550)
        mel.connect_material_expressions(m1, "", m2, "A")
        mel.connect_material_expressions(tint, "", m2, "B")
        mel.connect_material_property(m2, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        _wind_wpo(mel, mat, 200, 900, 6.0)
        mel.recompile_material(mat)

    return _create_material(MASTER_FOLIAGE, configure, overwrite)


def create_master_bark(overwrite):
    mel = unreal.MaterialEditingLibrary

    def configure(mat):
        mel.delete_all_material_expressions(mat)
        set_props(mat, used_with_instanced_static_meshes=True, used_with_nanite=True)  # VERIFY flag names
        bc = _param_tex(mel, mat, "BaseColor", -900, -300)
        nm = _param_tex(mel, mat, "Normal", -900, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        rg = _param_tex(mel, mat, "Roughness", -900, 300, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        mel.connect_material_property(bc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(nm, "RGB", unreal.MaterialProperty.MP_NORMAL)
        mel.connect_material_property(rg, "R", unreal.MaterialProperty.MP_ROUGHNESS)
        _wind_wpo(mel, mat, 200, 600, 2.0)
        mel.recompile_material(mat)

    return _create_material(MASTER_BARK, configure, overwrite)


def create_master_decal(overwrite):
    """Deferred mesh decal (DBuffer colour + normal + roughness), soft opacity from the atlas."""
    mel = unreal.MaterialEditingLibrary

    def configure(mat):
        mel.delete_all_material_expressions(mat)
        set_props(mat, material_domain=unreal.MaterialDomain.MD_DEFERRED_DECAL)
        set_props(mat, decal_blend_mode=getattr(unreal.DecalBlendMode, "DBM_DBUFFER_COLOR_NORMAL_ROUGHNESS",
                                                getattr(unreal.DecalBlendMode, "DBM_DBUFFER_TRANSLUCENT_COLOR_NORMAL_ROUGHNESS", None)))  # VERIFY enum
        bc = _param_tex(mel, mat, "BaseColor", -900, -300)
        nm = _param_tex(mel, mat, "Normal", -900, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        rg = _param_tex(mel, mat, "Roughness", -900, 300, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        op = _param_tex(mel, mat, "Opacity", -900, 600, unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
        mel.connect_material_property(bc, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(nm, "RGB", unreal.MaterialProperty.MP_NORMAL)
        mel.connect_material_property(rg, "R", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(op, "R", unreal.MaterialProperty.MP_OPACITY)
        mel.recompile_material(mat)

    return _create_material(MASTER_DECAL, configure, overwrite)


def _create_material(path, configure, overwrite):
    existing = eal().load_asset(path) if eal().does_asset_exist(path) else None
    if existing and not overwrite:
        log("Matériau maître conservé : %s" % path)
        return existing
    folder, name = path.rsplit("/", 1)
    ensure_dir(folder)
    mat = existing or unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    configure(mat)
    eal().save_loaded_asset(mat)
    log("Matériau maître créé : %s" % path)
    return mat


@guarded("2. Matériaux")
def stage_materials(args, man, root):
    log("%d instances de matériaux à créer (%s)" % (len(man["materials"]), ", ".join(sorted(man["materials"]))))
    static = sorted(k for k, m in man["materials"].items() if not m.get("wind", True))
    log("Instances sans vent (UseWind = false, pas de WPO) : %s" % ", ".join(static))
    if args.dry_run:
        return {}
    foliage = create_master_foliage(args.overwrite_materials)
    bark = create_master_bark(args.overwrite_materials)
    ensure_dir(MI_ROOT)
    out = {}
    mel = unreal.MaterialEditingLibrary
    for mname, m in man["materials"].items():
        mi_name = "MI_" + mname[2:]
        path = MI_ROOT + "/" + mi_name
        mi = eal().load_asset(path) if eal().does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            mi_name, MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, foliage if m["kind"] == "foliage" else bark)
        sub = "Foliage" if m["kind"] == "foliage" else "Bark"
        for mp, p in m["maps"].items():
            tpath = "%s/%s/%s" % (TEX_ROOT, sub, os.path.splitext(os.path.basename(p))[0])
            tex = eal().load_asset(tpath) if eal().does_asset_exist(tpath) else None
            if tex is not None and mp in ("BaseColor", "Normal", "Opacity", "Translucency", "Roughness"):
                mel.set_material_instance_texture_parameter_value(mi, mp, tex)
        if not m.get("wind", True):
            # deadwood: static switch off -> no WPO compiled at all (VERIFY: 5.x Python exposes the static switch setter)
            try:
                mel.set_material_instance_static_switch_parameter_value(mi, "UseWind", False)
            except Exception as exc:
                log("%s : UseWind à décocher à la main (%s) ; amplitude mise à 0 en attendant" % (mi_name, exc), "WARN")
            mel.set_material_instance_scalar_parameter_value(mi, "WindAmplitudeCm", 0.0)
        eal().save_loaded_asset(mi)
        out[mname] = mi
    # trackway decals
    dec = man.get("decals") or {}
    if dec.get("textures"):
        dmat = create_master_decal(args.overwrite_materials)
        path = MI_ROOT + "/MI_Decal_Trackways"
        mi = eal().load_asset(path) if eal().does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_Decal_Trackways", MI_ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, dmat)
        for mp, p in dec["textures"].items():
            tpath = "%s/Decals/%s" % (TEX_ROOT, os.path.splitext(os.path.basename(p))[0])
            if eal().does_asset_exist(tpath) and mp != "Height":
                mel.set_material_instance_texture_parameter_value(mi, mp, eal().load_asset(tpath))
        eal().save_loaded_asset(mi)
        out["M_Decal_Trackways"] = mi
    log("%d instances prêtes" % len(out))
    return out


# ----------------------------------------------------------------------------------------------- meshes
def fbx_options():
    ui = unreal.FbxImportUI()
    set_props(ui, import_mesh=True, import_textures=False, import_materials=False, import_as_skeletal=False,
              import_animations=False, mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH)
    d = ui.get_editor_property("static_mesh_import_data")
    set_props(d, combine_meshes=True, auto_generate_collision=False, generate_lightmap_u_vs=False,
              import_uniform_scale=1.0, convert_scene=True, convert_scene_unit=False, remove_degenerates=True,
              normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
              vertex_color_import_option=unreal.VertexColorImportOption.REPLACE, one_convex_hull_per_ucx=True)
    return ui


def import_lod(mesh, lod, path):
    """LOD1/LOD2 from their own FBX. VERIFY: the call moved between subsystems across 5.x versions."""
    try:
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if hasattr(sub, "import_lod"):
            return sub.import_lod(mesh, lod, path)
    except Exception:
        pass
    if hasattr(unreal, "EditorStaticMeshLibrary") and hasattr(unreal.EditorStaticMeshLibrary, "import_lod"):
        return unreal.EditorStaticMeshLibrary.import_lod(mesh, lod, path)  # VERIFY (deprecated library)
    raise RuntimeError("import_lod indisponible : Static Mesh Editor > LOD Settings > LOD Import > %s" % path)


def set_lod_screen_sizes(mesh, sizes):
    try:
        mesh.set_editor_property("auto_compute_lod_screen_size", False)  # VERIFY
        models = list(mesh.get_editor_property("source_models"))  # VERIFY: may not be exposed in 5.4
        for i, m in enumerate(models[: len(sizes)]):
            ss = m.get_editor_property("screen_size")
            ss.set_editor_property("default", float(sizes[i]))
            m.set_editor_property("screen_size", ss)
            models[i] = m
        mesh.set_editor_property("source_models", models)
        return True
    except Exception as exc:
        log("Tailles d'écran LOD à régler à la main (%s) : %s" % (sizes, exc), "WARN")
        return False


@guarded("3. Maillages")
def stage_meshes(args, man, root, instances):
    assets = {k: v for k, v in man["assets"].items() if not args.only or k in args.only}
    log("%d maillages (LOD0 + LOD1 + LOD2, + LOD3 cartes croisées pour les arbres)" % len(assets))
    out = {}
    for aid, a in sorted(assets.items()):
        files = {k: rel(root, v) for k, v in a["files"].items()}
        miss = [p for p in files.values() if not os.path.exists(p)]
        dest = FLORA_ROOT + "/" + CATEGORY_FOLDER.get(a["category"], "Misc")
        if args.dry_run:
            log("[simulation] %-26s -> %s/%s  tris %s  collision=%s nanite=%s%s" % (aid, dest, a["ue_name"], a["triangles"], a["collision"], a["nanite"],
                                                                            ("  MANQUANT: %s" % miss) if miss else ""))
            continue
        if miss:
            log("%s : fichiers manquants %s" % (aid, miss), "WARN")
            continue
        ensure_dir(dest)
        t = import_task(files["LOD0"], dest, a["ue_name"], fbx_options())
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
        mesh = eal().load_asset(dest + "/" + a["ue_name"])
        if mesh is None:
            log("%s : import FBX échoué" % aid, "ERROR")
            continue
        for lod in (1, 2, 3):  # LOD3 = crossed billboards (trees, tall palm)
            key = "LOD%d" % lod
            if key in files:
                try:
                    import_lod(mesh, lod, files[key])
                except Exception as exc:
                    log("%s %s : %s" % (aid, key, exc), "WARN")
        set_lod_screen_sizes(mesh, a["lod_screen_size"])
        # material slots by name -> MI
        try:
            mats = list(mesh.get_editor_property("static_materials"))
            for i, sm in enumerate(mats):
                slot = str(sm.get_editor_property("material_slot_name"))
                key = slot if slot.startswith("M_") else "M_" + slot
                mi = (instances or {}).get(key) or (instances or {}).get(slot)
                if mi is None:
                    path = MI_ROOT + "/MI_" + key[2:]
                    mi = eal().load_asset(path) if eal().does_asset_exist(path) else None
                if mi is not None:
                    mesh.set_material(i, mi)
                else:
                    log("%s : pas d'instance pour l'emplacement %s" % (aid, slot), "WARN")
        except Exception as exc:
            log("%s : affectation des matériaux à vérifier (%s)" % (aid, exc), "WARN")
        if a["nanite"]:
            try:
                ns = mesh.get_editor_property("nanite_settings")
                ns.set_editor_property("enabled", True)
                mesh.set_editor_property("nanite_settings", ns)
            except Exception as exc:
                log("%s : Nanite à activer à la main (%s)" % (aid, exc), "WARN")
        eal().save_loaded_asset(mesh)
        out[aid] = mesh
        log("%s importé (%s)" % (aid, dest))
    # trackway mesh decals (single LOD, deferred-decal material instance)
    for d in (man.get("decals") or {}).get("meshes") or []:
        f = rel(root, d.get("file", ""))
        if args.dry_run:
            log("[simulation] décalque %-30s -> %s  (%s)%s" % (d["ue_name"], DECAL_ROOT, d.get("description_fr", ""),
                                                                "" if os.path.exists(f) else "  MANQUANT: %s" % f))
            continue
        if not os.path.exists(f):
            log("décalque %s : fichier manquant %s" % (d["ue_name"], f), "WARN")
            continue
        ensure_dir(DECAL_ROOT)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([import_task(f, DECAL_ROOT, d["ue_name"], fbx_options())])
        mesh = eal().load_asset(DECAL_ROOT + "/" + d["ue_name"])
        mi = (instances or {}).get("M_Decal_Trackways") or (eal().load_asset(MI_ROOT + "/MI_Decal_Trackways")
                                                            if eal().does_asset_exist(MI_ROOT + "/MI_Decal_Trackways") else None)
        if mesh is not None and mi is not None:
            mesh.set_material(0, mi)
            set_props(mesh, cast_shadow=False)  # VERIFY: mesh decals never cast shadows anyway
            eal().save_loaded_asset(mesh)
            out["decal:" + d["ue_name"]] = mesh
    return out


# ----------------------------------------------------------------------------------------------- foliage types
FOLIAGE_SETTINGS = {  # category -> (align to normal, align max angle, random pitch, ground slope max, scale range, collision)
    "tree": (False, 0.0, 3.0, 30.0, (0.85, 1.15), "BlockAll"),
    "palm": (False, 0.0, 4.0, 30.0, (0.85, 1.15), "BlockAll"),
    "shrub": (True, 15.0, 6.0, 35.0, (0.8, 1.2), "NoCollision"),
    "fern": (True, 25.0, 8.0, 40.0, (0.75, 1.2), "NoCollision"),
    "horsetail": (True, 15.0, 5.0, 35.0, (0.8, 1.2), "NoCollision"),
    "herb": (True, 30.0, 8.0, 40.0, (0.8, 1.2), "NoCollision"),
    "aquatic": (False, 0.0, 4.0, 20.0, (0.8, 1.2), "NoCollision"),
    "deadwood": (True, 35.0, 0.0, 40.0, (0.8, 1.2), "BlockAll"),
}


@guarded("4. Types de feuillage")
def stage_foliage(args, man, meshes):
    assets = {k: v for k, v in man["assets"].items() if not args.only or k in args.only}
    for aid, a in sorted(assets.items()):
        s = FOLIAGE_SETTINGS.get(a["category"], FOLIAGE_SETTINGS["shrub"])
        cull = a["cull_distance_cm"]
        name = "FT_" + a["ue_name"][3:]
        if args.dry_run:
            log("[simulation] %s : cull %s-%s cm, échelle %s, pente max %s°, collision %s%s" % (name, cull["start"], cull["end"], a.get("scale_range") or s[4],
                                                                                         s[3], s[5], ", sans WPO" if a.get("static_no_wpo") else ""))
            continue
        mesh = (meshes or {}).get(aid)
        if mesh is None:
            p = FLORA_ROOT + "/" + CATEGORY_FOLDER.get(a["category"], "Misc") + "/" + a["ue_name"]
            mesh = eal().load_asset(p) if eal().does_asset_exist(p) else None
        if mesh is None:
            continue
        ensure_dir(FT_ROOT)
        path = FT_ROOT + "/" + name
        ft = eal().load_asset(path) if eal().does_asset_exist(path) else None
        if ft is None:
            factory = getattr(unreal, "FoliageType_InstancedStaticMeshFactory", None)  # VERIFY factory exposure
            ft = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FT_ROOT, unreal.FoliageType_InstancedStaticMesh,
                                                                         factory() if factory else None)
        if ft is None:
            log("%s : création impossible (Foliage Mode > + Foliage Type à la main)" % name, "WARN")
            continue
        set_props(ft, mesh=mesh)
        set_props(ft, cull_distance=unreal.Int32Interval(min=int(cull["start"]), max=int(cull["end"])))  # VERIFY struct ctor
        sr = a.get("scale_range") or s[4]  # manifest: per asset, trees / palms capped at the PFT height of the data
        set_props(ft, align_to_normal=s[0], align_max_angle=s[1], random_pitch_angle=s[2], random_yaw=True,
                  ground_slope_angle=unreal.FloatInterval(min=0.0, max=s[3]),
                  scale_x=unreal.FloatInterval(min=sr[0], max=sr[1]), scaling=unreal.FoliageScaling.UNIFORM)
        set_props(ft, cast_shadow=True, enable_density_scaling=a["category"] in ("fern", "horsetail", "herb", "shrub", "aquatic"))
        if a.get("static_no_wpo"):
            set_props(ft, evaluate_world_position_offset=False)  # VERIFY (UFoliageType::bEvaluateWorldPositionOffset, 5.1+)
        try:
            bi = ft.get_editor_property("body_instance")
            bi.set_editor_property("collision_profile_name", s[5])
            ft.set_editor_property("body_instance", bi)
        except Exception as exc:
            log("%s : collision à régler (%s)" % (name, exc), "WARN")
        eal().save_loaded_asset(ft)
        log("%s prêt" % name)


# ----------------------------------------------------------------------------------------------- PCG
PCG_NOTE = """PCG_Noctis_Foliage — une chaîne par asset (voir Docs/ENVIRONMENT.md §4.1), dans l'ordre du helper Blender :
  Get Landscape Data -> Surface Sampler (points/m² = per_ha / 10 000, demi-étendue E petite pour atteindre per_ha, Looseness 1)
  -> ∩ Get Texture Data T_PcgSpawn_<Asset> (canal R explicite ; R = G = B = A = probabilité de maintien cuite :
     masque^gamma x agrégation x pente x tampon d'eau x exclusion chenal / bras mort x clairières des POI x biais d'habitat)
  -> Density Noise (Add, [0, 1], densité source inversée) -> Density Filter [0, 0.99999]   = tirage : garde U < p
  -> Bounds Modifier (Set, ±min_spacing/2) -> Self Pruning (Large to small, aléatoire)
  -> Difference (Binary) contre les chaînes déjà posées (arbres / palmiers : tous les plus grands ; autres : même type)
  -> [flottantes : + profondeur d'eau T_PcgWaterDepth -> Z += profondeur] -> Transform Points (lacet, inclinaison, échelle
     du manifeste) -> Static Mesh Spawner (maillage de l'asset, distance de disparition, WPO coupé pour le bois mort).
Bois mort : chaînes des habitats forestiers seulement ; le bois flotté des barres / berges / falaises (T_PcgSpawn_Driftwood*)
  revient à PCG_Noctis_RiverDebris (build_level.py). Décalques de pistes : chaîne T_PcgSpawn_TrackwayBank (berges, lacet
  aléatoire) + chaîne le long des splines taguées noctis.trail (Spline Sampler, orientées sur la tangente)."""


def pcg_chain_plan(man, root):
    """The PCG graph as it will be built (one dict per chain), from the manifest and pcg_spawn.json. Used both by
    build_pcg_graph() in the editor and by the dry-run report, so the report describes the real graph."""
    plan = load_pcg_plan(man, root)
    if plan is None:
        return None, []
    size_cm = float(plan["size_m"]) * 100.0
    chains = []
    for ch in plan["chains"]:
        if ch.get("graph") != "PCG_Noctis_Foliage":
            continue
        if ch["category"] == "decal":
            meshes = [d["ue_name"] for d in ((man.get("decals") or {}).get("meshes") or [])]
            mesh_paths = [DECAL_ROOT + "/" + m for m in meshes]
            cull = 6000
        else:
            a = man["assets"].get(ch["asset"])
            if a is None:
                continue
            mesh_paths = [FLORA_ROOT + "/" + CATEGORY_FOLDER.get(a["category"], "Misc") + "/" + a["ue_name"]]
            cull = int(a["cull_distance_cm"]["end"])
        label = ch["asset"] if ch["category"] != "deadwood" else "deadwood/" + ch["asset"]
        chains.append(dict(
            label=label, asset=ch["asset"], category=ch["category"], texture_file=rel(root, ch["texture"]), sha1=ch.get("sha1"),
            texture_asset=ch["texture_ue"], points_per_m2=ch["points_per_m2"], per_ha=ch["per_ha"],
            candidate_per_ha=ch.get("candidate_per_ha", ch["per_ha"]), texture_scale=ch.get("texture_scale", 1.0),
            sampler_extent_cm=ch["sampler_extent_m"] * 100.0, looseness=plan.get("looseness", 1.0),
            sampler_cap_per_ha=ch.get("sampler_cap_per_ha"),
            draw=dict(noise_mode="ADD", noise_min=0.0, noise_max=1.0, invert_source_density=True, filter_lower=0.0, filter_upper=0.99999),
            prune_extent_cm=ch["prune_extent_m"] * 100.0,
            blockers=list(ch.get("blockers", [])),
            scale=ch.get("scale_range", [0.85, 1.15]), tilt_deg=ch.get("tilt_deg", 0.0), yaw=(0.0, 360.0),
            on_water=ch.get("on_water", False), water_depth_asset=plan["water_depth"]["texture_ue"], water_depth_scale_cm=plan["water_depth"]["scale_m"] * 100.0,
            median_depth_cm=ch.get("median_depth_m", 0.0) * 100.0,
            meshes=mesh_paths, cull_end_cm=cull, evaluate_wpo=not ch.get("static_no_wpo", False),
            texture_transform=dict(location_cm=(size_cm / 2, -size_cm / 2, 0.0), scale=(size_cm / 2, size_cm / 2, 1.0)),
            stats=ch.get("stats", {})))
    return plan, chains


def _sha1(path):
    import hashlib
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def report_pcg_plan(chains):
    """Dry-run check of the graph as built: map coverage and expected density per asset; an asset that can never
    spawn (coverage 0), a missing / stale texture or a threshold filter instead of the random draw is an ERROR."""
    if not chains:
        log("Plan PCG absent : lancer Tools/EnvironmentFactory/factory.py --stages landscape (pcg_spawn.json)", "ERROR")
        return
    log("%-34s %8s %8s %9s %9s %9s %8s %s" % ("chaîne", "couvert.", "p moyen", "inst./ha", "candid./ha", "plafond", "élagage", "bloqueurs"))
    stems = {}
    for c in chains:
        st = c["stats"]
        if not os.path.exists(c["texture_file"]):
            log("%s : texture absente %s" % (c["label"], c["texture_file"]), "ERROR")
            continue
        if c.get("sha1") and _sha1(c["texture_file"]) != c["sha1"]:
            log("%s : texture modifiée depuis le calcul des statistiques (relancer --stages landscape)" % c["label"], "ERROR")
        d = c["draw"]
        if not (d["noise_mode"] == "ADD" and d["invert_source_density"] and d["filter_lower"] == 0.0 and d["filter_upper"] < 1.0):
            log("%s : le filtre n'est pas un tirage aléatoire (seuil) — interdit" % c["label"], "ERROR")
        kept = st.get("expected_candidates_kept_per_ha", 0.0)
        log("%-34s %8.3f %8.4f %9.2f %9.1f %9.0f %7.1fm %d" % (c["label"], st.get("coverage", 0), st.get("mean_p", 0), kept,
                                                               c["candidate_per_ha"], c["sampler_cap_per_ha"] or 0, c["prune_extent_cm"] / 50.0,
                                                               len(c["blockers"])))
        if st.get("coverage", 0) <= 0 or st.get("max_p", 0) <= 0:
            log("%s : aucune zone de la carte ne peut faire apparaître cet asset (couverture 0)" % c["label"], "ERROR")
        if c["sampler_cap_per_ha"] and c["candidate_per_ha"] > c["sampler_cap_per_ha"]:
            log("%s : %s candidats/ha au-delà du plafond du Surface Sampler (%s/ha)" % (c["label"], c["candidate_per_ha"], c["sampler_cap_per_ha"]), "WARN")
        if c["category"] in ("tree", "palm"):
            stems[c["asset"]] = kept
    tot = sum(stems.values())
    if tot > 0:
        palms = sum(v for k, v in stems.items() if k.startswith("palm_"))
        log("Tiges arborescentes attendues (avant élagage) : %.1f/ha en moyenne sur la carte, dont palmiers %.1f %%" % (tot, 100.0 * palms / tot))


@guarded("5. Graphe PCG")
def stage_pcg(args, man, root):
    rules = man["scatter"]["pft_rules"]
    for pft, r in rules.items():
        log("PCG %s : masque %s (gamma %s), %s inst./ha, espacement %s m, pente < %s°, tampon eau %s m, assets %s" % (
            pft, r["mask"], r.get("density_gamma", 1.0), r["per_ha"], r["min_spacing_m"], r["slope_max_deg"], r["water_buffer_m"], r["assets"]))
    plan, chains = pcg_chain_plan(man, root)
    if args.dry_run:
        log(PCG_NOTE)
        report_pcg_plan(chains)
        if plan:
            debris = [c for c in plan["chains"] if c.get("graph") == "PCG_Noctis_RiverDebris"]
            log("Pour PCG_Noctis_RiverDebris (build_level.py, à brancher à la main) : %s" % ", ".join(
                "%s <- %s (%s/ha)" % (c["asset"], c["texture_ue"], c["per_ha"]) for c in debris))
        return
    if not chains:
        log("pcg_spawn.json absent : graphe non construit (factory.py --stages landscape)", "ERROR")
        return
    path = PCG_ROOT + "/PCG_Noctis_Foliage"
    if eal().does_asset_exist(path) and not args.overwrite_pcg:
        log("%s existe : conservé (--overwrite-pcg pour le régénérer)" % path)
        return
    try:
        ensure_dir(PCG_ROOT)
        if eal().does_asset_exist(path):
            eal().delete_asset(path)  # rebuilt from scratch: no stale nodes of the threshold version
        graph = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "PCG_Noctis_Foliage", PCG_ROOT, unreal.PCGGraph, unreal.PCGGraphFactory())  # VERIFY factory name
        build_pcg_graph(graph, chains)
        eal().save_loaded_asset(graph)
        log("Graphe PCG créé : %s (%d chaînes)" % (path, len(chains)))
    except Exception as exc:
        log("Graphe PCG non créé automatiquement (%s). Construction manuelle :\n%s" % (exc, PCG_NOTE), "ERROR")


def _pcg_class(*names):
    for n in names:
        if hasattr(unreal, n):
            return getattr(unreal, n)
    raise AttributeError("aucune classe PCG parmi %s" % (names,))


def _enum(enum_names, member_names):
    for en in enum_names:
        e = getattr(unreal, en, None)
        if e is None:
            continue
        for m in member_names:
            if hasattr(e, m):
                return getattr(e, m)
    return None


def build_pcg_graph(graph, chains):
    """One chain per entry of pcg_chain_plan(). VERIFY every node class / pin / property name on 5.4: each chain is
    built inside its own try block, a failure is logged with the chain label and the other chains are still built."""
    def add(cls):
        res = graph.add_node_of_type(cls)  # VERIFY: returns (node, settings) in Python
        return res if isinstance(res, tuple) else (res, res.get_settings())

    out_node = graph.get_output_node()
    land, _ = add(_pcg_class("PCGGetLandscapeSettings"))  # "Get Landscape Data"
    tex_cls = _pcg_class("PCGTextureSamplerSettings", "PCGGetTextureDataSettings")  # "Get Texture Data" (VERIFY)
    inter_cls = _pcg_class("PCGIntersectionSettings", "PCGInnerIntersectionSettings")
    red = _enum(["PCGTextureColorChannel"], ["RED", "R"])
    final = {}

    def texture_node(asset_path, tr):
        tex, ts = add(tex_cls)
        if eal().does_asset_exist(asset_path):
            set_props(ts, texture=eal().load_asset(asset_path))
        else:
            log("texture %s absente (étape « 1. Textures » de ce script, fichiers de Build/Environment/pcg)" % asset_path, "WARN")
        # Explicit colour channel (the default has been Alpha); the baked textures also carry the value in RGBA.
        if red is not None:
            set_props(ts, color_channel=red)
            try:
                log("    %s : canal relu = %s" % (asset_path.rsplit("/", 1)[-1], ts.get_editor_property("color_channel")))
            except Exception as exc:
                log("    canal non relu (%s) — VERIFY" % exc, "WARN")
        else:
            log("    enum PCGTextureColorChannel introuvable : canal laissé par défaut (sans effet : R = G = B = A dans ces textures)", "WARN")
        set_props(ts, use_absolute_transform=True,
                  transform=unreal.Transform(location=unreal.Vector(*tr["location_cm"]), rotation=unreal.Rotator(0, 0, 0),
                                             scale=unreal.Vector(*tr["scale"])))  # texture spans [-1, 1] of this transform
        dmul = _enum(["PCGTextureDensityFunction"], ["MULTIPLY", "Multiply"])
        if dmul is not None:
            set_props(ts, density_function=dmul)
        return tex

    for c in chains:
        try:
            samp, ss = add(_pcg_class("PCGSurfaceSamplerSettings"))
            e = c["sampler_extent_cm"]
            set_props(ss, points_per_squared_meter=float(c["points_per_m2"]), point_extents=unreal.Vector(e, e, 50.0),
                      looseness=float(c["looseness"]))
            graph.add_edge(land, "Out", samp, "Surface")
            inter, _ = add(inter_cls)
            graph.add_edge(samp, "Out", inter, "In")
            graph.add_edge(texture_node(c["texture_asset"], c["texture_transform"]), "Out", inter, "In")
            # random draw: density' = (1 - p) + U, U ~ [0, 1); keep density' < 1  <=>  U < p
            noise, ns = add(_pcg_class("PCGDensityNoiseSettings"))
            set_props(ns, density_mode=_enum(["PCGDensityNoiseMode"], ["ADD", "Add"]), density_noise_min=c["draw"]["noise_min"],
                      density_noise_max=c["draw"]["noise_max"], invert_source_density=c["draw"]["invert_source_density"])
            graph.add_edge(inter, "Out", noise, "In")
            flt, fs = add(_pcg_class("PCGDensityFilterSettings"))
            set_props(fs, lower_bound=c["draw"]["filter_lower"], upper_bound=c["draw"]["filter_upper"], invert_filter=False)
            graph.add_edge(noise, "Out", flt, "In")
            bm, bs = add(_pcg_class("PCGBoundsModifierSettings"))
            r = c["prune_extent_cm"]
            set_props(bs, mode=_enum(["PCGBoundsModifierMode"], ["SET", "Set"]), bounds_min=unreal.Vector(-r, -r, -50.0),
                      bounds_max=unreal.Vector(r, r, 50.0))
            graph.add_edge(flt, "Out", bm, "In")
            prune, ps = add(_pcg_class("PCGSelfPruningSettings"))
            set_props(ps, pruning_type=_enum(["PCGSelfPruningType"], ["LARGE_TO_SMALL", "LargeToSmall"]), radius_similarity_factor=0.25,
                      randomized_pruning=True)
            graph.add_edge(bm, "Out", prune, "In")
            last = prune
            if c["blockers"]:
                diff, dfs = add(_pcg_class("PCGDifferenceSettings"))
                set_props(dfs, density_function=_enum(["PCGDifferenceDensityFunction"], ["BINARY", "Binary"]),
                          mode=_enum(["PCGDifferenceMode"], ["DISCRETE", "Discrete"]))
                graph.add_edge(prune, "Out", diff, "Source")
                for b in c["blockers"]:
                    if b in final:
                        graph.add_edge(final[b], "Out", diff, "Differences")
                last = diff
            final[c["asset"]] = last
            if c["on_water"]:
                last = _float_on_water(graph, add, last, c, texture_node)
            tr, trs = add(_pcg_class("PCGTransformPointsSettings"))
            t = c["tilt_deg"]
            # unreal.Rotator(roll, pitch, yaw)
            set_props(trs, rotation_min=unreal.Rotator(-t, -t, c["yaw"][0]), rotation_max=unreal.Rotator(t, t, c["yaw"][1]),
                      scale_min=unreal.Vector(*([c["scale"][0]] * 3)), scale_max=unreal.Vector(*([c["scale"][1]] * 3)), uniform_scale=True)
            graph.add_edge(last, "Out", tr, "In")
            sp, sps = add(_pcg_class("PCGStaticMeshSpawnerSettings"))
            _spawner_meshes(sps, c)
            graph.add_edge(tr, "Out", sp, "In")
            graph.add_edge(sp, "Out", out_node, "In")
            log("  chaîne %s construite" % c["label"])
        except Exception as exc:
            log("  chaîne %s : %s — à construire à la main (voir PCG_NOTE)" % (c["label"], exc), "ERROR")
    _trail_trackways(graph, add, out_node, chains)


def _spawner_meshes(sps, c):
    try:
        sps.set_editor_property("mesh_selector_type", unreal.PCGMeshSelectorWeighted)  # VERIFY
        sel = sps.get_editor_property("mesh_selector_parameters")
        entries = []
        for mp in c["meshes"]:
            e = unreal.PCGMeshSelectorWeightedEntry()
            d = e.get_editor_property("descriptor")
            d.set_editor_property("static_mesh", eal().load_asset(mp))
            d.set_editor_property("instance_end_cull_distance", int(c["cull_end_cm"]))  # VERIFY
            if not c["evaluate_wpo"]:
                try:
                    d.set_editor_property("evaluate_world_position_offset", False)  # VERIFY (FSoftISMComponentDescriptor)
                except Exception as exc:
                    log("  %s : WPO à couper à la main sur le spawner (%s)" % (c["label"], exc), "WARN")
            e.set_editor_property("descriptor", d)
            e.set_editor_property("weight", 1)
            entries.append(e)
        sel.set_editor_property("mesh_entries", entries)
    except Exception as exc:
        log("Spawner %s : maillages à choisir à la main (%s)" % (c["label"], exc), "WARN")


def _float_on_water(graph, add, last, c, texture_node):
    """Floating plants: points sampled on the pond / river bed are lifted by the export-time water depth
    (T_PcgWaterDepth, value = depth / 4 m): density reset to 1, x depth texture -> $Density = depth / 4,
    Add Attribute DepthScaleCm = 400, Attribute Maths Lift = $Density x DepthScaleCm, Attribute Maths
    $Position.Z = $Position.Z + Lift. VERIFY (selector helpers of UE 5.4 Python); when any of them is missing the
    chain falls back to a constant lift by the median depth of the chain (logged)."""
    H = getattr(unreal, "PCGAttributePropertySelectorBlueprintHelpers", None)
    try:
        if H is None or not all(hasattr(H, f) for f in ("set_point_property", "set_attribute_name", "update")):
            raise AttributeError("PCGAttributePropertySelectorBlueprintHelpers.set_point_property / set_attribute_name / update")
        reset, rs = add(_pcg_class("PCGDensityNoiseSettings"))
        set_props(rs, density_mode=_enum(["PCGDensityNoiseMode"], ["SET", "Set"]), density_noise_min=1.0, density_noise_max=1.0)
        graph.add_edge(last, "Out", reset, "In")
        inter, _ = add(_pcg_class("PCGIntersectionSettings", "PCGInnerIntersectionSettings"))
        graph.add_edge(reset, "Out", inter, "In")
        graph.add_edge(texture_node(c["water_depth_asset"], c["texture_transform"]), "Out", inter, "In")
        cst, cs = add(_pcg_class("PCGAddAttributeSettings", "PCGCreateAttributeSettings"))
        set_props(cs, output_target=H.set_attribute_name(cs.get_editor_property("output_target"), "DepthScaleCm"))
        try:
            cs.get_editor_property("attribute_types").set_editor_property("type", unreal.PCGMetadataTypes.DOUBLE)
            cs.get_editor_property("attribute_types").set_editor_property("double_value", float(c["water_depth_scale_cm"]))
        except Exception as exc:
            raise RuntimeError("valeur constante DepthScaleCm (%s)" % exc)
        graph.add_edge(inter, "Out", cst, "In")
        m1, m1s = add(_pcg_class("PCGMetadataMathsSettings"))
        set_props(m1s, operation=_enum(["PCGMetadataMathsOperation"], ["MULTIPLY", "Multiply"]),
                  input_source1=H.set_point_property(m1s.get_editor_property("input_source1"), unreal.PCGPointProperties.DENSITY),
                  input_source2=H.set_attribute_name(m1s.get_editor_property("input_source2"), "DepthScaleCm"),
                  output_target=H.set_attribute_name(m1s.get_editor_property("output_target"), "Lift"))
        graph.add_edge(cst, "Out", m1, "In")
        m2, m2s = add(_pcg_class("PCGMetadataMathsSettings"))
        set_props(m2s, operation=_enum(["PCGMetadataMathsOperation"], ["ADD", "Add"]),
                  input_source1=H.update(m2s.get_editor_property("input_source1"), "$Position.Z"),
                  input_source2=H.set_attribute_name(m2s.get_editor_property("input_source2"), "Lift"),
                  output_target=H.update(m2s.get_editor_property("output_target"), "$Position.Z"))
        graph.add_edge(m1, "Out", m2, "In")
        return m2
    except Exception as exc:
        off = c["median_depth_cm"]
        log("  %s : élévation par attribut indisponible (%s) ; décalage constant de %.0f cm (profondeur médiane)" % (c["label"], exc, off), "WARN")
        tr, trs = add(_pcg_class("PCGTransformPointsSettings"))
        set_props(trs, offset_min=unreal.Vector(0, 0, off), offset_max=unreal.Vector(0, 0, off))
        graph.add_edge(last, "Out", tr, "In")
        return tr


def _trail_trackways(graph, add, out_node, chains):
    """Trackway decals along the trail splines (build_level.py stage 'trails', actors tagged noctis.trail): one decal
    every ~7 m kept with p = 0.35, oriented on the spline tangent (+90 deg: the decal mesh walks along +Y)."""
    dec = [c for c in chains if c["category"] == "decal"]
    if not dec:
        return
    c = dec[0]
    try:
        src, srs = add(_pcg_class("PCGDataFromActorSettings"))  # "Get Actor Data"
        sel = srs.get_editor_property("actor_selector")
        set_props(sel, actor_filter=_enum(["PCGActorFilter"], ["ALL_WORLD_ACTORS", "AllWorldActors"]),
                  actor_selection=_enum(["PCGActorSelection"], ["BY_TAG", "ByTag"]), actor_selection_tag="noctis.trail",
                  select_multiple=True)
        srs.set_editor_property("actor_selector", sel)
        spl, sps = add(_pcg_class("PCGSplineSamplerSettings"))
        params = sps.get_editor_property("sampler_params")
        set_props(params, mode=_enum(["PCGSplineSamplingMode"], ["DISTANCE", "Distance"]), distance_increment=700.0)
        sps.set_editor_property("sampler_params", params)
        graph.add_edge(src, "Out", spl, "In")
        noise, ns = add(_pcg_class("PCGDensityNoiseSettings"))
        set_props(ns, density_mode=_enum(["PCGDensityNoiseMode"], ["SET", "Set"]), density_noise_min=0.0, density_noise_max=1.0)
        graph.add_edge(spl, "Out", noise, "In")
        flt, fs = add(_pcg_class("PCGDensityFilterSettings"))
        set_props(fs, lower_bound=0.0, upper_bound=0.35)
        graph.add_edge(noise, "Out", flt, "In")
        tr, trs = add(_pcg_class("PCGTransformPointsSettings"))
        set_props(trs, rotation_min=unreal.Rotator(0, 0, 78.0), rotation_max=unreal.Rotator(0, 0, 102.0),
                  offset_min=unreal.Vector(-60.0, -60.0, 0.0), offset_max=unreal.Vector(60.0, 60.0, 0.0),
                  scale_min=unreal.Vector(0.9, 0.9, 1.0), scale_max=unreal.Vector(1.1, 1.1, 1.0))
        graph.add_edge(flt, "Out", tr, "In")
        sp, sps2 = add(_pcg_class("PCGStaticMeshSpawnerSettings"))
        _spawner_meshes(sps2, c)
        graph.add_edge(tr, "Out", sp, "In")
        graph.add_edge(sp, "Out", out_node, "In")
        log("  chaîne trackways/noctis.trail construite")
    except Exception as exc:
        log("  chaîne trackways/noctis.trail : %s — à construire à la main (Get Actor Data tag noctis.trail -> Spline Sampler 7 m"
            " -> tirage 0,35 -> Transform lacet 90° -> SM_Decal_Trackway_*)" % exc, "WARN")


# ----------------------------------------------------------------------------------------------- main
def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = repo_root()
    if not os.path.exists(args.manifest):
        log("Manifeste absent : %s — lancer Tools/EnvironmentFactory/factory.py" % args.manifest, "ERROR")
        return 1
    with open(args.manifest, encoding="utf-8") as f:
        man = json.load(f)
    if man.get("schema") != "noctis.environment_manifest/1":
        log("Schéma de manifeste inattendu : %s" % man.get("schema"), "ERROR")
        return 1
    log("Manifeste %s : %d assets, %d matériaux, %d sols (%s)" % (man["generator"]["version"], len(man["assets"]), len(man["materials"]),
                                                                len(man["ground_materials"]), "éditeur" if IN_UNREAL else "simulation"))
    instances, meshes = {}, {}
    if "textures" not in args.skip:
        stage_textures(args, man, root)
    if "materials" not in args.skip:
        instances = stage_materials(args, man, root) or {}
    if "meshes" not in args.skip:
        meshes = stage_meshes(args, man, root, instances) or {}
    if "foliage" not in args.skip:
        stage_foliage(args, man, meshes)
    if "pcg" not in args.skip:
        stage_pcg(args, man, root)
    errors = sum(1 for l in LOG_LINES if " ERROR " in l)
    log("Terminé : %d erreur(s), %d avertissement(s)" % (errors, sum(1 for l in LOG_LINES if " WARN " in l)))
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    status = main()
    if not IN_UNREAL:
        sys.exit(status)
