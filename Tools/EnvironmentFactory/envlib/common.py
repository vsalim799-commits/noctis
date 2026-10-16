"""Paths, logging, texture-set writers and the FBX exporter shared by the environment factory stages."""
from __future__ import annotations

import json
import os
import sys
import time

import numpy as np

from . import texgen as tg

TOOL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.abspath(os.path.join(TOOL_DIR, "..", ".."))
BUILD = os.path.join(ROOT, "Build", "Environment")
SIM_DIR = os.path.join(BUILD, "sim")
TEX_DIR = os.path.join(BUILD, "textures")
FBX_DIR = os.path.join(BUILD, "fbx")
META_DIR = os.path.join(BUILD, "meta")
LIB_DIR = os.path.join(BUILD, "library")
LIBRARY_BLEND = os.path.join(LIB_DIR, "noctis_flora.blend")
GROUND_DIR = os.path.join(BUILD, "ground")
LANDSCAPE_DIR = os.path.join(BUILD, "landscape")
MANIFEST = os.path.join(BUILD, "manifest.json")
OUT_DIR = os.path.join(ROOT, "Output", "environment")
NOCTIS_SIM = os.path.join(ROOT, "build", "noctis_sim")

# FBX for UE5 (1 unit = 1 cm): the export scene uses unit scale 0.01 and geometry x100, so the file carries
# UnitScaleFactor = 1 and no node scaling (same convention as Tools/CreatureFactory).
FBX_SETTINGS = dict(axis_forward="-Z", axis_up="Y", apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
                    global_scale=1.0, bake_space_transform=False, mesh_smooth_type="FACE", use_tspace=True,
                    use_triangles=True, use_mesh_modifiers=True, path_mode="STRIP", object_types={"MESH"},
                    colors_type="LINEAR", use_custom_props=False, add_leaf_bones=False, bake_anim=False)

_T0 = time.time()


def log(msg):
    print("[env %7.1fs] %s" % (time.time() - _T0, msg), flush=True)


def ensure_dirs():
    for d in (BUILD, TEX_DIR, FBX_DIR, META_DIR, LIB_DIR, GROUND_DIR, LANDSCAPE_DIR, OUT_DIR):
        os.makedirs(d, exist_ok=True)


def texture_path(prefix, map_name):
    return os.path.join(TEX_DIR, "T_%s_%s.png" % (prefix, map_name))


def save_texture_set(prefix, maps, kinds=("BaseColor", "Normal", "Roughness", "Height", "Opacity", "Translucency"),
                     directory=None):
    """Writes T_<prefix>_<Map>.png. BaseColor sRGB 8 bit, Normal DirectX 8 bit, Height 16 bit, the rest 8 bit linear."""
    files = {}
    key = {"BaseColor": "basecolor", "Normal": "normal", "Roughness": "roughness", "Height": "height",
           "Opacity": "opacity", "Translucency": "translucency"}
    for k in kinds:
        a = maps.get(key[k])
        if a is None:
            continue
        p = os.path.join(directory or TEX_DIR, "T_%s_%s.png" % (prefix, k))
        if k == "Height":
            tg.save_png(p, a, bits=16)
        else:
            tg.save_png(p, a)
        files[k] = os.path.relpath(p, ROOT)
    return files


def textures_exist(prefix, kinds):
    return all(os.path.exists(texture_path(prefix, k)) for k in kinds)


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False, default=_json_default)


def _json_default(o):
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    raise TypeError(type(o))


def read_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def reset_blender():
    import bpy
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)


def export_fbx(objects, path):
    """Exports mesh objects (metres, pivot at origin) to an FBX in centimetres for UE5.
    Objects named UCX_<mesh> are exported as-is (Unreal convex collision convention)."""
    import bpy
    os.makedirs(os.path.dirname(path), exist_ok=True)
    src_scene = bpy.context.window.scene if bpy.context.window else bpy.context.scene
    tmp = bpy.data.scenes.new("fbx_export_tmp")
    tmp.unit_settings.system = "METRIC"
    tmp.unit_settings.scale_length = 0.01
    tmp.unit_settings.length_unit = "CENTIMETERS"
    copies = []
    for ob in objects:
        me = ob.data.copy()
        me.transform(ob.matrix_world)
        import mathutils
        me.transform(mathutils.Matrix.Scale(100.0, 4))
        cp = bpy.data.objects.new(ob.name, me)
        tmp.collection.objects.link(cp)
        copies.append(cp)
    # Exported names must match exactly (Blender may have suffixed them with .001 when the originals exist).
    for ob, cp in zip(objects, copies):
        if cp.name != ob.name:
            orig = ob.name
            ob.name = orig + "__src"
            cp.name = orig
            ob["_orig_name"] = orig
    if bpy.context.window:
        bpy.context.window.scene = tmp
    with bpy.context.temp_override(scene=tmp, view_layer=tmp.view_layers[0]):
        for o in tmp.objects:
            o.select_set(True)
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, **FBX_SETTINGS)
    if bpy.context.window:
        bpy.context.window.scene = src_scene
    for cp in copies:
        me = cp.data
        bpy.data.objects.remove(cp)
        bpy.data.meshes.remove(me)
    for ob in objects:
        if "_orig_name" in ob:
            ob.name = ob["_orig_name"]
            del ob["_orig_name"]
    bpy.data.scenes.remove(tmp)
    return path


def fbx_check(path):
    """Units / extents read back from the binary FBX (reuses the creature factory's minimal reader)."""
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("cf_export", os.path.join(ROOT, "Tools", "CreatureFactory", "lib", "export.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        fbx_report = mod.fbx_report
    except Exception as exc:
        return {"error": "fbx reader unavailable: %s" % exc}
    try:
        return fbx_report(path)
    except Exception as exc:  # pragma: no cover
        return {"error": str(exc)}


class Timer:
    def __init__(self):
        self.t = {}

    def run(self, name, fn, *a, **k):
        t0 = time.time()
        r = fn(*a, **k)
        self.t[name] = round(time.time() - t0, 1)
        log("%s: %.1f s" % (name, self.t[name]))
        return r
