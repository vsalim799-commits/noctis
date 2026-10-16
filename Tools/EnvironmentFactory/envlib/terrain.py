"""Terrain crops for Blender scenes, built from the simulation exports (export-terrain / export-ground).

Frames: simulation x = east, y = north (metres). Blender X = east, Y = north, Z = up; a crop object is placed
so that Blender (X, Y) = sim (x, y) - origin, origin = crop centre (kept small for float precision); the
object carries custom properties noctis_origin_x/y, noctis_size_m used by the vegetation helper.

Heights: bicubic interpolation of heightmap.png (4.06 m spacing) plus a small procedural micro-relief
(design assumption, documented): +-0.15 m fbm away from the water, so close-ups do not show the 4 m grid.
"""
from __future__ import annotations

import math
import os

import numpy as np
from PIL import Image
from scipy import ndimage

from . import common
from . import texgen as tg

HABITATS = ["channel", "riverbank", "sandbar", "oxbow_lake", "backswamp", "riparian_forest", "floodplain_open",
            "levee_woodland", "upland_conifer_forest", "cutbank_cliff"]
PFTS = ["fern", "horsetail", "angio_herb", "angio_shrub", "angio_tree", "conifer", "palm", "cycadophyte", "ginkgo",
        "aquatic_macrophyte"]


def _gray(path):
    a = np.asarray(Image.open(path))
    if a.ndim == 3:
        a = a[..., 0]
    return a


class SimData:
    """Simulation rasters with sampling in simulation coordinates (bilinear, row 0 = north)."""

    def __init__(self, sim_dir=None):
        self.dir = sim_dir or common.SIM_DIR
        t = common.read_json(os.path.join(self.dir, "terrain.json"))
        if t is None:
            raise FileNotFoundError("terrain.json absent in %s: run 'factory.py --stages sim'" % self.dir)
        self.meta = t
        self.size = float(t["size_m"])
        self.hmin, self.hmax = float(t["min_height_m"]), float(t["max_height_m"])
        hm = _gray(os.path.join(self.dir, "heightmap.png")).astype(np.float64)
        self.height = self.hmin + hm / 65535.0 * (self.hmax - self.hmin)
        self._cache = {}

    def raster(self, name):
        if name not in self._cache:
            p = os.path.join(self.dir, name + ".png")
            if not os.path.exists(p):
                self._cache[name] = None
            elif name == "water_surface":
                a = _gray(p).astype(np.float64)
                self._cache[name] = np.where(a > 0, self.hmin + a / 65535.0 * (self.hmax - self.hmin), np.nan)
            else:
                self._cache[name] = _gray(p).astype(np.float32) / 255.0
        return self._cache[name]

    def _coords(self, arr, x, y, vertex=False):
        n = arr.shape[0]
        if vertex:  # heightmap: samples at i * spacing
            sp = self.size / (n - 1)
            col = np.asarray(x) / sp
            row = (self.size - np.asarray(y)) / sp
        else:  # cell-centred rasters
            cs = self.size / n
            col = np.asarray(x) / cs - 0.5
            row = (self.size - np.asarray(y)) / cs - 0.5
        return row, col

    def sample(self, name, x, y, order=1):
        arr = self.height if name == "height" else self.raster(name)
        if arr is None:
            return np.zeros(np.shape(x), np.float32)
        row, col = self._coords(arr, x, y, vertex=(name == "height"))
        if name == "height" and order == 3:
            # cubic spline without overshoot: clamp to the 2x2 surrounding samples (otherwise the 5 m step of a
            # cutbank rings into 1-2 m pillow-shaped mounds along the bank lip)
            z = ndimage.map_coordinates(arr, [row, col], order=3, mode="nearest")
            n = arr.shape[0]
            r0 = np.clip(np.floor(row).astype(int), 0, n - 1)
            c0 = np.clip(np.floor(col).astype(int), 0, n - 1)
            r1, c1 = np.minimum(r0 + 1, n - 1), np.minimum(c0 + 1, n - 1)
            q = np.stack([arr[r0, c0], arr[r0, c1], arr[r1, c0], arr[r1, c1]])
            return np.clip(z, q.min(0), q.max(0))
        if name == "water_surface":
            filled = self.water_level_field()
            return ndimage.map_coordinates(filled, [row, col], order=1, mode="nearest")
        return ndimage.map_coordinates(arr, [row, col], order=order, mode="nearest")

    def water_level_field(self):
        """Water surface extended to dry cells (nearest wet cell), smoothed: for building water planes."""
        if "_wl" not in self._cache:
            ws = self.raster("water_surface")
            if ws is None:
                self._cache["_wl"] = np.full((8, 8), self.hmin)
            else:
                wet = ~np.isnan(ws)
                _, (iy, ix) = ndimage.distance_transform_edt(~wet, return_indices=True)
                f = ws[iy, ix]
                self._cache["_wl"] = ndimage.gaussian_filter(f, 3.0)
        return self._cache["_wl"]

    def habitat_weights(self, x, y, smooth=True):
        out = {}
        for h in HABITATS:
            r = self.raster("weight_" + h)
            if r is None:
                continue
            out[h] = self.sample("weight_" + h, x, y, order=1)
        return out


