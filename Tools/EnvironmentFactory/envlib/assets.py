"""Stage 'flora': textures -> LOD0..2 meshes -> collision -> FBX (UE5) -> Blender library -> metadata.

Outputs (Build/Environment, gitignored):
  textures/T_Leaf_<Atlas>_{BaseColor,Normal,Opacity,Translucency,Roughness}.png  leaf atlases 2048 px
  textures/T_Bark_<Bark>_{BaseColor,Normal,Roughness,Height}.png                   tileable 1024 px (stems 512)
  fbx/<asset>/SM_<Name>.fbx (+ UCX_ collision), SM_<Name>_LOD1.fbx, SM_<Name>_LOD2.fbx   centimetres, Z up
  meta/<asset>.json                                                                 metadata (see README)
  library/noctis_flora.blend                                                        collections <asset>_LOD0..2
"""
from __future__ import annotations

import datetime
import os
import time

import numpy as np

from . import bark as barkmod
from . import catalog, common, deadwood, foliage, paint, plants, treegen
from .meshkit import Mesh, convex_hull_mesh, prism_points

GENERATOR_VERSION = "2026.10-env2"
LEAF_MAPS = ("BaseColor", "Normal", "Opacity", "Translucency", "Roughness")
BARK_MAPS = ("BaseColor", "Normal", "Roughness", "Height")


def camel(key):
    return "".join(p.capitalize() for p in key.split("_"))


def leaf_prefix(atlas):
    return "Leaf_" + camel(atlas)


def bark_prefix(bark):
    return ("Bark_" if bark not in ("deadwood", "green_stem", "soil_clod", "horsetail_stem") else "") + camel(bark)


def material_name(prefix):
    return "M_" + prefix


# ----------------------------------------------------------------------------------------------- textures
def ensure_atlas(atlas, force=False):
    prefix = leaf_prefix(atlas)
    if not force and common.textures_exist(prefix, LEAF_MAPS):
        return prefix
    t0 = time.time()
    maps, _ = foliage.build_atlas(atlas, seed=sum(map(ord, atlas)) * 7)
    common.save_texture_set(prefix, maps, LEAF_MAPS)
    common.log("atlas %s: %.1f s" % (atlas, time.time() - t0))
    return prefix


def ensure_bark(bark, force=False):
    prefix = bark_prefix(bark)
    if not force and common.textures_exist(prefix, BARK_MAPS):
        return prefix
    fn, _ = barkmod.BARKS[bark]
    common.save_texture_set(prefix, fn(sum(map(ord, bark))), BARK_MAPS)
    return prefix


def atlas_rects(atlas):
    lay = foliage.ATLASES[atlas]
    n = lay["grid"][0] * lay["grid"][1]
    return lay, [paint.cell_uv_rect(i, lay["grid"]) for i in range(n)]


# ----------------------------------------------------------------------------------------------- build
def slot_prefixes(a):
    """Material slot -> texture prefix (None = unused) for one asset."""
    b = a["builder"]
    if b == "tree":
        return {0: ensure_bark(a["bark"]), 1: ensure_atlas(a["atlas"])}
    if b in ("palm", "cycad"):
        return {0: ensure_bark(a["bark"]), 1: ensure_atlas(a["atlas"]), 2: ensure_bark("green_stem")}
    if b == "horsetail":
        return {0: ensure_bark("horsetail_stem"), 1: ensure_atlas("horsetail_whorl")}
    if b in ("fern", "emergent", "floating", "herb"):
        return {1: ensure_atlas(a["atlas"]), 2: ensure_bark("green_stem")}
    # deadwood
    return {0: ensure_bark(a["bark"]), 1: ensure_bark(catalog.DEADWOOD_INNER), 2: ensure_bark("soil_clod")}


