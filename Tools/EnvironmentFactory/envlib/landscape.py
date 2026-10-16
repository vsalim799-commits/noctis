"""Stage 'landscape': Unreal landscape paint layers derived from the simulation masks (+ PCG spawn textures).

Outputs (Build/Environment/landscape/, heightmap resolution 2017 x 2017, row 0 = north edge like heightmap.png):
  layer_<Layer>.png        8-bit weightmaps, softened borders, the layers sum to exactly 255 per pixel
                           (Landscape Mode > Paint > Target Layers > Import, weight-blended layers)
  landscape_masks.png      RGBA, sampled by M_NoctisLandscape in world space (not paint layers):
                           R = static wetness index (export-ground), G = predicted corridors (export-level
                           mask_corridor_use), B = flood risk (export-level mask_flood_risk), A = water mask
  landscape_masks2.png     RGBA: R = ground-cover density (fern + horsetail + herb + shrub simulated masks: colours
                           the far ground where those small plants are culled), G = shore wetness band (0-12 m from
                           the water, bars and banks darker / wetter), B = 0 and A = 1 (reserved)
Layer rules (weights from the habitat masks, GAME ASSUMPTIONS):
  * SiltDry (desiccation cracks) only in open floodplain hollows with the lowest initial soil moisture and no tree
    cover; the rest of the floodplain is the damp, debris-covered Silt layer.
  * Strata (cutbank sections) only on slopes steeper than ~20-28 deg; the flat top / foot of a cutbank cell goes
    back to Silt / Mud (the biplanar strata projection is meaningless on flat ground).
The PCG spawn-probability textures are baked right after (envlib/pcgbake.py).
"""
from __future__ import annotations

import os

import numpy as np
from PIL import Image
from scipy import ndimage

from . import common
from . import texgen as tg

# UE layer name -> (ground material, {habitat: weight}); "_dry" = floodplain_open x dry hollows (see dry_mask)
LAYERS = {
    "Gravel": ("RiverGravel", {"channel": 1.0}),
    "Mud": ("RiverbankMud", {"riverbank": 1.0}),
    "Sand": ("PointBarSand", {"sandbar": 1.0}),
    "Backswamp": ("BackswampOrganicMud", {"backswamp": 1.0, "oxbow_lake": 1.0}),
    "LitterBroadleaf": ("LeafLitterBroadleaf", {"riparian_forest": 1.0, "levee_woodland": 0.6}),
    "Silt": ("FloodplainSilt", {"floodplain_open": 1.0, "levee_woodland": 0.4}),
    "SiltDry": ("FloodplainSiltDry", {"_dry": 1.0}),
    "LitterConifer": ("LeafLitterConifer", {"upland_conifer_forest": 1.0}),
    "Strata": ("CutbankStrata", {"cutbank_cliff": 1.0}),
}
TRAIL_LAYER = "Mud"          # trails (corridors) blend towards this layer in the material
STRATA_SLOPE_DEG = (20.0, 28.0)
DRY_MOISTURE = (0.12, 0.22)  # moisture_initial: fully dry below 0.12, not dry above 0.22


