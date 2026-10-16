"""Noctis — Hell Creek vegetation for Blender scenes (beauty shots of the creature factory, environment renders).

Uses the outputs of Tools/EnvironmentFactory (run it first):
    Build/Environment/library/noctis_flora.blend    collections <asset_id>_LOD0/1/2 (metres, pivot at the base)
    Build/Environment/manifest.json                 asset metadata + scatter rules (densities per plant type)
    Build/Environment/sim/                          simulation masks (export-terrain + export-ground, seed 7)

Typical use inside a scene script (bpy available, run with the noctis-tools venv):

    import vegetation_library as veg
    terrain = veg.build_terrain(center=(3650, 4480), size_m=600, res_m=1.5, shading=True, water=True)
    veg.append_assets()                                   # every asset, LOD0..2 (instancing sources, not rendered)
    stats = veg.scatter(terrain, camera=bpy.context.scene.camera, seed=7, density=1.0,
                        keep_clear=[(x_creature, y_creature, 6.0)])   # simulation coordinates (m)

Coordinates: simulation x = east, y = north (m). A terrain built here sits at Blender (X, Y) = sim - origin
(custom properties noctis_origin_x / noctis_origin_y on the terrain object); sim_to_blender() converts.
LOD by camera distance (lod_distances), frustum culling with a margin, per-category maximum distances, and a
global instance budget keep Cycles renders reasonable. Nothing here is a scientific claim: densities are game
assumptions documented in Docs/ENVIRONMENT.md and each asset's metadata.
"""
from __future__ import annotations

import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ENV_TOOL = os.path.join(ROOT, "Tools", "EnvironmentFactory")
if ENV_TOOL not in sys.path:
    sys.path.append(ENV_TOOL)

BUILD = os.path.join(ROOT, "Build", "Environment")
LIBRARY = os.path.join(BUILD, "library", "noctis_flora.blend")
MANIFEST = os.path.join(BUILD, "manifest.json")
SOURCES_COLLECTION = "NoctisFloraSources"
SCATTER_COLLECTION = "NoctisScatter"

# Render budget per category: max distance from the camera (m) and LOD switch distances (m).
CATEGORY = {
    "tree": dict(max_m=4000, lod=(70, 260)),
    "palm": dict(max_m=700, lod=(40, 160)),
    "shrub": dict(max_m=400, lod=(30, 110)),
    "fern": dict(max_m=110, lod=(18, 55)),
    "horsetail": dict(max_m=140, lod=(18, 60)),
    "herb": dict(max_m=70, lod=(14, 40)),
    "aquatic": dict(max_m=200, lod=(20, 70)),
    "deadwood": dict(max_m=450, lod=(30, 120)),
}
SMALL_DEADWOOD_MAX_M = 70


# ----------------------------------------------------------------------------------------------- data
def manifest():
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            return json.load(f)
    # fallback: catalogue of the factory (no triangle counts / sizes)
    from envlib import catalog
    return {"assets": {k: dict(category=v["category"], pft=v["pft"], on_water=v.get("on_water", False),
                               bounds_m={"height": 5.0, "width": 5.0}) for k, v in catalog.ASSETS.items()},
            "scatter": {"pft_rules": catalog.PFT_RULES, "deadwood_rules": catalog.DEADWOOD_RULES}}


def sim_data():
    from envlib.terrain import SimData
    return SimData(os.path.join(BUILD, "sim"))


def sim_to_blender(terrain, x, y):
    return np.asarray(x) - terrain["noctis_origin_x"], np.asarray(y) - terrain["noctis_origin_y"]


def blender_to_sim(terrain, X, Y):
    return np.asarray(X) + terrain["noctis_origin_x"], np.asarray(Y) + terrain["noctis_origin_y"]


