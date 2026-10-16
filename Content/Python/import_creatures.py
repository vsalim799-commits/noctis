# -*- coding: utf-8 -*-
"""Imports the creatures produced by Tools/CreatureFactory into /Game/Creatures/<species_id>/ and fills their
UNoctisSpeciesVisualData (DA_<species_id>), the asset ANoctisCreatureManager looks up by species id.

Usage (editor Python console or command line):
    py "Content/Python/import_creatures.py"                          # every species found in Build/Creatures
    py "Content/Python/import_creatures.py" tyrannosaurus_rex        # one or more species ids
    py "Content/Python/import_creatures.py" --scale 100              # FBX authored in metres without unit conversion
    py "Content/Python/import_creatures.py" --interchange            # keep the Interchange FBX importer (5.5 default)
    py "Content/Python/import_creatures.py" --resolution 2k          # 2K maps where the factory baked them (BaseColor, ORM;
                                                                     # its 2K normal is OpenGL-only: the 4K Normal_DX is kept)

Factory contract (Tools/CreatureFactory/README.md, "export"; schema "noctis.creature_asset/1"):
    Build/Creatures/<id>/export/<id>_metadata.json   files, clips, skeleton (bones, retarget chains), sizes, textures
    Build/Creatures/<id>/export/SK_<id>.fbx (+ SK_<id>_LOD1..3.fbx)   skeletal mesh, 1 unit = 1 cm, faces +Y like the
                                                     UE Mannequin per the factory README (DA MeshYawOffsetDeg = -90;
                                                     measured on the imported pelvis -> head direction when the
                                                     Python AnimPose library is available), material slots
                                                     M_Body, M_Eye, M_Claw, M_Teeth
    Build/Creatures/<id>/export/A_<id>_<clip>.fbx    one animation per clip, root motion on bone "root"
    Build/Creatures/<id>/export/textures/T_<id>_<Map>.png  (4K; textures/2k/ holds the 2K set)
        BaseColor (sRGB), Normal_DX (UE convention; Normal = OpenGL, imported with Flip Green Channel only when no
        _DX map exists), ORM (R occlusion, G roughness, B metallic; linear), SSS (subsurface weight, linear),
        Eye_BaseColor (sRGB), Height / Cavity / Masks (linear; imported, not used by the master material)
Without metadata the script falls back to a tolerant search (first matching file wins, see find_inputs()).

Created per species:
    SK_<id> with LODs 1..3, its Skeleton and PhysicsAsset; Animations/A_<id>_<clip> sequences (root motion enabled with
    the root locked: the simulation places the actor and UNoctisCreatureAnimInstance ignores the extracted motion);
    Textures/T_<id>_*; MI_<id>_<slot> instances (body, eye, claws, teeth) of the shared master
    /Game/Creatures/_Shared/M_NoctisCreatureSkin (Subsurface shading; ORM packing; custom primitive data 0..4 = wetness,
    mud, condition, marking seed, scar count written by ANoctisCreatureActor);
    ABP_<id> (Animation Blueprint, parent UNoctisCreatureAnimInstance, empty graph to author);
    IK_<id> (IK Rig: retarget root, chains from the metadata, foot goals; best effort, see the notes printed in the log);
    DA_<id> (UNoctisSpeciesVisualData: mesh, ABP, materials, bones, clips with their authored speed / cycle / stride,
    MeshBodyLengthM, MeshYawOffsetDeg).
Call sounds are linked when import_audio.py has already imported them (it links them as well, in either order).

Nothing here could be run where it was written (no Unreal Editor): calls whose Python API moved between versions are
marked VERIFY and run inside nc.step() so that one failing step is reported without aborting the import.
"""
from __future__ import annotations

import argparse
import glob
import math
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
METADATA_SCHEMA = "noctis.creature_asset/"
# Tools/CreatureFactory meshes face +Y (UE Mannequin convention): rotate the mesh component by -90 deg of yaw.
FACTORY_MESH_YAW_DEG = -90.0

# Texture maps understood by the master material, and the auxiliary maps imported for artists.
MAP_TOKENS = {
    "BaseColor": ("basecolor", "albedo", "diffuse", "color"),
    "Normal": ("normal", "nrm", "nor", "normalgl", "normaldx", "normalogl"),
    "ORM": ("orm", "occlusionroughnessmetallic"),
    "SSS": ("sss", "subsurface", "scatter", "transmission"),
    "Height": ("height", "displacement", "disp"),
    "Cavity": ("cavity",),
    "Masks": ("masks", "mask"),
    # Separate maps are not wired by the master (ORM packing); they are reported.
    "Roughness": ("roughness", "rough", "rgh"),
    "AO": ("ao", "occlusion", "ambientocclusion"),
}
MASTER_MAPS = ("BaseColor", "Normal", "ORM", "SSS")
TEXTURE_EXTENSIONS = (".png", ".tga", ".exr", ".tif", ".tiff", ".jpg")
SLOT_TOKENS = ("body", "skin", "eye", "eyes", "teeth", "tooth", "claws", "claw", "keratin", "beak", "horn", "horns", "tongue", "mouth", "feathers")
SLOT_ALIASES = {"skin": "body", "eyes": "eye", "tooth": "teeth", "claw": "claws", "horns": "horn"}

