"""Stage 'sheet': contact sheet of every asset (Cycles thumbnails of LOD0) and of the ground texture sets.

Output: Output/environment/assets_sheet.png (+ thumbnails in Build/Environment/sheet/).
"""
from __future__ import annotations

import math
import os
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from . import catalog, common

THUMB = 400


def _font(size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def render_thumbs(ids=None, samples=20, size=THUMB, threads=2, lod=0, out_dir=None):
    import bpy
    from mathutils import Vector
    out_dir = out_dir or os.path.join(common.BUILD, "sheet")
    os.makedirs(out_dir, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=common.LIBRARY_BLEND)
    sc = bpy.context.scene
    ids = ids or [a for a in catalog.ASSETS if bpy.data.collections.get("%s_LOD0" % a)]
    # hide everything
    root = bpy.data.collections["NoctisFlora"]
    for c in root.children:
        c.hide_render = True
    sc.render.engine = "CYCLES"
    cy = sc.cycles
    cy.device = "CPU"
    cy.samples = samples
    cy.use_adaptive_sampling = True
    cy.use_denoising = True
    cy.transparent_max_bounces = 32
    cy.max_bounces = 6
    sc.render.resolution_x = sc.render.resolution_y = size
    sc.render.threads_mode = "FIXED"
    sc.render.threads = threads
    sc.render.film_transparent = False
    sc.view_settings.view_transform = "AgX"
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    w = bpy.data.worlds.new("sheet_world")
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.62, 0.70, 0.80, 1)
    bg.inputs[1].default_value = 0.9
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = 3.5
    sun.data.angle = math.radians(3)
    sun.rotation_euler = (math.radians(45), 0, math.radians(-35))
    import bmesh
    me = bpy.data.meshes.new("ground")
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, radius=60, segments=48)
    bm.to_mesh(me)
    g = bpy.data.objects.new("ground", me)
    sc.collection.objects.link(g)
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.20, 0.17, 0.13, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    me.materials.append(gm)
    water = None
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 50
    out = {}
    for aid in ids:
        c = bpy.data.collections.get("%s_LOD%d" % (aid, lod))
        if c is None:
            continue
        c.hide_render = False
        obs = [o for o in c.objects if o.type == "MESH"]
        P = np.concatenate([np.array([o.matrix_world @ Vector(b) for b in o.bound_box]) for o in obs])
        lo, hi = P.min(0), P.max(0)
        ctr = (lo + hi) / 2
        ext = hi - lo
        rad = 0.5 * float(np.linalg.norm(ext))
        dist = rad / math.tan(math.radians(18.0)) * 1.05
        el = math.radians(14 if ext[2] > 1.5 else 32)
        az = math.radians(-35)
        tgt = Vector((ctr[0], ctr[1], ctr[2]))
        cam.location = tgt + Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el))) * dist
        cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
        cam.data.clip_start = max(0.01, dist * 0.01)
        cam.data.clip_end = dist * 10
        g.hide_render = False
        if catalog.ASSETS[aid].get("on_water") or catalog.ASSETS[aid]["category"] == "aquatic":
            gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.05, 0.07, 0.05, 1)
            gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.08
        else:
            gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.20, 0.17, 0.13, 1)
            gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
        path = os.path.join(out_dir, "%s_lod%d.png" % (aid, lod))
        sc.render.filepath = path
        t0 = time.time()
        bpy.ops.render.render(write_still=True)
        out[aid] = path
        common.log("thumb %s (%.1f s)" % (aid, time.time() - t0))
        c.hide_render = True
    return out


def ground_swatch(name, size=THUMB):
    """Lit preview (numpy) of a tiled ground texture set: 2 x 2 tiles to show seamlessness."""
    base = os.path.join(common.GROUND_DIR, "T_Ground_%s_BaseColor.png" % name)
    nrm = os.path.join(common.GROUND_DIR, "T_Ground_%s_Normal.png" % name)
    if not os.path.exists(base):
        return None
    a = np.asarray(Image.open(base).convert("RGB").resize((size // 2, size // 2), Image.LANCZOS)).astype(float) / 255
    n = np.asarray(Image.open(nrm).convert("RGB").resize((size // 2, size // 2), Image.LANCZOS)).astype(float) / 255 * 2 - 1
    n[..., 1] *= -1  # DirectX -> OpenGL
    l = np.array([-0.5, 0.45, 0.75])
    l /= np.linalg.norm(l)
    sh = np.clip((n * l).sum(-1), 0, 1)
    img = a * (0.25 + 0.85 * sh[..., None])
    img = np.tile(img, (2, 2, 1))
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


def compose(thumbs, metas, ground_names, path, cols=7):
    tiles = []
    for aid, p in thumbs.items():
        im = Image.open(p).convert("RGB")
        m = metas.get(aid, {})
        tris = [l["triangles"] for l in m.get("lods", [])]
        h = m.get("bounds_m", {}).get("height")
        tiles.append((im, catalog.ASSETS[aid]["ue_name"].replace("SM_", ""), "%s tris · %.1f m" % ("/".join("%dk" % round(t / 1000) if t >= 1000 else str(t) for t in tris), h or 0)))
    for g in ground_names:
        im = ground_swatch(g)
        if im is not None:
            tiles.append((im, "Ground_" + g, "2K tileable (2x2)"))
    rows = math.ceil(len(tiles) / cols)
    pad, lab = 8, 46
    W = cols * (THUMB + pad) + pad
    H = rows * (THUMB + lab + pad) + pad + 60
    sheet = Image.new("RGB", (W, H), (24, 24, 26))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 14), "NOCTIS · Hell Creek (Maastrichtien) · EnvironmentFactory — assets LOD0 (triangles LOD0/LOD1/LOD2, hauteur)",
           fill=(230, 230, 230), font=_font(26))
    f1, f2 = _font(17), _font(15)
    for i, (im, a, b) in enumerate(tiles):
        r, c = divmod(i, cols)
        x = pad + c * (THUMB + pad)
        y = 60 + pad + r * (THUMB + lab + pad)
        sheet.paste(im.resize((THUMB, THUMB)), (x, y))
        d.text((x + 4, y + THUMB + 3), a, fill=(235, 235, 235), font=f1)
        d.text((x + 4, y + THUMB + 24), b, fill=(170, 170, 170), font=f2)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)
    return path


def run(samples=20, ids=None):
    t0 = time.time()
    thumbs = render_thumbs(ids=ids, samples=samples)
    if ids:  # partial refresh: keep the cached thumbnails of the other assets
        d = os.path.join(common.BUILD, "sheet")
        thumbs = {a: thumbs.get(a) or os.path.join(d, "%s_lod0.png" % a) for a in catalog.ASSETS
                  if a in thumbs or os.path.exists(os.path.join(d, "%s_lod0.png" % a))}
    metas = {aid: common.read_json(os.path.join(common.META_DIR, aid + ".json"), {}) for aid in thumbs}
    from . import ground
    path = compose(thumbs, metas, list(ground.MATERIALS), os.path.join(common.OUT_DIR, "assets_sheet.png"))
    common.log("sheet: %s (%.1f s)" % (path, time.time() - t0))
    return path, time.time() - t0
