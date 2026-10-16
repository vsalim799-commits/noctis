"""Stage 'export': game assets + metadata.

  Build/Creatures/<sp>/export/
    SK_<sp>.fbx                skeletal mesh (LOD0) + skeleton, bind pose, no animation
    SK_<sp>_LOD1..3.fbx        decimated LODs (same skeleton/weights/UVs) to import as LODs in UE5
    A_<sp>_<clip>.fbx          one animation per Action (skeleton only, baked every frame)
    <sp>.glb                   three.js: mesh + skeleton + all clips (metres, +Y up), 2K textures
    textures/                  copies of the 4K maps (T_<sp>_*.png)
    <sp>_metadata.json         sizes, mass, bones, clips, textures, triangles, export settings,
                               science sources and the list of speculative elements
FBX for UE5 (1 UE unit = 1 cm): the scene is converted to centimetres before export (scene unit
scale 0.01, armature/mesh/root-motion x100) so the file has UnitScaleFactor = 1 (cm), positions in
cm and no node scaling; axis_forward='-Z', axis_up='Y' (Blender defaults). The creature faces -Y in
Blender, i.e. +Y in UE after the importer's axis conversion (same as the UE Mannequin).
"""
from __future__ import annotations

import json
import os
import shutil
import struct
import zlib
import numpy as np

FBX_COMMON = dict(add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=True,
                  armature_nodetype="NULL", axis_forward="-Z", axis_up="Y", apply_unit_scale=True,
                  apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, bake_space_transform=False,
                  mesh_smooth_type="FACE", use_tspace=True, use_triangles=True, use_mesh_modifiers=True,
                  path_mode="STRIP")


# ----------------------------------------------------------------------------------------------
# minimal binary FBX reader (verification of units / scales / extents)
def _read_fbx(path):
    with open(path, "rb") as f:
        data = f.read()
    ver = struct.unpack_from("<I", data, 23)[0]
    wide = ver >= 7500

    def read_node(o):
        if wide:
            end, nprop, plen = struct.unpack_from("<QQQ", data, o)
            o += 24
        else:
            end, nprop, plen = struct.unpack_from("<III", data, o)
            o += 12
        nl = data[o]
        o += 1
        name = data[o:o + nl].decode("latin1")
        o += nl
        if end == 0:
            return None, o
        props = []
        for _ in range(nprop):
            t = chr(data[o])
            o += 1
            if t == "Y":
                props.append(struct.unpack_from("<h", data, o)[0]); o += 2
            elif t == "C":
                props.append(bool(data[o])); o += 1
            elif t == "I":
                props.append(struct.unpack_from("<i", data, o)[0]); o += 4
            elif t == "F":
                props.append(struct.unpack_from("<f", data, o)[0]); o += 4
            elif t == "D":
                props.append(struct.unpack_from("<d", data, o)[0]); o += 8
            elif t == "L":
                props.append(struct.unpack_from("<q", data, o)[0]); o += 8
            elif t in "fdlib":
                n, enc, cl = struct.unpack_from("<III", data, o)
                o += 12
                raw = data[o:o + cl]
                o += cl
                if enc == 1:
                    raw = zlib.decompress(raw)
                dt = {"f": "<f4", "d": "<f8", "l": "<i8", "i": "<i4", "b": "u1"}[t]
                props.append(np.frombuffer(raw, dt, n))
            elif t in "SR":
                n = struct.unpack_from("<I", data, o)[0]
                o += 4
                props.append(data[o:o + n].decode("latin1", "replace") if t == "S" else data[o:o + n])
                o += n
            else:
                raise ValueError(t)
        children = []
        while o < end:
            c, o = read_node(o)
            if c is None:
                break
            children.append(c)
        return dict(name=name, props=props, children=children), end

    o = 27
    nodes = []
    while o < len(data) - 160:
        n, o = read_node(o)
        if n is None:
            break
        nodes.append(n)
    return nodes