# Per-slot material instance defaults (claws use the factory's vertex colours: dark base -> light tip).
SLOT_DEFAULTS = {
    "body": {"SubsurfaceAmount": 0.45, "RoughnessScale": 1.0, "VertexColorAmount": 0.0, "Tint": (1.0, 1.0, 1.0)},
    "eye": {"SubsurfaceAmount": 0.0, "RoughnessScale": 0.15, "VertexColorAmount": 0.0, "Tint": (1.0, 1.0, 1.0)},
    "claws": {"SubsurfaceAmount": 0.0, "RoughnessScale": 0.55, "VertexColorAmount": 1.0, "Tint": (0.35, 0.31, 0.26)},
    "teeth": {"SubsurfaceAmount": 0.08, "RoughnessScale": 0.5, "VertexColorAmount": 0.0, "Tint": (0.86, 0.81, 0.70)},
}

# IK Rig chains when the metadata has none: (chain, start bone regex, end bone regex, goal bone regex or None).
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
# Metadata chain key -> (IK Rig chain name, has an IK goal on its end bone).
METADATA_CHAINS = {
    "spine": ("Spine", False), "neck": ("Neck", False), "tail": ("Tail", False), "jaw": ("Jaw", False),
    "leg_l": ("LeftLeg", True), "leg_r": ("RightLeg", True), "arm_l": ("LeftArm", True), "arm_r": ("RightArm", True),
}


# ----------------------------------------------------------------------------------------------- inputs

def _abs(path: str) -> str:
    return path if os.path.isabs(path) else nc.repo_path(*path.replace("\\", "/").split("/"))


def load_metadata(species: str):
    """Factory metadata (schema noctis.creature_asset/1) or None."""
    for candidate in (nc.repo_path("Build", "Creatures", species, "export", f"{species}_metadata.json"),
                      nc.repo_path("Output", "factory", species, f"{species}_metadata.json")):
        doc = nc.load_json(candidate)
        if doc and str(doc.get("schema", "")).startswith(METADATA_SCHEMA):
            return doc, candidate
        if doc:
            nc.warn(f"{candidate} : schéma '{doc.get('schema')}' inattendu (attendu {METADATA_SCHEMA}1) — ignoré.")
    return None, None


def _dedupe_textures(paths: list, resolution: str) -> list:
    """One file per map name: the requested resolution set (4K top level / textures/2k), export/ copies first."""
    def rank(path):
        p = path.replace("\\", "/").lower()
        in_2k = "/2k/" in p
        wrong_res = in_2k if resolution == "4k" else not in_2k
        return (wrong_res, "/export/" not in p, p)
    best = {}
    for path in sorted(paths, key=rank):
        best.setdefault(os.path.basename(path).lower(), path)
    return sorted(best.values())


