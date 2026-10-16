"""Stage 'detail': dense high-poly with REAL displaced skin relief.

The smooth anatomy SDF is meshed at the hero voxel size (quality h_high), every vertex is pushed along
the SDF normal by the skin relief of lib/skin.py (scales, feature scales, rugose plates, folds,
wrinkles, scutes, pads). Outputs:
    Build/Creatures/<sp>/highpoly.npz   V, F, N (base normals), channels (disp, top, rnd, crease, feature, scute)
    Build/Creatures/<sp>/highpoly.blend the displaced mesh (+ claws) with the channels as attributes
    Output/factory/<sp>/stages/detail_*.png clay renders
The bake stage evaluates the same relief function per texel, so the maps equal the sculpt (but at
texel resolution, finer than the mesh)."""
from __future__ import annotations

import os
import pickle
import numpy as np

from .mesher import mesh_sdf, mesh_volume_area
from .sdf_stage import compile_body
from . import skin


def run(ctx):
    doc = ctx.anatomy()
    comp = compile_body(doc)
    h = ctx.q["h_high"]
    V, F = mesh_sdf(comp, h, log=ctx.log)
    # Newton-project the marching-cubes vertices on the exact surface, then SDF normals
    V = comp.project(V, iters=2, max_step=h)
    G = comp.gradient(V, eps=1e-3)
    N = G / (np.linalg.norm(G, axis=1, keepdims=True) + 1e-12)
    ch, info = skin.evaluate(comp, doc, V, N, log=ctx.log, filter_width=0.8 * h)
    disp = ch[:, 0]
    Vd = V + N * disp[:, None]
    ctx.log(f"  displacement: min {disp.min()*1000:.1f} mm, max {disp.max()*1000:.1f} mm, mean |d| {np.abs(disp).mean()*1000:.2f} mm")
    vol, area = mesh_volume_area(Vd, F)
    ctx.log(f"  high-poly: {len(Vd)} verts, {len(F)} tris, area {area:.2f} m2, volume {vol:.3f} m3")
    np.savez(ctx.path("highpoly.npz"), V=Vd.astype(np.float32), F=F, N=N.astype(np.float16), C=ch.astype(np.float16))
    ctx.save_json("detail_stats.json", dict(verts=int(len(Vd)), tris=int(len(F)), voxel_m=h, disp_min_m=float(disp.min()),
                                            disp_max_m=float(disp.max()), area_m2=float(area), volume_m3=float(vol)))
    # Blender file + clay previews
    from . import bl
    import bpy
    bl.reset()
    ob = bl.mesh_from_numpy("highpoly", bl.to_bl(Vd), F)
    for k, name in enumerate(["disp", "top", "rnd", "crease", "feature", "scute"]):
        bl.set_attr(ob.data, name, ch[:, k])
    with open(ctx.path("accessories.pkl"), "rb") as f:
        acc = pickle.load(f)
    objs = [ob]
    for c in acc["claws"]:
        objs.append(bl.mesh_from_numpy(c["name"], bl.to_bl(c["V"]), c["F"]))
    bpy.ops.wm.save_as_mainfile(filepath=ctx.path("highpoly.blend"), compress=True)
    if ctx.options.get("no_preview"):
        return
    os.makedirs(ctx.opath("stages"), exist_ok=True)
    ps = bl.quick_clay(objs, ctx.opath("stages"), "detail", views=("head", "front34", "foot"), samples=24)
    ctx.log("  previews: " + ", ".join(ps))