def fbx_report(path):
    nodes = _read_fbx(path)
    rep = {}
    def find(ns, name):
        return [n for n in ns if n["name"] == name]
    gs = find(nodes, "GlobalSettings")
    if gs:
        for p70 in find(gs[0]["children"], "Properties70"):
            for p in p70["children"]:
                if p["props"] and p["props"][0] in ("UnitScaleFactor", "OriginalUnitScaleFactor", "UpAxis", "UpAxisSign", "FrontAxis", "FrontAxisSign", "CoordAxis"):
                    rep[p["props"][0]] = p["props"][-1]
    objs = find(nodes, "Objects")
    models = []
    ext = None
    if objs:
        for n in objs[0]["children"]:
            if n["name"] == "Model":
                info = dict(name=str(n["props"][1]).split("\x00")[0], type=n["props"][2])
                for p70 in find(n["children"], "Properties70"):
                    for p in p70["children"]:
                        if p["props"][0] in ("Lcl Translation", "Lcl Scaling", "Lcl Rotation"):
                            info[p["props"][0]] = [round(float(x), 4) for x in p["props"][-3:]]
                models.append(info)
            if n["name"] == "Geometry":
                for c in n["children"]:
                    if c["name"] == "Vertices":
                        V = np.asarray(c["props"][0]).reshape(-1, 3)
                        e = V.max(0) - V.min(0)
                        ext = e if ext is None else np.maximum(ext, e)
    lt = [np.linalg.norm(m["Lcl Translation"]) for m in models if m.get("type") == "LimbNode" and "Lcl Translation" in m]
    if lt:
        rep["max_bone_local_translation_file_units"] = round(float(max(lt)), 2)
    sc_ = [m["Lcl Scaling"] for m in models if "Lcl Scaling" in m]
    rep["node_scalings"] = sorted({tuple(round(x, 3) for x in v) for v in sc_}) if sc_ else [(1.0, 1.0, 1.0)]
    rep["models"] = models[:6]
    rep["n_models"] = len(models)
    if ext is not None:
        rep["max_geometry_extent_file_units"] = [round(float(x), 2) for x in ext]
    return rep


# ----------------------------------------------------------------------------------------------
def gltf_settings_group(bpy):
    g = bpy.data.node_groups.get("glTF Material Output")
    if g:
        return g
    g = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
    g.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
    g.interface.new_socket("Thickness", in_out="INPUT", socket_type="NodeSocketFloat")
    return g


