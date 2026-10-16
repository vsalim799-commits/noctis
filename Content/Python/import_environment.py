# -*- coding: utf-8 -*-
"""Noctis — imports the Hell Creek environment assets of Tools/EnvironmentFactory into /Game/Environment (UE 5.4+).

Reads Build/Environment/manifest.json (written by `factory.py --stages manifest`) and creates:
  /Game/Environment/Textures/{Foliage,Bark,Ground}/T_*         leaf atlases, bark / stem sets, 2K ground sets
  /Game/Environment/Materials/M_Noctis_Foliage, M_Noctis_Bark  master materials (created when absent)
  /Game/Environment/Materials/Instances/MI_<slot>              one instance per material slot of the FBX files
  /Game/Environment/Flora/<Category>/SM_<Name>                 static meshes: LOD0 (+ UCX collision) and LOD1/LOD2
  /Game/Environment/FoliageTypes/FT_<Name>                     foliage types (cull distances, scale, slope, collision)
  /Game/Noctis/PCG/PCG_Noctis_Foliage                          PCG graph (best effort, only when absent; see below)

Run inside the editor (Python Editor Script Plugin):
    py "Content/Python/import_environment.py" [--manifest FILE] [--only conifer_metasequoia,...] [--skip pcg,foliage]
                                              [--overwrite-materials] [--overwrite-pcg]
Run outside Unreal:  python Content/Python/import_environment.py --dry-run     (checks the manifest and every file)

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
LEVEL_TEX_ROOT = "/Game/Noctis/Level/Textures/Terrain"  # foliage_*.png / weight_*.png imported by build_level.py
MASTER_FOLIAGE = MAT_ROOT + "/M_Noctis_Foliage"
MASTER_BARK = MAT_ROOT + "/M_Noctis_Bark"
MPC_WEATHER = "/Game/Environment/MPC_NoctisWeather"  # setup_project.py
CATEGORY_FOLDER = {"tree": "Trees", "shrub": "Shrubs", "palm": "Palms", "fern": "Ferns", "horsetail": "Horsetails",
                   "aquatic": "Aquatic", "herb": "Herbs", "deadwood": "Deadwood"}

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
TEX_KIND = {  # map suffix -> (sRGB, compression)
    "BaseColor": (True, "TC_DEFAULT"), "Normal": (False, "TC_NORMALMAP"), "Roughness": (False, "TC_MASKS"),
    "Opacity": (False, "TC_GRAYSCALE"), "Translucency": (False, "TC_GRAYSCALE"), "Height": (False, "TC_GRAYSCALE"),
}


def texture_list(man, root):
    out = []
    for mname, m in man["materials"].items():
        sub = "Foliage" if m["kind"] == "foliage" else "Bark"
        for mp, path in m["maps"].items():
            out.append((rel(root, path), TEX_ROOT + "/" + sub, mp))
    for g, info in man["ground_materials"].items():
        for mp, path in info["textures"].items():
            out.append((rel(root, path), TEX_ROOT + "/Ground", mp))
    seen, uniq = set(), []
    for t in out:
        if t[0] not in seen:
            seen.add(t[0])
            uniq.append(t)
    return uniq


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
        set_props(tex, lod_group=unreal.TextureGroup.TEXTUREGROUP_WORLD)  # mips kept (alpha-tested foliage, tiling ground)
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
    """World position offset: sway along MPC WindDirection, amplitude * vertex R (flexibility) * WindSpeed,
    phase from vertex G and world position. Simple and cheap; Pivot Painter can replace it later."""
    mpc = eal().load_asset(MPC_WEATHER) if eal().does_asset_exist(MPC_WEATHER) else None
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, x - 900, y)
    time = mel.create_material_expression(mat, unreal.MaterialExpressionTime, x - 900, y + 150)
    freq = _scalar(mel, mat, "WindFrequency", 1.3, x - 900, y + 250)
    amp = _scalar(mel, mat, "WindAmplitudeCm", amplitude_cm, x - 900, y + 350)
    tf = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 700, y + 150)
    mel.connect_material_expressions(time, "", tf, "A")
    mel.connect_material_expressions(freq, "", tf, "B")
    ph = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 700, y + 50)
    mel.connect_material_expressions(vc, "G", ph, "A")
    ph.set_editor_property("const_b", 6.2832)
    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, x - 550, y + 100)
    mel.connect_material_expressions(tf, "", add, "A")
    mel.connect_material_expressions(ph, "", add, "B")
    sine = mel.create_material_expression(mat, unreal.MaterialExpressionSine, x - 420, y + 100)
    mel.connect_material_expressions(add, "", sine, "")
    k = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 300, y + 100)
    mel.connect_material_expressions(sine, "", k, "A")
    mel.connect_material_expressions(vc, "R", k, "B")
    k2 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 180, y + 100)
    mel.connect_material_expressions(k, "", k2, "A")
    mel.connect_material_expressions(amp, "", k2, "B")
    direction = None
    speed = None
    if mpc is not None:
        direction = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x - 300, y + 300)
        set_props(direction, collection=mpc, parameter_name="WindDirection")  # VERIFY property names
        speed = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x - 300, y + 420)
        set_props(speed, collection=mpc, parameter_name="WindSpeed")
    if speed is not None:
        k3 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 80, y + 160)
        mel.connect_material_expressions(k2, "", k3, "A")
        sc = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 180, y + 400)
        mel.connect_material_expressions(speed, "", sc, "A")
        sc.set_editor_property("const_b", 0.15)
        mel.connect_material_expressions(sc, "", k3, "B")
        k2 = k3
    vec = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y + 200)
    mel.connect_material_expressions(k2, "", vec, "A")
    if direction is not None:
        mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x - 120, y + 300)
        set_props(mask, r=True, g=True, b=True, a=False)
        mel.connect_material_expressions(direction, "", mask, "")
        mel.connect_material_expressions(mask, "", vec, "B")
    else:
        c = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x - 120, y + 300)
        c.set_editor_property("constant", unreal.LinearColor(1, 0, 0, 0))
        mel.connect_material_expressions(c, "", vec, "B")
    mel.connect_material_property(vec, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def create_master_foliage(overwrite):
    mel = unreal.MaterialEditingLibrary

    def configure(mat):
        mel.delete_all_material_expressions(mat)
        set_props(mat, blend_mode=unreal.BlendMode.BLEND_MASKED, two_sided=True,
                  shading_model=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, opacity_mask_clip_value=0.45)
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
        if m["kind"] == "bark" and ("Deadwood" in mname or "SoilClod" in mname):
            mel.set_material_instance_scalar_parameter_value(mi, "WindAmplitudeCm", 0.0)  # deadwood does not sway
        eal().save_loaded_asset(mi)
        out[mname] = mi
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
    log("%d maillages (LOD0 + LOD1 + LOD2)" % len(assets))
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
        for lod in (1, 2):
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
            log("[simulation] %s : cull %s-%s cm, échelle %s, pente max %s°, collision %s" % (name, cull["start"], cull["end"], s[4], s[3], s[5]))
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
        set_props(ft, align_to_normal=s[0], align_max_angle=s[1], random_pitch_angle=s[2], random_yaw=True,
                  ground_slope_angle=unreal.FloatInterval(min=0.0, max=s[3]),
                  scale_x=unreal.FloatInterval(min=s[4][0], max=s[4][1]), scaling=unreal.FoliageScaling.UNIFORM)
        set_props(ft, cast_shadow=True, enable_density_scaling=a["category"] in ("fern", "horsetail", "herb", "shrub", "aquatic"))
        try:
            bi = ft.get_editor_property("body_instance")
            bi.set_editor_property("collision_profile_name", s[5])
            ft.set_editor_property("body_instance", bi)
        except Exception as exc:
            log("%s : collision à régler (%s)" % (name, exc), "WARN")
        eal().save_loaded_asset(ft)
        log("%s prêt" % name)


# ----------------------------------------------------------------------------------------------- PCG
PCG_NOTE = """PCG_Noctis_Foliage (une branche par type fonctionnel, voir Docs/ENVIRONMENT.md §4) :
  Get Landscape Data -> Surface Sampler (points/m² = per_ha/10000 de la règle, Looseness 1) ->
  Get Texture Data T_Foliage<Pft> (bornes du paysage) -> Attribute Op Density *= R (masque de densité) -> Density ^ gamma ->
  Density Noise (taille = cluster_m, amplitude = cluster_amp) -> Density Filter (0.5..1) ->
  Normal To Density / Filter by slope (pente < slope_max_deg) -> Difference avec T_WeightChannel > 0.3 dilaté de water_buffer_m ->
  Self Pruning (rayon = min_spacing_m) -> Transform Points (lacet 0-360°, échelle 0.82-1.15) ->
  Static Mesh Spawner (sélecteur pondéré : assets[] et parts de la règle).
