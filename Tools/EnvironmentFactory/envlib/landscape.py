"""Stage 'landscape': Unreal landscape paint layers derived from the simulation masks.

Outputs (Build/Environment/landscape/, heightmap resolution 2017 x 2017, row 0 = north edge like heightmap.png):
  layer_<Layer>.png        8-bit weightmaps, softened borders, the layers sum to exactly 255 per pixel
                           (Landscape Mode > Paint > Target Layers > Import, weight-blended layers)
  landscape_masks.png      RGBA, sampled by M_NoctisLandscape in world space (not paint layers):
                           R = static wetness index (export-ground), G = predicted corridors (export-level
                           mask_corridor_use), B = flood risk (export-level mask_flood_risk), A = water mask
"""
from __future__ import annotations

import os

import numpy as np
from PIL import Image
from scipy import ndimage

from . import common
from . import texgen as tg

# UE layer name -> (ground material, {habitat: weight})
LAYERS = {
    "Gravel": ("RiverGravel", {"channel": 1.0}),
    "MudTracks": ("RiverbankMudTracks", {"riverbank": 1.0}),
    "Sand": ("PointBarSand", {"sandbar": 1.0}),
    "Backswamp": ("BackswampOrganicMud", {"backswamp": 1.0, "oxbow_lake": 1.0}),
    "LitterBroadleaf": ("LeafLitterBroadleaf", {"riparian_forest": 1.0, "levee_woodland": 0.6}),
    "Silt": ("FloodplainSilt", {"floodplain_open": 1.0, "levee_woodland": 0.4}),
    "LitterConifer": ("LeafLitterConifer", {"upland_conifer_forest": 1.0}),
    "Strata": ("CutbankStrata", {"cutbank_cliff": 1.0}),
}


def _load(name, sim_dir):
    p = os.path.join(sim_dir, name + ".png")
    if not os.path.exists(p):
        return None
    a = np.asarray(Image.open(p))
    return (a[..., 0] if a.ndim == 3 else a).astype(np.float32) / 255.0


def _resample(a, n, order=1):
    """Cell-centred raster (row 0 north) -> vertex grid of n samples covering the same square."""
    m = a.shape[0]
    t = np.arange(n) / (n - 1) * m - 0.5
    R, C = np.meshgrid(t, t, indexing="ij")
    return ndimage.map_coordinates(a, [R, C], order=order, mode="nearest")


def run(sim_dir=None, out_dir=None, soften_px=1.6, warp_px=2.5):
    sim_dir = sim_dir or common.SIM_DIR
    out_dir = out_dir or common.LANDSCAPE_DIR
    os.makedirs(out_dir, exist_ok=True)
    t = common.read_json(os.path.join(sim_dir, "terrain.json"))
    n = int(t["samples"])
    warp_r = (tg.fbm((n, n), 21, octaves=((24, 1.0), (96, 0.5))) - 0.5) * 2 * warp_px
    warp_c = (tg.fbm((n, n), 22, octaves=((24, 1.0), (96, 0.5))) - 0.5) * 2 * warp_px
    R0, C0 = np.meshgrid(np.arange(n, dtype=np.float32), np.arange(n, dtype=np.float32), indexing="ij")
    W = {}
    for layer, (_, habs) in LAYERS.items():
        acc = np.zeros((n, n), np.float32)
        for h, k in habs.items():
            a = _load("weight_" + h, sim_dir)
            if a is not None:
                acc += k * _resample(a, n)
        acc = ndimage.map_coordinates(acc, [R0 + warp_r, C0 + warp_c], order=1, mode="nearest")
        W[layer] = ndimage.gaussian_filter(acc, soften_px)
    stack = np.stack([W[k] for k in LAYERS], -1)
    s = stack.sum(-1, keepdims=True)
    stack = np.where(s > 1e-4, stack / np.maximum(s, 1e-4), 0)
    stack[..., list(LAYERS).index("Silt")] += (s[..., 0] <= 1e-4)  # uncovered pixels -> silt
    # 8-bit quantisation with an exact sum of 255 (largest remainder)
    f = stack * 255.0
    q = np.floor(f).astype(np.int32)
    rem = 255 - q.sum(-1)
    frac = f - q
    order = np.argsort(-frac, axis=-1)
    for k in range(stack.shape[-1]):
        add = (rem > k).astype(np.int32)
        idx = order[..., k]
        np.put_along_axis(q, idx[..., None], np.take_along_axis(q, idx[..., None], -1) + add[..., None], -1)
    files = {}
    for i, layer in enumerate(LAYERS):
        p = os.path.join(out_dir, "layer_%s.png" % layer)
        Image.fromarray(q[..., i].astype(np.uint8), "L").save(p)
        files[layer] = os.path.relpath(p, common.ROOT)
    # auxiliary masks
    wet = _load("wetness", sim_dir)
    water = _load("water_mask", sim_dir)
    lvl = os.path.join(common.ROOT, "Output", "level")
    trails = _load("mask_corridor_use", lvl)
    flood = _load("mask_flood_risk", lvl)
    chans = []
    for a, sig in ((wet, 1.5), (trails, 2.0), (flood, 2.0), (water, 0.8)):
        chans.append(ndimage.gaussian_filter(_resample(a, n), sig) if a is not None else np.zeros((n, n), np.float32))
    p = os.path.join(out_dir, "landscape_masks.png")
    tg.save_png(p, np.stack(chans, -1))
    files["_masks"] = os.path.relpath(p, common.ROOT)
    common.write_json(os.path.join(out_dir, "landscape_layers.json"), layer_info(files))
    common.log("landscape: %d layers %dx%d -> %s" % (len(LAYERS), n, n, out_dir))
    return files


def layer_info(files=None):
    if files is None:
        files = {k: os.path.relpath(os.path.join(common.LANDSCAPE_DIR, "layer_%s.png" % k), common.ROOT) for k in LAYERS}
        files["_masks"] = os.path.relpath(os.path.join(common.LANDSCAPE_DIR, "landscape_masks.png"), common.ROOT)
    return {
        "layers": [{"name": k, "ground_material": v[0], "habitats": v[1], "weightmap": files.get(k)} for k, v in LAYERS.items()],
        "masks": {"file": files.get("_masks"), "channels": {"R": "wetness (statique)", "G": "corridors prédits", "B": "risque de crue", "A": "eau"},
                  "uv": "U = X / taille (cm), V = -Y / taille (Y Unreal négatif vers le nord) — voir Docs/ENVIRONMENT.md"},
        "resolution": "2017 x 2017 (= heightmap), ligne 0 = bord nord",
        "blend": "couches pondérées (somme = 255), mélange par hauteur dans M_NoctisLandscape",
    }