def build_lod(aid, a, lod):
    """Returns (Mesh, info, hulls list of (V, F))."""
    b = a["builder"]
    if b == "tree":
        lay, rects = atlas_rects(a["atlas"])
        m, info, branches = treegen.build_tree(a["spec"], a["seed"], lod=lod, bark_tile=barkmod.BARKS[a["bark"]][1],
                                               atlas_layout=lay, uv_rects=rects)
        hulls = []
        if lod == 0 and a.get("collision") in ("trunk", "trunk_small"):
            hulls = treegen.trunk_collision(branches, max_z=4.0 if a["collision"] == "trunk" else 2.0)
        return m, info, hulls
    if b in plants.BUILDERS:
        if b == "horsetail":
            lay, rects = atlas_rects("horsetail_whorl")
        else:
            lay, rects = atlas_rects(a["atlas"])
        tile = barkmod.BARKS[a.get("bark", "green_stem")][1]
        m, info = plants.BUILDERS[b](a["spec"], a["seed"], lod, rects, lay, tile)
        hulls = []
        if lod == 0 and a.get("collision") in ("trunk", "trunk_small"):
            sp = a["spec"]
            r = sp.get("trunk_r", 0.2) * 1.1
            h = max(0.5, min(sp.get("trunk_h", 1.0), 4.0))
            hulls = [convex_hull_mesh(prism_points(np.array([0, 0, -0.2]), np.array([0, 0, 1.0]), r, h + 0.2, 8))]
        return m, info, hulls
    m, info, hulls = deadwood.BUILDERS[b](a["spec"], a["seed"], lod, barkmod.BARKS[a["bark"]][1],
                                          barkmod.BARKS[catalog.DEADWOOD_INNER][1])
    if lod == 0 and a.get("nanite"):
        # Nanite-ready LOD0: real bark / wood relief displaced from the tileable height maps
        from PIL import Image
        hb = np.asarray(Image.open(common.texture_path(bark_prefix(a["bark"]), "Height"))).astype(np.float32) / 65535.0
        hw = np.asarray(Image.open(common.texture_path(bark_prefix(catalog.DEADWOOD_INNER), "Height"))).astype(np.float32) / 65535.0
        m.displace({0: (hb, 0.035), 1: (hw, 0.02)})
    return m, info, hulls


def compact_slots(m, slots):
    """Remaps material indices to the used slots only. Returns ordered prefix list."""
    used = sorted({mat for _, _, mat in m.polys})
    order = [s for s in used if slots.get(s)]
    remap = {s: i for i, s in enumerate(order)}
    m.polys = [(F, UV, remap[mat]) for F, UV, mat in m.polys if mat in remap]
    return [slots[s] for s in order]


def slot_material_name(prefix, a):
    """Material slot name in the FBX. Deadwood gets its own '_Static' slots (same textures): in Unreal they map to
    material instances with the wind (World Position Offset) switched off, so dead logs never evaluate WPO."""
    return material_name(prefix) + ("_Static" if a.get("category") == "deadwood" else "")


def blender_materials(prefixes, a=None):
    from . import materials
    mats = []
    for p in prefixes:
        name = slot_material_name(p, a or {})
        if p.startswith("Leaf_"):
            mats.append(materials.leaf_material(name, p))
        else:
            mats.append(materials.bark_material(name, p))
    return mats


def hull_object(name, V, F, collection):
    import bpy
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in V], [], [tuple(int(i) for i in f) for f in F])
    me.update()
    ob = bpy.data.objects.new(name, me)
    collection.objects.link(ob)
    ob.display_type = "WIRE"
    return ob


