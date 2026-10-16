"""Spawn-probability textures for the Unreal PCG graph PCG_Noctis_Foliage (part of the 'landscape' stage).

Why bake: the first PCG graph intersected the simulation masks with the sampled points and kept them with a hard
Density Filter [0.5, 1]. The simulated masks never reach 0.5 for most plant types (fern max 0.498, palm 0.18,
aquatic 0.03...), so whole groups never spawned, while trees spawned at full density wherever the mask passed.
Here every factor of the keep probability used by the Blender renders is evaluated per texel (mask ** gamma,
cluster noise, slope limit, water buffer, channel / oxbow exclusion, POI clearings, habitat bias of the asset
mix, shore / open-water bands of the aquatic plants), so the graph only has to draw a uniform random number per
point and keep it when it is below the texel value (Density Noise + Density Filter, see import_environment.py).

Outputs (Build/Environment/pcg/):
  T_PcgSpawn_<Asset>.png      one texture per plant asset of PCG_Noctis_Foliage, RGBA with R = G = B = A = keep
                              probability (8 bit), so the sampled value does not depend on the colour channel the
                              PCG texture data reads (its default channel has been Alpha in UE 5.x).
                              1024 px (8 m texels) for trees / palms / shrubs / deadwood, 2048 px (4 m) for small plants.
  T_PcgSpawn_Deadwood<Asset>  forest deadwood (PCG_Noctis_Foliage); T_PcgSpawn_Driftwood<Asset>: bars, banks and
                              cutbanks, for build_level.py's PCG_Noctis_RiverDebris graph (not used by the foliage graph)
  T_PcgSpawn_TrackwayBank     mesh decals of trackways at the water's edge (riverbanks, bars)
  T_PcgWaterDepth             water depth at export time / 4 m (all channels): height offset of floating plants
  pcg_spawn.json              chain plan + coverage statistics read by import_environment.py (also by --dry-run)
Images: row 0 = north edge, mapped on the landscape bounds like the other masks (U -> +X, V -> +Y in Unreal).
"""
from __future__ import annotations

import hashlib
import math
import os

import numpy as np
from scipy import ndimage

from . import catalog, common, fields
from . import texgen as tg

PCG_DIR = os.path.join(common.BUILD, "pcg")
UE_TEX_ROOT = "/Game/Environment/PCG/Textures"
CLUSTER_SEED = 7                    # world property (simulation seed), not the scatter seed of a render
SMALL = {"fern", "horsetail", "herb", "aquatic"}
CLEARING_TYPES = {"basecamp": 1.0, "flood_refuge": 1.0, "blind": 1.0}   # build_level.py tags these noctis.clearing
SMALL_CLEARING_FRACTION = 0.5       # ferns / herbs keep growing in the outer half of a clearing
TRACKWAY_PER_HA = 80.0
SAMPLER_LOOSENESS = 1.0


# ----------------------------------------------------------------------------------------------- shared formulas
def pft_probability(rule, mask, clu, calm=None):
    """Keep probability of a candidate point of a plant functional type (before slope / water / clearings)."""
    amp = rule.get("cluster_amp", 0.5)
    p = np.clip(mask, 0.0, 1.0) ** rule.get("density_gamma", 1.0) * ((1 - amp) + amp * clu)
    if calm is not None and rule.get("calm_water_per_ha"):
        p = np.maximum(p, rule["calm_water_per_ha"] / rule["per_ha"] * calm * clu)
    return np.clip(p, 0.0, 1.0)


def calm_water(rule, hab):
    """Weighted sum of the calm-water habitats of an aquatic rule (hab: {habitat: weight array})."""
    out = None
    for h, k in rule.get("calm_water_habitats", {}).items():
        if h in hab:
            out = hab[h] * k if out is None else out + hab[h] * k
    return out


