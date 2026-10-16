"""Stage 'retopo': animation-ready low-poly from the smooth sculpt.

  base.npz (marching cubes of the smooth SDF) -> Blender -> voxel remesh 2 cm (QuadriFlow input) ->
  QuadriFlow quad remesh (target = quality retopo_faces quads; topology not mirrored) -> vertices Newton-projected
  on the exact SDF surface + tangential relaxation -> lowpoly.blend / lowpoly.npz.
Accessories: claws decimated to ~360 triangles each, teeth and eyeballs kept as generated.
LODs are produced from the final skinned mesh in the export stage."""
from __future__ import annotations

import pickle
import numpy as np

from .sdf_stage import compile_body
from .mesher import adjacency, vertex_normals


def polys_numpy(me):
    n = len(me.polygons)
    ls = np.empty(n, np.int32)
    lt = np.empty(n, np.int32)
    me.polygons.foreach_get("loop_start", ls)
    me.polygons.foreach_get("loop_total", lt)
    lv = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get("vertex_index", lv)
    return [lv[s:s + t] for s, t in zip(ls, lt)]


def mesh_check(me):
    """(boundary edges, non-manifold edges, connected components) of a Blender mesh."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    E = {}
    for p in me.polygons:
        vs = list(p.vertices)
        for k in range(len(vs)):
            e = (min(vs[k], vs[(k + 1) % len(vs)]), max(vs[k], vs[(k + 1) % len(vs)]))
            E[e] = E.get(e, 0) + 1
    e = np.array(list(E.keys()))
    n = len(me.vertices)
    nc = connected_components(coo_matrix((np.ones(len(e)), (e[:, 0], e[:, 1])), shape=(n, n)), directed=False)[0]
    return sum(1 for c in E.values() if c == 1), sum(1 for c in E.values() if c > 2), nc


def relax_project(comp, V, polys, iters=4, lam=0.5, log=print):
    """Tangential Laplacian relaxation + projection on the SDF zero set (keeps the shape exact)."""
    edges = set()
    for p in polys:
        for k in range(len(p)):
            a, b = int(p[k]), int(p[(k + 1) % len(p)])
            edges.add((min(a, b), max(a, b)))
    E = np.array(sorted(edges))
    from scipy.sparse import coo_matrix
    n = len(V)
    A = coo_matrix((np.ones(2 * len(E)), (np.r_[E[:, 0], E[:, 1]], np.r_[E[:, 1], E[:, 0]])), shape=(n, n)).tocsr()
    deg = np.asarray(A.sum(1)).ravel()
    V = comp.project(V, iters=3, max_step=0.03)
    for it in range(iters):
        G = comp.gradient(V)
        Nn = G / (np.linalg.norm(G, axis=1, keepdims=True) + 1e-12)
        L = (A @ V) / deg[:, None] - V
        L -= (L * Nn).sum(1)[:, None] * Nn          # tangential part only
        V = V + lam * L
        V = comp.project(V, iters=2, max_step=0.02)
    d = comp.field(V)
    log(f"  projected on SDF: |f| max {np.abs(d).max()*1000:.2f} mm, mean {np.abs(d).mean()*1000:.3f} mm")
    return V


def run(ctx):
    import bpy
    from . import bl
    doc = ctx.anatomy()
    comp = compile_body(doc)
    d = np.load(ctx.path("base.npz"))
    V, F = d["V"], d["F"]
    bl.reset()
    ob = bl.mesh_from_numpy("body_src", bl.to_bl(V), F)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    # QuadriFlow input: a clean voxel remesh (2 cm). A collapse-decimated input (sliver triangles)
    # made QuadriFlow collapse the thin tail on this model.
    ob.data.remesh_voxel_size = 0.02
    ob.data.use_remesh_fix_poles = True
    bpy.ops.object.voxel_remesh()
    ctx.log(f"  voxel-remeshed input: {len(ob.data.polygons)} faces")
    target = ctx.q["retopo_faces"]
    # Note: Blender's QuadriFlow 'use_mesh_symmetry' leaves the two halves unwelded on this mesh and a
    # bisected half with a preserved boundary ignores the face target, so the topology is not mirrored.
    bpy.ops.object.quadriflow_remesh(use_mesh_symmetry=False, use_preserve_sharp=False, use_preserve_boundary=False,
                                     preserve_attributes=False, smooth_normals=False, mode="FACES", target_faces=target, seed=3)
    b, nm, nc = mesh_check(ob.data)
    ctx.log(f"  quadriflow check: boundary edges {b}, non-manifold {nm}, components {nc}")
    ctx.log(f"  quadriflow: {len(ob.data.polygons)} faces, {len(ob.data.vertices)} verts")
    me = ob.data
    Vb = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", Vb)
    Va = bl.from_bl(Vb.reshape(-1, 3).astype(np.float64))
    polys = polys_numpy(me)
    Va = relax_project(comp, Va, polys, log=ctx.log)
    me.vertices.foreach_set("co", bl.to_bl(Va).astype(np.float32).ravel())
    me.update()
    ob.name = "body"
    me.name = "body"
    ntri = sum(len(p) - 2 for p in polys)
    # accessories
    with open(ctx.path("accessories.pkl"), "rb") as f:
        acc = pickle.load(f)
    for c in acc["claws"]:
        o = bl.mesh_from_numpy(c["name"], bl.to_bl(c["V"]), c["F"])
        mm = o.modifiers.new("dec", "DECIMATE")
        mm.ratio = min(1.0, 360 / len(c["F"]))
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier="dec")
        o["bone"] = c["bone"]
        c["V_low"] = bl.from_bl(np.array([v.co for v in o.data.vertices]))
        c["F_low"] = np.array([list(p.vertices) for p in o.data.polygons], np.int32)
    with open(ctx.path("accessories.pkl"), "wb") as f:
        pickle.dump(acc, f)
    np.savez(ctx.path("lowpoly.npz"), V=Va, P=np.array([len(p) for p in polys]), I=np.concatenate(polys))
    bpy.ops.wm.save_as_mainfile(filepath=ctx.path("lowpoly.blend"), compress=True)
    claw_tris = sum(len(c["F_low"]) for c in acc["claws"])
    teeth_tris = sum(len(t["F"]) for t in acc["teeth"])
    st = dict(body_quads=len(polys), body_tris=int(ntri), body_verts=int(len(Va)), claw_tris=int(claw_tris), teeth_tris=int(teeth_tris))
    ctx.save_json("retopo_stats.json", st)
    ctx.log(f"  low-poly: {st}")
    if ctx.options.get("no_preview"):
        return
    # clay check
    import os
    os.makedirs(ctx.opath("stages"), exist_ok=True)
    mod = ob.modifiers.new("wire", "WIREFRAME")
    mod.thickness = 0.004
    mod.use_replace = False
    mod.material_offset = 1
    mat_w = bpy.data.materials.new("wire")
    mat_w.use_nodes = True
    mat_w.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.02, 0.025, 1)
    ob.data.materials.append(bl.clay_material("clay_low"))
    ob.data.materials.append(mat_w)
    ps = bl.quick_clay([o for o in bpy.data.objects if o.type == "MESH"], ctx.opath("stages"), "retopo", views=("head", "front34"), samples=16)
    ctx.log("  previews: " + ", ".join(ps))