# ----------------------------------------------------------------------------------------------- terrain
def build_terrain(center, size_m, res_m=2.0, shading=True, water=True, detail=None, name="NoctisTerrain", mask_px=1024):
    """Terrain crop from the simulation heightmap (+ habitat-blended ground material and river water).
    detail: optional dict(center=(x, y), size=m, res=m) high-resolution patch near the camera."""
    import bpy
    from envlib import terrain as T
    from envlib import terrain_shading as TS
    sim = sim_data()
    coll = bpy.data.collections.get("NoctisTerrain") or bpy.data.collections.new("NoctisTerrain")
    if coll.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(coll)
    mat = None
    if shading:
        masks, _ = T.crop_masks(sim, center, size_m, px=mask_px)
        mat = TS.terrain_material(masks)
    ob = T.build_terrain(center, size_m, res_m=res_m, name=name, detail=detail, sim=sim, collection=coll, material=mat)
    if water:
        T.build_water(center, size_m, res_m=max(2.0, res_m), name=name + "_Water", sim=sim, collection=coll,
                      material=TS.water_material())
    return ob


# ----------------------------------------------------------------------------------------------- library
def append_assets(asset_ids=None, lods=(0, 1, 2), library=LIBRARY):
    """Appends <asset>_LOD<n> collections from the library into a hidden source collection. Returns
    {(asset_id, lod): collection}. Already appended collections are reused."""
    import bpy
    if not os.path.exists(library):
        raise FileNotFoundError("%s absent: run Tools/EnvironmentFactory/factory.py --stages flora" % library)
    scene = bpy.context.scene
    src = bpy.data.collections.get(SOURCES_COLLECTION)
    if src is None:
        src = bpy.data.collections.new(SOURCES_COLLECTION)
        scene.collection.children.link(src)
    wanted = []
    with bpy.data.libraries.load(library, link=False) as (data_from, data_to):
        names = [n for n in data_from.collections if "_LOD" in n]
        for n in names:
            aid, lod = n.rsplit("_LOD", 1)
            if (asset_ids is None or aid in asset_ids) and int(lod) in lods and bpy.data.collections.get(n) is None:
                wanted.append(n)
        data_to.collections = wanted
    out = {}
    for c in bpy.data.collections:
        if "_LOD" in c.name and c.name.rsplit("_LOD", 1)[1].isdigit():
            aid, lod = c.name.rsplit("_LOD", 1)
            if (asset_ids is None or aid in asset_ids) and int(lod) in lods:
                if c.name not in src.children:
                    src.children.link(c)
                out[(aid, int(lod))] = c
    # Sources stay out of the render: hide the collection in render (instances are evaluated from it anyway).
    src.hide_render = True
    src.hide_viewport = True
    return out


