"""Stage 'render': Cycles studio stills of the game asset (low-poly + baked maps, as exported).

Studio (like the reference benchmark): dark charcoal seamless cyclorama with a lighter grey floor,
large soft top key slightly in front, two rim lights behind, a weak fill from the camera side.
Principled BSDF with subsurface scattering (weight from the SSS map), roughness / normal maps, and
the baked micro-occlusion multiplied into the albedo (render only).
Shots (1600x900, Cycles 64 spp + OpenImageDenoise): side, front34, head, flank, midstride (walk clip
mid-stride, deformation check), foot. Output: Output/factory/<sp>/<sp>_<shot>.png + contact sheet.
"""
from __future__ import annotations

import math
import os
import numpy as np

# shot: (eye, target, lens, action, frame or phase, fstop)
SHOTS = {
    "side": ((-0.4, 2.3, 23.0), (-0.6, 2.05, 0.0), 50.0, "idle", 1, None),
    "front34": ((10.8, 3.6, 9.6), (0.9, 2.25, 0.0), 42.0, "idle", 60, None),
    "head": ((7.3, 4.45, 3.3), (3.75, 3.72, 0.1), 50.0, "idle", 1, 11.0),
    "flank": ((2.6, 2.7, 5.6), (0.5, 2.45, 0.35), 45.0, "idle", 1, None),
    "midstride": ((3.5, 3.0, 15.5), (-0.4, 2.0, 0.0), 45.0, "walk", 0.62, None),
    "foot": ((2.2, 0.75, 2.9), (0.5, 0.35, 0.45), 55.0, "idle", 1, 7.0),
}


def setup_body_material(bpy):
    m = bpy.data.materials["M_Body"]
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    tex = {n.image.name: n for n in nt.nodes if n.type == "TEX_IMAGE"}
    bc = [n for k, n in tex.items() if "BaseColor" in k][0]
    orm = [n for k, n in tex.items() if "ORM" in k][0]
    sep = [n for n in nt.nodes if n.type == "SEPARATE_COLOR"][0]
    # albedo x AO^0.6 (micro-occlusion only visible at texel scale)
    pw = nt.nodes.new("ShaderNodeMath")
    pw.operation = "POWER"
    pw.inputs[1].default_value = 0.6
    nt.links.new(sep.outputs["Red"], pw.inputs[0])
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type = "RGBA"
    mul.blend_type = "MULTIPLY"
    mul.inputs[0].default_value = 1.0
    nt.links.new(bc.outputs["Color"], mul.inputs[6])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    for k in ("Red", "Green", "Blue"):
        nt.links.new(pw.outputs[0], comb.inputs[k])
    nt.links.new(comb.outputs[0], mul.inputs[7])
    nt.links.new(mul.outputs[2], p.inputs["Base Color"])
    # subsurface weight from the SSS map
    sp = [k for k in tex if "BaseColor" in k][0].replace("BaseColor", "SSS")
    import os as _os
    path = bpy.path.abspath(bc.image.filepath).replace("BaseColor", "SSS")
    if _os.path.exists(path):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(path, check_existing=True)
        t.image.colorspace_settings.name = "Non-Color"
        mm = nt.nodes.new("ShaderNodeMath")
        mm.operation = "MULTIPLY"
        mm.inputs[1].default_value = 0.35
        nt.links.new(t.outputs["Color"], mm.inputs[0])
        nt.links.new(mm.outputs[0], p.inputs["Subsurface Weight"])
    p.inputs["Subsurface Radius"].default_value = (1.0, 0.38, 0.2)
    p.inputs["Subsurface Scale"].default_value = 0.03
    p.inputs["Specular IOR Level"].default_value = 0.42
    nm = [n for n in nt.nodes if n.type == "NORMAL_MAP"][0]
    nm.inputs["Strength"].default_value = 1.0


