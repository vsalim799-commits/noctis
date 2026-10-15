"""Blender (bpy 4.2, headless) helpers: anatomy<->Blender axes, numpy mesh I/O, the Cycles studio
(charcoal cyclorama, soft top key, rim, fill), cameras and materials.

Axes. Anatomy space: +x forward, +y up, +z animal right (metres). Blender scene: Z up and the
creature faces -Y (Blender's 'front' convention), so its right side is -X:
    blender = (-z, -x, y)          anatomy = (-Y_b, Z_b, -X_b)
"""
from __future__ import annotations

import math
import os
import numpy as np
import bpy
from mathutils import Vector, Matrix, Euler


def to_bl(P):
    P = np.asarray(P, dtype=np.float64)
    if P.ndim == 1:
        return np.array([-P[2], -P[0], P[1]])
    return np.stack([-P[:, 2], -P[:, 0], P[:, 1]], 1)


def from_bl(P):
    P = np.asarray(P, dtype=np.float64)
    if P.ndim == 1:
        return np.array([-P[1], P[2], -P[0]])
    return np.stack([-P[:, 1], P[:, 2], -P[:, 0]], 1)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 1.0
    return sc


def mesh_from_numpy(name, V, F, smooth=True, collection=None):
    me = bpy.data.meshes.new(name)
    V = np.asarray(V, np.float32)
    F = np.asarray(F, np.int32)
    me.vertices.add(len(V))
    me.vertices.foreach_set("co", V.ravel())
    nl = F.shape[1]
    me.loops.add(F.size)
    me.loops.foreach_set("vertex_index", F.ravel())
    me.polygons.add(len(F))
    me.polygons.foreach_set("loop_start", np.arange(0, F.size, nl, dtype=np.int32))
    me.polygons.foreach_set("loop_total", np.full(len(F), nl, np.int32))
    me.update(calc_edges=True)
    me.validate(clean_customdata=False)
    if smooth:
        me.polygons.foreach_set("use_smooth", np.ones(len(F), bool))
    ob = bpy.data.objects.new(name, me)
    (collection or bpy.context.scene.collection).objects.link(ob)
    return ob


def mesh_to_numpy(ob, world=False, evaluated=False):
    if evaluated:
        dg = bpy.context.evaluated_depsgraph_get()
        obe = ob.evaluated_get(dg)
        me = obe.to_mesh()
    else:
        me = ob.data
    V = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", V)
    V = V.reshape(-1, 3).astype(np.float64)
    me.calc_loop_triangles()
    T = np.empty(len(me.loop_triangles) * 3, np.int32)
    me.loop_triangles.foreach_get("vertices", T)
    if world:
        M = np.array(ob.matrix_world)
        V = V @ M[:3, :3].T + M[:3, 3]
    if evaluated:
        obe.to_mesh_clear()
    return V, T.reshape(-1, 3)


def set_attr(me, name, values, domain="POINT"):
    values = np.asarray(values, np.float32)
    if values.ndim == 2 and values.shape[1] in (3, 4):
        if values.shape[1] == 3:
            values = np.concatenate([values, np.ones((len(values), 1), np.float32)], 1)
        a = me.color_attributes.get(name) or me.color_attributes.new(name, "FLOAT_COLOR", domain)
        a.data.foreach_set("color", values.ravel())
    else:
        a = me.attributes.get(name) or me.attributes.new(name, "FLOAT", domain)
        a.data.foreach_set("value", values.ravel())
    return a


# ----------------------------------------------------------------------------------------------
def cycles(sc, samples=64, res=(1600, 900), denoise=True, threads=None, pct=100):
    sc.render.engine = "CYCLES"
    cy = sc.cycles
    cy.device = "CPU"
    cy.samples = samples
    cy.use_adaptive_sampling = True
    cy.adaptive_threshold = 0.02
    cy.use_denoising = denoise
    try:
        cy.denoiser = "OPENIMAGEDENOISE"
    except Exception:
        pass
    cy.max_bounces = 6
    cy.diffuse_bounces = 3
    cy.glossy_bounces = 3
    cy.transmission_bounces = 2
    cy.transparent_max_bounces = 4
    cy.caustics_reflective = False
    cy.caustics_refractive = False
    cy.blur_glossy = 0.5
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = pct
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    sc.view_settings.view_transform = "AgX"
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    sc.view_settings.exposure = 0.0
    if threads:
        sc.render.threads_mode = "FIXED"
        sc.render.threads = threads
    return sc


def emission_world(sc, color=(0.012, 0.012, 0.013), strength=1.0):
    w = bpy.data.worlds.new("studio_world")
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (*color, 1)
    bg.inputs[1].default_value = strength
    return w


