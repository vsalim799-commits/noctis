"""Crossed-billboard LOD3 for the trees and the tall palm (part of the 'flora' stage, after the library is saved).

For each tree-sized asset, the LOD1 mesh is rendered once in Cycles from the side with an orthographic camera and a
transparent film: the Diffuse Color pass gives the albedo, the alpha of the combined pass the opacity, the Normal
pass (converted to the card's tangent space, DirectX convention) the normal map. LOD3 = 3 vertical cards crossed at
60 deg (6 triangles, two-sided foliage material), exported as SM_<Name>_LOD3.fbx next to the other LODs.
In Unreal it is imported as LOD3 (screen size ~0.035); beyond it, World Partition HLOD still applies.
"""
from __future__ import annotations

import math
import os

import numpy as np

from . import catalog, common
from . import texgen as tg
from .meshkit import Mesh

IMPOSTOR_CATEGORIES = ("tree", "palm")
LOD3_SCREEN_SIZE = 0.035
PX = 1024  # longest side of the impostor texture


def eligible(aid):
    a = catalog.ASSETS[aid]
    return a["category"] in IMPOSTOR_CATEGORIES and a["spec"].get("trunk_h", 10.0) >= 1.0


def prefix(aid):
    return "Leaf_Impostor" + "".join(w.capitalize() for w in aid.split("_"))


def _render(ob_list, half_w, z0, z1, path_base, samples=24):
    """Orthographic side render of the objects; returns (albedo rgb, alpha, normal tangent rgb) arrays."""
    import bpy
    from mathutils import Vector
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.transparent_max_bounces = 64
    sc.render.film_transparent = True
    w_m, h_m = 2 * half_w, z1 - z0
    if w_m >= h_m:
        rx, ry = PX, max(64, int(round(PX * h_m / w_m)))
    else:
        rx, ry = max(64, int(round(PX * w_m / h_m))), PX
    sc.render.resolution_x, sc.render.resolution_y = rx, ry
    sc.render.resolution_percentage = 100
    sc.render.threads_mode = "FIXED"
    sc.render.threads = 2
    sc.view_settings.view_transform = "Standard"
    vl = sc.view_layers[0]
    vl.use_pass_diffuse_color = True
    vl.use_pass_normal = True
    cam = bpy.data.objects.get("ImpostorCam") or bpy.data.objects.new("ImpostorCam", bpy.data.cameras.new("ImpostorCam"))
    if cam.name not in sc.collection.objects:
        sc.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(w_m, h_m)
    cam.location = Vector((0.0, -200.0, (z0 + z1) / 2))
    cam.rotation_euler = (math.radians(90), 0, 0)  # looks along +Y
    cam.data.clip_start, cam.data.clip_end = 1.0, 500.0
    sc.camera = cam
    w = bpy.data.worlds.get("ImpostorWorld") or bpy.data.worlds.new("ImpostorWorld")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    sc.use_nodes = True
    nt = sc.node_tree
    nt.nodes.clear()
    rl = nt.nodes.new("CompositorNodeRLayers")
    comp = nt.nodes.new("CompositorNodeComposite")
    nt.links.new(rl.outputs["Image"], comp.inputs["Image"])
    fo = nt.nodes.new("CompositorNodeOutputFile")
    fo.base_path = os.path.dirname(path_base)
    fo.format.file_format = "OPEN_EXR"
    fo.format.color_depth = "32"
    fo.file_slots.clear()
    for nm in ("diff", "alpha", "normal"):
        fo.file_slots.new(os.path.basename(path_base) + "_" + nm + "_")
    sa = nt.nodes.new("CompositorNodeSetAlpha")
    nt.links.new(rl.outputs["DiffCol"], sa.inputs["Image"])
    nt.links.new(rl.outputs["Alpha"], sa.inputs["Alpha"])
    nt.links.new(sa.outputs["Image"], fo.inputs[0])
    nt.links.new(rl.outputs["Alpha"], fo.inputs[1])
    nt.links.new(rl.outputs["Normal"], fo.inputs[2])
    sc.frame_current = 1
    bpy.ops.render.render(write_still=False)
    out = []
    for nm in ("diff", "alpha", "normal"):
        p = os.path.join(os.path.dirname(path_base), os.path.basename(path_base) + "_" + nm + "_0001.exr")
        im = bpy.data.images.load(p)
        a = np.array(im.pixels[:], np.float32).reshape(im.size[1], im.size[0], 4)[::-1]
        bpy.data.images.remove(im)
        os.remove(p)
        out.append(a)
    return out