def run(ctx):
    import bpy
    from PIL import Image
    sp = ctx.species
    exp = ctx.path("export")
    os.makedirs(exp, exist_ok=True)
    doc = ctx.anatomy()
    sdata = ctx.species_data()
    bpy.ops.wm.open_mainfile(filepath=ctx.path("anim.blend"))
    sc = bpy.context.scene
    rig = bpy.data.objects["Armature"]
    sk = bpy.data.objects[f"SK_{sp}"]
    clips = ctx.load_json("anim_clips.json")["clips"]
    actions = [bpy.data.actions[n] for n in clips]
    # glTF occlusion hookup on the body material
    m = bpy.data.materials["M_Body"]
    nt = m.node_tree
    sep = [n for n in nt.nodes if n.type == "SEPARATE_COLOR"][0]
    grp = nt.nodes.new("ShaderNodeGroup")
    grp.node_tree = gltf_settings_group(bpy)
    nt.links.new(sep.outputs["Red"], grp.inputs["Occlusion"])
    # --- LODs ---------------------------------------------------------------------------------
    lods = []
    tri0 = sum(len(p.vertices) - 2 for p in sk.data.polygons)
    for i, r in enumerate(ctx.q["lods"], start=1):
        o = sk.copy()
        o.data = sk.data.copy()
        o.name = f"SK_{sp}_LOD{i}"
        o.data.name = o.name
        sc.collection.objects.link(o)
        d = o.modifiers.new("lod", "DECIMATE")
        d.ratio = r
        d.use_collapse_triangulate = True
        d.vertex_group_factor = 1.0
        # keep the armature modifier last
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_move_to_index(modifier="lod", index=0)
        bpy.ops.object.modifier_apply(modifier="lod")
        lods.append(o)
    tri = {"LOD0": tri0}
    for i, o in enumerate(lods, start=1):
        tri[f"LOD{i}"] = sum(len(p.vertices) - 2 for p in o.data.polygons)
    ctx.log(f"  triangles: {tri}")

    def select_only(objs):
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.hide_set(False)
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]

    # --- GLB (metres, +Y up), 2K textures --------------------------------------------------------
    tex_dir = ctx.path("textures")
    small = os.path.join(tex_dir, "2k")
    os.makedirs(small, exist_ok=True)
    swaps = []
    for img in bpy.data.images:
        if img.filepath and os.path.exists(bpy.path.abspath(img.filepath)):
            src = bpy.path.abspath(img.filepath)
            im = Image.open(src)
            if im.size[0] > 2048:
                dst = os.path.join(small, os.path.basename(src))
                im.resize((2048, 2048), Image.LANCZOS).save(dst)
                swaps.append((img, img.filepath))
                img.filepath = dst
                img.reload()
    for o in lods:
        o.hide_set(True)
    rig.animation_data.action = None
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    select_only([rig, sk])
    glb = os.path.join(exp, f"{sp}.glb")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", use_selection=True, export_yup=True, export_apply=False,
                              export_animations=True, export_animation_mode="ACTIONS", export_force_sampling=True,
                              export_def_bones=True, export_skins=True, export_tangents=True, export_materials="EXPORT",
                              export_image_format="AUTO", export_frame_range=False, export_anim_single_armature=True,
                              export_reset_pose_bones=True)
    for img, fp in swaps:
        img.filepath = fp
        img.reload()
    ctx.log(f"  glb: {os.path.getsize(glb)/1e6:.1f} MB")
    # --- convert the scene to centimetres for UE --------------------------------------------------
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.01
    sc.unit_settings.length_unit = "CENTIMETERS"
    bpy.context.view_layer.objects.active = rig
    select_only([rig])
    bpy.ops.object.mode_set(mode="EDIT")
    # connected bones share head/tail: read everything first, then write (scaling in place would
    # scale shared joints twice)
    ht = {eb.name: (eb.head.copy() * 100.0, eb.tail.copy() * 100.0) for eb in rig.data.edit_bones}
    for eb in rig.data.edit_bones:
        eb.use_connect = False
    for eb in rig.data.edit_bones:
        eb.head, eb.tail = ht[eb.name]
    for eb in rig.data.edit_bones:
        if eb.parent and (eb.head - eb.parent.tail).length < 1e-3:
            eb.use_connect = True
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in [sk] + lods:
        me = o.data
        co = np.empty(len(me.vertices) * 3, np.float32)
        me.vertices.foreach_get("co", co)
        me.vertices.foreach_set("co", co * 100.0)
        me.update()
    for act in bpy.data.actions:
        for fc in act.fcurves:
            if fc.data_path.endswith(".location"):
                for kp in fc.keyframe_points:
                    kp.co[1] *= 100.0
                    kp.handle_left[1] *= 100.0
                    kp.handle_right[1] *= 100.0
    # --- skeletal mesh FBX (LOD0 + LODs) ---------------------------------------------------------
    files = {}
    for o, nm in [(sk, f"SK_{sp}")] + [(o, o.name) for o in lods]:
        select_only([rig, o])
        p = os.path.join(exp, f"{nm}.fbx")
        bpy.ops.export_scene.fbx(filepath=p, use_selection=True, object_types={"ARMATURE", "MESH"}, bake_anim=False, **FBX_COMMON)
        files[nm] = p
    # --- one FBX per Action ---------------------------------------------------------------------
    anim_files = {}
    for act in actions:
        rig.animation_data.action = act
        sc.frame_start, sc.frame_end = int(act.frame_range[0]), int(act.frame_range[1])
        select_only([rig])
        p = os.path.join(exp, f"A_{sp}_{act.name}.fbx")
        bpy.ops.export_scene.fbx(filepath=p, use_selection=True, object_types={"ARMATURE"}, bake_anim=True,
                                 bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                                 bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
                                 **FBX_COMMON)
        anim_files[act.name] = p
    rig.animation_data.action = bpy.data.actions["walk"]
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(exp, f"{sp}_ue_cm.blend"), compress=True)
    # textures
    tdst = os.path.join(exp, "textures")
    os.makedirs(tdst, exist_ok=True)
    texinfo = {}
    for fn in sorted(os.listdir(tex_dir)):
        if fn.endswith(".png"):
            shutil.copy2(os.path.join(tex_dir, fn), os.path.join(tdst, fn))
            with Image.open(os.path.join(tex_dir, fn)) as im:
                texinfo[fn] = dict(size=list(im.size), mode=im.mode)
    # --- verification of the FBX units ------------------------------------------------------------
    ver = fbx_report(files[f"SK_{sp}"])
    ver_anim = fbx_report(anim_files["walk"])
    ctx.log(f"  FBX check: {json.dumps(ver)[:600]}")
    # --- metadata -------------------------------------------------------------------------------
    meta = build_metadata(ctx, doc, sdata, tri, clips, texinfo, files, anim_files, glb, ver, ver_anim)
    with open(os.path.join(exp, f"{sp}_metadata.json"), "w") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    os.makedirs(ctx.out, exist_ok=True)
    shutil.copy2(os.path.join(exp, f"{sp}_metadata.json"), ctx.opath(f"{sp}_metadata.json"))
    ctx.log(f"  metadata: {os.path.join(exp, f'{sp}_metadata.json')}")