def asset_shares(rule, ids, hab):
    """Share of each asset of a rule, with its habitat bias applied softly (weights of the habitats, not the
    dominant habitat only): factor = 1 + sum_h w_h (bias_h - 1). Returns {aid: array}, summing to 1."""
    base = None
    W = {}
    for a in ids:
        f = 1.0
        for h, bias in (rule.get("habitat_bias") or {}).items():
            if a in bias and h in hab:
                f = f + hab[h] * (bias[a] - 1.0)
        W[a] = rule["assets"][a] * f
        base = W[a] if base is None else base + W[a]
    return {a: W[a] / np.maximum(base, 1e-9) for a in ids}


def water_ok(rule, aid, wd, cell=0.0):
    """Where the asset may stand given the signed distance to the shore wd (m, negative = in the water).
    cell > 0 (baked texels): a band narrower than a texel would never contain a texel centre; it is widened by half a
    texel on each side and its value scaled by width / (width + cell), which keeps the expected density."""
    band = (rule.get("water_band") or {}).get(aid)
    if band:
        lo, hi = band
        if cell > 0:
            k = (hi - lo) / (hi - lo + cell)
            return ((wd >= lo - cell / 2) & (wd <= hi + cell / 2)).astype(np.float32) * k
        return ((wd >= lo) & (wd <= hi)).astype(np.float32)
    return (wd > rule.get("water_buffer_m", 2.0)).astype(np.float32)


def slope_ok(rule, slope):
    return (slope < rule.get("slope_max_deg", 35)).astype(np.float32)


def open_water_exclusion(hab):
    """Channel and oxbow lake habitats are excluded for every land plant (soft edge on the 8 m habitat cells)."""
    w = hab.get("channel", 0.0) + hab.get("oxbow_lake", 0.0)
    return 1.0 - tg.smoothstep(0.2, 0.45, np.asarray(w, np.float64))


def clearings():
    """POI clearings from the level JSON (types tagged noctis.clearing by build_level.py): [(x, y, r)]."""
    p = os.path.join(common.ROOT, "Data", "Levels", "hell_creek_valley.json")
    out = []
    if not os.path.exists(p):
        return out
    d = common.read_json(p, {})
    for poi in d.get("pois", []):
        if poi.get("type") in CLEARING_TYPES:
            x, y = poi["position_m"][:2]
            out.append((float(x), float(y), float(poi.get("radius_m", 10.0)) * CLEARING_TYPES[poi["type"]]))
    return out


def clearing_factor(x, y, cat, clears):
    f = np.ones(np.shape(x), np.float32)
    for cx, cy, r in clears:
        rr = r * (SMALL_CLEARING_FRACTION if cat in SMALL else 1.0)
        d = np.hypot(np.asarray(x) - cx, np.asarray(y) - cy)
        f *= tg.smoothstep(rr * 0.85, rr, d).astype(np.float32)
    return f


def sampler_extent_m(rule_per_ha, spacing_m):
    """Point half-extent for the PCG Surface Sampler. UE splits the area in cells of 2 E (1 + looseness) and keeps
    at most one point per cell (VERIFY, PCGSurfaceSampler.cpp): E must stay small enough for per_ha to be
    reachable; the pruning extent (min spacing / 2) is applied afterwards with a Bounds Modifier."""
    target_spacing = math.sqrt(1e4 / max(rule_per_ha, 1e-6))
    return round(min(spacing_m * 0.5, target_spacing / (2 * (1 + SAMPLER_LOOSENESS) * 1.2)), 3)


def sampler_cap_per_ha(extent_m):
    cell = 2 * extent_m * (1 + SAMPLER_LOOSENESS)
    return 1e4 / (cell * cell)


