"""Silhouette check of the small plants at their LOD switch distances (review aid, not a factory stage).

Output/environment/lod_check_small_plants.png: for each (LOD, distance) pair, the horsetail stands (both variants)
and two ferns side by side (1.6 m apart), seen from eye level (1.6 m) through a 26 mm lens (~69 deg horizontal
field, a typical game camera) at 1920 x 1080; the region around the plants is cropped and enlarged with
nearest-neighbour filtering, so each panel shows the real on-screen pixels.
    /home/user/.venvs/noctis-tools/bin/python -c "import sys; sys.path.insert(0, 'Tools/EnvironmentFactory'); \\
        from envlib import lodcheck; lodcheck.run()"
"""
from __future__ import annotations

import math
import os

from . import common

ASSETS = ("horsetail_unbranched", "horsetail_branched", "fern_tufted", "fern_marsh")
ROWS = ((0, 8.0), (1, 20.0), (2, 40.0), (2, 60.0))


def run(samples=16, assets=ASSETS, rows=ROWS, panel_h=360):
    import bpy
    import numpy as np
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector
    from PIL import Image, ImageDraw
    from .sheet import _font
    bpy.ops.wm.open_mainfile(filepath=common.LIBRARY_BLEND)
    sc = bpy.context.scene
    # exclude the source collections from the view layer (hide_render on a collection would also hide its instances)
    for lc in bpy.context.view_layer.layer_collection.children:
        lc.exclude = True
    sc.use_nodes = False
    sc.render.film_transparent = False
    me = bpy.data.meshes.new("ground")
    me.from_pydata([(-300, -10, 0), (300, -10, 0), (300, 300, 0), (-300, 300, 0)], [], [(0, 1, 2, 3)])
    g = bpy.data.objects.new("ground", me)
    sc.collection.objects.link(g)
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.13, 0.09, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    me.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 26
    cam.data.sensor_width = 36
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(55), 0, math.radians(30))
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.62, 0.72, 1)
    sc.render.engine = "CYCLES"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.transparent_max_bounces = 64
    sc.render.resolution_x, sc.render.resolution_y = 1920, 1080
    sc.render.resolution_percentage = 100
    sc.render.threads_mode = "FIXED"
    sc.render.threads = 2
    sc.view_settings.view_transform = "AgX"
    panels = []
    tmp = os.path.join(common.BUILD, "lodcheck")
    os.makedirs(tmp, exist_ok=True)
    for k, (lod, dist) in enumerate(rows):
        for o in [o for o in sc.collection.objects if o.name.startswith("lc_")]:
            bpy.data.objects.remove(o, do_unlink=True)
        for j, aid in enumerate(assets):
            coll = bpy.data.collections.get("%s_LOD%d" % (aid, lod))
            inst = bpy.data.objects.new("lc_%s" % aid, None)
            inst.instance_type = "COLLECTION"
            inst.instance_collection = coll
            inst.location = ((j - (len(assets) - 1) / 2) * 1.6, dist, 0.0)
            inst.rotation_euler = (0, 0, 0.7 * j)
            sc.collection.objects.link(inst)
        cam.location = Vector((0, 0, 1.6))
        cam.rotation_euler = (Vector((0, dist, 0.6)) - cam.location).to_track_quat("-Z", "Y").to_euler()
        bpy.context.view_layer.update()
        corners = [Vector((x, dist + dy, z)) for x in (-3.4, 3.4) for dy in (-1.0, 1.0) for z in (0.0, 1.5)]
        uv = np.array([world_to_camera_view(sc, cam, c)[:2] for c in corners])
        u0, v0 = np.clip(uv.min(0) - 0.01, 0, 1)
        u1, v1 = np.clip(uv.max(0) + 0.01, 0, 1)
        sc.render.use_border = True
        sc.render.use_crop_to_border = True
        sc.render.border_min_x, sc.render.border_max_x = float(u0), float(u1)
        sc.render.border_min_y, sc.render.border_max_y = float(v0), float(v1)
        p = os.path.join(tmp, "row%d.png" % k)
        sc.render.filepath = p
        bpy.ops.render.render(write_still=True)
        im = Image.open(p).convert("RGB")
        s = panel_h / im.size[1]
        im = im.resize((max(1, int(im.size[0] * s)), panel_h), Image.NEAREST)
        panels.append((im, "LOD%d à %d m (1,5 m de haut ≈ %d px à l'écran)" % (lod, dist, int((v1 - v0) * 1080))))
    W = sum(im.size[0] for im, _ in panels) + 10 * (len(panels) + 1)
    sheet = Image.new("RGB", (W, panel_h + 70), (24, 24, 26))
    d = ImageDraw.Draw(sheet)
    d.text((10, 8), "Prêles (sans rameaux, à verticilles) et fougères aux distances de LOD — objectif 26 mm, 1920 x 1080, pixels agrandis",
           fill=(230, 230, 230), font=_font(18))
    x = 10
    for im, lab in panels:
        sheet.paste(im, (x, 36))
        d.text((x + 4, 36 + panel_h + 6), lab, fill=(200, 200, 200), font=_font(15))
        x += im.size[0] + 10
    out = os.path.join(common.OUT_DIR, "lod_check_small_plants.png")
    sheet.save(out)
    return out