def find_inputs(species: str, resolution: str) -> dict:
    base = nc.repo_path("Build", "Creatures", species)
    metadata, metadata_path = load_metadata(species)
    anatomy = nc.repo_path("Data", "Anatomy", f"{species}.json")
    if not os.path.isfile(anatomy):
        candidates = sorted(glob.glob(os.path.join(base, "anatomy*.json")))
        anatomy = candidates[-1] if candidates else None

    if metadata:
        files = metadata.get("files") or {}
        meshes = files.get("skeletal_mesh_fbx") or {}
        main = meshes.get(f"SK_{species}") or next((p for k, p in sorted(meshes.items()) if not re.search(r"_LOD\d+$", k)), None)
        lods = [_abs(p) for k, p in sorted(meshes.items(), key=lambda kv: kv[0]) if re.search(r"_LOD\d+$", k)]
        animations = {clip: _abs(p) for clip, p in sorted((files.get("animation_fbx") or {}).items())}
        # export/textures holds the delivered set; textures/ (and textures/2k) the bake output it was copied from.
        textures = [p for d in (os.path.join(base, "export", "textures"), os.path.join(base, "textures"))
                    for p in glob.glob(os.path.join(d, "**", "*"), recursive=True) if p.lower().endswith(TEXTURE_EXTENSIONS)]
        return {"base": base, "metadata": metadata, "metadata_path": metadata_path, "mesh": _abs(main) if main else None,
                "lods": [p for p in lods if os.path.isfile(p)], "animations": {c: p for c, p in animations.items() if os.path.isfile(p)},
                "textures": _dedupe_textures(textures, resolution), "anatomy": anatomy}

    # Tolerant fallback for other producers.
    fbx = sorted(glob.glob(os.path.join(base, "**", "*.fbx"), recursive=True))
    anim_dirs = (os.sep + "anim" + os.sep, os.sep + "anims" + os.sep, os.sep + "animations" + os.sep)
    is_anim = lambda p: any(d in p for d in anim_dirs) or os.path.basename(p).startswith("A_") or "_anim_" in os.path.basename(p).lower()
    is_lod = lambda p: re.search(r"_LOD\d+\.fbx$", p, re.IGNORECASE) is not None
    meshes = [p for p in fbx if not is_anim(p) and not is_lod(p)]
    preferred = [os.path.join(base, d, f"{n}.fbx") for d in ("export", "") for n in (f"SK_{species}", species)]
    mesh = next((p for p in preferred if os.path.isfile(p)), None)
    if mesh is None and meshes:
        mesh = max(meshes, key=os.path.getsize)
    lods = []
    if mesh:
        stem = os.path.splitext(mesh)[0]
        lods = sorted(p for p in fbx if re.match(re.escape(stem) + r"_LOD\d+\.fbx$", p, re.IGNORECASE))
    animations = {}
    for path in (p for p in fbx if is_anim(p)):
        clip = re.sub(r"^(A_|anim_)", "", os.path.splitext(os.path.basename(path))[0], flags=re.IGNORECASE).replace(f"{species}_", "")
        animations.setdefault(clip, path)
    textures = [p for p in glob.glob(os.path.join(base, "**", "*"), recursive=True) if p.lower().endswith(TEXTURE_EXTENSIONS)]
    textures = [p for p in textures if os.sep + "preview" not in p.lower() and os.sep + "renders" not in p.lower()]
    return {"base": base, "metadata": None, "metadata_path": None, "mesh": mesh, "lods": lods, "animations": animations,
            "textures": _dedupe_textures(textures, resolution), "anatomy": anatomy}


def classify_texture(path: str):
    """-> (slot, map, normal_convention) or None. Stem tokens are matched from the end: <...>_<slot>_<map>[_dx|_gl]."""
    stem = os.path.splitext(os.path.basename(path))[0].lower()
    tokens = [t for t in re.split(r"[_\-. ]+", stem) if t]
    if not tokens:
        return None
    convention = None
    if tokens[-1] in ("dx", "directx", "gl", "ogl", "opengl") and len(tokens) > 1 and tokens[-2] in ("normal", "nrm", "nor"):
        convention = "dx" if tokens[-1] in ("dx", "directx") else "gl"
        tokens = tokens[:-1]
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
        return None
    if map_name == "Normal":
        if candidate in ("normaldx",):
            convention = "dx"
        elif candidate in ("normalgl", "normalogl"):
            convention = "gl"
        # Tools/CreatureFactory: "_Normal" alone is OpenGL (+Y); other producers default to UE's DirectX convention.
        convention = convention or "gl_or_unknown"
    slot_token = tokens[-used - 1] if len(tokens) > used else ""
    slot = slot_token if slot_token in SLOT_TOKENS else "body"
    return SLOT_ALIASES.get(slot, slot), map_name, convention


def pick_textures(paths: list, species: str) -> dict:
    """slot -> map -> (path, flip_green). Prefers DirectX normals; reports maps the master does not use."""
    found = {}
    for path in paths:
        kind = classify_texture(path)
        if not kind:
            continue
        slot, map_name, convention = kind
        found.setdefault(slot, {}).setdefault(map_name, []).append((path, convention))
    result = {}
    for slot, maps in found.items():
        for map_name, entries in maps.items():
            if map_name == "Normal":
                dx = [p for p, c in entries if c == "dx"]
                gl = [p for p, c in entries if c != "dx"]
                if dx:
                    result.setdefault(slot, {})[map_name] = (dx[0], False)
                else:
                    # OpenGL normal: flip green for UE (only Tools/CreatureFactory names are known to be OpenGL).
                    result.setdefault(slot, {})[map_name] = (gl[0], True)
            else:
                result.setdefault(slot, {})[map_name] = (entries[0][0], False)
            if map_name in ("Roughness", "AO"):
                nc.warn(f"{species} : carte {map_name} séparée ({os.path.basename(entries[0][0])}) — le matériau maître lit l'ORM "
                        "empaqueté (R occlusion, G rugosité, B métal) : l'empaqueter.")
    return result


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


