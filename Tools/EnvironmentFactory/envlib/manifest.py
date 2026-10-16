"""Stage 'manifest': Build/Environment/manifest.json, the data contract read by
Content/Python/import_environment.py, Content/Python/setup_landscape_material.py and the Blender vegetation helper
(Tools/CreatureFactory/scenes/vegetation_library.py)."""
from __future__ import annotations

import datetime
import os

from . import catalog, common, ground, landscape
from .assets import GENERATOR_VERSION, LEAF_MAPS, BARK_MAPS, leaf_prefix, bark_prefix, material_name


def run():
    assets = {}
    textures = {}
    for aid, a in catalog.ASSETS.items():
        meta = common.read_json(os.path.join(common.META_DIR, aid + ".json"))
        if meta is None:
            common.log("manifest: %s has no metadata (run the flora stage)" % aid)
            continue
        assets[aid] = {
            "ue_name": meta["ue_name"], "label_fr": meta["label_fr"], "category": meta["category"], "pft": meta["plant_functional_type"],
            "files": meta["files"], "materials": list(meta["materials"]), "bounds_m": meta["bounds_m"],
            "triangles": [l["triangles"] for l in meta["lods"]], "lod_screen_size": meta["lod_screen_size"],
            "cull_distance_cm": meta["cull_distance_cm"], "collision": meta["collision"]["type"], "nanite": meta["nanite"]["recommended"],
            "on_water": meta.get("on_water", False), "metadata": os.path.relpath(os.path.join(common.META_DIR, aid + ".json"), common.ROOT),
            "confidence": meta["botany"].get("confidence"),
            "scale_range": catalog.scale_range(aid, (meta.get("bounds_m") or {}).get("height")),
            "static_no_wpo": meta["category"] == "deadwood",
        }
        for mname, maps in meta["materials"].items():
            kind = "foliage" if mname.startswith("M_Leaf_") else "bark"
            textures[mname] = {"kind": kind, "maps": maps, "wind": not mname.endswith("_Static")}
    data = {
        "schema": "noctis.environment_manifest/1",  # v1 + decals / pcg / scale_range (additive)
        "generator": {"tool": "Tools/EnvironmentFactory", "version": GENERATOR_VERSION, "date": datetime.date.today().isoformat()},
        "units": {"fbx": "centimetres (UnitScaleFactor 1), Z up, pivot at the plant base",
                  "normal_maps": "DirectX (green = -Y), Unreal native"},
        "ue": {"root": "/Game/Environment", "meshes": "/Game/Environment/Flora/<Category>", "textures": "/Game/Environment/Textures",
               "materials": "/Game/Environment/Materials", "foliage_types": "/Game/Environment/FoliageTypes",
               "landscape_material": "/Game/Noctis/Landscape/M_NoctisLandscape"},
        "assets": assets,
        "materials": textures,
        "ground_materials": ground.material_info(),
        "decals": ground.decal_info(),
        "landscape": landscape.layer_info(),
        "scatter": {"pft_rules": catalog.PFT_RULES, "deadwood_rules": catalog.DEADWOOD_RULES,
                    "river_debris_habitats": list(catalog.RIVER_DEBRIS_HABITATS),
                    "note": "Densités = hypothèses de jeu (instances / ha pour un masque de densité = 1)."},
        "pcg": {"plan": os.path.relpath(os.path.join(common.BUILD, "pcg", "pcg_spawn.json"), common.ROOT),
                "note": "Textures de probabilité de maintien par asset (envlib/pcgbake.py) ; voir Docs/ENVIRONMENT.md §4."},
        "simulation": {"dir": os.path.relpath(common.SIM_DIR, common.ROOT), "terrain": common.read_json(os.path.join(common.SIM_DIR, "terrain.json")),
                       "ground": common.read_json(os.path.join(common.SIM_DIR, "ground.json"))},
    }
    common.write_json(common.MANIFEST, data)
    common.log("manifest: %d assets -> %s" % (len(assets), common.MANIFEST))
    return data