def _cleanup(state):
    """Put the scene back as it was: the library .blend is saved right after, it must not keep the impostor setup."""
    import bpy
    sc = bpy.context.scene
    vl = sc.view_layers[0]
    if sc.node_tree:
        sc.node_tree.nodes.clear()
    sc.use_nodes = state["use_nodes"]
    sc.render.film_transparent = state["film_transparent"]
    sc.camera = state["camera"]
    sc.world = state["world"]
    vl.use_pass_diffuse_color, vl.use_pass_normal = state["passes"]
    cam = bpy.data.objects.get("ImpostorCam")
    if cam is not None:
        data = cam.data
        bpy.data.objects.remove(cam, do_unlink=True)
        bpy.data.cameras.remove(data)
    w = bpy.data.worlds.get("ImpostorWorld")
    if w is not None and w is not state["world"]:
        bpy.data.worlds.remove(w)


def run(lod_objects_by_asset):
    """lod_objects_by_asset: {aid: blender object of LOD1}. Writes textures, LOD3 FBX, returns {aid: info}."""
    import bpy
    sc = bpy.context.scene
    state = dict(use_nodes=sc.use_nodes, film_transparent=sc.render.film_transparent, camera=sc.camera, world=sc.world,
                 passes=(sc.view_layers[0].use_pass_diffuse_color, sc.view_layers[0].use_pass_normal))
    try:
        return _run(lod_objects_by_asset)
    finally:
        _cleanup(state)


def _run(lod_objects_by_asset):
    import bpy
    res = {}
    tmp = os.path.join(common.BUILD, "impostor_tmp")
    os.makedirs(tmp, exist_ok=True)
    for aid, ob in lod_objects_by_asset.items():
        a = catalog.ASSETS[aid]
        # isolate the object in render
        hidden = []
        for o in bpy.context.scene.objects:
            if o.type == "MESH" and o is not ob and not o.hide_render:
                o.hide_render = True
                hidden.append(o)
        prev = ob.hide_render
        ob.hide_render = False
        V = np.array([v.co[:] for v in ob.data.vertices])
        half_w = float(max(np.abs(V[:, 0]).max(), np.abs(V[:, 1]).max())) * 1.02
        z0, z1 = min(0.0, float(V[:, 2].min())), float(V[:, 2].max()) * 1.01
        diff, alpha, nrm = _render([ob], half_w, z0, z1, os.path.join(tmp, aid))
        ob.hide_render = prev
        for o in hidden:
            o.hide_render = False
        al = np.clip(alpha[..., 0], 0, 1)
        col = np.clip(diff[..., :3], 0, 1)
        col = tg.linear_to_srgb(col)
        col = tg.dilate_color(col, al)
        # world normal -> card tangent space (card faces -Y: X right, Z up, -Y towards the viewer)
        n = nrm[..., :3]
        t = np.stack([n[..., 0], n[..., 2], -n[..., 1]], -1)
        t = np.where((t[..., 2] < 0)[..., None], -t, t)  # two-sided cards: back faces turned towards the viewer
        t = 0.5 * t + 0.5 * np.array([0.0, 0.0, 1.0])     # calmer lighting of the distant crown
        t = t / np.maximum(np.linalg.norm(t, axis=-1, keepdims=True), 1e-6)
        t = np.where((al > 0.01)[..., None], t, np.array([0.0, 0.0, 1.0]))
        t[..., 1] *= -1  # DirectX
        maps = dict(basecolor=col, opacity=al, normal=tg.to_u8(t * 0.5 + 0.5), translucency=al * 0.35,
                    roughness=np.full(al.shape, 0.75))
        files = common.save_texture_set(prefix(aid), maps, ("BaseColor", "Normal", "Opacity", "Translucency", "Roughness"))
        # LOD3 mesh: 3 crossed cards
        m = Mesh()
        w = 2 * half_w
        for k in range(3):
            th = math.radians(60 * k)
            side = np.array([math.cos(th), math.sin(th), 0.0])
            m.card(np.array([0.0, 0.0, z0]), np.array([0.0, 0.0, 1.0]), side, w, z1 - z0, (0.0, 0.0, 1.0, 1.0), 0,
                   normal=np.array([-math.sin(th), math.cos(th), 0.0]), wind_phase=0.1 * k, flex=(0.0, 0.3), segments=1,
                   height_norm=max(z1, 1.0))
        from .assets import material_name, blender_materials
        mats = blender_materials([prefix(aid)])
        name = a["ue_name"] + "_LOD3"
        lob = m.to_blender(name, mats)
        fn = os.path.join(common.FBX_DIR, aid, name + ".fbx")
        common.export_fbx([lob], fn)
        bpy.data.objects.remove(lob, do_unlink=True)
        res[aid] = dict(file=os.path.relpath(fn, common.ROOT), triangles=m.tri_count(), material=material_name(prefix(aid)),
                        textures=files, size_m=[round(w, 2), round(z1 - z0, 2)])
        common.log("impostor %s: %s px, %.1f x %.1f m" % (aid, al.shape[::-1], w, z1 - z0))
    return res
