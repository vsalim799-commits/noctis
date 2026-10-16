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
LOD by camera distance (lod_distances), frustum culling with a margin, per-category maximum distances (density
faded out over the last 25 % with randomised radii, so no visible ring), and a global instance budget keep Cycles
renders reasonable. The keep probability of every candidate uses the SAME functions as the Unreal spawn-probability
bake (Tools/EnvironmentFactory/envlib/pcgbake.py): the renders show the distribution the PCG graph produces.
Nothing here is a scientific claim: densities are game assumptions documented in Docs/ENVIRONMENT.md and each
asset's metadata.
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
    "decal": dict(max_m=90, lod=(1e9, 1e9)),
}
SMALL_DEADWOOD_MAX_M = 70
FADE_FRACTION = 0.25   # density fades linearly to 0 over the last 25 % of max_m (per-instance random radius)
LOD_JITTER = 0.12      # per-instance jitter of the LOD switch distances


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
def build_terrain(center, size_m, res_m=2.0, shading=True, water=True, detail=None, name="NoctisTerrain", mask_px=1024,
                  water_kw=None, canopy_shade=0.0):
    """Terrain crop from the simulation heightmap (+ habitat-blended ground material and river water).
    detail: optional dict(center=(x, y), size=m, res=m) high-resolution patch near the camera.
    water_kw: options of the water shader (color, wave_scale, bump_strength: 0.03 = calm mirror, 0.08 = breeze).
    canopy_shade: 0..1 darkening of the forest floor under wooded habitats (aerial shots: ~0.75, hides the lit litter
    seen through the porous alpha-card crowns; eye-level shots: 0-0.3)."""
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
        mat = TS.terrain_material(masks, canopy_shade=canopy_shade)
    ob = T.build_terrain(center, size_m, res_m=res_m, name=name, detail=detail, sim=sim, collection=coll, material=mat)
    if water:
        T.build_water(center, size_m, res_m=max(2.0, res_m), name=name + "_Water", sim=sim, collection=coll,
                      material=TS.water_material(**(water_kw or {})))
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
    """Isotropic patchiness field (envlib.fields), identical to the one baked for Unreal."""
    from envlib import fields
    return fields.cluster_noise(x, y, scale_m, seed)


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


_CORRIDOR = {}


def corridor_mask(sim, x, y):
    """Predicted animal corridors (Output/level/mask_corridor_use.png, export-level), sampled at sim coordinates."""
    if "a" not in _CORRIDOR:
        p = os.path.join(ROOT, "Output", "level", "mask_corridor_use.png")
        a = None
        if os.path.exists(p):
            from PIL import Image
            a = np.asarray(Image.open(p)).astype(np.float32)
            a = (a[..., 0] if a.ndim == 3 else a) / 255.0
        _CORRIDOR["a"] = a
    a = _CORRIDOR["a"]
    if a is None:
        return np.zeros(np.shape(x), np.float32)
    from scipy import ndimage
    n = a.shape[0]
    cs = sim.size / n
    return ndimage.map_coordinates(a, [(sim.size - np.asarray(y)) / cs - 0.5, np.asarray(x) / cs - 0.5], order=1, mode="nearest")


def _decal_object(items, terrain, bvhs, collection):
    """One mesh holding every trackway decal: 0.75 x 3 m strips conforming to the terrain (ray cast), 1.5 cm above it,
    UVs in the cell of the trackway atlas (Tools/EnvironmentFactory envlib/ground.py DECALS)."""
    import bpy
    from mathutils import Vector
    from envlib import ground, materials, paint
    tw = terrain.matrix_world
    tinv = tw.inverted()
    cw, ch = ground.DECAL_CELL_M
    us = np.linspace(-cw / 2, cw / 2, 7)    # ~12 cm grid: follows the 10 cm detail patch of a close-up
    vs = np.linspace(-ch / 2, ch / 2, 25)
    V, F, UV = [], [], []
    for (x, y, yaw, cell) in items:
        c, s_ = math.cos(yaw), math.sin(yaw)
        u0, v0, u1, v1 = paint.cell_uv_rect(int(cell), ground.DECAL_GRID)
        base = len(V)
        for j, v in enumerate(vs):
            for i, u in enumerate(us):
                # local +v = walking direction (yaw, radians from +x), local +u to its right
                X = x + v * c + u * s_
                Y = y + v * s_ - u * c
                o = tinv @ Vector((X - terrain["noctis_origin_x"], Y - terrain["noctis_origin_y"], 2000.0))
                z = None
                for b in bvhs:
                    hit = b.ray_cast(o, Vector((0, 0, -1)))
                    if hit[0] is not None:
                        z = (tw @ hit[0]).z
                        break
                V.append((X - terrain["noctis_origin_x"], Y - terrain["noctis_origin_y"], (z if z is not None else 0.0) + 0.03))
        for j in range(len(vs) - 1):
            for i in range(len(us) - 1):
                a = base + j * len(us) + i
                F.append((a, a + 1, a + len(us) + 1, a + len(us)))
                for (ii, jj) in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)):
                    UV.append((u0 + (u1 - u0) * ii / (len(us) - 1), v0 + (v1 - v0) * jj / (len(vs) - 1)))
    me = bpy.data.meshes.new("TrackwayDecals")
    me.from_pydata(V, [], F)
    uvl = me.uv_layers.new(name="UVMap")
    uvl.data.foreach_set("uv", np.asarray(UV, np.float32).ravel())
    me.materials.append(materials.decal_material())
    ob = bpy.data.objects.new("TrackwayDecals", me)
    ob.visible_shadow = False
    collection.objects.link(ob)
    return ob