# ----------------------------------------------------------------------------------------------- bake fields
class BakeGrid:
    """Simulation fields evaluated at the texel centres of an n x n texture over the landscape (row 0 = north)."""

    def __init__(self, sim, n):
        self.sim = sim
        self.n = n
        size = sim.size
        self.cell = size / n
        c = (np.arange(n) + 0.5) * self.cell
        self.X, self.Y = np.meshgrid(c, size - c)
        Z = sim.sample("height", self.X, self.Y, order=1)
        Wl = sim.sample("water_surface", self.X, self.Y)
        it = max(1, int(round(8.0 / self.cell)))
        wm = ndimage.binary_dilation(sim.sample("water_mask", self.X, self.Y) > 0.05, iterations=it)
        water = (Z < Wl) & wm
        din = ndimage.distance_transform_edt(water) * self.cell
        dout = ndimage.distance_transform_edt(~water) * self.cell
        # texel centre to shore: subtract half a texel so that a texel straddling the shore reads ~0
        self.wd = np.where(water, -np.maximum(din - 0.5 * self.cell, 0), np.maximum(dout - 0.5 * self.cell, 0))
        self.depth = np.clip(np.where(water, Wl - Z, 0.0), 0, None)
        gy, gx = np.gradient(Z, self.cell)
        self.slope = np.degrees(np.arctan(np.hypot(gx, gy)))
        self.hab = sim.habitat_weights(self.X, self.Y)
        self._masks = {}

    def mask(self, name):
        if name not in self._masks:
            self._masks[name] = self.sim.sample(name, self.X, self.Y)
        return self._masks[name]


def _save(name, p, normalise=True):
    """Writes the texture. normalise: the texture stores p / max(p) (full 8-bit range, no low probability lost to
    quantisation) and the chain samples per_ha x max(p) candidates instead of per_ha: same expected density, fewer
    candidate points. Returns (path, sha1, p8, scale) with p ~ p8 / 255 * scale."""
    p = np.clip(p, 0, 1)
    scale = float(p.max()) if normalise else 1.0
    scale = min(1.0, max(scale, 1e-6))
    p8 = (p / scale * 255.0 + 0.5).astype(np.uint8)
    rgba = np.repeat(p8[..., None], 4, -1)
    path = os.path.join(PCG_DIR, "T_PcgSpawn_%s.png" % name)
    from PIL import Image
    Image.fromarray(rgba, "RGBA").save(path, compress_level=6)
    with open(path, "rb") as f:
        sha = hashlib.sha1(f.read()).hexdigest()
    return path, sha, p8, scale


def _stats(p8, per_ha, size_m, scale=1.0):
    p = p8.astype(np.float64) / 255.0 * scale
    area_ha = size_m * size_m / 1e4
    return {"coverage": round(float((p8 > 0).mean()), 5), "mean_p": round(float(p.mean()), 6), "max_p": round(float(p.max()), 4),
            "expected_candidates_kept_per_ha": round(per_ha * float(p.mean()), 3),
            "expected_total_before_pruning": int(per_ha * float(p.mean()) * area_ha), "texture_scale": round(scale, 6)}