def _configure_texture(texture, map_name: str, flip_green: bool = False) -> None:
    if map_name == "BaseColor":
        nc.set_props(texture, srgb=True, compression_settings=unreal.TextureCompressionSettings.TC_DEFAULT)
    elif map_name == "Normal":
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_NORMALMAP, flip_green_channel=flip_green)
    elif map_name in ("SSS", "Cavity"):
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_GRAYSCALE)
    elif map_name == "Height":
        # 16-bit height (0..1 = bake height range): keep precision for displacement / parallax experiments.
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_GRAYSCALE)  # VERIFY: TC_SINGLEFLOAT for 16-bit
    else:
        nc.set_props(texture, srgb=False, compression_settings=unreal.TextureCompressionSettings.TC_MASKS)
    nc.set_props(texture, lod_group=unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    nc.save(texture)


def default_textures() -> dict:
    """Small textures whose sampler types match the master's parameters (avoids sampler-type compile errors)."""
    wanted = {
        "BaseColor": ("T_NoctisDefault_BaseColor", (128, 118, 100, 255)),
        "Normal": ("T_NoctisDefault_Normal", (128, 128, 255, 255)),
        "ORM": ("T_NoctisDefault_ORM", (255, 178, 0, 255)),
        "SSS": ("T_NoctisDefault_SSS", (128, 128, 128, 255)),
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
    """M_NoctisCreatureSkin: Subsurface shading model, ORM packing, wet / mud / condition from custom primitive data."""
    existing = nc.load_asset(MASTER_MATERIAL)
    if existing and not overwrite:
        try:
            names = [str(n) for n in unreal.MaterialEditingLibrary.get_texture_parameter_names(existing)]  # VERIFY (5.x)
        except Exception:  # noqa: BLE001
            names = list(MASTER_MAPS)
        if all(m in names for m in MASTER_MAPS):
            return existing
        nc.info(f"{MASTER_MATERIAL} : paramètres {names} d'une version précédente (sans ORM / SSS) — recréé.")
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

    def vector_param(material, name, rgb, x, y):
        param = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
        param.set_editor_property("parameter_name", name)
        param.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        # Vector parameters output float4: mask to RGB before mixing with float3 values.
        mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x + 200, y)
        nc.set_props(mask, r=True, g=True, b=True, a=False)
        mel.connect_material_expressions(param, "", mask, "")
        return mask

    def const(material, value, x, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, x, y)
        node.set_editor_property("r", value)
        return node

    def lerp(material, a, b, alpha, x, y, a_pin="", b_pin="", alpha_pin=""):
        node = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, a_pin, node, "A")
        mel.connect_material_expressions(b, b_pin, node, "B")
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
        base = texture_param(material, "BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -2000, -400)
        normal = texture_param(material, "Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -2000, 0)
        orm = texture_param(material, "ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -2000, 300)
        # Linear grayscale: the SSS weight map is imported with sRGB off and TC_Grayscale (sampler must match or the
        # material fails to compile).
        sss = texture_param(material, "SSS", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, -2000, 700)
        tint = vector_param(material, "Tint", (1.0, 1.0, 1.0), -2000, -700)
        vertex = mel.create_material_expression(material, unreal.MaterialExpressionVertexColor, -2000, -900)
        vertex_amount = scalar_param(material, "VertexColorAmount", 0.0, -1800, -1000)
        wet = scalar_param(material, "Wetness", 0.0, -1300, -650, primitive_index=0)
        mud = scalar_param(material, "Mud", 0.0, -1300, -550, primitive_index=1)
        mud_color = vector_param(material, "MudColor", (0.11, 0.085, 0.06), -1600, -800)
        # Albedo: texture x tint x (optional) vertex colour (claws: dark base -> light tip, baked by the factory rig).
        # VertexColor's first (unnamed) output is RGB.
        vertex_mix = lerp(material, const(material, 1.0, -1600, -980), vertex, vertex_amount, -1400, -950)
        albedo = multiply(material, multiply(material, base, tint, -1600, -500, "RGB", ""), vertex_mix, -1300, -450)
        # Wet skin darkens and becomes glossy; mud covers the body (artists can mask it with vertex colour or Masks).
        wet_dark = lerp(material, const(material, 1.0, -1100, -700), const(material, 0.55, -1100, -620), wet, -900, -650)
        darkened = multiply(material, albedo, wet_dark, -700, -450)
        muddy = lerp(material, darkened, mud_color, multiply(material, mud, const(material, 0.8, -900, -500), -800, -520), -500, -450)
        mel.connect_material_property(muddy, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
        # ORM: R ambient occlusion, G roughness, B metallic (always 0 for skin, keratin and enamel: not connected).
        rough_scale = scalar_param(material, "RoughnessScale", 1.0, -1300, 400)
        rough = multiply(material, orm, rough_scale, -1100, 300, "G", "")
        wet_gloss = lerp(material, const(material, 1.0, -1100, 150), const(material, 0.35, -1100, 210), wet, -900, 180)
        rough_wet = multiply(material, rough, wet_gloss, -700, 300)
        rough_final = lerp(material, rough_wet, const(material, 1.0, -700, 420), mud, -500, 300)
        mel.connect_material_property(rough_final, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        # Subsurface shading model: SubsurfaceColor = scattered colour (flesh), Opacity = scattering amount.
        mel.connect_material_property(vector_param(material, "SubsurfaceColor", (0.62, 0.13, 0.08), -900, 650), "",
                                      unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        amount = multiply(material, sss, scalar_param(material, "SubsurfaceAmount", 0.45, -1300, 850), -900, 800, "R", "")
        mel.connect_material_property(amount, "", unreal.MaterialProperty.MP_OPACITY)
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
        nc.set_props(data, import_uniform_scale=scale, convert_scene=True, convert_scene_unit=False, import_morph_targets=True,
                     use_t0_as_ref_pose=False, normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    else:
        nc.set_props(ui, import_mesh=False, import_as_skeletal=True, import_animations=True, skeleton=skeleton,
                     mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION)
        data = ui.get_editor_property("anim_sequence_import_data")
        nc.set_props(data, import_uniform_scale=scale, convert_scene=True, convert_scene_unit=False,
                     animation_length=unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME, remove_redundant_keys=False)
    return ui


def import_lods(mesh, lod_paths: list) -> int:
    """LOD1..n from separate FBX files (factory export) into the same skeletal mesh."""
    imported = 0
    subsystem = None
    try:
        subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)  # VERIFY: 5.1+ subsystem with import_lod
    except Exception:  # noqa: BLE001
        subsystem = None
    for index, path in enumerate(lod_paths, start=1):
        result = -1
        for label, fn in (("SkeletalMeshEditorSubsystem.import_lod", lambda: subsystem.import_lod(mesh, index, path) if subsystem else -1),
                          ("EditorSkeletalMeshLibrary.import_lod", lambda: unreal.EditorSkeletalMeshLibrary.import_lod(mesh, index, path))):
            try:
                result = fn()
            except Exception as exc:  # noqa: BLE001 - API moved between versions
                nc.info(f"    {label} indisponible ({exc})")
                result = -1
            if result is not None and result >= 0:
                break
        if result is not None and result >= 0:
            imported += 1
        else:
            nc.warn(f"LOD{index} non importé ({os.path.basename(path)}) : l'ajouter à la main (Skeletal Mesh > LOD Settings > Import LOD).")
    if imported:
        nc.save(mesh)
    return imported


def configure_sequence(sequence, clip: str, info: dict) -> None:
    """Root motion on, root locked to the reference pose: the simulation moves the actor (the clip must not)."""
    nc.set_props(sequence, enable_root_motion=True)
    try:
        nc.set_props(sequence, root_motion_root_lock=unreal.RootMotionRootLock.REF_POSE)  # VERIFY: enum ERootMotionRootLock
    except AttributeError:
        nc.warn(f"{sequence.get_name()} : RootMotionRootLock indisponible — vérifier « Root Motion Root Lock = Ref Pose ».")
    nc.save(sequence)


def _bone_names_from_mesh(mesh) -> list:
    component = unreal.new_object(unreal.SkeletalMeshComponent)
    component.set_skeletal_mesh_asset(mesh)  # VERIFY: SetSkeletalMeshAsset (5.1+); older: set_skeletal_mesh
    return [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]


def _bone_names_from_anatomy(path) -> list:
    doc = nc.load_json(path) if path else None
    bones = (doc or {}).get("bones") or []
    return [b["name"] for b in bones if isinstance(b, dict) and b.get("name")]


def _bone_names_from_metadata(metadata) -> list:
    bones = ((metadata or {}).get("skeleton") or {}).get("deform_bones") or []
    return [b["name"] if isinstance(b, dict) else str(b) for b in bones if (isinstance(b, dict) and b.get("name")) or isinstance(b, str)]


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


def retarget_chains(bones: list, metadata) -> list:
    """[(chain, start, end, goal bone or None)] from the metadata's skeleton.retarget_chains, else from bone names."""
    chains = []
    spec = ((metadata or {}).get("skeleton") or {}).get("retarget_chains") or {}
    for key, members in spec.items():
        if key not in METADATA_CHAINS or not members:
            continue
        name, has_goal = METADATA_CHAINS[key]
        chains.append((name, members[0], members[-1], members[-1] if has_goal else None))
    if chains:
        return chains
    for chain, start_rx, end_rx, goal_rx in RETARGET_CHAINS:
        start = _first(bones, start_rx)
        ends = [b for b in bones if re.match(end_rx, b, re.IGNORECASE)]
        end = ends[-1] if ends else None
        if start and end:
            chains.append((chain, start, end, _first(bones, goal_rx) if goal_rx else None))
    return chains


def create_ik_rig(species: str, folder: str, mesh, bones: list, metadata):
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
    for chain, start, end, goal_bone in retarget_chains(bones, metadata):
        goal_name = ""
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
            "d'animation d'une autre espèce de même plan corporel (ex. théropode -> théropode).")
    return rig


def create_anim_blueprint(species: str, folder: str, skeleton):
    path = f"{folder}/ABP_{species}"
    if not nc.load_asset(path):
        factory = unreal.AnimBlueprintFactory()
        nc.set_props(factory, target_skeleton=skeleton, parent_class=unreal.NoctisCreatureAnimInstance.static_class())
        nc.create_asset(path, unreal.AnimBlueprint, factory)
        nc.info(f"ABP_{species} créé (graphe vide) : machine à états de locomotion (Speed, Gait) jouant GetLocomotionSequence() "
                "ou les A_* importées au rythme LocomotionPlayRate, Two Bone IK / Leg IK sur FootTargetsComponent (alpha "
                "FootIKAlpha x FootContact), modification des os du cou / de la queue (NeckVertical, TailLateral...), Look At "
                "(LookTarget), mâchoire (JawOpen). Voir Docs/UE5_INTEGRATION.md.")
    return unreal.EditorAssetLibrary.load_blueprint_class(path)


def measured_yaw_offset(skeleton, head: str, pelvis: str):
    """Yaw (deg) bringing the pelvis -> head direction of the imported reference pose onto +X, or None.
    UE yaw turns +X towards +Y, so a head on +Y (Mannequin convention) needs -90."""
    if not skeleton or not head or not pelvis:
        return None
    try:
        pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)  # VERIFY: AnimPose library (UE 5.1+)
        space = unreal.AnimPoseSpaces.WORLD
        h = unreal.AnimPoseExtensions.get_bone_pose(pose, head, space).translation
        p = unreal.AnimPoseExtensions.get_bone_pose(pose, pelvis, space).translation
    except Exception as exc:  # noqa: BLE001 - API absent or renamed
        nc.info(f"    pose de référence illisible depuis Python ({exc}) : orientation non mesurée.")
        return None
    dx, dy = h.x - p.x, h.y - p.y
    if math.hypot(dx, dy) < 1.0:  # cm
        return None
    yaw = -math.degrees(math.atan2(dy, dx))
    # Snap to the nearest quarter turn when the skeleton is axis-aligned (it is for the factory rigs).
    snapped = round(yaw / 90.0) * 90.0
    return snapped if abs(snapped - yaw) < 10.0 else yaw


def mesh_yaw_offset(metadata, mesh, skeleton=None, head: str = "", pelvis: str = "") -> float:
    """Yaw (deg) that turns the mesh's forward axis onto the actor's +X: explicit metadata, else measured on the
    imported skeleton (pelvis -> head), else the factory convention (-90), else a bounding-box guess."""
    explicit = ((metadata or {}).get("ue5") or {}).get("mesh_yaw_offset_deg")
    if explicit is not None:
        return float(explicit)
    measured = measured_yaw_offset(skeleton, head, pelvis)
    if measured is not None:
        if metadata and abs(((measured - FACTORY_MESH_YAW_DEG) + 180.0) % 360.0 - 180.0) > 1.0:
            nc.warn(f"{mesh.get_name()} : tête mesurée à un lacet de {measured:+.0f}° (convention de la fabrique : "
                    f"{FACTORY_MESH_YAW_DEG:+.0f}°) — la mesure est retenue.")
        return measured
    if metadata:
        return FACTORY_MESH_YAW_DEG
    extent = mesh.get_imported_bounds().box_extent
    if extent.y > 1.5 * extent.x:
        nc.warn(f"{mesh.get_name()} : maillage allongé selon Y — MeshYawOffsetDeg = -90 supposé (vérifier que la tête pointe vers +X de l'acteur).")
        return -90.0
    return 0.0


def clip_struct(sequence, info: dict):
    clip = unreal.NoctisAnimClip()
    nc.set_props(clip, sequence=sequence, loop=bool(info.get("loop", False)), speed_ms=float(info.get("speed_ms") or 0.0),
                 cycle_seconds=float(info.get("period_s") or 0.0), stride_m=float(info.get("stride_m") or 0.0),
                 duty_factor=float(info.get("duty_factor") or 0.0))
    return clip


def import_species(species: str, doc: dict, args) -> bool:
    inputs = find_inputs(species, args.resolution)
    if not inputs["mesh"] or not os.path.isfile(inputs["mesh"]):
        nc.warn(f"{species} : aucun FBX de maillage dans {inputs['base']} — espèce ignorée (lancer Tools/CreatureFactory, étape export).")
        return False
    metadata = inputs["metadata"]
    folder = f"{CREATURES_ROOT}/{species}"
    nc.ensure_directory(folder)
    nc.info(f"{species} : maillage {os.path.relpath(inputs['mesh'], nc.project_dir())} (+{len(inputs['lods'])} LOD), "
            f"{len(inputs['animations'])} animation(s), {len(inputs['textures'])} texture(s), "
            f"métadonnées {os.path.relpath(inputs['metadata_path'], nc.project_dir()) if inputs['metadata_path'] else '—'}, "
            f"anatomie {inputs['anatomy'] or '—'}")

    # Skeletal mesh (+ skeleton + physics asset), then its LODs.
    nc.import_files([nc.make_import_task(inputs["mesh"], folder, f"SK_{species}", _fbx_options("mesh", args.scale))])
    mesh = nc.load_asset(f"{folder}/SK_{species}")
    if not mesh:
        nc.error(f"{species} : import du maillage échoué.")
        return False
    skeleton = mesh.get_editor_property("skeleton")
    if inputs["lods"]:
        nc.step(f"{species} : LOD", lambda: import_lods(mesh, inputs["lods"]))

    # Animations (one FBX per clip), root motion locked.
    clips_info = (metadata or {}).get("clips") or {}
    tasks = []
    for clip, path in inputs["animations"].items():
        tasks.append((clip, nc.make_import_task(path, f"{folder}/Animations", f"A_{species}_{clip}", _fbx_options("anim", args.scale, skeleton))))
    nc.import_files([t for _, t in tasks])
    sequences = {}
    for clip, _task in tasks:
        sequence = nc.load_asset(f"{folder}/Animations/A_{species}_{clip}")
        if sequence:
            nc.step(f"A_{species}_{clip}", lambda sequence=sequence, clip=clip: configure_sequence(sequence, clip, clips_info.get(clip) or {}))
            sequences[clip] = sequence
    if metadata and metadata.get("root_motion"):
        nc.info(f"    {metadata['root_motion']}")

    # Textures and material instances (one per mesh material slot).
    master = ensure_master_material(False)
    picked = pick_textures(inputs["textures"], species)
    tex_tasks = []
    for slot, maps in picked.items():
        for map_name, (path, flip_green) in maps.items():
            name = f"T_{species}_{slot}_{map_name}" if slot != "body" else f"T_{species}_{map_name}"
            tex_tasks.append((slot, map_name, name, flip_green, nc.make_import_task(path, f"{folder}/Textures", name)))
    nc.import_files([t[-1] for t in tex_tasks])
    textures = {}
    for slot, map_name, name, flip_green, _task in tex_tasks:
        texture = nc.load_asset(f"{folder}/Textures/{name}")
        if texture:
            _configure_texture(texture, map_name, flip_green)
            textures.setdefault(slot, {})[map_name] = texture

    slot_names = [str(s.get_editor_property("material_slot_name")) for s in mesh.get_editor_property("materials")]
    def slot_of(slot_name: str) -> str:
        lowered = slot_name.lower()
        for token, slot in (("eye", "eye"), ("claw", "claws"), ("teeth", "teeth"), ("tooth", "teeth"), ("body", "body"), ("skin", "body")):
            if token in lowered:
                return slot
        return "body"
    wanted_slots = sorted({slot_of(n) for n in slot_names} | set(textures)) or ["body"]
    materials = {}
    for slot in wanted_slots:
        mi_path = f"{folder}/MI_{species}_{slot}"
        # Slots without textures of their own (factory claws / teeth; the eye has only Eye_BaseColor) keep the master's flat
        # defaults for the missing maps and get their look from Tint, RoughnessScale and the vertex colours.
        maps = dict(textures.get(slot) or {})
        defaults = SLOT_DEFAULTS.get(slot, SLOT_DEFAULTS["body"])

        def configure(mi, maps=maps, defaults=defaults):
            mel = unreal.MaterialEditingLibrary
            mel.set_material_instance_parent(mi, master)
            for map_name, texture in maps.items():
                if map_name in MASTER_MAPS:
                    mel.set_material_instance_texture_parameter_value(mi, map_name, texture)
            for name in ("SubsurfaceAmount", "RoughnessScale", "VertexColorAmount"):
                mel.set_material_instance_scalar_parameter_value(mi, name, float(defaults[name]))
            tint = defaults["Tint"]
            mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        materials[slot] = nc.create_asset(mi_path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew(), configure, overwrite=True)

    # Bones: factory metadata, else the anatomy spec, else the imported skeleton.
    bones = _bone_names_from_metadata(metadata) or _bone_names_from_anatomy(inputs["anatomy"])
    if not bones:
        bones = nc.step(f"{species} : os du squelette", lambda: _bone_names_from_mesh(mesh)) or []
    mapping = bone_map(bones)

    anim_class = nc.step(f"ABP_{species}", lambda: create_anim_blueprint(species, folder, skeleton))
    nc.step(f"IK_{species}", lambda: create_ik_rig(species, folder, mesh, bones, metadata))

    # Length (snout to tail tip at scale 1): measured on the sculpt by the factory, else the anatomy target, else bounds.
    extent = mesh.get_imported_bounds().box_extent
    bounds_length_m = 2.0 * max(extent.x, extent.y) / 100.0
    model_length_m = float((((metadata or {}).get("sizes") or {}).get("model") or {}).get("length_m") or 0.0)
    anatomy_doc = nc.load_json(inputs["anatomy"]) if inputs["anatomy"] else None
    spec_length_m = float(((anatomy_doc or {}).get("targets") or {}).get("total_length_m") or 0.0)
    mesh_length_m = model_length_m or spec_length_m or bounds_length_m
    reference = model_length_m or spec_length_m
    if reference > 0.0 and not (0.8 * reference <= bounds_length_m <= 1.25 * reference):
        nc.warn(f"{species} : boîte englobante {bounds_length_m:.2f} m pour une longueur de modèle de {reference:.2f} m — échelle d'import ?")
    adult_m = float(((doc or {}).get("sim") or {}).get("adult_length_m") or 0.0)
    if adult_m > 0.0 and not (0.5 * adult_m <= mesh_length_m <= 2.0 * adult_m):
        nc.warn(f"{species} : maillage de {mesh_length_m:.2f} m pour une longueur adulte simulée de {adult_m:.2f} m — "
                f"échelle d'import à vérifier (--scale {100.0 * args.scale if mesh_length_m < adult_m else args.scale / 100.0:g}).")
    yaw = mesh_yaw_offset(metadata, mesh, skeleton, mapping.get("head_bone") or "", mapping.get("pelvis_bone") or "")

    # Visual data asset.
    def configure_da(da):
        nc.set_props(da, species_id=unreal.Name(species), skeletal_mesh=mesh, mesh_body_length_m=max(0.05, mesh_length_m),
                     mesh_yaw_offset_deg=yaw)
        if anim_class:
            nc.set_props(da, anim_class=anim_class)
        overrides = [materials.get(slot_of(n)) or materials.get("body") for n in slot_names]
        if overrides and any(overrides):
            nc.set_props(da, material_overrides=overrides)
        for key, value in mapping.items():
            if value:
                nc.set_props(da, **{key: (unreal.Name(value) if isinstance(value, str) else [unreal.Name(v) for v in value])})
        if sequences:
            # VERIFY: a Python dict converts to TMap<FName, FNoctisAnimClip> through set_editor_property.
            nc.set_props(da, clips={unreal.Name(clip): clip_struct(seq, clips_info.get(clip) or {}) for clip, seq in sequences.items()})
        # Small animals are never worth a full actor far away.
        if adult_m > 0.0:
            nc.set_props(da, max_actor_distance_m=float(min(600.0, max(60.0, adult_m * 60.0))))
        link_call_sounds(da, species, doc)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.NoctisSpeciesVisualData.static_class())
    nc.create_asset(f"{folder}/DA_{species}", unreal.NoctisSpeciesVisualData, factory, configure_da, overwrite=True)
    walk = clips_info.get("walk") or {}
    nc.info(f"{species} : DA_{species} prêt — {len(materials)} matériau(x) {sorted(materials)}, {len(sequences)} clip(s), "
            f"longueur {mesh_length_m:.2f} m, lacet {yaw:+.0f}°, pieds {mapping['foot_bones']}"
            + (f", marche {walk.get('speed_ms')} m/s / {walk.get('period_s')} s par foulée" if walk else "") + ".")
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
    parser.add_argument("--resolution", choices=("4k", "2k"), default="4k", help="jeu de textures de la fabrique (4k = hero)")
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