def run(ctx_assets=None, force_textures=False, export=True):
    import bpy
    common.ensure_dirs()
    common.reset_blender()
    scene = bpy.context.scene
    root = bpy.data.collections.new("NoctisFlora")
    scene.collection.children.link(root)
    colls = bpy.data.collections.new("Collision")
    scene.collection.children.link(colls)
    # The library .blend is always rebuilt complete (meshes are cheap; textures are cached), so a partial run
    # (--assets) never drops assets from it; FBX / metadata are only rewritten for the selected ids.
    ids = list(catalog.ASSETS)
    selected = set(ctx_assets or ids)
    report = {}
    from . import impostor
    impostor_src = {}
    for aid in ids:
        a = catalog.ASSETS[aid]
        t0 = time.time()
        slots = slot_prefixes(a)
        name = a["ue_name"]
        lod_objs, lod_info = [], []
        coll_objs = []
        for lod in range(3):
            m, info, hulls = build_lod(aid, a, lod)
            prefixes = compact_slots(m, slots)
            mats = blender_materials(prefixes, a)
            c = bpy.data.collections.new("%s_LOD%d" % (aid, lod))
            root.children.link(c)
            obname = name if lod == 0 else "%s_LOD%d" % (name, lod)
            ob = m.to_blender(obname, mats, collection=c)
            ob["noctis_asset"] = aid
            ob["noctis_lod"] = lod
            lo, hi = m.bounds()
            lod_objs.append(ob)
            lod_info.append(dict(lod=lod, triangles=m.tri_count(), vertices=int(sum(len(v) for v in m.V)),
                                 materials=[slot_material_name(p, a) for p in prefixes], **{k: v for k, v in info.items()}))
            if lod == 0:
                bounds = (lo, hi)
                for k, (V, F) in enumerate(hulls):
                    coll_objs.append(hull_object("UCX_%s_%02d" % (name, k), V, F, colls))
        files = {}
        fbx_ok = {}
        if aid not in selected:
            continue
        if export and impostor.eligible(aid):
            impostor_src[aid] = lod_objs[1]
        if export:
            d = os.path.join(common.FBX_DIR, aid)
            for lod, ob in enumerate(lod_objs):
                fn = os.path.join(d, ob.name + ".fbx")
                common.export_fbx([ob] + (coll_objs if lod == 0 else []), fn)
                files["LOD%d" % lod] = os.path.relpath(fn, common.ROOT)
            fbx_ok = common.fbx_check(os.path.join(d, name + ".fbx"))
        meta = build_metadata(aid, a, slots, lod_info, bounds, coll_objs, files, fbx_ok)
        common.write_json(os.path.join(common.META_DIR, aid + ".json"), meta)
        report[aid] = dict(triangles=[li["triangles"] for li in lod_info], seconds=round(time.time() - t0, 1))
        common.log("%-24s tris LOD0/1/2 = %s  (%.1f s)" % (aid, report[aid]["triangles"], time.time() - t0))
    if export:
        report["_decals"] = export_decal_meshes()
        if impostor_src:
            imp = impostor.run(impostor_src)
            for aid, info in imp.items():
                mp = os.path.join(common.META_DIR, aid + ".json")
                meta = common.read_json(mp)
                meta["files"]["LOD3"] = info["file"]
                meta["lods"].append(dict(lod=3, triangles=info["triangles"], materials=[info["material"]], kind="crossed_billboards",
                                         size_m=info["size_m"]))
                meta["lod_screen_size"] = list(meta["lod_screen_size"][:3]) + [impostor.LOD3_SCREEN_SIZE]
                meta["materials"][info["material"]] = info["textures"]
                meta["impostor"] = {"lod3": "3 cartes croisées à 60° (texture rendue depuis le LOD1 : albédo, opacité, normale)",
                                    "screen_size": impostor.LOD3_SCREEN_SIZE,
                                    "note": "Au-delà : HLOD World Partition (couche HLOD_Foliage_Instanced du niveau)."}
                common.write_json(mp, meta)
            report["_impostors"] = len(imp)
    # Library: relative image paths, collision collection excluded from rendering.
    colls.hide_render = True
    colls.hide_viewport = True
    os.makedirs(common.LIB_DIR, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=common.LIBRARY_BLEND, relative_remap=True, compress=True)
    try:  # texture paths relative to the library (Build/Environment is relocatable)
        bpy.ops.file.make_paths_relative()
        bpy.ops.wm.save_mainfile(compress=True)
    except Exception as exc:
        common.log("relative paths: %s" % exc)
    common.log("library: %s" % common.LIBRARY_BLEND)
    return report