def cyclorama(name="cyclorama", depth=60.0, width=80.0, radius=7.0, height=30.0, back=16.0, color=(0.045, 0.045, 0.047), floor_color=None):
    """Seamless backdrop: floor (towards -Y) curving up into a wall at +Y = back."""
    prof = []
    for y in np.linspace(-depth, back - radius, 12):
        prof.append((y, 0.0))
    for a in np.linspace(0, math.pi / 2, 24)[1:]:
        prof.append((back - radius + radius * math.sin(a), radius - radius * math.cos(a)))
    for z in np.linspace(radius, height, 8)[1:]:
        prof.append((back, z))
    xs = np.linspace(-width / 2, width / 2, 9)
    V = []
    for x in xs:
        for (y, z) in prof:
            V.append((x, y, z))
    n = len(prof)
    F = []
    for i in range(len(xs) - 1):
        for j in range(n - 1):
            a = i * n + j
            F.append((a, a + n, a + n + 1, a + 1))
    ob = mesh_from_numpy(name, np.array(V), np.array(F), smooth=True)
    mat = bpy.data.materials.new(name + "_mat")
    mat.use_nodes = True
    nt = mat.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Roughness"].default_value = 0.85
    p.inputs["Specular IOR Level"].default_value = 0.25
    # floor a bit lighter than the wall, blended along the curve (like the reference studio)
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["Position"], sep.inputs[0])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value = 0.0
    mr.inputs["From Max"].default_value = radius * 0.9
    nt.links.new(sep.outputs["Z"], mr.inputs["Value"])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    fc = floor_color or tuple(min(1, c * 2.4) for c in color)
    mix.inputs[6].default_value = (*fc, 1)
    mix.inputs[7].default_value = (*color, 1)
    nt.links.new(mr.outputs[0], mix.inputs[0])
    nt.links.new(mix.outputs[2], p.inputs["Base Color"])
    ob.data.materials.append(mat)
    ob.visible_shadow = True
    return ob


def area_light(name, loc, target, size=4.0, size_y=None, power=1000.0, color=(1, 1, 1), spread=180.0):
    ld = bpy.data.lights.new(name, "AREA")
    ld.shape = "RECTANGLE" if size_y else "SQUARE"
    ld.size = size
    if size_y:
        ld.size_y = size_y
    ld.energy = power
    ld.color = color
    try:
        ld.spread = math.radians(spread)
    except Exception:
        pass
    ob = bpy.data.objects.new(name, ld)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = Vector(loc)
    look_at(ob, target)
    return ob


def look_at(ob, target, roll=0.0):
    d = Vector(target) - ob.location
    q = d.to_track_quat("-Z", "Y")
    ob.rotation_mode = "XYZ"
    ob.rotation_euler = q.to_euler()
    if roll:
        ob.rotation_euler.rotate_axis("Z", roll)


def camera(name, loc, target, lens=50.0, sensor=36.0, dof=None):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.sensor_width = sensor
    cd.clip_start = 0.05
    cd.clip_end = 400
    ob = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = Vector(loc)
    look_at(ob, target)
    if dof:
        cd.dof.use_dof = True
        cd.dof.focus_distance = (Vector(target) - Vector(loc)).length
        cd.dof.aperture_fstop = dof
    return ob


def studio(sc, subject_center, subject_size, cam_dir, key_power=1.0, rim=True, fill=True, backdrop_rot=None):
    """Lights + backdrop around a subject, given the camera direction (unit vector from subject to camera, Blender axes)."""
    c = Vector(subject_center)
    s = float(subject_size)
    d = Vector(cam_dir).normalized()
    dh = Vector((d.x, d.y, 0)).normalized()
    side = Vector((-dh.y, dh.x, 0))
    objs = []
    # backdrop: wall behind the subject, opposite to the camera
    cyc = cyclorama(back=0.75 * s + 6.0, radius=0.35 * s + 3.0)
    ang = math.atan2(-dh.x, dh.y)  # rotate +Y wall to face the camera
    cyc.rotation_euler = (0, 0, ang + math.pi)
    cyc.location = (c.x, c.y, 0)
    objs.append(cyc)
    # soft top key: large panel above, a little towards the camera and to one side
    key = area_light("key", c + Vector((0, 0, 1.05 * s + 5)) + dh * (0.25 * s) + side * (0.15 * s), c, size=0.8 * s + 2, power=key_power * 2600.0 * (s / 6.0) ** 2, color=(1.0, 0.97, 0.92))
    objs.append(key)
    if rim:
        r1 = area_light("rim", c + Vector((0, 0, 0.55 * s + 2.0)) - dh * (0.9 * s + 3) + side * (0.45 * s), c, size=0.35 * s + 1, power=key_power * 1500.0 * (s / 6.0) ** 2, color=(0.92, 0.95, 1.0))
        r2 = area_light("rim2", c + Vector((0, 0, 0.35 * s + 1.0)) - dh * (0.8 * s + 3) - side * (0.6 * s), c, size=0.3 * s + 1, power=key_power * 700.0 * (s / 6.0) ** 2, color=(0.95, 0.95, 1.0))
        objs += [r1, r2]
    if fill:
        f = area_light("fill", c + dh * (1.1 * s + 4) - side * (0.5 * s) + Vector((0, 0, 0.25 * s)), c, size=0.6 * s + 2, power=key_power * 260.0 * (s / 6.0) ** 2, color=(0.9, 0.93, 1.0))
        objs.append(f)
    return objs


def clay_material(name="clay", color=(0.32, 0.29, 0.25)):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    p = mat.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Roughness"].default_value = 0.6
    p.inputs["Subsurface Weight"].default_value = 0.12
    p.inputs["Subsurface Radius"].default_value = (1.0, 0.45, 0.25)
    p.inputs["Subsurface Scale"].default_value = 0.02
    p.inputs["Specular IOR Level"].default_value = 0.45
    return mat


def render(sc, cam, path):
    sc.camera = cam
    sc.render.filepath = path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.render.render(write_still=True)
    return path