def studio(bpy, bl):
    """Lights fixed around the creature (Blender space: creature along Y facing -Y, centre ~(0, 0, 2.3))."""
    from mathutils import Vector
    sc = bpy.context.scene
    bl.emission_world(sc, color=(0.006, 0.006, 0.0065), strength=1.0)
    cyc = bl.cyclorama(depth=70.0, width=90.0, radius=9.0, height=40.0, back=26.0, color=(0.012, 0.012, 0.013),
                       floor_color=(0.05, 0.05, 0.052))
    cyc.rotation_euler = (0, 0, -math.pi / 2)      # wall behind the creature's right side camera (+X_b is the far side)
    c = Vector((0.0, 0.3, 2.3))
    L = []
    L.append(bl.area_light("key", (-3.0, -1.5, 12.5), c, size=9.0, size_y=6.0, power=4200, color=(1.0, 0.965, 0.92), spread=100))
    L.append(bl.area_light("rim_back", (9.0, 4.0, 7.5), c + Vector((0, 0, 1.0)), size=4.0, power=2200, color=(0.85, 0.9, 1.0), spread=70))
    L.append(bl.area_light("rim_head", (6.0, -9.0, 6.5), Vector((0.0, -4.0, 3.9)), size=2.5, power=700, color=(0.9, 0.93, 1.0), spread=70))
    L.append(bl.area_light("fill", (-14.0, -6.0, 3.0), c, size=6.0, power=260, color=(0.9, 0.93, 1.0), spread=120))
    return L


def place_camera(bpy, bl, name, eye, target, lens, fstop=None, offset=np.zeros(3)):
    cam = bl.camera(name, bl.to_bl(np.asarray(eye) + offset), bl.to_bl(np.asarray(target) + offset), lens=lens)
    if fstop:
        cam.data.dof.use_dof = True
        cam.data.dof.focus_distance = float(np.linalg.norm(np.asarray(eye) - np.asarray(target)))
        cam.data.dof.aperture_fstop = fstop
    return cam


def run(ctx):
    import bpy
    from PIL import Image, ImageDraw
    from . import bl
    sp = ctx.species
    bpy.ops.wm.open_mainfile(filepath=ctx.path("anim.blend"))
    sc = bpy.context.scene
    rig = bpy.data.objects["Armature"]
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    setup_body_material(bpy)
    samples = ctx.options.get("samples") or ctx.q["render_samples"]
    bl.cycles(sc, samples=samples, res=tuple(ctx.q["render_res"]))
    sc.cycles.adaptive_threshold = 0.015
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    studio(bpy, bl)
    shots = ctx.options.get("shots")
    names = shots.split(",") if shots else list(SHOTS)
    outs = []
    for name in names:
        eye, tgt, lens, act, fr, fstop = SHOTS[name]
        a = bpy.data.actions[act]
        rig.animation_data.action = a
        if isinstance(fr, float):
            f0, f1 = a.frame_range
            frame = int(round(f0 + fr * (f1 - f0)))
        else:
            frame = fr
        sc.frame_set(frame)
        root = bl.from_bl(np.array(rig.matrix_world @ rig.pose.bones["root"].head))
        off = np.array([root[0], 0.0, root[2]])
        cam = place_camera(bpy, bl, f"cam_{name}", eye, tgt, lens, fstop, off)
        p = ctx.opath(f"{sp}_{name}.png")
        bl.render(sc, cam, p)
        outs.append((name, p))
        ctx.log(f"  rendered {name} (action {act}, frame {frame}) -> {p}")
    # contact sheet of all available shots
    allp = [(n, ctx.opath(f"{sp}_{n}.png")) for n in SHOTS if os.path.exists(ctx.opath(f"{sp}_{n}.png"))]
    if allp:
        W, H = 800, 450
        cols = 2
        rows = math.ceil(len(allp) / cols)
        sheet = Image.new("RGB", (W * cols, H * rows), (18, 18, 20))
        dr = ImageDraw.Draw(sheet)
        for i, (n, pth) in enumerate(allp):
            im = Image.open(pth).convert("RGB").resize((W, H), Image.LANCZOS)
            x, y = (i % cols) * W, (i // cols) * H
            sheet.paste(im, (x, y))
            dr.text((x + 10, y + 8), f"{sp} - {n}", fill=(200, 200, 200))
        sheet.save(ctx.opath(f"{sp}_contact_sheet.png"))
