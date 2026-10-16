"""Stage 'rig': armature from the anatomy bone spec, skin weights, materials, IK controls.

Skeleton: the deform bones of Data/Anatomy/<sp>.json (stable names, see README: root, pelvis,
spine_01..03, neck_01..03, head, jaw, eye_l/r, tail_01..10, thigh/shin/metatarsus/toes_l/r,
upperarm/forearm/hand_l/r). Armature object named 'Armature' (UE5 then uses 'root' as the root bone).
Bone roll: trunk/neck/head/jaw/tail bones have their local Z axis towards world up; limb bones have Z
towards the animal's front.
Control bones (not deformed, not exported to the game skeleton): ik_foot_l/r (at the
metatarsophalangeal joint), pole_knee_l/r, look_target. IK constraints (chain thigh-shin-metatarsus)
and a damped-track head look are created with influence 0: the procedural clips are baked FK; the
controls are kept for hand authoring (not keyed by the procedural clips).
Weights: SDF ownership weights of each bone's volumes at the low-poly vertices (lib/sdf.attributes),
Laplacian-smoothed on the mesh, 4 influences max, normalised. Accessories are rigid (1 bone each).
"""
from __future__ import annotations

import pickle
import numpy as np

from .sdf_stage import compile_body

TRUNK_PREFIX = ("root", "pelvis", "spine", "neck", "head", "jaw", "tail", "eye")


