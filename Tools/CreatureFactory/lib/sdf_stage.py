"""Stage 'sdf': mesh the smooth anatomy SDF (no skin detail) -> Build/<sp>/base.npz, plus the rigid
accessories (claws, teeth, eyeballs) -> accessories.npz, and clay previews in Output/factory/<sp>/stages/."""
from __future__ import annotations

import os
import pickle
import numpy as np

from .sdf import Compiled
from .mesher import mesh_sdf, mesh_volume_area
from . import accessories as acc


def body_shapes(doc):
    return [s for s in doc["shapes"] if s.get("feature") != "claw"]


def compile_body(doc, band=0.08):
    return Compiled(body_shapes(doc), [b["name"] for b in doc["bones"]], band=band)


def run(ctx):
    doc = ctx.anatomy()
    comp = compile_body(doc)
    V, F = mesh_sdf(comp, ctx.q["h_base"], log=ctx.log)
    vol, area = mesh_volume_area(V, F)
    lo, hi = V.min(0), V.max(0)
    stats = dict(verts=len(V), tris=len(F), volume_m3=round(vol, 3), area_m2=round(area, 2),
                 length_m=round(hi[0] - lo[0], 3), height_m=round(hi[1], 3), width_m=round(hi[2] - lo[2], 3),
                 mass_at_950_kg_m3=round(vol * 950))
    ctx.log(f"  base sculpt: {stats}")
    np.savez_compressed(ctx.path("base.npz"), V=V, F=F)
    claws = acc.claw_meshes(doc, log=ctx.log)
    teeth = acc.teeth_meshes(doc)
    eyes = [dict(name=f"eye_{e['side']}", bone=e["bone"], center=e["center"], radius=e["radius"], direction=e["direction"]) for e in doc["eyes"]]
    with open(ctx.path("accessories.pkl"), "wb") as f:
        pickle.dump(dict(claws=claws, teeth=teeth, eyes=eyes), f)
    ctx.save_json("sdf_stats.json", stats)
    if ctx.options.get("no_preview"):
        return
    # clay preview
    from . import bl
    bl.reset()
    ob = bl.mesh_from_numpy("base", bl.to_bl(V), F)
    objs = [ob]
    for c in claws:
        objs.append(bl.mesh_from_numpy(c["name"], bl.to_bl(c["V"]), c["F"]))
    os.makedirs(ctx.opath("stages"), exist_ok=True)
    ps = bl.quick_clay(objs, ctx.opath("stages"), "sdf", views=("front34", "head"))
    ctx.log("  previews: " + ", ".join(ps))