def micro_relief(X, Y, seed=3, amp=0.15):
    """Deterministic procedural micro-relief (metres) from world coordinates (sum of hashed sines)."""
    rng = np.random.default_rng(seed)
    h = np.zeros_like(X, dtype=np.float64)
    for k in range(10):
        f = 2 * math.pi / rng.uniform(3.0, 40.0)
        a = rng.uniform(0, 2 * math.pi)
        ph = rng.uniform(0, 2 * math.pi)
        h += math.sqrt(f) * np.sin((X * math.cos(a) + Y * math.sin(a)) * f + ph)
    return amp * h / 10.0


def height_at(sim, x, y, micro=0.15):
    z = sim.sample("height", x, y, order=3)
    if micro:
        wet = sim.sample("water_mask", x, y)
        z = z + micro_relief(x, y, amp=micro) * (1 - np.clip(wet * 2, 0, 1))
    return z


def build_terrain(center, size_m, res_m=2.0, name="NoctisTerrain", detail=None, sim=None, collection=None,
                  micro=0.15, material=None):
    """Creates the terrain crop object. detail = dict(center=(x, y), size=m, res=m, displace=callable or None)
    adds a high-resolution patch (and removes the coarse faces under it)."""
    import bpy
    sim = sim or SimData()
    cx, cy = center
    n = int(round(size_m / res_m)) + 1
    xs = cx - size_m / 2 + np.arange(n) * res_m
    ys = cy - size_m / 2 + np.arange(n) * res_m
    X, Y = np.meshgrid(xs, ys)
    Z = height_at(sim, X, Y, micro)
    keep = np.ones((n - 1, n - 1), bool)
    if detail:
        dx0 = detail["center"][0] - detail["size"] / 2 + res_m
        dx1 = detail["center"][0] + detail["size"] / 2 - res_m
        dy0 = detail["center"][1] - detail["size"] / 2 + res_m
        dy1 = detail["center"][1] + detail["size"] / 2 - res_m
        qx = (xs[:-1] + xs[1:]) / 2
        qy = (ys[:-1] + ys[1:]) / 2
        QX, QY = np.meshgrid(qx, qy)
        keep = ~((QX > dx0) & (QX < dx1) & (QY > dy0) & (QY < dy1))
    ob = _grid_object(name, X - cx, Y - cy, Z, keep, (cx, cy, size_m), collection)
    if detail:
        dn = int(round(detail["size"] / detail["res"])) + 1
        dxs = detail["center"][0] - detail["size"] / 2 + np.arange(dn) * detail["res"]
        dys = detail["center"][1] - detail["size"] / 2 + np.arange(dn) * detail["res"]
        DX, DY = np.meshgrid(dxs, dys)
        DZ = height_at(sim, DX, DY, micro) + 0.01
        if detail.get("displace"):
            DZ = DZ + detail["displace"](DX, DY)
        pob = _grid_object(name + "_Detail", DX - cx, DY - cy, DZ, np.ones((dn - 1, dn - 1), bool), (cx, cy, size_m), collection)
        pob["noctis_detail"] = True
        ob["noctis_detail_object"] = pob.name
    for o in [ob] + ([bpy.data.objects[ob["noctis_detail_object"]]] if detail else []):
        if material is not None:
            o.data.materials.append(material)
    return ob


def _grid_object(name, X, Y, Z, keep, crop, collection):
    import bpy
    rows, cols = X.shape
    V = np.stack([X.ravel(), Y.ravel(), Z.ravel()], 1).astype(np.float32)
    ii, jj = np.meshgrid(np.arange(rows - 1), np.arange(cols - 1), indexing="ij")
    a = ii * cols + jj
    F = np.stack([a, a + 1, a + cols + 1, a + cols], -1)[keep]
    me = bpy.data.meshes.new(name)
    me.vertices.add(len(V))
    me.vertices.foreach_set("co", V.ravel())
    me.loops.add(F.size)
    me.loops.foreach_set("vertex_index", F.ravel().astype(np.int32))
    me.polygons.add(len(F))
    me.polygons.foreach_set("loop_start", (np.arange(len(F)) * 4).astype(np.int32))
    me.polygons.foreach_set("loop_total", np.full(len(F), 4, np.int32))
    me.update(calc_edges=True)
    me.polygons.foreach_set("use_smooth", np.ones(len(F), bool))
    # UV over the crop square (mask images)
    cx, cy, size = crop
    uv = me.uv_layers.new(name="MaskUV")
    lv = F.ravel()
    U = (V[lv, 0] / size + 0.5)
    Vv = (V[lv, 1] / size + 0.5)
    uv.data.foreach_set("uv", np.stack([U, Vv], 1).astype(np.float32).ravel())
    ob = bpy.data.objects.new(name, me)
    (collection or bpy.context.scene.collection).objects.link(ob)
    ob["noctis_origin_x"] = float(cx)
    ob["noctis_origin_y"] = float(cy)
    ob["noctis_size_m"] = float(size)
    return ob