def material_body(ctx, bpy, name="M_Body"):
    sp = ctx.species
    tex = ctx.path("textures")
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    def img(fn, cs="sRGB", x=-700, y=0):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(f"{tex}/{fn}", check_existing=True)
        n.image.colorspace_settings.name = cs
        n.location = (x, y)
        return n
    bc = img(f"T_{sp}_BaseColor.png", "sRGB", y=300)
    nt.links.new(bc.outputs["Color"], p.inputs["Base Color"])
    orm = img(f"T_{sp}_ORM.png", "Non-Color", y=0)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    sep.location = (-400, 0)
    nt.links.new(orm.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], p.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], p.inputs["Metallic"])
    nm = img(f"T_{sp}_Normal.png", "Non-Color", y=-300)
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nmap.location = (-400, -300)
    nt.links.new(nm.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Subsurface Weight"].default_value = 0.0
    p.inputs["Subsurface Radius"].default_value = (1.0, 0.42, 0.24)
    p.inputs["Subsurface Scale"].default_value = 0.035
    p.inputs["Specular IOR Level"].default_value = 0.42
    return m


def material_simple(bpy, name, color, rough, spec=0.5, use_vcol=False, coat=0.0, image=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Specular IOR Level"].default_value = spec
    p.inputs["Coat Weight"].default_value = coat
    if use_vcol:
        a = nt.nodes.new("ShaderNodeVertexColor")
        a.layer_name = "Col"
        nt.links.new(a.outputs["Color"], p.inputs["Base Color"])
    if image:
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(image, check_existing=True)
        nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
    return m


def bone_weights(comp, V, polys, names, iters=3):
    _, bw, _, _ = comp.attributes(V, sig_scale=0.35, sig_add=0.025)
    from scipy.sparse import coo_matrix
    E = set()
    for p in polys:
        for k in range(len(p)):
            a, b = int(p[k]), int(p[(k + 1) % len(p)])
            E.add((min(a, b), max(a, b)))
    E = np.array(sorted(E))
    n = len(V)
    A = coo_matrix((np.ones(2 * len(E)), (np.r_[E[:, 0], E[:, 1]], np.r_[E[:, 1], E[:, 0]])), shape=(n, n)).tocsr()
    deg = np.asarray(A.sum(1)).ravel()
    W = bw.astype(np.float64)
    W[:, names.index("root")] = 0
    for _ in range(iters):
        W = 0.5 * W + 0.5 * (A @ W) / deg[:, None]
    # top 4
    idx = np.argsort(-W, 1)[:, :4]
    W4 = np.take_along_axis(W, idx, 1)
    W4[W4 < 0.01] = 0
    W4 /= W4.sum(1, keepdims=True) + 1e-12
    return idx, W4


def run(ctx):
    import bpy
    from mathutils import Vector
    from . import bl
    doc = ctx.anatomy()
    comp = compile_body(doc)
    bpy.ops.wm.open_mainfile(filepath=ctx.path("uv.blend"))
    sc = bpy.context.scene
    body = bpy.data.objects["body"]
    for o in list(bpy.data.objects):
        if o != body:
            bpy.data.objects.remove(o, do_unlink=True)
    # ---------------- armature ----------------
    arm = bpy.data.armatures.new("Armature")
    rig = bpy.data.objects.new("Armature", arm)
    sc.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones
    for b in doc["bones"]:
        e = eb.new(b["name"])
        e.head = Vector(bl.to_bl(b["head"]))
        e.tail = Vector(bl.to_bl(b["tail"]))
        if b["name"].startswith(TRUNK_PREFIX):
            e.align_roll(Vector((0, 0, 1)))
        else:
            e.align_roll(Vector((0, -1, 0)))
        e.use_deform = True
    for b in doc["bones"]:
        if b.get("parent"):
            eb[b["name"]].parent = eb[b["parent"]]
            # connect when the child starts at the parent's tail
            if (eb[b["name"]].head - eb[b["parent"]].tail).length < 1e-4:
                eb[b["name"]].use_connect = True
    bones = {b["name"]: b for b in doc["bones"]}
    for s in "lr":
        mtp = np.array(bones[f"toes_{s}"]["head"])
        ik = eb.new(f"ik_foot_{s}")
        ik.head = Vector(bl.to_bl(mtp))
        ik.tail = Vector(bl.to_bl(mtp + [0.4, 0, 0]))
        ik.align_roll(Vector((0, 0, 1)))
        ik.parent = eb["root"]
        ik.use_deform = False
        knee = np.array(bones[f"shin_{s}"]["head"])
        pk = eb.new(f"pole_knee_{s}")
        pk.head = Vector(bl.to_bl(knee + [1.5, 0, 0]))
        pk.tail = Vector(bl.to_bl(knee + [1.7, 0, 0]))
        pk.parent = eb["root"]
        pk.use_deform = False
    hd = np.array(bones["head"]["head"])
    lt = eb.new("look_target")
    lt.head = Vector(bl.to_bl(hd + [4.0, -0.3, 0]))
    lt.tail = Vector(bl.to_bl(hd + [4.3, -0.3, 0]))
    lt.parent = eb["root"]
    lt.use_deform = False
    bpy.ops.object.mode_set(mode="POSE")
    for s in "lr":
        c = rig.pose.bones[f"metatarsus_{s}"].constraints.new("IK")
        c.target = rig
        c.subtarget = f"ik_foot_{s}"
        c.pole_target = rig
        c.pole_subtarget = f"pole_knee_{s}"
        c.pole_angle = 0.0
        c.chain_count = 3
        c.influence = 0.0
    c = rig.pose.bones["head"].constraints.new("DAMPED_TRACK")
    c.target = rig
    c.subtarget = "look_target"
    c.track_axis = "TRACK_Y"
    c.influence = 0.0
    bpy.ops.object.mode_set(mode="OBJECT")
    for b in rig.pose.bones:
        b.rotation_mode = "QUATERNION"
    # ---------------- weights ----------------
    names = [b["name"] for b in doc["bones"]]
    me = body.data
    V = bl.from_bl(np.array([v.co for v in me.vertices]))
    polys = [np.array(p.vertices) for p in me.polygons]
    idx, W = bone_weights(comp, V, polys, names)
    for nme in names:
        body.vertex_groups.new(name=nme)
    vg = body.vertex_groups
    for j in range(4):
        for bi in np.unique(idx[:, j]):
            sel = np.nonzero((idx[:, j] == bi) & (W[:, j] > 0))[0]
            for v, w in zip(sel.tolist(), W[sel, j].tolist()):
                vg[names[bi]].add([v], w, "REPLACE")
    used = sorted({names[i] for i in np.unique(idx[W > 0])})
    ctx.log(f"  body weights: {len(used)} bones used, max influences 4")
    # ---------------- accessories ----------------
    with open(ctx.path("accessories.pkl"), "rb") as f:
        acc = pickle.load(f)
    pal = doc["palette"]
    m_body = material_body(ctx, bpy)
    m_eye = material_simple(bpy, "M_Eye", (0.3, 0.2, 0.05), 0.04, spec=0.8, coat=1.0, image=ctx.path("textures", f"T_{ctx.species}_Eye_BaseColor.png"))
    m_claw = material_simple(bpy, "M_Claw", tuple(pal["claw"]), 0.55, spec=0.5, use_vcol=True)
    m_tooth = material_simple(bpy, "M_Teeth", tuple(pal["tooth"]), 0.3, spec=0.6)
    me.materials.clear()
    me.materials.append(m_body)
    objs = [body]
    for c in acc["claws"]:
        Vc, Fc = c["V_low"], c["F_low"]
        o = bl.mesh_from_numpy(c["name"], bl.to_bl(Vc), Fc)
        ax = np.asarray(c["tip"]) - np.asarray(c["base"])
        t = np.clip(((Vc - c["base"]) @ ax) / (ax @ ax), 0, 1)
        colv = np.array(pal["claw"])[None] * (1 - t[:, None] ** 2) + np.array(pal["claw_tip"])[None] * (t[:, None] ** 2)
        a = o.data.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
        a.data.foreach_set("color", np.concatenate([colv, np.ones((len(colv), 1))], 1).astype(np.float32).ravel())
        o.data.materials.append(m_claw)
        o.vertex_groups.new(name=c["bone"]).add(list(range(len(Vc))), 1.0, "REPLACE")
        o.data.uv_layers.new(name="UVMap")
        objs.append(o)
    for t_ in acc["teeth"]:
        o = bl.mesh_from_numpy(t_["name"], bl.to_bl(t_["V"]), t_["F"])
        o.data.materials.append(m_tooth)
        o.vertex_groups.new(name=t_["bone"]).add(list(range(len(t_["V"]))), 1.0, "REPLACE")
        o.data.uv_layers.new(name="UVMap")
        objs.append(o)
    from .accessories import eye_mesh
    for e in acc["eyes"]:
        Ve, Fe, UVe = eye_mesh(e["center"], e["radius"], e["direction"])
        me_ = bpy.data.meshes.new(e["name"])
        verts = [tuple(v) for v in bl.to_bl(Ve)]
        me_.from_pydata(verts, [], [tuple(f) for f in Fe])
        me_.update()
        uvl = me_.uv_layers.new(name="UVMap")
        for poly in me_.polygons:
            poly.use_smooth = True
            for li in range(poly.loop_start, poly.loop_start + poly.loop_total):
                uvl.data[li].uv = UVe[me_.loops[li].vertex_index]
        o = bpy.data.objects.new(e["name"], me_)
        sc.collection.objects.link(o)
        o.data.materials.append(m_eye)
        o.vertex_groups.new(name=e["bone"]).add(list(range(len(Ve))), 1.0, "REPLACE")
        objs.append(o)
    # join everything into one skinned mesh
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    body.name = f"SK_{ctx.species}"
    body.data.name = f"SK_{ctx.species}"
    body.parent = rig
    mod = body.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    # stats
    me = body.data
    me.calc_loop_triangles()
    st = dict(bones_deform=[b["name"] for b in doc["bones"]], control_bones=[b.name for b in arm.bones if not b.use_deform],
              mesh_tris=len(me.loop_triangles), mesh_verts=len(me.vertices),
              materials=[m.name for m in me.materials])
    ctx.save_json("rig_stats.json", st)
    ctx.log(f"  rig: {len(st['bones_deform'])} deform bones, {st['mesh_tris']} tris, materials {st['materials']}")
    bpy.ops.wm.save_as_mainfile(filepath=ctx.path("rig.blend"), compress=True)