def _instancer_group():
    import bpy
    name = "NoctisScatterInstancer"
    g = bpy.data.node_groups.get(name)
    if g:
        return g
    g = bpy.data.node_groups.new(name, "GeometryNodeTree")
    g.interface.new_socket("Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    g.interface.new_socket("Collection", in_out="INPUT", socket_type="NodeSocketCollection")
    g.interface.new_socket("Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    n = g.nodes
    gi = n.new("NodeGroupInput")
    go = n.new("NodeGroupOutput")
    ci = n.new("GeometryNodeCollectionInfo")
    ci.transform_space = "ORIGINAL"
    ci.inputs["Separate Children"].default_value = False
    iop = n.new("GeometryNodeInstanceOnPoints")
    rot = n.new("GeometryNodeInputNamedAttribute")
    rot.data_type = "FLOAT_VECTOR"
    rot.inputs["Name"].default_value = "noctis_rot"
    scl = n.new("GeometryNodeInputNamedAttribute")
    scl.data_type = "FLOAT_VECTOR"
    scl.inputs["Name"].default_value = "noctis_scale"
    m2p = n.new("GeometryNodeMeshToPoints")
    l = g.links
    l.new(gi.outputs["Geometry"], m2p.inputs["Mesh"])
    l.new(m2p.outputs["Points"], iop.inputs["Points"])
    l.new(gi.outputs["Collection"], ci.inputs["Collection"])
    l.new(ci.outputs["Instances"], iop.inputs["Instance"])
    try:
        e2r = n.new("FunctionNodeEulerToRotation")
        l.new(rot.outputs["Attribute"], e2r.inputs["Euler"])
        l.new(e2r.outputs["Rotation"], iop.inputs["Rotation"])
    except Exception:
        l.new(rot.outputs["Attribute"], iop.inputs["Rotation"])
    l.new(scl.outputs["Attribute"], iop.inputs["Scale"])
    l.new(iop.outputs["Instances"], go.inputs["Geometry"])
    return g


def _make_instancer(name, P, R, S, source, collection):
    import bpy
    me = bpy.data.meshes.new(name)
    me.vertices.add(len(P))
    me.vertices.foreach_set("co", np.asarray(P, np.float32).ravel())
    a = me.attributes.new("noctis_rot", "FLOAT_VECTOR", "POINT")
    a.data.foreach_set("vector", np.asarray(R, np.float32).ravel())
    b = me.attributes.new("noctis_scale", "FLOAT_VECTOR", "POINT")
    b.data.foreach_set("vector", np.asarray(S, np.float32).ravel())
    ob = bpy.data.objects.new(name, me)
    collection.objects.link(ob)
    mod = ob.modifiers.new("instances", "NODES")
    g = _instancer_group()
    mod.node_group = g
    ident = [it.identifier for it in g.interface.items_tree if getattr(it, "in_out", "") == "INPUT" and it.socket_type == "NodeSocketCollection"][0]
    mod[ident] = source
    return ob


# ----------------------------------------------------------------------------------------------- scatter
def _cluster_noise(x, y, scale_m, seed):
    rng = np.random.default_rng(seed)
    h = np.zeros_like(x, dtype=np.float64)
    for k in range(6):
        f = 2 * math.pi / (scale_m * rng.uniform(0.6, 1.6))
        a = rng.uniform(0, 2 * math.pi)
        h += np.sin((x * math.cos(a) + y * math.sin(a)) * f + rng.uniform(0, 6.3))
    return np.clip(0.5 + h / 6.0 * 1.2, 0, 1)


def _thin(P, r, rng, existing=None, r_existing=None):
    """Greedy minimum-distance thinning (random order). existing: points that block new ones."""
    from scipy.spatial import cKDTree
    if len(P) == 0:
        return np.zeros(0, bool)
    keep = np.ones(len(P), bool)
    if existing is not None and len(existing):
        t = cKDTree(existing)
        d, _ = t.query(P, k=1)
        keep &= d > (r_existing if r_existing is not None else r)
    tree = cKDTree(P)
    pairs = tree.query_pairs(r, output_type="ndarray")
    if len(pairs):
        order = rng.permutation(len(P))
        rank = np.empty(len(P), int)
        rank[order] = np.arange(len(P))
        pairs = pairs[np.argsort(np.minimum(rank[pairs[:, 0]], rank[pairs[:, 1]]))]
        for i, j in pairs:
            if keep[i] and keep[j]:
                if rank[i] < rank[j]:
                    keep[j] = False
                else:
                    keep[i] = False
    return keep


class _Field:
    """Per-scatter sampling context: masks, water distance, slope over the terrain crop."""

    def __init__(self, terrain, sim):
        from envlib import terrain as T
        from scipy import ndimage
        self.T = T
        self.sim = sim
        self.ox, self.oy = terrain["noctis_origin_x"], terrain["noctis_origin_y"]
        self.size = terrain["noctis_size_m"]
        res = 2.0
        n = int(self.size / res) + 1
        xs = self.ox - self.size / 2 + np.arange(n) * res
        ys = self.oy - self.size / 2 + np.arange(n) * res
        X, Y = np.meshgrid(xs, ys)
        Z = T.height_at(sim, X, Y, 0.0)
        Wl = sim.sample("water_surface", X, Y)
        wm = ndimage.binary_dilation(sim.sample("water_mask", X, Y) > 0.05, iterations=4)
        water = (Z < Wl) & wm
        din = ndimage.distance_transform_edt(water) * res
        dout = ndimage.distance_transform_edt(~water) * res
        self.wdist = np.where(water, -din, dout)  # signed distance to the shoreline (negative = in water)
        gy, gx = np.gradient(Z, res)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy)))
        self.x0, self.y0, self.res, self.n = xs[0], ys[0], res, n

    def grid(self, arr, x, y):
        from scipy import ndimage
        col = (np.asarray(x) - self.x0) / self.res
        row = (np.asarray(y) - self.y0) / self.res
        return ndimage.map_coordinates(arr, [row, col], order=1, mode="nearest")