def crop_masks(sim, center, size_m, px=1024, out_dir=None, warp_m=6.0):
    """Mask images over the crop (row 0 = north = UV v 1): habitat weights (softened, slightly warped borders),
    wetness, corridors, water. Packed RGBA PNGs; returns dict name -> (path, channel)."""
    out_dir = out_dir or os.path.join(common.BUILD, "terrain_cache", "crop_%d_%d_%d" % (center[0], center[1], size_m))
    os.makedirs(out_dir, exist_ok=True)
    cx, cy = center
    xs = cx - size_m / 2 + (np.arange(px) + 0.5) * size_m / px
    ys = cy + size_m / 2 - (np.arange(px) + 0.5) * size_m / px  # row 0 = north
    X, Y = np.meshgrid(xs, ys)
    # domain warp so the 8 m habitat cells do not show as blocks
    wx = (tg.fbm((px, px), 11, octaves=((6, 1.0), (24, 0.5))) - 0.5) * 2 * warp_m
    wy = (tg.fbm((px, px), 12, octaves=((6, 1.0), (24, 0.5))) - 0.5) * 2 * warp_m
    Xw, Yw = X + wx, Y + wy
    layers = {}
    for h in HABITATS:
        if sim.raster("weight_" + h) is not None:
            layers[h] = ndimage.gaussian_filter(sim.sample("weight_" + h, Xw, Yw), 1.2)
        else:
            layers[h] = np.zeros_like(X, dtype=np.float32)
    layers["wetness"] = sim.sample("wetness", Xw, Yw)
    mc = os.path.join(common.ROOT, "Output", "level", "mask_corridor_use.png")
    if os.path.exists(mc):
        a = _gray(mc).astype(np.float32) / 255.0
        n = a.shape[0]
        cs = sim.size / n
        layers["trails"] = ndimage.map_coordinates(a, [(sim.size - Y) / cs - 0.5, X / cs - 0.5], order=1, mode="nearest")
    else:
        layers["trails"] = np.zeros_like(X, dtype=np.float32)
    layers["water"] = ndimage.gaussian_filter(sim.sample("water_mask", X, Y), 0.8)
    packs = {"A": ["channel", "riverbank", "sandbar", "backswamp"],
             "B": ["riparian_forest", "floodplain_open", "levee_woodland", "upland_conifer_forest"],
             "C": ["cutbank_cliff", "wetness", "trails", "water"]}
    out = {}
    for k, names in packs.items():
        arr = np.stack([layers[nm] for nm in names], -1)
        p = os.path.join(out_dir, "mask_%s.png" % k)
        tg.save_png(p, arr)
        for i, nm in enumerate(names):
            out[nm] = (p, "RGBA"[i])
    return out, layers


def build_water(center, size_m, res_m=3.0, name="NoctisWater", sim=None, collection=None, material=None, margin=0.6):
    """Water surface over the crop: z = smoothed simulated river level; faces kept where the terrain lies
    below the water level + margin, so the shoreline comes from the terrain intersection."""
    sim = sim or SimData()
    cx, cy = center
    n = int(round(size_m / res_m)) + 1
    xs = cx - size_m / 2 + np.arange(n) * res_m
    ys = cy - size_m / 2 + np.arange(n) * res_m
    X, Y = np.meshgrid(xs, ys)
    W = sim.sample("water_surface", X, Y)
    Zt = height_at(sim, X, Y, 0.0)
    under = (Zt < W + margin)
    keep = under[:-1, :-1] | under[1:, :-1] | under[:-1, 1:] | under[1:, 1:]
    # only near actual water bodies (avoid flooding low dry hollows far from the river)
    wm = sim.sample("water_mask", X, Y) > 0.05
    wm = ndimage.binary_dilation(wm, iterations=4)
    keep &= wm[:-1, :-1]
    ob = _grid_object(name, X - cx, Y - cy, W, keep, (cx, cy, size_m), collection)
    if material is not None:
        ob.data.materials.append(material)
    return ob