def dry_mask(moisture, trees):
    """Weight of desiccation-cracked silt inside the open floodplain: driest initial soil moisture, no canopy."""
    m = 1.0 - tg.smoothstep(DRY_MOISTURE[0], DRY_MOISTURE[1], moisture)
    return m * (1 - tg.smoothstep(0.15, 0.3, trees))


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
    moist = _load("moisture_initial", sim_dir)
    trees = np.clip(_load("foliage_angio_tree", sim_dir) + _load("foliage_conifer", sim_dir), 0, 1)
    dry = dry_mask(_resample(moist, n), _resample(trees, n)) if moist is not None else np.zeros((n, n), np.float32)
    fo = _resample(_load("weight_floodplain_open", sim_dir), n)
    # slope of the landscape (heightmap at this very resolution)
    t_meta = common.read_json(os.path.join(sim_dir, "terrain.json"))
    hm = np.asarray(Image.open(os.path.join(sim_dir, "heightmap.png"))).astype(np.float64)
    hm = t_meta["min_height_m"] + hm / 65535.0 * (t_meta["max_height_m"] - t_meta["min_height_m"])
    sp = t_meta["size_m"] / (n - 1)
    gy, gx = np.gradient(hm, sp)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    steep = tg.smoothstep(STRATA_SLOPE_DEG[0], STRATA_SLOPE_DEG[1], ndimage.gaussian_filter(slope, 1.0))
    W = {}
    for layer, (_, habs) in LAYERS.items():
        acc = np.zeros((n, n), np.float32)
        for h, k in habs.items():
            if h == "_dry":
                acc += k * fo * dry
                continue
            a = _load("weight_" + h, sim_dir)
            if a is not None:
                acc += k * _resample(a, n)
        if layer == "Silt":
            acc = acc - fo * dry
        acc = ndimage.map_coordinates(acc, [R0 + warp_r, C0 + warp_c], order=1, mode="nearest")
        W[layer] = ndimage.gaussian_filter(np.clip(acc, 0, None), soften_px)
    flat_strata = W["Strata"] * (1 - steep)
    W["Strata"] = W["Strata"] * steep
    W["Silt"] = W["Silt"] + flat_strata
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
    # second mask: ground cover, shore wetness band
    gc = np.zeros((n, n), np.float32)
    for pft, k in (("fern", 1.4), ("horsetail", 1.0), ("angio_herb", 1.0), ("angio_shrub", 0.8)):
        a = _load("foliage_" + pft, sim_dir)
        if a is not None:
            gc += k * _resample(a, n)
    gc = ndimage.gaussian_filter(np.clip(gc / 0.9, 0, 1), 1.5)
    shore = np.zeros((n, n), np.float32)
    if water is not None:
        wv = _resample(water, n) > 0.5
        dist = ndimage.distance_transform_edt(~wv) * (t_meta["size_m"] / (n - 1))
        shore = (1 - tg.smoothstep(0.0, 12.0, dist)) * (~wv)
    p2 = os.path.join(out_dir, "landscape_masks2.png")
    tg.save_png(p2, np.stack([gc, shore, np.zeros((n, n)), np.ones((n, n))], -1))
    files["_masks2"] = os.path.relpath(p2, common.ROOT)
    common.write_json(os.path.join(out_dir, "landscape_layers.json"), layer_info(files))
    common.log("landscape: %d layers %dx%d -> %s" % (len(LAYERS), n, n, out_dir))
    return files


def layer_info(files=None):
    if files is None:
        files = {k: os.path.relpath(os.path.join(common.LANDSCAPE_DIR, "layer_%s.png" % k), common.ROOT) for k in LAYERS}
        files["_masks"] = os.path.relpath(os.path.join(common.LANDSCAPE_DIR, "landscape_masks.png"), common.ROOT)
        files["_masks2"] = os.path.relpath(os.path.join(common.LANDSCAPE_DIR, "landscape_masks2.png"), common.ROOT)
    return {
        "layers": [{"name": k, "ground_material": v[0], "habitats": {h: w for h, w in v[1].items() if h != "_dry"},
                    "rule": ("floodplain_open x creux secs (humidité initiale < 0,22, sans canopée)" if "_dry" in v[1] else
                             ("pentes > %d-%d°" % STRATA_SLOPE_DEG if k == "Strata" else "")),
                    "weightmap": files.get(k)} for k, v in LAYERS.items()],
        "trail_layer": TRAIL_LAYER,
        "masks": {"file": files.get("_masks"), "channels": {"R": "wetness (statique)", "G": "corridors prédits", "B": "risque de crue", "A": "eau"},
                  "uv": "U = X / taille, V = (Y + taille) / taille (cm ; V = 0 au bord nord, Y Unreal = -taille) — voir Docs/ENVIRONMENT.md §3.2"},
        "masks2": {"file": files.get("_masks2"), "channels": {"R": "couvert bas (fougères, prêles, herbacées, arbustes)",
                                                              "G": "bande humide de rive (0-12 m de l'eau)", "B": "réservé (0)", "A": "réservé (1)"}},
        "resolution": "2017 x 2017 (= heightmap), ligne 0 = bord nord",
        "blend": "couches pondérées (somme = 255), mélange par hauteur dans M_NoctisLandscape",
    }