def build_metadata(ctx, doc, sdata, tri, clips, texinfo, files, anim_files, glb, ver, ver_anim):
    sim = sdata["sim"]
    sci = sdata["science"]
    def stat(name):
        try:
            return ctx.load_json(name)
        except Exception:
            return None
    bones = [dict(name=b["name"], parent=b["parent"], head_m=[round(x, 4) for x in b["head"]], tail_m=[round(x, 4) for x in b["tail"]])
             for b in doc["bones"]]
    claims = {k: dict(summary=sci[k]["summary"], confidence=sci[k]["confidence"], sources=sci[k]["sources"])
              for k in ("integument", "soft_tissue", "size", "mass", "locomotion", "speed", "communication") if k in sci}
    bib = ctx.bibliography()
    used = sorted({s for c in claims.values() for s in c["sources"]} | set(doc["targets"].get("sources", [])) |
                  {"alexander1976_dinosaur_speeds", "vanbijlert2021_trex_walking", "persons2011_trex_tail", "cullen2023_theropod_lips",
                   "stevens2006_binocular_vision", "riede2016_closed_mouth_vocal", "bell2017_tyrannosaur_integument"})
    refs = {s: dict(verified=bib.get(s, {}).get("verified"), title=bib.get(s, {}).get("title")) for s in used if s in bib}
    missing = [s for s in used if s not in bib]
    meta = {
        "schema": "noctis.creature_asset/1",
        "species_id": ctx.species,
        "scientific_name": sdata.get("scientific_name"),
        "quality": ctx.quality,
        "generator": "Tools/CreatureFactory/factory.py",
        "anatomy_spec": f"Data/Anatomy/{ctx.species}.json",
        "sizes": {
            "species_data": dict(adult_length_m=sim["adult_length_m"], hip_height_m=sim["hip_height_m"], adult_mass_kg=sim["adult_mass_kg"],
                                 foot_length_m=sim.get("foot_length_m")),
            "model": stat("sdf_stats.json"),
            "note": "model length/height measured on the sculpt (metres); mass_at_950_kg_m3 = sculpt volume x 950 kg/m3 (rough check only, air sacs ignored)",
        },
        "triangles": tri,
        "stage_stats": {k: stat(f"{k}_stats.json") for k in ("detail", "retopo", "uv", "bake", "rig")},
        "skeleton": {
            "root": "root", "armature_object": "Armature (skipped by UE as a bone)",
            "deform_bones": bones,
            "control_bones_not_exported": ["ik_foot_l", "ik_foot_r", "pole_knee_l", "pole_knee_r", "look_target"],
            "bone_axes": "Blender: primary Y along the bone; trunk/neck/head/jaw/tail/eyes roll so local Z = world up, limb bones local Z = forward",
            "retarget_chains": {"spine": ["pelvis", "spine_01", "spine_02", "spine_03"], "neck": ["neck_01", "neck_02", "neck_03", "head"],
                                "tail": [f"tail_{i:02d}" for i in range(1, 11)],
                                "leg_l": ["thigh_l", "shin_l", "metatarsus_l", "toes_l"], "leg_r": ["thigh_r", "shin_r", "metatarsus_r", "toes_r"],
                                "arm_l": ["upperarm_l", "forearm_l", "hand_l"], "arm_r": ["upperarm_r", "forearm_r", "hand_r"],
                                "jaw": ["jaw"], "eyes": ["eye_l", "eye_r"]},
        },
        "clips": clips,
        "root_motion": "Clips carry root motion on the 'root' bone (walk/walk_fast: forward translation; turn_left_90: yaw). In UE5 enable 'Enable Root Motion' on the AnimSequence (Root Motion Root Lock: Ref Pose). three.js: the root track can be removed for in-place playback.",
        "textures": texinfo,
        "texture_notes": {
            "BaseColor": "sRGB, albedo (palette SPÉCULATIF)",
            "Normal": "tangent space MikkTSpace, OpenGL (+Y) — Blender/three.js/glTF",
            "Normal_DX": "same, DirectX (-Y) — UE5 default (or use Normal + 'Flip Green Channel')",
            "ORM": "R ambient occlusion, G roughness, B metallic (0) — linear (sRGB off in UE)",
            "Height": "16-bit, 0..1 = height_range_m of bake_stats",
            "SSS": "subsurface weight multiplier (linear)",
            "Cavity": "relief cavities (linear)",
            "Masks": "R scutes/pads, G rugose/feature scales, B soft thin skin (lips, lids, throat)",
            "texel_density": "see stage_stats.uv.texel_density_px_per_m (head chart boosted x1.6)",
        },
        "files": {
            "skeletal_mesh_fbx": {k: os.path.relpath(v, ctx.repo) for k, v in files.items()},
            "animation_fbx": {k: os.path.relpath(v, ctx.repo) for k, v in anim_files.items()},
            "glb": os.path.relpath(glb, ctx.repo),
        },
        "export_settings": {
            "fbx": {**{k: (sorted(v) if isinstance(v, set) else v) for k, v in FBX_COMMON.items()},
                     "scene_unit_scale": 0.01, "scene_length_unit": "CENTIMETERS",
                     "conversion": "armature, meshes and location keys scaled x100 before export: file units = cm, no node scale",
                     "animation": "bake_anim, every frame (step 1), no simplification, start/end keyed, deform bones only"},
            "fbx_verification_skeletal_mesh": ver,
            "fbx_verification_walk": {k: v for k, v in ver_anim.items() if k != "models"},
            "ue5_import": ["Skeletal Mesh: Import Uniform Scale 1.0, Convert Scene ON, Convert Scene Unit OFF (file already in cm), Use T0 As Ref Pose OFF",
                           "Import the LOD FBX files as LOD1..3 of the same Skeletal Mesh (Import LOD)",
                           "Normal map: T_*_Normal_DX.png (or T_*_Normal.png with Flip Green Channel)",
                           "ORM / Masks / Height / SSS / Cavity: sRGB OFF",
                           "the creature faces +Y (like the Mannequin): rotate the mesh component -90 deg yaw to face the actor +X"],
            "glb": "metres, +Y up, deform bones only, all Actions as glTF animations (sampled every frame), 2K textures",
        },
        "science": {
            "claims_used": claims,
            "applied": {
                "integument": "small non-imbricate pebbly scales over the whole body (Bell et al. 2017); no feathers (sparse plumage not demonstrated for adult T. rex)",
                "lips": "extra-oral tissues cover the teeth at rest (Cullen et al. 2023, confidence: inferred); teeth modelled but hidden at rest",
                "eyes": "forward-facing orbits, binocular field ~55 deg (Stevens 2006): eye axis 25 deg forward of lateral",
                "walk": "preferred walk speed sim.locomotion.walk_speed_ms (van Bijlert et al. 2021); stride from Alexander (1976)",
                "no_run": "no running clip: adult T. rex limited to walking / fast walking (Hutchinson & Garcia 2002; Sellers et al. 2017)",
                "vocal": "closed-mouth vocal posture only (Riede et al. 2016 hypothesis); no open-mouth roar",
                "tail": "deep tail base with a large M. caudofemoralis (Persons & Currie 2011)",
            },
            "references": refs,
            "references_not_in_bibliography": missing,
        },
        "speculative_elements": [
            "SPÉCULATIF: all colours (palette, countershading, mottling, dorsal/tail bands, iris colour, round pupil)",
            "scale sizes and their regional distribution (design values, not measured on fossils)",
            "sparse larger feature scales (not documented for T. rex)",
            "cornified rugose skin over the skull rugosities (plausible, design)",
            "scutate scales on the metatarsus/toes and plantar pads (phylogenetic bracket with birds; not preserved for T. rex)",
            "skin folds on the neck, throat and joints; wrinkles (design)",
            "soft-tissue volumes (muscles, jowls, belly) over the skeleton",
            "dried mud on the feet",
            "resting posture (on the pubic boot, legs folded) and all behaviours other than walking",
            "mouth interior not modelled (bite clip uses a small gape only)",
        ],
    }
    return meta
