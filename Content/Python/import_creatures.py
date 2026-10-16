# -*- coding: utf-8 -*-
"""Imports the creatures produced by Tools/CreatureFactory into /Game/Creatures/<species_id>/ and fills their
UNoctisSpeciesVisualData (DA_<species_id>), the asset UNoctisCreatureManager looks up by species id.

Usage (editor Python console or command line):
    py "Content/Python/import_creatures.py"                          # every species found in Build/Creatures
    py "Content/Python/import_creatures.py" tyrannosaurus_rex        # one or more species ids
    py "Content/Python/import_creatures.py" --scale 100              # FBX authored in metres without unit conversion
    py "Content/Python/import_creatures.py" --interchange            # keep the Interchange FBX importer (5.5 default)

Expected factory output (tolerant: the first matching file wins, see find_inputs()):
    Build/Creatures/<id>/<id>.fbx | SK_<id>.fbx | mesh/*.fbx      skeletal mesh (+ skeleton), +X forward, Z up, cm
    Build/Creatures/<id>/anim/*.fbx | A_*.fbx | *_anim_*.fbx      animation sequences on the same skeleton
    Build/Creatures/<id>/textures/<anything>_<map>.<png|tga|exr|tif>
        <map> = basecolor|albedo|diffuse, normal|nrm, roughness|rough, ao|occlusion, sss|subsurface|scatter
        an optional slot token before the map (…_eye_basecolor.png, …_teeth_roughness.png) gives one material per slot
    Data/Anatomy/<id>.json (or Build/Creatures/<id>/anatomy*.json): skeleton spec, "bones": [{"name", "parent", ...}]
        used for the bone map (feet, neck, tail, head, jaw); otherwise bones are read from the imported skeleton.

Created per species:
    SK_<id>, its Skeleton and PhysicsAsset, A_<id>_<clip> sequences,
    T_<id>_* textures (sRGB only for colour maps; masks linear, BC5 normals), MI_<id>_<slot> instances of the shared
    master /Game/Creatures/_Shared/M_NoctisCreatureSkin (Subsurface shading; custom primitive data 0..4 =
    wetness, mud, condition, marking seed, scar count written by ANoctisCreatureActor),
    ABP_<id> (Animation Blueprint, parent UNoctisCreatureAnimInstance, empty graph to author),
    IK_<id> (IK Rig: retarget root, chains, foot goals; best effort, see the retarget notes printed in the log),
    DA_<id> (UNoctisSpeciesVisualData).
Call sounds are linked when import_audio.py has already imported them (it links them as well, in either order).
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import struct
import sys
import tempfile
import zlib

import unreal

import noctis_ue_common as nc

CREATURES_ROOT = "/Game/Creatures"
SHARED = f"{CREATURES_ROOT}/_Shared"
MASTER_MATERIAL = f"{SHARED}/M_NoctisCreatureSkin"

MAP_TOKENS = {
    "BaseColor": ("basecolor", "albedo", "diffuse"),
    "Normal": ("normal", "nrm", "nor", "normalgl", "normaldx"),
    "Roughness": ("roughness", "rough", "rgh"),
    "AO": ("ao", "occlusion", "ambientocclusion"),
    "Subsurface": ("sss", "subsurface", "scatter", "transmission"),
}
TEXTURE_EXTENSIONS = (".png", ".tga", ".exr", ".tif", ".tiff", ".jpg")
SLOT_TOKENS = ("body", "skin", "eye", "eyes", "teeth", "tooth", "claws", "claw", "keratin", "beak", "horn", "horns", "tongue", "mouth", "feathers")

# IK Rig chains: (chain, start bone regex, end bone regex, goal bone regex or None).
RETARGET_CHAINS = [
    ("Spine", r"^spine_?0*1$", r"^spine_?\d+$", None),
    ("Neck", r"^neck_?0*1$", r"^neck_?\d+$", None),
    ("Head", r"^(head|skull)$", r"^(head|skull)$", None),
    ("Jaw", r"^jaw$", r"^jaw$", None),
    ("Tail", r"^tail_?0*1$", r"^tail_?\d+$", None),
    ("LeftLeg", r"^thigh_?l$", r"^toes?_?l$", r"^toes?_?l$"),
    ("RightLeg", r"^thigh_?r$", r"^toes?_?r$", r"^toes?_?r$"),
    ("LeftArm", r"^upper_?arm_?l$", r"^hand_?l$", r"^hand_?l$"),
    ("RightArm", r"^upper_?arm_?r$", r"^hand_?r$", r"^hand_?r$"),
]


# ----------------------------------------------------------------------------------------------- inputs

def find_inputs(species: str) -> dict:
    base = nc.repo_path("Build", "Creatures", species)
    fbx = sorted(glob.glob(os.path.join(base, "**", "*.fbx"), recursive=True))
    anim_dirs = (os.sep + "anim" + os.sep, os.sep + "anims" + os.sep, os.sep + "animations" + os.sep)
    is_anim = lambda p: any(d in p for d in anim_dirs) or os.path.basename(p).startswith("A_") or "_anim_" in os.path.basename(p).lower()
    meshes = [p for p in fbx if not is_anim(p)]
    preferred = [os.path.join(base, f"{species}.fbx"), os.path.join(base, f"SK_{species}.fbx")]
    mesh = next((p for p in preferred if os.path.isfile(p)), None)
    if mesh is None and meshes:
        mesh = max(meshes, key=os.path.getsize)  # LOD/proxy exports are smaller than the main mesh
    textures = [p for p in glob.glob(os.path.join(base, "**", "*"), recursive=True) if p.lower().endswith(TEXTURE_EXTENSIONS)]
    textures = [p for p in textures if os.sep + "preview" not in p.lower() and os.sep + "renders" not in p.lower()]
    anatomy = nc.repo_path("Data", "Anatomy", f"{species}.json")
    if not os.path.isfile(anatomy):
        candidates = sorted(glob.glob(os.path.join(base, "anatomy*.json")))
        anatomy = candidates[-1] if candidates else None
    return {"base": base, "mesh": mesh, "animations": [p for p in fbx if is_anim(p)], "textures": sorted(textures), "anatomy": anatomy}


def classify_texture(path: str):
    """-> (slot, map) or None. File stem tokens are matched from the end: <...>_<slot>_<map>."""
    stem = os.path.splitext(os.path.basename(path))[0].lower()
    tokens = [t for t in re.split(r"[_\-. ]+", stem) if t]
    if not tokens:
        return None
    map_name = None
    used = 1
    # One-token map names ("normal") or two-token ones ("base_color", "ambient_occlusion").
    for count in (2, 1):
        if len(tokens) < count:
            continue
        candidate = "".join(tokens[-count:])
        for name, aliases in MAP_TOKENS.items():
            if candidate in aliases:
                map_name, used = name, count
                break
        if map_name:
            break
    if map_name is None:
        if tokens[-1] == "orm":
            nc.warn(f"{os.path.basename(path)} : carte ORM empaquetée non gérée par le matériau maître (exporter roughness et ao séparément).")
        return None
    slot_token = tokens[-used - 1] if len(tokens) > used else ""
    slot = slot_token if slot_token in SLOT_TOKENS else "body"
    slot = {"skin": "body", "eyes": "eye", "tooth": "teeth", "claw": "claws", "horns": "horn"}.get(slot, slot)
    return slot, map_name


# ----------------------------------------------------------------------------------------------- shared assets

def _write_png(path: str, rgba) -> None:
    """4x4 RGBA8 PNG (pure Python) for the master material's default textures."""
    w = h = 4
    raw = b"".join(b"\x00" + bytes(rgba) * w for _ in range(h))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def _configure_texture(texture, map_name: str) -> None:
    if map_name in ("BaseColor", "Subsurface"):
        nc.set_props(texture, srgb=True, compression_settings=unreal.TextureCompressionSettings.TC_DEFAULT)
    elif map_name == "Normal":
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP)
    else:
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS)
    nc.set_props(texture, lod_group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    nc.save(texture)


def default_textures() -> dict:
    """Small textures whose sampler types match the master's parameters (avoids sampler-type compile errors)."""
    wanted = {
        "BaseColor": ("T_NoctisDefault_BaseColor", (128, 118, 100, 255)),
        "Normal": ("T_NoctisDefault_Normal", (128, 128, 255, 255)),
        "Roughness": ("T_NoctisDefault_Roughness", (178, 178, 178, 255)),
        "AO": ("T_NoctisDefault_AO", (255, 255, 255, 255)),
        "Subsurface": ("T_NoctisDefault_Subsurface", (150, 40, 30, 255)),
    }
    result = {}
    tasks = []
    temp_dir = tempfile.mkdtemp(prefix="noctis_")
    for map_name, (asset_name, rgba) in wanted.items():
        existing = nc.load_asset(f"{SHARED}/{asset_name}")
        if existing:
            result[map_name] = existing
            continue
        png = os.path.join(temp_dir, f"{asset_name}.png")
        _write_png(png, rgba)
        tasks.append((map_name, nc.make_import_task(png, SHARED, asset_name)))
    nc.ensure_directory(SHARED)
    nc.import_files([t for _, t in tasks])
    for map_name, _task in tasks:
        texture = nc.load_asset(f"{SHARED}/{wanted[map_name][0]}")
        if texture:
            _configure_texture(texture, map_name)
            result[map_name] = texture
    return result


def ensure_master_material(overwrite: bool):
    existing = nc.load_asset(MASTER_MATERIAL)
    if existing and not overwrite:
        return existing
    defaults = default_textures()
    mel = unreal.MaterialEditingLibrary

    def texture_param(material, name, sampler, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", sampler)
        if name in defaults:
            node.set_editor_property("texture", defaults[name])
        return node

    def scalar_param(material, name, value, x, y, primitive_index=None):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        if primitive_index is not None:
            # Driven per actor through UPrimitiveComponent::SetCustomPrimitiveDataFloat (no dynamic material instances).
            nc.set_props(node, use_custom_primitive_data=True, primitive_data_index=primitive_index)
        return node

    def const(material, value, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
        node.set_editor_property("r", value)
        return node

    def lerp(material, a, b, alpha, x, y, alpha_pin=""):
        node = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", node, "A")
        mel.connect_material_expressions(b, "", node, "B")
        mel.connect_material_expressions(alpha, alpha_pin, node, "Alpha")
        return node

    def multiply(material, a, b, x, y, a_pin="", b_pin=""):
        node = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(a, a_pin, node, "A")
        mel.connect_material_expressions(b, b_pin, node, "B")
        return node

    def configure(material):
        mel.delete_all_material_expressions(material)
        nc.set_props(material, shading_model=unreal.MaterialShadingModel.MSM_SUBSURFACE, used_with_skeletal_mesh=True,
                     used_with_morph_targets=True)
        base = texture_param(material, "BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1600, -400)
        normal = texture_param(material, "Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -1600, 0)
        rough = texture_param(material, "Roughness", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1600, 300)
        ao = texture_param(material, "AO", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -1600, 600)
        sss = texture_param(material, "Subsurface", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -1600, 900)
        wet = scalar_param(material, "Wetness", 0.0, -1300, -650, primitive_index=0)
        mud = scalar_param(material, "Mud", 0.0, -1300, -550, primitive_index=1)
        mud_param = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1500, -800)
        mud_param.set_editor_property("parameter_name", "MudColor")
        mud_param.set_editor_property("default_value", unreal.LinearColor(0.11, 0.085, 0.06, 1.0))
        # Vector parameters output float4: mask to RGB before mixing with the float3 colour.
        mud_color = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1300, -800)
        nc.set_props(mud_color, r=True, g=True, b=True, a=False)
        mel.connect_material_expressions(mud_param, "", mud_color, "")
        # Wet skin darkens and becomes glossy; mud covers the lower body (artists can mask it with vertex colour).
        wet_dark = lerp(material, const(material, 1.0, -1100, -700), const(material, 0.55, -1100, -620), wet, -900, -650)
        darkened = multiply(material, base, wet_dark, -700, -450, "RGB", "")
        muddy = lerp(material, darkened, mud_color, multiply(material, mud, const(material, 0.8, -900, -500), -800, -520), -500, -450)
        mel.connect_material_property(muddy, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
        wet_gloss = lerp(material, const(material, 1.0, -1100, 200), const(material, 0.35, -1100, 260), wet, -900, 220)
        rough_wet = multiply(material, rough, wet_gloss, -700, 300, "R", "")
        rough_final = lerp(material, rough_wet, const(material, 1.0, -700, 420), mud, -500, 300)
        mel.connect_material_property(rough_final, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(ao, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        mel.connect_material_property(sss, "RGB", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        # Subsurface shading model: Opacity = scattering amount.
        mel.connect_material_property(scalar_param(material, "SubsurfaceAmount", 0.45, -700, 900), "", unreal.MaterialProperty.MP_OPACITY)
        mel.recompile_material(material)

    return nc.create_asset(MASTER_MATERIAL, unreal.Material, unreal.MaterialFactoryNew(), configure, overwrite=True)


# ----------------------------------------------------------------------------------------------- per species

def _fbx_options(kind: str, scale: float, skeleton=None):
    ui = unreal.FbxImportUI()
    nc.set_props(ui, import_materials=False, import_textures=False, automated_import_should_detect_type=False)
    if kind == "mesh":
        nc.set_props(ui, import_mesh=True, import_as_skeletal=True, import_animations=False, create_physics_asset=True,
                     mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        data = ui.get_editor_property("skeletal_mesh_import_data")
        nc.set_props(data, import_uniform_scale=scale, convert_scene=True, import_morph_targets=True, use_t0_as_ref_pose=False,
                     normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    else:
        nc.set_props(ui, import_mesh=False, import_as_skeletal=True, import_animations=True, skeleton=skeleton,
                     mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION)
        data = ui.get_editor_property("anim_sequence_import_data")
        nc.set_props(data, import_uniform_scale=scale, convert_scene=True,
                     animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME, remove_redundant_keys=False)
    return ui


def _bone_names_from_mesh(mesh) -> list:
    component = unreal.new_object(unreal.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)  # VERIFY: SetSkeletalMeshAsset (5.1+); older: set_skeletal_mesh
    return [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]


def _bone_names_from_anatomy(path) -> list:
    doc = nc.load_json(path) if path else None
    bones = (doc or {}).get("bones") or []
    return [b["name"] for b in bones if isinstance(b, dict) and b.get("name")]


def _first(bones, pattern):
    rx = re.compile(pattern, re.IGNORECASE)
    return next((b for b in bones if rx.match(b)), None)


def _numbered(bones, prefix):
    rx = re.compile(rf"^{prefix}_?(\d+)$", re.IGNORECASE)
    found = [(int(m.group(1)), b) for b in bones for m in [rx.match(b)] if m]
    return [b for _, b in sorted(found)]


def bone_map(bones: list) -> dict:
    return {
        "pelvis_bone": _first(bones, r"^(pelvis|hips?)$"),
        "head_bone": _first(bones, r"^(head|skull)$"),
        "jaw_bone": _first(bones, r"^jaw$"),
        # ENoctisFootSlot order: HindLeft, HindRight, ForeLeft, ForeRight.
        "foot_bones": [b for b in (_first(bones, r"^toes?_?l$"), _first(bones, r"^toes?_?r$"), _first(bones, r"^hand_?l$"), _first(bones, r"^hand_?r$")) if b],
        "neck_bones": _numbered(bones, "neck"),
        "tail_bones": _numbered(bones, "tail"),
    }


def create_ik_rig(species: str, folder: str, mesh, bones: list):
    """IK Rig with retarget chains and foot goals (best effort: the controller API changed in 5.4 and again in 5.5)."""
    if nc.load_asset(f"{folder}/IK_{species}"):
        nc.info(f"IK_{species} existe : non modifié (le supprimer pour le régénérer).")
        return None
    rig = nc.create_asset(f"{folder}/IK_{species}", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory(), None, overwrite=False)
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(mesh)
    pelvis = _first(bones, r"^(pelvis|hips?)$")
    if pelvis:
        controller.set_retarget_root(pelvis)
    solver_index = -1
    try:
        solver_index = controller.add_solver(unreal.IKRigFBIKSolver)  # VERIFY: 5.4 takes the solver class; 5.6 a struct type
        if pelvis:
            controller.set_root_bone(pelvis, solver_index)
    except Exception as exc:  # noqa: BLE001
        nc.warn(f"IK_{species} : solveur Full Body IK non ajouté ({exc}) — l'ajouter dans l'éditeur d'IK Rig.")
    notes = []
    for chain, start_rx, end_rx, goal_rx in RETARGET_CHAINS:
        start = _first(bones, start_rx)
        ends = [b for b in bones if re.match(end_rx, b, re.IGNORECASE)]
        end = ends[-1] if ends else None
        if not start or not end:
            continue
        goal_name = ""
        if goal_rx:
            goal_bone = _first(bones, goal_rx)
            if goal_bone:
                goal_name = f"{chain}Goal"
                try:
                    controller.add_new_goal(goal_name, goal_bone)
                    if solver_index >= 0:
                        controller.connect_goal_to_solver(goal_name, solver_index)
                except Exception as exc:  # noqa: BLE001
                    nc.warn(f"IK_{species} : but {goal_name} non créé ({exc}).")
                    goal_name = ""
        controller.add_retarget_chain(chain, start, end, goal_name)
        notes.append(f"{chain}: {start} -> {end}" + (f" (but {goal_name})" if goal_name else ""))
    nc.save(rig)
    nc.info(f"IK_{species} — chaînes de reciblage : " + "; ".join(notes))
    nc.info("    Reciblage : créer un IK Retargeter IK_<source> -> IK_" + species + " ; associer les chaînes de même nom ; les pieds "
            "restent pilotés par la simulation (FootIK de UNoctisCreatureAnimInstance), le reciblage ne sert qu'aux clips "
            "d'animation d'une autre espèce de même plan corporel (ex. theropode -> theropode).")
    return rig


def create_anim_blueprint(species: str, folder: str, skeleton):
    path = f"{folder}/ABP_{species}"
    if not nc.load_asset(path):
        factory = unreal.AnimBlueprintFactory()
        nc.set_props(factory, target_skeleton=skeleton, parent_class=unreal.NoctisCreatureAnimInstance.static_class())
        nc.create_asset(path, unreal.AnimBlueprint, factory)
        nc.info(f"ABP_{species} créé (graphe vide) : ajouter blendspace de locomotion (Speed, GaitPhase), Two Bone IK / Leg IK sur "
                "FootTargets, AnimDynamics ou Control Rig pour les tissus mous (SoftTissue), Look At (LookTarget), JawOpen.")
    return unreal.EditorAssetLibrary.load_blueprint_class(path)


def import_species(species: str, doc: dict, args) -> bool:
    inputs = find_inputs(species)
    if not inputs["mesh"]:
        nc.warn(f"{species} : aucun FBX de maillage dans {inputs['base']} — espèce ignorée.")
        return False
    folder = f"{CREATURES_ROOT}/{species}"
    nc.ensure_directory(folder)
    nc.info(f"{species} : maillage {os.path.relpath(inputs['mesh'], nc.project_dir())}, {len(inputs['animations'])} animation(s), "
            f"{len(inputs['textures'])} texture(s), anatomie {inputs['anatomy'] or '—'}")

    # Skeletal mesh (+ skeleton + physics asset).
    nc.import_files([nc.make_import_task(inputs["mesh"], folder, f"SK_{species}", _fbx_options("mesh", args.scale))])
    mesh = nc.load_asset(f"{folder}/SK_{species}")
    if not mesh:
        nc.error(f"{species} : import du maillage échoué.")
        return False
    skeleton = mesh.get_editor_property("skeleton")

    # Animations.
    tasks = []
    for path in inputs["animations"]:
        clip = re.sub(r"^(A_|anim_)", "", os.path.splitext(os.path.basename(path))[0], flags=re.IGNORECASE)
        clip = clip.replace(f"{species}_", "")
        tasks.append(nc.make_import_task(path, f"{folder}/Animations", f"A_{species}_{clip}", _fbx_options("anim", args.scale, skeleton)))
    nc.import_files(tasks)

    # Textures and material instances.
    master = ensure_master_material(False)
    by_slot = {}
    tex_tasks = []
    for path in inputs["textures"]:
        kind = classify_texture(path)
        if not kind:
            continue
        slot, map_name = kind
        name = f"T_{species}_{slot}_{map_name}"
        by_slot.setdefault(slot, {})[map_name] = name
        tex_tasks.append((map_name, name, nc.make_import_task(path, f"{folder}/Textures", name)))
    nc.import_files([t for _, _, t in tex_tasks])
    for map_name, name, _task in tex_tasks:
        texture = nc.load_asset(f"{folder}/Textures/{name}")
        if texture:
            _configure_texture(texture, map_name)
    materials = {}
    for slot, maps in sorted(by_slot.items()):
        mi_path = f"{folder}/MI_{species}_{slot}"

        def configure(mi, maps=maps):
            unreal.MaterialEditingLibrary.set_material_instance_parent(mi, master)
            for map_name, texture_name in maps.items():
                texture = nc.load_asset(f"{folder}/Textures/{texture_name}")
                if texture:
                    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, map_name, texture)
            if "Subsurface" not in maps:
                unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "SubsurfaceAmount", 0.25 if slot == "body" else 0.0)
        materials[slot] = nc.create_asset(mi_path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), configure, overwrite=True)

    # Bones.
    bones = _bone_names_from_anatomy(inputs["anatomy"])
    if not bones:
        bones = nc.step(f"{species} : os du squelette", lambda: _bone_names_from_mesh(mesh)) or []
    mapping = bone_map(bones)

    anim_class = nc.step(f"ABP_{species}", lambda: create_anim_blueprint(species, folder, skeleton))
    nc.step(f"IK_{species}", lambda: create_ik_rig(species, folder, mesh, bones))

    # Length: the anatomy spec's snout-to-tail target when the mesh was built from it, else the imported bounds (cm).
    bounds = mesh.get_imported_bounds()
    extent = bounds.box_extent
    bounds_length_m = 2.0 * max(extent.x, extent.y) / 100.0
    anatomy_doc = nc.load_json(inputs["anatomy"]) if inputs["anatomy"] else None
    spec_length_m = float(((anatomy_doc or {}).get("targets") or {}).get("total_length_m") or 0.0)
    mesh_length_m = spec_length_m if spec_length_m > 0.0 else bounds_length_m
    if spec_length_m > 0.0 and not (0.8 * spec_length_m <= bounds_length_m <= 1.25 * spec_length_m):
        nc.warn(f"{species} : boîte englobante {bounds_length_m:.2f} m pour une longueur d'anatomie de {spec_length_m:.2f} m — échelle d'import ?")
    adult_m = float(((doc or {}).get("sim") or {}).get("adult_length_m") or 0.0)
    if adult_m > 0.0 and not (0.5 * adult_m <= mesh_length_m <= 2.0 * adult_m):
        nc.warn(f"{species} : maillage de {mesh_length_m:.2f} m pour une longueur adulte simulée de {adult_m:.2f} m — "
                f"échelle d'import à vérifier (--scale {100.0 * args.scale if mesh_length_m < adult_m else args.scale / 100.0:g}).")

    # Visual data asset.
    def configure_da(da):
        nc.set_props(da, species_id=unreal.Name(species), skeletal_mesh=mesh, mesh_body_length_m=max(0.05, mesh_length_m))
        if anim_class:
            nc.set_props(da, anim_class=anim_class)
        # Material slot order of the imported mesh: match slot names, else "body" everywhere.
        overrides = []
        for slot_info in mesh.get_editor_property("materials"):
            slot_name = str(slot_info.get_editor_property("material_slot_name")).lower()
            chosen = next((m for s, m in materials.items() if s in slot_name), materials.get("body"))
            overrides.append(chosen)
        if overrides and any(overrides):
            nc.set_props(da, material_overrides=overrides)
        for key, value in mapping.items():
            if value:
                nc.set_props(da, **{key: (unreal.Name(value) if isinstance(value, str) else [unreal.Name(v) for v in value])})
        # Small animals are never worth a full actor far away.
        if adult_m > 0.0:
            nc.set_props(da, max_actor_distance_m=float(min(600.0, max(60.0, adult_m * 60.0))))
        link_call_sounds(da, species, doc)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.NoctisSpeciesVisualData.static_class())
    nc.create_asset(f"{folder}/DA_{species}", unreal.NoctisSpeciesVisualData, factory, configure_da, overwrite=True)
    nc.info(f"{species} : DA_{species} prêt ({len(materials)} matériau(x), os pieds {mapping['foot_bones']}).")
    return True


def link_call_sounds(da, species: str, doc: dict) -> int:
    """CallSounds <- /Game/Audio/Creatures/<species>/voc_<species>_<call>_voiceA (imported by import_audio.py)."""
    calls = (((doc or {}).get("sim") or {}).get("vocal") or {}).get("call_types") or []
    entries = []
    for call in calls:
        call_id = call.get("id")
        sound = nc.load_asset(f"/Game/Audio/Creatures/{species}/voc_{species}_{call_id}_voiceA") if call_id else None
        if sound:
            entry = unreal.NoctisCallSound()
            entry.set_editor_property("call_id", unreal.Name(call_id))
            entry.set_editor_property("sound", sound)
            entries.append(entry)
    if entries:
        nc.set_props(da, call_sounds=entries)
    return len(entries)


# ----------------------------------------------------------------------------------------------- main

def main(argv) -> int:
    parser = argparse.ArgumentParser(prog="import_creatures.py")
    parser.add_argument("species", nargs="*", help="identifiants d'espèces (défaut : tous les dossiers de Build/Creatures)")
    parser.add_argument("--scale", type=float, default=1.0, help="échelle uniforme d'import FBX (1 = FBX en cm)")
    parser.add_argument("--interchange", action="store_true", help="garder l'importeur FBX Interchange (ignore les options FbxImportUI)")
    parser.add_argument("--rebuild-master", action="store_true", help="recrée M_NoctisCreatureSkin")
    args = parser.parse_args(argv)

    species_docs = nc.species_files()
    root = nc.repo_path("Build", "Creatures")
    wanted = args.species or (sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))) if os.path.isdir(root) else [])
    if not wanted:
        nc.warn(f"Aucune créature dans {root} : lancer d'abord Tools/CreatureFactory.")
        return nc.summary("import_creatures")

    # UE 5.5 routes FBX through Interchange by default, which ignores FbxImportUI: use the legacy importer for this run.
    cvar = "Interchange.FeatureFlags.Import.FBX"
    previous = None
    if not args.interchange:
        try:
            previous = unreal.SystemLibrary.get_console_variable_bool_value(cvar)
            unreal.SystemLibrary.execute_console_command(None, f"{cvar} 0")
        except Exception:  # noqa: BLE001 - cvar absent before Interchange FBX existed
            previous = None
    try:
        nc.step("matériau maître", lambda: ensure_master_material(args.rebuild_master))
        for species in wanted:
            if species not in species_docs:
                nc.warn(f"{species} : aucun Data/Species/*/{species}.json — import quand même (SpeciesId devra correspondre).")
            nc.step(species, lambda species=species: import_species(species, species_docs.get(species), args))
    finally:
        if previous is not None:
            unreal.SystemLibrary.execute_console_command(None, f"{cvar} {1 if previous else 0}")
    nc.info("Valider ensuite : Outils > Noctis > Valider les données scientifiques (vérifie DA_* contre Data/Species).")
    return nc.summary("import_creatures")


if __name__ == "__main__":
    main(sys.argv[1:])