def build_metadata(aid, a, slots, lod_info, bounds, coll_objs, files, fbx_ok):
    lo, hi = bounds
    pft = a["pft"]
    rule = catalog.PFT_RULES.get(pft) or {}
    tex = {}
    for s, p in slots.items():
        if not p:
            continue
        kinds = LEAF_MAPS if p.startswith("Leaf_") else BARK_MAPS
        tex[slot_material_name(p, a)] = {k: os.path.relpath(common.texture_path(p, k), common.ROOT) for k in kinds}
    return {
        "schema": "noctis.environment_asset/1",
        "id": aid,
        "ue_name": a["ue_name"],
        "label_fr": a["label_fr"],
        "category": a["category"],
        "plant_functional_type": pft,
        "generator": {"tool": "Tools/EnvironmentFactory", "version": GENERATOR_VERSION, "builder": a["builder"], "seed": a["seed"],
                      "date": datetime.date.today().isoformat()},
        "units": {"blender": "m, Z up, pivot at the base (0,0,0) on the ground", "fbx": "cm (UnitScaleFactor 1), Z up after UE import"},
        "bounds_m": {"min": [round(float(x), 3) for x in lo], "max": [round(float(x), 3) for x in hi],
                     "height": round(float(hi[2]), 2), "width": round(float(max(hi[0] - lo[0], hi[1] - lo[1])), 2)},
        "lods": lod_info,
        "lod_screen_size": list(a.get("lod_screen_size", (1.0, 0.3, 0.1))),
        "nanite": {"recommended": bool(a.get("nanite", False)),
                   "note": ("Bois mort opaque : LOD0 utilisable en Nanite." if a.get("nanite") else
                            "Feuillage en cartes alpha : en UE 5.4 garder les LOD classiques (Nanite + masqué coûte cher en surdessin). "
                            "Au-delà du LOD2 : HLOD ou imposteur (carte d'imposteur octaédrique cuite par un outil tiers ou HLOD Mesh Approximation).")},
        "impostor": {"recommended_from_screen_size": 0.06, "note": "Pas d'imposteur généré ici : voir README (HLOD / imposteurs)."},
        "collision": {"type": a.get("collision", "none"), "ucx_hulls": len(coll_objs),
                      "note": "UCX_ convexes dans le FBX du LOD0 (convention Unreal)." if coll_objs else "Pas de collision (végétation traversable)."},
        "materials": tex,
        "wind_vertex_color": {"R": "souplesse 0 (tronc) .. 1 (bout de feuille)", "G": "phase aléatoire par élément", "B": "hauteur normalisée"},
        "cull_distance_cm": {"start": a.get("cull_cm", (0, 0))[0], "end": a.get("cull_cm", (0, 0))[1]},
        "scatter": {"pft_mask": rule.get("mask"), "share_in_pft": rule.get("assets", {}).get(aid),
                    "deadwood_rule": catalog.DEADWOOD_RULES.get(aid)},
        "files": files,
        "fbx_verification": fbx_ok,
        "botany": a["botany"],
        "on_water": bool(a.get("on_water", False)),
    }


def export_decal_meshes():
    """Trackway mesh decals for Unreal (deferred-decal material on a thin subdivided plane, ISM / PCG friendly):
    fbx/decals/SM_Decal_Trackway_<Variant>.fbx, 0.75 x 3 m, pivot at the centre, +Y = walking direction, UVs in
    the cell of T_Decal_Trackways (envlib/ground.py). Writes meta/decals.json."""
    import bpy
    from . import ground, paint
    cw, ch = ground.DECAL_CELL_M
    us = np.linspace(-cw / 2, cw / 2, 4)
    vs = np.linspace(-ch / 2, ch / 2, 13)
    out = []
    mat = bpy.data.materials.get("M_Decal_Trackways") or bpy.data.materials.new("M_Decal_Trackways")
    for cell, (name, desc) in enumerate(ground.DECALS):
        m = Mesh()
        u0, v0, u1, v1 = paint.cell_uv_rect(cell, ground.DECAL_GRID)
        P = np.stack(np.meshgrid(us, vs), -1)
        P = np.concatenate([P, np.full(P.shape[:2] + (1,), 0.01)], -1)
        UV = np.stack(np.meshgrid(u0 + (u1 - u0) * (us - us[0]) / cw, v0 + (v1 - v0) * (vs - vs[0]) / ch), -1)
        m.surface(P, UV, 0, height_norm=1.0)
        ob = m.to_blender(name, [mat])
        fn = os.path.join(common.FBX_DIR, "decals", name + ".fbx")
        common.export_fbx([ob], fn)
        out.append(dict(ue_name=name, cell=cell, description_fr=desc, file=os.path.relpath(fn, common.ROOT),
                        triangles=m.tri_count(), size_m=[cw, ch], material="M_Decal_Trackways"))
        bpy.data.objects.remove(ob, do_unlink=True)
    common.write_json(os.path.join(common.META_DIR, "decals.json"), {"schema": "noctis.environment_decals/1", "decals": out,
                                                                      "note_fr": "Décalques maillés (domaine Deferred Decal) : pistes d'empreintes placées par PCG sur les berges et le long des corridors. Silhouettes génériques, pas des moulages d'ichnotaxons."})
    return len(out)