Bois mort (§4.2) : une chaîne par (asset, habitat) : Surface Sampler (inst./ha de l'habitat) x T_Weight<Habitat>
  -> Self Pruning (espacement de la règle) -> Transform Points -> Static Mesh Spawner SM_Deadwood_*."""


@guarded("5. Graphe PCG")
def stage_pcg(args, man):
    rules = man["scatter"]["pft_rules"]
    for pft, r in rules.items():
        log("PCG %s : masque %s (gamma %s), %s inst./ha, espacement %s m, pente < %s°, tampon eau %s m, assets %s" % (
            pft, r["mask"], r.get("density_gamma", 1.0), r["per_ha"], r["min_spacing_m"], r["slope_max_deg"], r["water_buffer_m"], r["assets"]))
    for a, r in man["scatter"]["deadwood_rules"].items():
        log("PCG bois mort %s : densités par habitat %s (près des rivières : berges, bancs, forêts riveraines)" % (a, r["habitats"]))
    if args.dry_run:
        log(PCG_NOTE)
        return
    path = PCG_ROOT + "/PCG_Noctis_Foliage"
    if eal().does_asset_exist(path) and not args.overwrite_pcg:
        log("%s existe : conservé (--overwrite-pcg pour le régénérer)" % path)
        return
    try:
        ensure_dir(PCG_ROOT)
        graph = eal().load_asset(path) if eal().does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "PCG_Noctis_Foliage", PCG_ROOT, unreal.PCGGraph, unreal.PCGGraphFactory())  # VERIFY factory name
        build_pcg_graph(graph, man)
        eal().save_loaded_asset(graph)
        log("Graphe PCG créé : %s" % path)
    except Exception as exc:
        log("Graphe PCG non créé automatiquement (%s). Construction manuelle :\n%s" % (exc, PCG_NOTE), "WARN")


def _mask_texture_path(mask):
    """weight_sandbar -> /Game/Noctis/Level/Textures/Terrain/T_WeightSandbar (asset names of build_level.py)."""
    return "%s/T_%s" % (LEVEL_TEX_ROOT, "".join(p.capitalize() for p in mask.split("_")))


def build_pcg_graph(graph, man):
    """Best effort: one chain per plant functional type, plus one chain per (deadwood asset, habitat).
    VERIFY every node class / pin name on 5.4."""
    def add(cls):
        res = graph.add_node_of_type(cls)  # VERIFY: returns (node, settings) in Python
        return res if isinstance(res, tuple) else (res, res.get_settings())

    out_node = graph.get_output_node()
    land, _ = add(unreal.PCGGetLandscapeSettings)  # VERIFY class name ("Get Landscape Data")

    def chain(label, per_ha, mask, spacing_m, assets, scale=(0.82, 1.15)):
        """Surface Sampler -> x density mask -> Density Filter -> Self Pruning -> Transform -> weighted Spawner."""
        samp, ss = add(unreal.PCGSurfaceSamplerSettings)
        set_props(ss, points_per_squared_meter=float(per_ha) / 10000.0, looseness=1.0)
        graph.add_edge(land, "Out", samp, "Surface")
        tex, ts = add(unreal.PCGGetTextureDataSettings)  # VERIFY ("Get Texture Data")
        tpath = _mask_texture_path(mask)
        if eal().does_asset_exist(tpath):
            set_props(ts, texture=eal().load_asset(tpath))
        else:
            log("%s : texture %s absente (build_level.py l'importe)" % (label, tpath), "WARN")
        # Point density *= mask value: intersection of the sampled points with the texture data (VERIFY class / pins)
        cls = next(getattr(unreal, c) for c in ("PCGInnerIntersectionSettings", "PCGIntersectionSettings", "PCGProjectionSettings")
                   if hasattr(unreal, c))
        sm, _ = add(cls)
        graph.add_edge(samp, "Out", sm, "In")
        graph.add_edge(tex, "Out", sm, "In")
        flt, fs = add(unreal.PCGDensityFilterSettings)
        set_props(fs, lower_bound=0.5, upper_bound=1.0)
        graph.add_edge(sm, "Out", flt, "In")
        prune, ps = add(unreal.PCGSelfPruningSettings)
        set_props(ps, radius_similarity_factor=0.25)
        graph.add_edge(flt, "Out", prune, "In")
        tr, trs = add(unreal.PCGTransformPointsSettings)
        set_props(trs, rotation_min=unreal.Rotator(0, 0, 0), rotation_max=unreal.Rotator(0, 0, 360),
                  scale_min=unreal.Vector(scale[0], scale[0], scale[0]), scale_max=unreal.Vector(scale[1], scale[1], scale[1]),
                  uniform_scale=True)
        graph.add_edge(prune, "Out", tr, "In")
        sp, sps = add(unreal.PCGStaticMeshSpawnerSettings)
        try:
            sps.set_editor_property("mesh_selector_type", unreal.PCGMeshSelectorWeighted)  # VERIFY
            sel = sps.get_editor_property("mesh_selector_parameters")
            entries = []
            for aid, w in assets.items():
                a = man["assets"].get(aid)
                if not a:
                    continue
                mp = FLORA_ROOT + "/" + CATEGORY_FOLDER.get(a["category"], "Misc") + "/" + a["ue_name"]
                e = unreal.PCGMeshSelectorWeightedEntry()
                d = e.get_editor_property("descriptor")
                d.set_editor_property("static_mesh", eal().load_asset(mp))
                d.set_editor_property("instance_end_cull_distance", int(a["cull_distance_cm"]["end"]))  # VERIFY
                e.set_editor_property("descriptor", d)
                e.set_editor_property("weight", max(1, int(round(w * 100))))
                entries.append(e)
            sel.set_editor_property("mesh_entries", entries)
        except Exception as exc:
            log("Spawner %s : maillages à choisir à la main (%s)" % (label, exc), "WARN")
        graph.add_edge(tr, "Out", sp, "In")
        graph.add_edge(sp, "Out", out_node, "In")

    for pft, r in man["scatter"]["pft_rules"].items():
        chain(pft, r["per_ha"], r["mask"], r["min_spacing_m"], r["assets"])
    # Deadwood (not a simulated plant type): one chain per habitat weight map, denser near the channel
    # (riverbank, sandbar, cutbank, riparian forest). Docs/ENVIRONMENT.md section 4.2.
    for aid, r in man["scatter"]["deadwood_rules"].items():
        for hab, per_ha in r["habitats"].items():
            chain("%s/%s" % (aid, hab), per_ha, "weight_" + hab, r["min_spacing_m"], {aid: 1.0}, scale=(0.8, 1.2))


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
        stage_pcg(args, man)
    errors = sum(1 for l in LOG_LINES if " ERROR " in l)
    log("Terminé : %d erreur(s), %d avertissement(s)" % (errors, sum(1 for l in LOG_LINES if " WARN " in l)))
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    status = main()
    if not IN_UNREAL:
        sys.exit(status)