def scatter(terrain, camera=None, asset_ids=None, seed=7, density=1.0, lod_distances=None, range_scale=1.0,
            frustum_margin=0.12, keep_clear=(), extra=(), budget=400000, sources=None, verbose=True):
    """Scatters the library assets over 'terrain' following the simulation masks.

    density: global multiplier of the per-hectare densities (manifest).  range_scale: multiplies the per-category
    maximum distances.  lod_distances: {category: (lod1_from_m, lod2_from_m)} overrides.  keep_clear: iterable of
    (x, y, radius) in simulation metres.  extra: hand-placed instances [{asset, x, y, yaw_deg?, scale?}].
    Returns a dict of statistics (instances per asset and LOD)."""
    import bpy
    from mathutils import Matrix, Vector
    from mathutils.bvhtree import BVHTree
    man = manifest()
    A = man["assets"]
    rules = man["scatter"]["pft_rules"]
    drules = man["scatter"]["deadwood_rules"]
    sources = sources or append_assets(asset_ids)
    sim = sim_data()
    F = _Field(terrain, sim)
    rng = np.random.default_rng(seed)
    coll = bpy.data.collections.get(SCATTER_COLLECTION) or bpy.data.collections.new(SCATTER_COLLECTION)
    if coll.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(coll)
    for o in list(coll.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    # camera (matrix_world is only refreshed by a depsgraph update after location / rotation changes)
    if camera is not None:
        bpy.context.view_layer.update()
        cam_m = camera.matrix_world.copy()
        cam_inv = cam_m.inverted()
        cpos = np.array(cam_m.translation)
        cx_sim, cy_sim = cpos[0] + F.ox, cpos[1] + F.oy
        sc = bpy.context.scene
        aspect = sc.render.resolution_x / sc.render.resolution_y
        fov = camera.data.angle
        tan_x = math.tan(fov / 2) if aspect >= 1 else math.tan(fov / 2) * aspect
        tan_y = tan_x / aspect
    else:
        cx_sim, cy_sim = F.ox, F.oy
    half = F.size / 2 - 1.0
    bx0, bx1, by0, by1 = F.ox - half, F.ox + half, F.oy - half, F.oy + half

    def domain(max_m):
        r = max_m * range_scale
        return max(bx0, cx_sim - r), min(bx1, cx_sim + r), max(by0, cy_sim - r), min(by1, cy_sim + r), r

    placed = {}  # aid -> list of (x, y, yaw, scale)
    tree_pts = np.zeros((0, 2))

    def accept_common(x, y, rule, cat):
        ok = np.ones(len(x), bool)
        wd = F.grid(F.wdist, x, y)
        buf = rule.get("water_buffer_m", 2.0)
        ok &= wd > buf
        if cat == "aquatic":
            ok &= wd < 2.0  # aquatic plants stay in or right next to the water
        ok &= F.grid(F.slope, x, y) < rule.get("slope_max_deg", 35)
        for (kx, ky, kr) in keep_clear:
            ok &= np.hypot(x - kx, y - ky) > kr
        return ok

    def habitats_at(x, y):
        hw = sim.habitat_weights(x, y)
        names = list(hw)
        M = np.stack([hw[k] for k in names], 1)
        return names, M

    # --- plant functional types (big to small so trees block each other)
    order = ["conifer", "angio_tree", "ginkgo", "palm", "cycadophyte", "angio_shrub", "fern", "horsetail", "angio_herb", "aquatic_macrophyte"]
    for pft in order:
        rule = rules.get(pft)
        if not rule:
            continue
        ids = [a for a in rule["assets"] if (asset_ids is None or a in asset_ids) and a in A]
        if not ids:
            continue
        cat = A[ids[0]]["category"]
        x0, x1, y0, y1, rmax = domain(CATEGORY[cat]["max_m"])
        if x1 <= x0 or y1 <= y0:
            continue
        area_ha = (x1 - x0) * (y1 - y0) / 1e4
        n = int(rule["per_ha"] * area_ha * density)
        if n <= 0:
            continue
        n = min(n, budget)
        x = rng.uniform(x0, x1, n)
        y = rng.uniform(y0, y1, n)
        inr = np.hypot(x - cx_sim, y - cy_sim) < rmax
        x, y = x[inr], y[inr]
        mask = sim.sample(rule["mask"], x, y)
        clu = _cluster_noise(x, y, rule.get("cluster_m", 50), seed + len(pft))
        amp = rule.get("cluster_amp", 0.5)
        p = mask * ((1 - amp) + amp * clu)
        if rule.get("shoreline_per_ha"):  # aquatic plants also line calm shores (game assumption)
            wd = F.grid(F.wdist, x, y)
            p = np.maximum(p, (rule["shoreline_per_ha"] / rule["per_ha"]) * np.exp(-((wd + 1.0) / 1.8) ** 2) * clu)
        ok = (rng.random(len(x)) < p) & accept_common(x, y, rule, cat)
        x, y = x[ok], y[ok]
        P = np.stack([x, y], 1)
        if cat in ("tree", "palm") and len(P):
            keep = _thin(P, rule.get("min_spacing_m", 3.0), rng, existing=tree_pts, r_existing=rule.get("min_spacing_m", 3.0) * 0.6)
        else:
            keep = _thin(P, rule.get("min_spacing_m", 1.0), rng)
        x, y = x[keep], y[keep]
        if cat in ("tree", "palm"):
            tree_pts = np.concatenate([tree_pts, np.stack([x, y], 1)])
        # asset choice with habitat bias
        w = np.array([rule["assets"][a] for a in ids], float)
        W = np.repeat(w[None], len(x), 0)
        if rule.get("habitat_bias") and len(x):
            names, M = habitats_at(x, y)
            dom = np.array(names)[np.argmax(M, 1)]
            for hab, bias in rule["habitat_bias"].items():
                for a, f in bias.items():
                    if a in ids:
                        W[dom == hab, ids.index(a)] *= f
        W /= W.sum(1, keepdims=True)
        u = rng.random(len(x))
        choice = (u[:, None] > np.cumsum(W, 1)).sum(1)
        wdx = F.grid(F.wdist, x, y) if len(x) else np.zeros(0)
        for k, a in enumerate(ids):
            sel = choice == k
            if A[a].get("on_water"):
                sel &= wdx < -0.4  # floating plants only on open water
            placed.setdefault(a, []).extend(zip(x[sel], y[sel], rng.uniform(0, 360, sel.sum()), rng.uniform(0.82, 1.15, sel.sum())))
    # --- deadwood (habitat driven)
    for a, dr in drules.items():
        if a not in A or (asset_ids is not None and a not in asset_ids):
            continue
        maxm = SMALL_DEADWOOD_MAX_M if a == "deadwood_branches" else CATEGORY["deadwood"]["max_m"]
        x0, x1, y0, y1, rmax = domain(maxm)
        if x1 <= x0 or y1 <= y0:
            continue
        area_ha = (x1 - x0) * (y1 - y0) / 1e4
        dmax = max(dr["habitats"].values())
        n = int(dmax * area_ha * density)
        x = rng.uniform(x0, x1, n)
        y = rng.uniform(y0, y1, n)
        names, M = habitats_at(x, y)
        dens = np.zeros(len(x))
        for i, h in enumerate(names):
            dens += M[:, i] * dr["habitats"].get(h, 0.0)
        ok = (rng.random(len(x)) < dens / dmax) & (np.hypot(x - cx_sim, y - cy_sim) < rmax)
        ok &= accept_common(x, y, dict(water_buffer_m=0.5, slope_max_deg=40), "deadwood")
        x, y = x[ok], y[ok]
        keep = _thin(np.stack([x, y], 1), dr.get("min_spacing_m", 5), rng) if len(x) else np.zeros(0, bool)
        placed.setdefault(a, []).extend(zip(x[keep], y[keep], rng.uniform(0, 360, keep.sum()), rng.uniform(0.8, 1.2, keep.sum())))
    for e in extra:
        placed.setdefault(e["asset"], []).append((e["x"], e["y"], e.get("yaw_deg", rng.uniform(0, 360)), e.get("scale", 1.0)))
    # --- heights (ray cast on the terrain and its detail patch), LOD, frustum culling, instancers
    deps = bpy.context.evaluated_depsgraph_get()
    bvhs = [BVHTree.FromObject(terrain, deps)]
    if terrain.get("noctis_detail_object") and bpy.data.objects.get(terrain["noctis_detail_object"]):
        bvhs.insert(0, BVHTree.FromObject(bpy.data.objects[terrain["noctis_detail_object"]], deps))
    tw = terrain.matrix_world
    tinv = tw.inverted()
    stats = {}
    total = 0
    for a, items in placed.items():
        if not items:
            continue
        meta = A[a]
        cat = meta["category"]
        arr = np.array(items, float)
        X, Y = arr[:, 0] - F.ox, arr[:, 1] - F.oy
        Z = np.full(len(X), np.nan)
        for i in range(len(X)):
            o = tinv @ Vector((X[i], Y[i], 2000.0))
            for b in bvhs:
                hit = b.ray_cast(o, Vector((0, 0, -1)))
                if hit[0] is not None:
                    Z[i] = (tw @ hit[0]).z
                    break
        if meta.get("on_water"):
            Z = sim.sample("water_surface", arr[:, 0], arr[:, 1]) + 0.005
        good = ~np.isnan(Z)
        X, Y, Z, arr = X[good], Y[good], Z[good], arr[good]
        P = np.stack([X, Y, Z], 1)
        height = float(meta.get("bounds_m", {}).get("height", 5.0))
        radius = max(height, float(meta.get("bounds_m", {}).get("width", 3.0))) * 0.6
        if camera is not None:
            Pc = (np.array(cam_inv)[:3, :3] @ P.T).T + np.array(cam_inv)[:3, 3]
            depth = -Pc[:, 2]
            dist = np.linalg.norm(P - cpos, axis=1)
            vis = depth > -radius
            dz = np.maximum(depth, 0.1)
            vis &= np.abs(Pc[:, 0]) < dz * tan_x * (1 + frustum_margin) + radius
            vis &= np.abs(Pc[:, 1] - height * 0.5) < dz * tan_y * (1 + frustum_margin) + radius
            P, arr, dist = P[vis], arr[vis], dist[vis]
        else:
            dist = np.zeros(len(P))
        lods = (lod_distances or {}).get(cat, CATEGORY[cat]["lod"])
        L = np.where(dist < lods[0], 0, np.where(dist < lods[1], 1, 2))
        for lod in (0, 1, 2):
            sel = L == lod
            if not sel.any():
                continue
            src = sources.get((a, lod)) or sources.get((a, 0))
            if src is None:
                continue
            yaw = np.radians(arr[sel, 2])
            tilt = rng.normal(0, 0.03, (sel.sum(), 2)) if cat in ("tree", "palm") else rng.normal(0, 0.06, (sel.sum(), 2))
            R = np.stack([tilt[:, 0], tilt[:, 1], yaw], 1)
            s = arr[sel, 3]
            S = np.stack([s, s, s], 1)
            _make_instancer("scatter_%s_LOD%d" % (a, lod), P[sel], R, S, src, coll)
            stats["%s_LOD%d" % (a, lod)] = int(sel.sum())
            total += int(sel.sum())
    stats["_total"] = total
    if verbose:
        print("[vegetation_library] %d instances: %s" % (total, ", ".join("%s=%d" % kv for kv in sorted(stats.items()) if not kv[0].startswith("_"))))
    return stats
