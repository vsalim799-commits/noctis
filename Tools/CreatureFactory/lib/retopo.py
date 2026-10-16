"""Stage 'retopo': animation-ready low-poly from the smooth sculpt.

  base.npz (marching cubes of the smooth SDF) -> Blender OpenVDB voxel remesh (all quads, voxel size
  from the quad target; QuadriFlow rejected, see run()) -> vertices Newton-projected
  on the exact SDF surface + tangential relaxation -> lowpoly.blend / lowpoly.npz.
Accessories: claws decimated to ~360 triangles each, teeth and eyeballs kept as generated.
LODs are produced from the final skinned mesh in the export stage."""
from __future__ import annotations

import math
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


def densify(bpy, me, spots, bl):
    """Subdivide the faces around given anatomy-space points: spots = [(centre, radius, levels)].
    Level k subdivides faces within radius * (0.65 ** (k - 1)) once more."""
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(me)
    maxl = max(l for _, _, l in spots)
    for lev in range(1, maxl + 1):
        bm.faces.ensure_lookup_table()
        sel = set()
        for c, r, l in spots:
            if lev > l:
                continue
            cb = bl.to_bl(c)
            rr = r * (0.65 ** (lev - 1))
            for f in bm.faces:
                if (f.calc_center_median() - __import__("mathutils").Vector(cb)).length < rr:
                    sel.add(f)
        edges = list({e for f in sel for e in f.edges})
        if edges:
            bmesh.ops.subdivide_edges(bm, edges=edges, cuts=1, use_grid_fill=True, quad_corner_type="INNER_VERT")
    ngons = [f for f in bm.faces if len(f.verts) > 4]
    if ngons:
        bmesh.ops.triangulate(bm, faces=ngons)
    bm.to_mesh(me)
    bm.free()
    me.update()


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
    # Quad remesh. QuadriFlow (Blender) was tried first: on this model every seed/target tested lost
    # whole patches of the tail (max deviation 0.3-0.7 m from the sculpt), with or without its
    # symmetry option, so the robust OpenVDB voxel remesher is used: an all-quad, uniform mesh whose
    # voxel size is set from the quad target, then projected exactly on the SDF and relaxed.
    target = ctx.q["retopo_faces"]
    vox = ctx.q.get("retopo_voxel") or 0.034 * math.sqrt(48800.0 / target)
    ob.data.remesh_voxel_size = vox
    ob.data.use_remesh_fix_poles = True
    bpy.ops.object.voxel_remesh()
    b, nm, nc = mesh_check(ob.data)
    ctx.log(f"  voxel remesh {vox*100:.2f} cm: {len(ob.data.polygons)} quads, boundary edges {b}, non-manifold {nm}, components {nc}")
    me = ob.data
    Vb = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", Vb)
    Va = bl.from_bl(Vb.reshape(-1, 3).astype(np.float64))
    polys = polys_numpy(me)
    Va = relax_project(comp, Va, polys, log=ctx.log)
    # local density boost where the sculpt has small concave features the 4 cm grid cannot hold:
    # orbits (eye pit, lids, brow) and nostrils. Subdivide, then project the new vertices.
    me.vertices.foreach_set("co", bl.to_bl(Va).astype(np.float32).ravel())
    me.update()
    spots = [(np.array(e["center"]), 0.17, 2) for e in doc["eyes"]]
    spots += [(np.array(sh_["center"]), 0.09, 1) for sh_ in doc["shapes"] if sh_.get("tag") == "naris"]
    densify(bpy, me, spots, bl)
    Vb = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", Vb)
    Va = bl.from_bl(Vb.reshape(-1, 3).astype(np.float64))
    polys = polys_numpy(me)
    # inside the boosted spots, project along the mesh normal (ray to the SDF) so the surface dips into
    # the orbit / naris instead of bridging it; elsewhere Newton projection on the nearest surface
    from .mesher import vertex_normals
    tri = np.array([[p[0], p[k], p[k + 1]] for p in polys for k in range(1, len(p) - 1)])
    Nv = vertex_normals(Va, tri)
    near = np.zeros(len(Va), bool)
    for c, r, _ in spots:
        near |= np.linalg.norm(Va - c, axis=1) < r
    from .bake import ray_to_surface
    sv = np.zeros(int(near.sum()))
    ray_to_surface(np.ascontiguousarray(Va[near]), np.ascontiguousarray(-Nv[near]), sv, 0.09, *comp.args())
    Va[near] = Va[near] - Nv[near] * sv[:, None]
    Va[~near] = comp.project(Va[~near], iters=4, max_step=0.02)
    Va = relax_project(comp, Va, polys, iters=2, lam=0.3, log=ctx.log)
    ctx.log(f"  after eye/nostril densification: {len(polys)} faces, {len(Va)} verts")
    # coverage: distance from the sculpt (base mesh) to the low-poly surface
    from scipy.spatial import cKDTree
    rng = np.random.default_rng(0)
    pts = np.concatenate([rng.dirichlet(np.ones(len(p)), 12) @ Va[p] for p in polys])
    dd, _ = cKDTree(pts).query(V[::4])
    cover = dict(p99_9_m=round(float(np.percentile(dd, 99.9)), 4), max_m=round(float(dd.max()), 4))
    ctx.log(f"  sculpt -> low-poly deviation: {cover}")
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
    st = dict(deviation=cover, body_quads=len(polys), body_tris=int(ntri), body_verts=int(len(Va)), claw_tris=int(claw_tris), teeth_tris=int(teeth_tris))
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
