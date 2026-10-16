# -*- coding: utf-8 -*-
"""Creates the project assets the C++ code refers to by path, and the main map.

Usage (editor: Output Log, "Python" command line, or File > Execute Python Script):
    py "Content/Python/setup_project.py"                 # everything that is missing
    py "Content/Python/setup_project.py" --overwrite     # re-apply settings to existing assets
    py "Content/Python/setup_project.py" --sync-data     # also copy Data/ to Content/NoctisData (packaging option)
    py "Content/Python/setup_project.py" --only input,mpc,map
Headless:
    UnrealEditor-Cmd <Project>/Noctis.uproject -run=pythonscript -script="Content/Python/setup_project.py"

Created (paths are the ones hard-coded in Source/Noctis and Config/*.ini):
    /Game/Input/IA_*, IMC_Researcher, IMC_Vehicle, IMC_Drone, IMC_Tools, IMC_UI       (Enhanced Input)
    /Game/Environment/MPC_NoctisWeather                                               (weather + vegetation interaction)
    /Game/Environment/Vegetation/RT_NoctisTrails                                      (trail / trampling / burn mask)
    /Game/Environment/Water/M_NoctisWater                                             (placeholder river material)
    /Game/UI/WBP_NoctisHUD, WBP_FieldJournal, WBP_FieldMap, WBP_Spectrogram, WBP_Hypotheses, WBP_Reports
    /Game/Maps/HellCreek (empty World Partition map, not the Open World template and its sample landscape) with sky,
    fog, clouds, moon, wind and a
    PlayerStart at the simulation's base camp.
Audio assets come from import_audio.py, creature assets from import_creatures.py.

Not automatable here: the Landscape import (Python cannot drive Landscape > Import). The script prints the exact
values to type, computed by the simulation core itself (unreal.NoctisEditorLibrary.compute_terrain_info).
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys

import unreal

import noctis_ue_common as nc

INPUT_DIR = "/Game/Input"
MAP_PATH = "/Game/Maps/HellCreek"

# name -> value type. Must match the soft paths in Source/Noctis/Private/Research/*.cpp.
INPUT_ACTIONS = {
    "IA_Move": "AXIS2D",
    "IA_Look": "AXIS2D",
    "IA_Crouch": "BOOLEAN",
    "IA_Talk": "BOOLEAN",
    "IA_Headlamp": "BOOLEAN",
    "IA_Interact": "BOOLEAN",
    "IA_UseTool": "BOOLEAN",
    "IA_NextTool": "BOOLEAN",
    "IA_Zoom": "AXIS1D",
    "IA_Throttle": "AXIS1D",
    "IA_Brake": "AXIS1D",
    "IA_Steer": "AXIS1D",
    "IA_Engine": "BOOLEAN",
    "IA_Generator": "BOOLEAN",
    "IA_Lights": "BOOLEAN",
    "IA_Drone": "BOOLEAN",
    "IA_DroneAltitude": "AXIS1D",
    "IA_DroneOrbit": "BOOLEAN",
    "IA_DroneFollow": "BOOLEAN",
    "IA_DroneReturn": "BOOLEAN",
    # UI context (bound by the HUD Widget Blueprint).
    "IA_Journal": "BOOLEAN",
    "IA_Map": "BOOLEAN",
    "IA_Spectrogram": "BOOLEAN",
    "IA_Hypotheses": "BOOLEAN",
    "IA_Reports": "BOOLEAN",
    "IA_Pause": "BOOLEAN",
    "IA_TimeFaster": "BOOLEAN",
    "IA_TimeSlower": "BOOLEAN",
}

# Mapping contexts: (action, key, modifiers). Modifiers: "swizzle" (YXZ), "negate", "negate_y".
# Keyboard/mouse first, then gamepad. AZERTY players remap in the game settings (Enhanced Input user settings).
INPUT_CONTEXTS = {
    "IMC_Researcher": [
        ("IA_Move", "W", ["swizzle"]), ("IA_Move", "S", ["swizzle", "negate"]), ("IA_Move", "A", ["negate"]), ("IA_Move", "D", []),
        ("IA_Move", "Gamepad_Left2D", []),
        ("IA_Look", "Mouse2D", ["negate_y"]), ("IA_Look", "Gamepad_Right2D", ["negate_y"]),
        ("IA_Crouch", "C", []), ("IA_Crouch", "Gamepad_FaceButton_Right", []),
        ("IA_Talk", "V", []),
        ("IA_Headlamp", "L", []), ("IA_Headlamp", "Gamepad_DPad_Up", []),
        ("IA_Interact", "E", []), ("IA_Interact", "Gamepad_FaceButton_Left", []),
    ],
    "IMC_Tools": [
        ("IA_UseTool", "LeftMouseButton", []), ("IA_UseTool", "Gamepad_RightTrigger", []),
        ("IA_NextTool", "Tab", []), ("IA_NextTool", "Gamepad_RightShoulder", []),
        ("IA_Zoom", "MouseWheelAxis", []),
    ],
    "IMC_Vehicle": [
        ("IA_Throttle", "W", []), ("IA_Throttle", "S", ["negate"]), ("IA_Throttle", "Gamepad_RightTriggerAxis", []),
        ("IA_Brake", "SpaceBar", []), ("IA_Brake", "Gamepad_LeftTriggerAxis", []),
        ("IA_Steer", "D", []), ("IA_Steer", "A", ["negate"]), ("IA_Steer", "Gamepad_LeftX", []),
        ("IA_Engine", "I", []), ("IA_Generator", "G", []), ("IA_Lights", "L", []),
        ("IA_Interact", "E", []), ("IA_Interact", "Gamepad_FaceButton_Left", []),
        ("IA_Drone", "F", []),
        ("IA_Look", "Mouse2D", ["negate_y"]), ("IA_Look", "Gamepad_Right2D", ["negate_y"]),
    ],
    "IMC_Drone": [
        ("IA_Move", "W", ["swizzle"]), ("IA_Move", "S", ["swizzle", "negate"]), ("IA_Move", "A", ["negate"]), ("IA_Move", "D", []),
        ("IA_Move", "Gamepad_Left2D", []),
        ("IA_DroneAltitude", "SpaceBar", []), ("IA_DroneAltitude", "LeftControl", ["negate"]),
        ("IA_DroneAltitude", "Gamepad_RightTriggerAxis", []), ("IA_DroneAltitude", "Gamepad_LeftTriggerAxis", ["negate"]),
        ("IA_Look", "Mouse2D", ["negate_y"]), ("IA_Look", "Gamepad_Right2D", ["negate_y"]),
        ("IA_DroneOrbit", "O", []), ("IA_DroneFollow", "P", []), ("IA_DroneReturn", "R", []),
        ("IA_UseTool", "LeftMouseButton", []), ("IA_UseTool", "Gamepad_RightShoulder", []),
        ("IA_Interact", "E", []), ("IA_Interact", "Gamepad_FaceButton_Left", []),
    ],
    "IMC_UI": [
        ("IA_Journal", "J", []), ("IA_Map", "M", []), ("IA_Spectrogram", "K", []), ("IA_Hypotheses", "H", []), ("IA_Reports", "N", []),
        ("IA_Pause", "Escape", []), ("IA_Pause", "Gamepad_Special_Right", []),
        ("IA_TimeFaster", "Add", []), ("IA_TimeSlower", "Subtract", []),
    ],
}

# MPC_NoctisWeather: names written by ANoctisEnvironmentController and ANoctisVegetationInteraction.
MPC_SCALARS = {
    "CloudCover": 0.3, "RainRate": 0.0, "GroundWetness": 0.0, "WindSpeed": 2.0, "FogExtinction": 0.0, "Illuminance": 50000.0,
    "SunElevation": 30.0, "Temperature": 20.0, "Humidity": 0.6, "LightningFlash": 0.0, "RiverStage": 1.0, "Flooding": 0.0,
    "HourOfDay": 12.0, "InteractorCount": 0.0,
}
MPC_VECTORS = {"WindDirection": (1.0, 0.0, 0.0, 0.0), "TrailMaskRect": (0.0, 0.0, 1.0, 1.0)}
MPC_VECTORS.update({f"Interactor{i}": (0.0, 0.0, -1e7, 0.0) for i in range(32)})

WIDGETS = {
    "WBP_NoctisHUD": "UserWidget",
    "WBP_FieldJournal": "NoctisFieldJournalWidget",
    "WBP_FieldMap": "NoctisFieldMapWidget",
    "WBP_Spectrogram": "NoctisSpectrogramWidget",
    "WBP_Hypotheses": "NoctisHypothesisPanelWidget",
    "WBP_Reports": "NoctisReportViewerWidget",
}


# ----------------------------------------------------------------------------------------------- input

def _modifier_objects(owner, names):
    result = []
    for name in names:
        if name == "swizzle":
            mod = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=owner)
            nc.set_props(mod, order=unreal.InputAxisSwizzle.YXZ)
        elif name == "negate":
            mod = unreal.new_object(unreal.InputModifierNegate, outer=owner)
        elif name == "negate_y":
            mod = unreal.new_object(unreal.InputModifierNegate, outer=owner)
            nc.set_props(mod, x=False, y=True, z=False)
        else:
            continue
        result.append(mod)
    return result


def create_input(overwrite: bool) -> None:
    actions = {}
    for name, value_type in INPUT_ACTIONS.items():
        def configure(asset, value_type=value_type):
            nc.set_props(asset, value_type=getattr(unreal.InputActionValueType, value_type))
        actions[name] = nc.step(f"{name}", lambda name=name, configure=configure: nc.create_asset(
            f"{INPUT_DIR}/{name}", unreal.InputAction, unreal.InputAction_Factory(), configure, overwrite))

    for context_name, bindings in INPUT_CONTEXTS.items():
        def configure(imc, bindings=bindings):
            # VERIFY 5.5: the mapping array is the protected UPROPERTY "Mappings" (5.6 moved it to DefaultKeyMappings).
            try:
                imc.unmap_all()
            except Exception:  # noqa: BLE001
                pass
            for action_name, key_name, _mods in bindings:
                action = actions.get(action_name)
                if action:
                    imc.map_key(action, nc.make_key(key_name))
            mappings = imc.get_editor_property("mappings")
            for index, (action_name, key_name, mods) in enumerate(bindings):
                if index < len(mappings) and mods:
                    mapping = mappings[index]
                    mapping.set_editor_property("modifiers", _modifier_objects(imc, mods))
                    mappings[index] = mapping
            imc.set_editor_property("mappings", mappings)
        nc.step(context_name, lambda context_name=context_name, configure=configure: nc.create_asset(
            f"{INPUT_DIR}/{context_name}", unreal.InputMappingContext, unreal.InputMappingContext_Factory(), configure, overwrite))
    nc.info(f"Enhanced Input : {len(INPUT_ACTIONS)} actions, {len(INPUT_CONTEXTS)} contextes dans {INPUT_DIR}.")


# ----------------------------------------------------------------------------------------------- environment assets

def create_mpc(overwrite: bool) -> None:
    def configure(mpc):
        scalars = []
        for name, default in MPC_SCALARS.items():
            p = unreal.CollectionScalarParameter()
            p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", float(default))
            scalars.append(p)
        vectors = []
        for name, (r, g, b, a) in MPC_VECTORS.items():
            p = unreal.CollectionVectorParameter()
            p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", unreal.LinearColor(r, g, b, a))
            vectors.append(p)
        mpc.set_editor_property("scalar_parameters", scalars)
        mpc.set_editor_property("vector_parameters", vectors)
    nc.step("MPC_NoctisWeather", lambda: nc.create_asset("/Game/Environment/MPC_NoctisWeather", unreal.MaterialParameterCollection,
                                                         unreal.MaterialParameterCollectionFactoryNew(), configure, overwrite))


def create_trail_target(overwrite: bool) -> None:
    def configure(rt):
        # R = trail wear, G = recent trampling, B = burnt (ANoctisVegetationInteraction::UpdateMask). Linear data.
        nc.set_props(rt, size_x=512, size_y=512, render_target_format=unreal.TextureRenderTargetFormat.RTF_RGBA8,
                     clear_color=unreal.LinearColor(0.0, 0.0, 0.0, 1.0), address_x=unreal.TextureAddress.TA_CLAMP,
                     address_y=unreal.TextureAddress.TA_CLAMP)
    nc.step("RT_NoctisTrails", lambda: nc.create_asset("/Game/Environment/Vegetation/RT_NoctisTrails", unreal.TextureRenderTarget2D,
                                                       unreal.TextureRenderTargetFactoryNew(), configure, overwrite))


def create_water_material(overwrite: bool) -> None:
    """Placeholder river material: vertex colour = (flow x, flow y, depth/5 m) from ANoctisWaterActor.
    Replace it with a Single Layer Water material for shipping visuals (keep the vertex colour inputs)."""
    mel = unreal.MaterialEditingLibrary

    def configure(material):
        mel.delete_all_material_expressions(material)
        nc.set_props(material, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT,
                     translucency_lighting_mode=unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING, two_sided=False)
        vcol = mel.create_material_expression(material, unreal.MaterialExpressionVertexColor, -900, 0)
        shallow = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -250)
        shallow.set_editor_property("parameter_name", "ShallowColor")
        shallow.set_editor_property("default_value", unreal.LinearColor(0.20, 0.22, 0.14, 1.0))  # silty Cretaceous river
        deep = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -450)
        deep.set_editor_property("parameter_name", "DeepColor")
        deep.set_editor_property("default_value", unreal.LinearColor(0.04, 0.06, 0.04, 1.0))
        lerp = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -500, -300)
        mel.connect_material_expressions(shallow, "", lerp, "A")
        mel.connect_material_expressions(deep, "", lerp, "B")
        mel.connect_material_expressions(vcol, "B", lerp, "Alpha")
        mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
        opacity = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -500, 100)
        lo = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -800, 200)
        lo.set_editor_property("r", 0.55)
        hi = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -800, 300)
        hi.set_editor_property("r", 0.95)
        mel.connect_material_expressions(lo, "", opacity, "A")
        mel.connect_material_expressions(hi, "", opacity, "B")
        mel.connect_material_expressions(vcol, "B", opacity, "Alpha")
        mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 400)
        rough.set_editor_property("r", 0.06)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(material)

    nc.step("M_NoctisWater", lambda: nc.create_asset("/Game/Environment/Water/M_NoctisWater", unreal.Material, unreal.MaterialFactoryNew(),
                                                     configure, overwrite))


# ----------------------------------------------------------------------------------------------- UI

def create_widgets(overwrite: bool) -> None:
    for name, parent in WIDGETS.items():
        def make(name=name, parent=parent):
            factory = unreal.WidgetBlueprintFactory()
            parent_class = getattr(unreal, parent).static_class()
            # VERIFY: WidgetBlueprintFactory exposes "parent_class" (UBlueprintFactory::ParentClass) to Python.
            factory.set_editor_property("parent_class", parent_class)
            return nc.create_asset(f"/Game/UI/{name}", unreal.WidgetBlueprint, factory, None, overwrite)
        nc.step(name, make)
    nc.info("Widgets créés vides : la mise en page (BindWidgetOptional : SpectrogramImage, ReportText...) reste à faire dans le Designer UMG.")


# ----------------------------------------------------------------------------------------------- map

def _find_actor(actor_class, tag: str = ""):
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in subsystem.get_all_level_actors():
        if isinstance(actor, actor_class) and (not tag or actor.actor_has_tag(tag)):
            return actor
    return None


def _spawn(actor_class, location, rotation=None, label: str = ""):
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(actor_class, location, rotation or unreal.Rotator(0.0, 0.0, 0.0))
    if actor and label:
        actor.set_actor_label(label)
    return actor


def create_map(overwrite: bool) -> None:
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH) and not overwrite:
        level_subsystem.load_level(MAP_PATH)
        nc.info(f"{MAP_PATH} existe : ouverture et complément des acteurs manquants.")
    else:
        nc.ensure_directory("/Game/Maps")
        # Empty partitioned world ("Empty Open World"), NOT the "Open World" template: that template ships a sample
        # landscape which build_level.py would otherwise find instead of the Noctis terrain. Sun, moon, sky, fog and
        # clouds are spawned below when missing, so nothing from the template is needed.
        created = False
        try:
            created = level_subsystem.new_level(MAP_PATH, True)  # VERIFY: is_partitioned_world argument of NewLevel (UE 5.1+)
        except TypeError:
            created = False
        if not created:
            nc.warn("new_level(chemin, is_partitioned_world=True) indisponible : niveau vide non partitionné créé ; activer World Partition "
                    "(Tools > Convert Level). Ne pas partir du gabarit Open World : son paysage d'exemple n'est pas le terrain Noctis.")
            level_subsystem.new_level(MAP_PATH)

    info = None
    try:
        # Python returns (success, out_info) for a bool UFUNCTION with an out parameter; some versions return only the
        # out struct, or None on failure: accept the three shapes.
        result = unreal.NoctisEditorLibrary.compute_terrain_info()
        if isinstance(result, tuple):
            info = result[-1] if result and result[0] else None
        else:
            info = result
    except Exception as exc:  # noqa: BLE001
        nc.warn(f"compute_terrain_info indisponible ({exc}) : le module NoctisEditor est-il compilé ?")

    # Sun (ANoctisEnvironmentController takes the first untagged directional light), moon (tag "Moon"), sky, fog, clouds.
    if not _find_actor(unreal.DirectionalLight):
        _spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 50000), label="Sun")
    if not _find_actor(unreal.DirectionalLight, "Moon"):
        moon = _spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 50000), label="Moon")
        if moon:
            moon.set_editor_property("tags", [unreal.Name("Moon")])
            moon.get_editor_property("light_component").set_editor_property("intensity", 0.0)
    for cls, label in ((unreal.SkyAtmosphere, "SkyAtmosphere"), (unreal.SkyLight, "SkyLight"), (unreal.ExponentialHeightFog, "HeightFog"),
                       (unreal.VolumetricCloud, "VolumetricCloud"), (unreal.WindDirectionalSource, "Wind")):
        if not _find_actor(cls):
            _spawn(cls, unreal.Vector(0, 0, 0), label=label)
    sky_light = _find_actor(unreal.SkyLight)
    if sky_light:
        # Real-time capture follows the simulated sky (dawn, overcast, night).
        component = sky_light.get_editor_property("light_component")
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        nc.set_props(component, real_time_capture=True)

    # Presentation actors are spawned at runtime when absent (bAutoSpawnPresentationActors); placing the creature manager
    # lets designers tune it per map.
    if not _find_actor(unreal.NoctisCreatureManager):
        _spawn(unreal.NoctisCreatureManager, unreal.Vector(0, 0, 0), label="NoctisCreatureManager")
    if not _find_actor(unreal.NoctisEnvironmentController):
        _spawn(unreal.NoctisEnvironmentController, unreal.Vector(0, 0, 0), label="NoctisEnvironment")

    start = _find_actor(unreal.PlayerStart)
    if info is not None:
        camp = info.basecamp_location
        location = unreal.Vector(camp.x, camp.y, camp.z + 200.0)
        if start is None:
            start = _spawn(unreal.PlayerStart, location, label="PlayerStart_Basecamp")
        else:
            start.set_actor_location(location, False, False)
        fog = _find_actor(unreal.ExponentialHeightFog)
        if fog:
            fog.set_actor_location(unreal.Vector(camp.x, camp.y, info.valley_floor_z), False, False)
        nc.info("Landscape — Mode Landscape > Manage > Import from File, fichier Build/Terrain/heightmap.png "
                "(noctis_sim export-terrain --out Build/Terrain --env {env} --seed <Seed>) :".format(env=info.environment_id))
        nc.info(f"    résolution {info.samples} x {info.samples}, "
                f"Location = ({info.landscape_location.x:.1f}, {info.landscape_location.y:.1f}, {info.landscape_location.z:.1f}) cm, "
                f"Scale = ({info.landscape_scale.x:.3f}, {info.landscape_scale.y:.3f}, {info.landscape_scale.z:.4f}), "
                f"relief {info.min_height_m:.1f} .. {info.max_height_m:.1f} m.")
        nc.info("    Couches de peinture : Build/Terrain/weight_<habitat>.png ; densités de végétation : foliage_<type>.png (graphe PCG).")
    elif start is None:
        _spawn(unreal.PlayerStart, unreal.Vector(0, 0, 10000), label="PlayerStart")
        nc.warn("PlayerStart placé à l'origine : relancer après compilation du module NoctisEditor pour le placer au camp de base.")

    level_subsystem.save_current_level()
    nc.info(f"Carte {MAP_PATH} prête (GameDefaultMap / EditorStartupMap dans DefaultEngine.ini).")


# ----------------------------------------------------------------------------------------------- data copy

def sync_data() -> None:
    source = nc.repo_path("Data")
    target = os.path.join(nc.project_dir(), "Content", "NoctisData")
    if not os.path.isdir(os.path.join(source, "Species")):
        raise RuntimeError(f"{source} ne contient pas Species/")
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns("*.md", "Sources"))
    nc.info(f"Data/ copié dans {target} (empaqueté en fichiers lisibles : DirectoriesToAlwaysStageAsNonUFS=NoctisData).")


# ----------------------------------------------------------------------------------------------- main

def main(argv) -> int:
    parser = argparse.ArgumentParser(prog="setup_project.py")
    parser.add_argument("--overwrite", action="store_true", help="re-applique les réglages aux assets existants")
    parser.add_argument("--sync-data", action="store_true", help="copie Data/ dans Content/NoctisData")
    parser.add_argument("--only", default="", help="liste parmi input,mpc,rt,water,ui,map")
    args = parser.parse_args(argv)
    only = {s.strip() for s in args.only.split(",") if s.strip()}

    def wanted(key: str) -> bool:
        return not only or key in only

    nc.info(f"Projet : {nc.project_dir()} ; données : {nc.data_root()}")
    if wanted("input"):
        create_input(args.overwrite)
    if wanted("mpc"):
        create_mpc(args.overwrite)
    if wanted("rt"):
        create_trail_target(args.overwrite)
    if wanted("water"):
        create_water_material(args.overwrite)
    if wanted("ui"):
        create_widgets(args.overwrite)
    if wanted("map"):
        nc.step("carte", lambda: create_map(args.overwrite))
    if args.sync_data:
        nc.step("copie de Data/", sync_data)
    return nc.summary("setup_project")


if __name__ == "__main__":
    main(sys.argv[1:])