def scatter(terrain, camera=None, asset_ids=None, seed=7, density=1.0, lod_distances=None, range_scale=1.0,
            frustum_margin=0.12, keep_clear=(), extra=(), budget=400000, sources=None, verbose=True, decals=True,
            extra_decals=(), max_distances=None):
    """Scatters the library assets over 'terrain' following the simulation masks.

    density: global multiplier of the per-hectare densities (manifest).  range_scale: multiplies the per-category
    maximum distances.  lod_distances: {category: (lod1_from_m, lod2_from_m)} overrides.  keep_clear: iterable of
    (x, y, radius) or (x, y, radius, {categories}) in simulation metres (the 4th element limits the clearing to
    those categories, e.g. keep trees out of the lens but let ferns grow there).  extra: hand-placed instances [{asset, x, y, yaw_deg?, scale?}].
    decals: trackway decals at the water's edge (towards the water) and along predicted corridors (along them).
    extra_decals: hand-placed trackways [{x, y, cell (0 theropod, 1 hadrosaur, 2 small tridactyls, 3 trampled),
    yaw_deg? (walking direction, counter-clockwise from east; default: towards the water)}].
    max_distances: {category: metres} overrides of CATEGORY max_m; "cull" = the largest end cull distance of the
    category's assets in the manifest (what the game draws), e.g. {"fern": "cull", "herb": "cull"}.
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

    def max_m_of(cat):
        v = (max_distances or {}).get(cat)
        if v == "cull":
            ends = [a["cull_distance_cm"]["end"] for a in A.values() if a.get("category") == cat and a.get("cull_distance_cm")]
            return max(ends) / 100.0 if ends and max(ends) > 0 else CATEGORY[cat]["max_m"]
        return float(v) if v else CATEGORY[cat]["max_m"]

    def domain(max_m):
        r = max_m * range_scale
        return max(bx0, cx_sim - r), min(bx1, cx_sim + r), max(by0, cy_sim - r), min(by1, cy_sim + r), r

    placed = {}  # aid -> list of (x, y, yaw, scale)
    tree_pts = np.zeros((0, 2))
    from envlib import pcgbake as PB
    clears = PB.clearings()

    def in_range(x, y, rmax):
        """Candidates closer than a per-instance random radius in [1 - FADE_FRACTION, 1] x rmax: the density fades
        linearly to zero over the last FADE_FRACTION of the range instead of stopping on a visible ring."""
        u = rng.random(len(x))
        return np.hypot(x - cx_sim, y - cy_sim) < rmax * (1 - FADE_FRACTION + FADE_FRACTION * u)

    def keep_clear_ok(x, y, cat):
        ok = np.ones(len(x), bool)
        for kc in keep_clear:
            kx, ky, kr = kc[:3]
            if len(kc) > 3 and kc[3] and cat not in kc[3]:
                continue  # this clearing only applies to the listed categories
            ok &= np.hypot(x - kx, y - ky) > kr
        return ok

    def scale_of(a, n):
        lo, hi = A[a].get("scale_range") or [0.82, 1.15]
        return rng.uniform(lo, hi, n)

    # --- plant functional types (big to small so trees block each other); keep probability = envlib.pcgbake
    order = ["conifer", "angio_tree", "ginkgo", "palm", "cycadophyte", "angio_shrub", "fern", "horsetail", "angio_herb", "aquatic_macrophyte"]
    for pft in order:
        rule = rules.get(pft)
        if not rule:
            continue
        ids = [a for a in rule["assets"] if (asset_ids is None or a in asset_ids) and a in A]
        if not ids:
            continue
        cat = A[ids[0]]["category"]
        x0, x1, y0, y1, rmax = domain(max_m_of(cat))
        if x1 <= x0 or y1 <= y0:
            continue
        area_ha = (x1 - x0) * (y1 - y0) / 1e4
        n = int(rule["per_ha"] * area_ha * density)
        if n <= 0:
            continue
        n = min(n, budget)
        x = rng.uniform(x0, x1, n)
        y = rng.uniform(y0, y1, n)
        inr = in_range(x, y, rmax) & keep_clear_ok(x, y, cat)
        x, y = x[inr], y[inr]
        if not len(x):
            continue
        hab = sim.habitat_weights(x, y)
        clu = _cluster_noise(x, y, rule.get("cluster_m", 50), PB.CLUSTER_SEED + len(pft))
        p = PB.pft_probability(rule, sim.sample(rule["mask"], x, y), clu, PB.calm_water(rule, hab))
        wd = F.grid(F.wdist, x, y)
        common = PB.slope_ok(rule, F.grid(F.slope, x, y)) * PB.clearing_factor(x, y, cat, clears)
        if cat != "aquatic":
            common = common * PB.open_water_exclusion(hab)
        shares = PB.asset_shares(rule, ids, hab)
        PA = np.stack([p * shares[a] * PB.water_ok(rule, a, wd) * common for a in ids], 1)
        ptot = PA.sum(1)
        ok = rng.random(len(x)) < ptot
        x, y, PA = x[ok], y[ok], PA[ok]
        P = np.stack([x, y], 1)
        if cat in ("tree", "palm") and len(P):
            keep = _thin(P, rule.get("min_spacing_m", 3.0), rng, existing=tree_pts, r_existing=rule.get("min_spacing_m", 3.0) * 0.6)
        else:
            keep = _thin(P, rule.get("min_spacing_m", 1.0), rng)
        x, y, PA = x[keep], y[keep], PA[keep]
        if cat in ("tree", "palm"):
            tree_pts = np.concatenate([tree_pts, np.stack([x, y], 1)])
        if not len(x):
            continue
        W = PA / np.maximum(PA.sum(1, keepdims=True), 1e-12)
        u = rng.random(len(x))
        choice = np.minimum((u[:, None] > np.cumsum(W, 1)).sum(1), len(ids) - 1)
        for k, a in enumerate(ids):
            sel = choice == k
            placed.setdefault(a, []).extend(zip(x[sel], y[sel], rng.uniform(0, 360, sel.sum()), scale_of(a, sel.sum())))
    # --- deadwood (habitat driven; the Unreal split between the foliage and river-debris graphs is irrelevant here)
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
        hab = sim.habitat_weights(x, y)
        dens = np.zeros(len(x))
        for h, v in dr["habitats"].items():
            if h in hab:
                dens += hab[h] * v
        ok = (rng.random(len(x)) < dens / dmax * PB.clearing_factor(x, y, "deadwood", clears)) & in_range(x, y, rmax)
        ok &= (F.grid(F.wdist, x, y) > 0.5) & (F.grid(F.slope, x, y) < 40) & keep_clear_ok(x, y, "deadwood")
        x, y = x[ok], y[ok]
        keep = _thin(np.stack([x, y], 1), dr.get("min_spacing_m", 5), rng) if len(x) else np.zeros(0, bool)
        placed.setdefault(a, []).extend(zip(x[keep], y[keep], rng.uniform(0, 360, keep.sum()), scale_of(a, keep.sum())))
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
        jit = 1.0 + LOD_JITTER * (rng.random(len(dist)) * 2 - 1)  # no sharp LOD ring
        L = np.where(dist < lods[0] * jit, 0, np.where(dist < lods[1] * jit, 1, 2))
        for lod in (0, 1, 2):
            sel = L == lod
            if not sel.any():
                continue
            src = sources.get((a, lod)) or sources.get((a, 0))
            if src is None:
                continue
            yaw = np.radians(arr[sel, 2])
            tilt = rng.normal(0, 0.035, (sel.sum(), 2)) if cat in ("tree", "palm") else rng.normal(0, 0.07, (sel.sum(), 2))
            R = np.stack([tilt[:, 0], tilt[:, 1], yaw], 1)
            s = arr[sel, 3]
            S = np.stack([s, s, s], 1)
            _make_instancer("scatter_%s_LOD%d" % (a, lod), P[sel], R, S, src, coll)
            stats["%s_LOD%d" % (a, lod)] = int(sel.sum())
            total += int(sel.sum())
    # --- trackway decals (Unreal: mesh decals of PCG_Noctis_Foliage at the banks; trails graph along corridors)
    if decals and os.path.exists(os.path.join(BUILD, "ground", "T_Decal_Trackways_BaseColor.png")):
        x0, x1, y0, y1, rmax = domain(CATEGORY["decal"]["max_m"])
        if x1 > x0 and y1 > y0:
            area_ha = (x1 - x0) * (y1 - y0) / 1e4
            n = int(PB.TRACKWAY_PER_HA * 3 * area_ha)
            x = rng.uniform(x0, x1, n)
            y = rng.uniform(y0, y1, n)
            ok = in_range(x, y, rmax) & keep_clear_ok(x, y, "decal")
            x, y = x[ok], y[ok]
            hab = sim.habitat_weights(x, y)
            wd = F.grid(F.wdist, x, y)
            corr = np.clip(corridor_mask(sim, x, y) * 3.0, 0, 1)
            flat = F.grid(F.slope, x, y) < 25
            bank = np.clip(hab.get("riverbank", 0) + 0.5 * hab.get("sandbar", 0), 0, 1) * ((wd > 0.3) & (wd < 8.0)) * (0.3 + 0.7 * corr)
            trail = 0.35 * corr * (wd > 0.3)
            p = np.maximum(bank, trail) * flat / 3.0
            ok = rng.random(len(x)) < p
            x, y, bank, trail = x[ok], y[ok], bank[ok], trail[ok]
            keep = _thin(np.stack([x, y], 1), 4.0, rng) if len(x) else np.zeros(0, bool)
            x, y, bank, trail = x[keep], y[keep], bank[keep], trail[keep]
            items = []
            for e in extra_decals:
                if e.get("yaw_deg") is not None:
                    yaw = math.radians(e["yaw_deg"])
                else:
                    gx = F.grid(F.wdist, [e["x"] + 1.0], [e["y"]])[0] - F.grid(F.wdist, [e["x"] - 1.0], [e["y"]])[0]
                    gy = F.grid(F.wdist, [e["x"]], [e["y"] + 1.0])[0] - F.grid(F.wdist, [e["x"]], [e["y"] - 1.0])[0]
                    yaw = math.atan2(-gy, -gx)
                items.append((float(e["x"]), float(e["y"]), yaw, int(e.get("cell", 0))))
            for xi, yi, b, t in zip(x, y, bank, trail):
                if b >= t:  # walking to the water: down the gradient of the distance to the shore
                    gx = (F.grid(F.wdist, [xi + 1.0], [yi])[0] - F.grid(F.wdist, [xi - 1.0], [yi])[0])
                    gy = (F.grid(F.wdist, [xi], [yi + 1.0])[0] - F.grid(F.wdist, [xi], [yi - 1.0])[0])
                    yaw = math.atan2(-gy, -gx) + rng.normal(0, 0.35)
                    cell = int(rng.choice(4, p=[0.25, 0.25, 0.2, 0.3]))
                else:       # along the corridor: perpendicular to the gradient of the corridor mask
                    c = lambda xx, yy: float(corridor_mask(sim, [xx], [yy])[0])  # noqa: E731
                    gx, gy = c(xi + 3, yi) - c(xi - 3, yi), c(xi, yi + 3) - c(xi, yi - 3)
                    yaw = math.atan2(gx, -gy) + (math.pi if rng.random() < 0.5 else 0.0) + rng.normal(0, 0.2)
                    cell = int(rng.choice(4, p=[0.35, 0.35, 0.2, 0.1]))
                items.append((float(xi), float(yi), yaw, cell))
            if items:
                _decal_object(items, terrain, bvhs, coll)
                stats["trackway_decals"] = len(items)
    stats["_total"] = total
    if verbose:
        print("[vegetation_library] %d instances: %s" % (total, ", ".join("%s=%d" % kv for kv in sorted(stats.items()) if not kv[0].startswith("_"))))
    return stats