# ----------------------------------------------------------------------------------------------- run
def run():
    from .terrain import SimData
    os.makedirs(PCG_DIR, exist_ok=True)
    sim = SimData()
    grids = {}

    def grid(n):
        if n not in grids:
            grids[n] = BakeGrid(sim, n)
        return grids[n]

    clears = clearings()
    man_meta = {aid: common.read_json(os.path.join(common.META_DIR, aid + ".json"), {}) for aid in catalog.ASSETS}
    chains = []
    tree_order = []
    # ---- plants: one chain per asset, in the order of the Blender helper (big types first)
    order = ["conifer", "angio_tree", "ginkgo", "palm", "cycadophyte", "angio_shrub", "fern", "horsetail", "angio_herb", "aquatic_macrophyte"]
    for pft in order:
        rule = catalog.PFT_RULES[pft]
        ids = [a for a in rule["assets"] if a in catalog.ASSETS]
        cat = catalog.ASSETS[ids[0]]["category"]
        g = grid(2048 if cat in SMALL else 1024)
        clu = fields.cluster_noise(g.X, g.Y, rule.get("cluster_m", 50), CLUSTER_SEED + len(pft))
        calm = calm_water(rule, g.hab)
        p_pft = pft_probability(rule, g.mask(rule["mask"]), clu, calm)
        shares = asset_shares(rule, ids, g.hab)
        common_f = slope_ok(rule, g.slope) * clearing_factor(g.X, g.Y, cat, clears)
        if cat != "aquatic":
            common_f = common_f * open_water_exclusion(g.hab)
        same_pft = []
        for aid in ids:
            p = p_pft * shares[aid] * water_ok(rule, aid, g.wd, g.cell) * common_f
            name = "".join(w.capitalize() for w in aid.split("_"))
            path, sha, p8, sc = _save(name, p)
            meta = man_meta.get(aid, {})
            h = (meta.get("bounds_m") or {}).get("height")
            blockers = list(tree_order) if cat in ("tree", "palm") else list(same_pft)
            cand = rule["per_ha"] * sc
            ch = dict(asset=aid, pft=pft, category=cat, graph="PCG_Noctis_Foliage", texture=os.path.relpath(path, common.ROOT),
                      texture_ue="%s/T_PcgSpawn_%s" % (UE_TEX_ROOT, name), resolution=g.n, sha1=sha,
                      per_ha=rule["per_ha"], texture_scale=round(sc, 6), candidate_per_ha=round(cand, 4), points_per_m2=cand / 1e4,
                      sampler_extent_m=sampler_extent_m(cand, rule["min_spacing_m"]),
                      prune_extent_m=rule["min_spacing_m"] * 0.5, blockers=blockers,
                      scale_range=catalog.scale_range(aid, h), on_water=bool(catalog.ASSETS[aid].get("on_water")),
                      tilt_deg=2.0 if cat in ("tree", "palm") else 4.0, stats=_stats(p8, rule["per_ha"], sim.size, sc))
            ch["sampler_cap_per_ha"] = round(sampler_cap_per_ha(ch["sampler_extent_m"]), 1)
            ch["stats"]["coverage"] = round(float((p > 1e-6).mean()), 5)  # before quantisation
            if ch["on_water"]:
                sel = p > 0.01
                ch["median_depth_m"] = round(float(np.median(g.depth[sel])), 2) if sel.any() else 0.0
            chains.append(ch)
            same_pft.append(aid)
            if cat in ("tree", "palm"):
                tree_order.append(aid)
            common.log("pcg %-24s coverage %.3f  mean p %.4f  -> %.2f /ha" % (aid, ch["stats"]["coverage"], ch["stats"]["mean_p"],
                                                                              ch["stats"]["expected_candidates_kept_per_ha"]))
    # ---- deadwood: forest habitats -> foliage graph; river habitats -> RiverDebris graph (documented)
    g = grid(1024)
    for aid, dr in catalog.DEADWOOD_RULES.items():
        for graph, keep in (("PCG_Noctis_Foliage", lambda h: h not in catalog.RIVER_DEBRIS_HABITATS),
                            ("PCG_Noctis_RiverDebris", lambda h: h in catalog.RIVER_DEBRIS_HABITATS)):
            habs = {h: v for h, v in dr["habitats"].items() if keep(h)}
            if not habs:
                continue
            dmax = max(habs.values())
            dens = sum(g.hab[h] * v for h, v in habs.items() if h in g.hab) / dmax
            p = np.clip(dens, 0, 1) * (g.slope < 40) * (g.wd > 0.5) * clearing_factor(g.X, g.Y, "deadwood", clears)
            name = ("Deadwood" if graph == "PCG_Noctis_Foliage" else "Driftwood") + "".join(w.capitalize() for w in aid.split("_")[1:])
            path, sha, p8, sc = _save(name, p)
            cand = dmax * sc
            chains.append(dict(asset=aid, pft="deadwood", category="deadwood", graph=graph, habitats=habs,
                               texture=os.path.relpath(path, common.ROOT), texture_ue="%s/T_PcgSpawn_%s" % (UE_TEX_ROOT, name),
                               resolution=g.n, sha1=sha, per_ha=dmax, texture_scale=round(sc, 6), candidate_per_ha=round(cand, 4),
                               points_per_m2=cand / 1e4,
                               sampler_extent_m=sampler_extent_m(cand, dr["min_spacing_m"]), prune_extent_m=dr["min_spacing_m"] * 0.5,
                               sampler_cap_per_ha=round(sampler_cap_per_ha(sampler_extent_m(cand, dr["min_spacing_m"])), 1),
                               blockers=[], scale_range=catalog.scale_range(aid), on_water=False, tilt_deg=0.0,
                               static_no_wpo=True, stats=_stats(p8, dmax, sim.size, sc)))
    # ---- trackway decals at the water's edge (banks, bars), denser on predicted corridors
    g = grid(2048)
    corr = np.zeros_like(g.X)
    mc = os.path.join(common.ROOT, "Output", "level", "mask_corridor_use.png")
    if os.path.exists(mc):
        from PIL import Image
        a = np.asarray(Image.open(mc)).astype(np.float32)
        a = (a[..., 0] if a.ndim == 3 else a) / 255.0
        n = a.shape[0]
        cs = sim.size / n
        corr = ndimage.map_coordinates(a, [(sim.size - g.Y) / cs - 0.5, g.X / cs - 0.5], order=1, mode="nearest")
    bank = np.clip(g.hab.get("riverbank", 0) + 0.5 * g.hab.get("sandbar", 0), 0, 1)
    p = bank * ((g.wd > 0.3) & (g.wd < 8.0)) * (0.3 + 0.7 * np.clip(corr * 3.0, 0, 1)) * (g.slope < 25)
    path, sha, p8, sc = _save("TrackwayBank", p)
    from . import ground
    cand = TRACKWAY_PER_HA * sc
    chains.append(dict(asset="trackway_decals", pft="decal", category="decal", graph="PCG_Noctis_Foliage",
                       meshes=[m for m, _ in ground.DECALS], texture=os.path.relpath(path, common.ROOT),
                       texture_ue="%s/T_PcgSpawn_TrackwayBank" % UE_TEX_ROOT, resolution=g.n, sha1=sha,
                       per_ha=TRACKWAY_PER_HA, texture_scale=round(sc, 6), candidate_per_ha=round(cand, 4), points_per_m2=cand / 1e4,
                       sampler_extent_m=sampler_extent_m(cand, 4.0), prune_extent_m=2.0,
                       sampler_cap_per_ha=round(sampler_cap_per_ha(sampler_extent_m(cand, 4.0)), 1),
                       blockers=[], scale_range=[0.85, 1.15], on_water=False, tilt_deg=0.0, stats=_stats(p8, TRACKWAY_PER_HA, sim.size, sc)))
    # ---- water depth (floating plants sit on the water surface, not on the river bed)
    gd = grid(2048)
    dep = np.clip(gd.depth / 4.0, 0, 1)
    dpath, dsha, _, _ = _save("WaterDepth", dep, normalise=False)
    os.replace(dpath, os.path.join(PCG_DIR, "T_PcgWaterDepth.png"))
    plan = {
        "schema": "noctis.pcg_spawn/1",
        "note_fr": ("Probabilité de maintien cuite par texel (masque^gamma x agrégation x pente x eau x clairières x "
                    "biais d'habitat), normalisée par son maximum (texture = p / max p) ; la chaîne échantillonne "
                    "per_ha x max p candidats/ha (candidate_per_ha) puis tire U ~ [0, 1) par point et garde U < texture "
                    "(Density Noise Add sur densité inversée puis Density Filter [0, 0.99999])."),
        "size_m": sim.size, "looseness": SAMPLER_LOOSENESS, "cluster_seed": CLUSTER_SEED, "clearings": clears,
        "water_depth": {"texture": os.path.relpath(os.path.join(PCG_DIR, "T_PcgWaterDepth.png"), common.ROOT),
                        "texture_ue": "%s/T_PcgWaterDepth" % UE_TEX_ROOT, "scale_m": 4.0},
        "chains": chains,
    }
    common.write_json(os.path.join(PCG_DIR, "pcg_spawn.json"), plan)
    common.log("pcg: %d chains -> %s" % (len(chains), PCG_DIR))
    return plan
