"""Noctis — build the Hell Creek vertical-slice level in the Unreal Editor (UE 5.4+, Python Editor Script Plugin).

What it does (each stage is logged; every stage can be re-run, generated actors are replaced, never duplicated):

  1. level          open /Game/Maps/HellCreek, or create it as an EMPTY World Partition map (new_level(path, True)),
                    never from the "Open World" template whose sample landscape would be taken for the Noctis terrain
  2. textures       import the export-terrain weightmaps / foliage density masks and the export-level gameplay masks
                    as linear, uncompressed-enough, mip-less Texture2D assets (PCG and gameplay sample them)
  3. landscape      import heightmap.png as a Landscape with the exact scale / location below, or verify an existing
                    one: a landscape that is not the Noctis one (no 'noctis.landscape' tag and wrong component count /
                    extent, e.g. a template landscape) stops the stage with an error, and a Noctis landscape whose
                    transform differs is never moved or rescaled (World Partition streaming proxies would not follow
                    and a wrong import cannot be repaired by scaling): the stage stops with reimport instructions
                    (Unreal 5.4 has no stable Python call that creates a Landscape from a file, so a project helper is
                    used when present, otherwise exact manual steps)
  4. partition      check World Partition, runtime grid (manual step when the property is not scriptable), create
                    the editor data layers used by the level
  5. pois           place editor-only POI markers (placeholder meshes + labels) from Data/Levels/<level>.json and
                    assign them to their editor data layer
  6. trails         predicted game corridors as spline actors (or marker chains when no spline Blueprint exists)
  7. pcg            PCG volumes (partitioned) for foliage, trails, exposures and river debris, driven by the masks
  8. start          PlayerStart at the base camp (the only POI the player knows at the start)
  9. save           save dirty packages and write the build log

Inputs (produced by the headless runner, same seed / profile as 'run'):
  noctis_sim export-terrain --out Build/Terrain       -> heightmap.png/.r16, terrain.json, weight_*.png, foliage_*.png
  noctis_sim export-level                              -> Data/Levels/hell_creek_valley.json, Output/level/mask_*.png
  (Build/Terrain is the folder setup_project.py documents; Output/terrain is accepted as a fallback.)

Order of the editor scripts: setup_project.py first (map, sky, fog, lights, creature manager, input assets), then this
script for the level content. It is standalone on purpose (it does not import noctis_ue_common, which needs the
'unreal' module) so that --dry-run works outside the editor; when the NoctisEditor module is compiled,
NoctisEditorLibrary.compute_terrain_info() is used to cross-check the landscape transform computed here.

Run inside the editor:   py "Content/Python/build_level.py" [--terrain-dir DIR] [--level-json FILE] [--masks-dir DIR]
                         [--map /Game/Maps/HellCreek] [--skip pcg,trails] [--generate-pcg]
Run outside Unreal:      python Content/Python/build_level.py --dry-run   (no 'unreal' module: parses inputs, prints the plan)

Coordinate frames (must match FNoctisSpace in Source/Noctis/Public/NoctisSpace.h):
  core   : x = east, y = north, z = up, metres; heading radians, 0 = east, counter-clockwise
  Unreal : X = east, Y = south, Z = up, centimetres; yaw degrees, clockwise seen from above
  X_cm = x * 100 + off.X ;  Y_cm = -y * 100 + off.Y ;  Z_cm = z * 100 + off.Z ;  yaw = -heading
  off = UNoctisSettings::WorldOffsetCm (0 by default). The level JSON already carries ue_location_cm for every POI.

Landscape import math (values come from terrain.json written by export-terrain):
  samples N = 2017 -> quads N - 1 = 2016 = 16 components x (2 x 2 sections of 63 quads) = 16 x 126
  XY scale (cm per quad)  = spacing_m * 100 = size_m / (N - 1) * 100 = 8192 / 2016 * 100 = 406.349 cm
  Unreal stores heights as uint16 v with   Z_world = Location.Z + Scale.Z * (v - 32768) / 128   (cm, LANDSCAPE_ZSCALE = 1/128)
  export-terrain normalises v = (h - min) / (max - min) * 65535, so choosing
      Scale.Z    = (max - min) * 100 / 512           (cm; 65535 / 128 * Scale.Z = (max - min) * 100)
      Location.Z = (min + (max - min) / 2) * 100     (cm; v = 32768 maps to the middle of the range)
  reproduces the simulation heights to (max - min) / 65535 m (0.4 mm for 25 m of relief).
  Heightmap row 0 is the NORTH edge (sim y = size) = most negative Unreal Y, so
      Location.XY = (0 + off.X, -size_m * 100 + off.Y)    -> the landscape covers X in [0, size], Y in [-size, 0] (x100 cm)

Expected PCG graphs (create them once in /Game/Noctis/PCG; this script only assigns them and tags the volumes):
  PCG_Noctis_Foliage    Get Landscape Data -> Surface Sampler (density per plant functional type) -> per PFT branch:
                        Get Texture Data (T_Foliage<Pft>, e.g. T_FoliageAngioTree; transform = landscape bounds, U -> +X,
                        V -> +Y, linear, no sRGB) -> Attribute "Density" * texture R -> Density Filter -> Self Pruning
                        (radius per PFT) -> Difference with exclusions (T_WeightChannel, T_WeightOxbowLake, POI clearings tagged
                        noctis.clearing) -> Transform Points (random yaw / scale) -> Static Mesh Spawner (placeholder meshes)
  PCG_Noctis_Trails     Get Spline Data (actors tagged noctis.trail) -> Spline Sampler (2 m) -> width from the spline
                        tag "wear" -> Difference against foliage points -> ground-cover / decal placeholders
  PCG_Noctis_Exposures  T_WeightCutbankCliff (weight_cutbank_cliff.png) and the exposure markers -> points along the
                        cutbank edge (Get Texture Data -> threshold 0.5 -> edge detection / Spline from the mask border)
                        -> Static Mesh Spawner of blocking cliff / strata placeholder meshes (collision preset
                        BlockAll, yaw aligned to the edge, face towards the channel; the 4.06 m landscape turns the
                        vertical cutbank into a 30-51 deg ramp, so the mesh IS the wall), plus a thin walkable bench
                        mesh at the face foot (metrics.access_point of each exposure). Default WalkableFloorAngle
                        44.77 deg: banks are reached only at the creek fords, point bars and watering points.
  PCG_Noctis_RiverDebris T_WeightSandbar + T_MaskFloodRisk -> driftwood / gravel placeholders on bars and flood lines
  Texture asset names: <file>.png -> T_<CamelCase> (weight_sandbar.png -> T_WeightSandbar), imported under
  /Game/Noctis/Level/Textures/Terrain (export-terrain) and /Game/Noctis/Level/Textures/Level (export-level masks).
  Image row 0 is the north edge = Unreal Y = -size: with U -> +X and V -> +Y over the landscape bounds no flip is needed.
  One volume per graph covers the whole landscape (8192 m); generation is partitioned (PCG partition grid 256 m) and
  done on demand in the editor (baked, not runtime). This matches world_partition.pcg in the level JSON.

Lines marked  # VERIFY  use Unreal Python API that could not be checked against a running 5.4 editor when this script
was written (no engine available in the authoring environment). Each of them is wrapped so that a failure is logged
with the manual fallback and the remaining stages still run.
"""

import argparse
import datetime
import json
import math
import os
import sys

try:
    import unreal  # noqa: F401  (only available inside the Unreal Editor)

    IN_UNREAL = True
except ImportError:  # dry run outside the editor
    unreal = None
    IN_UNREAL = False

GENERATED_TAG = "noctis.generated"
POI_FOLDER = "Noctis/POI"
TRAIL_FOLDER = "Noctis/Trails"
PCG_FOLDER = "Noctis/PCG"
TEXTURE_ROOT = "/Game/Noctis/Level/Textures"
PCG_ROOT = "/Game/Noctis/PCG"
TRAIL_BLUEPRINT = "/Game/Noctis/Level/BP_NoctisTrail"
LANDSCAPE_MATERIAL = "/Game/Noctis/Landscape/M_NoctisLandscape"
LANDSCAPE_TAG = "noctis.landscape"  # set on the ALandscape actor once it is known to be the Noctis import

# Placeholder mesh and marker size per POI type (meshes from /Engine/BasicShapes, 100 cm unit shapes).
MARKERS = {
    "basecamp": ("/Engine/BasicShapes/Cube.Cube", (3.0, 3.0, 2.0)),
    "flood_refuge": ("/Engine/BasicShapes/Cube.Cube", (2.0, 2.0, 1.0)),
    "overlook": ("/Engine/BasicShapes/Cone.Cone", (1.5, 1.5, 3.0)),
    "blind": ("/Engine/BasicShapes/Cube.Cube", (2.0, 2.0, 1.6)),
    "ford": ("/Engine/BasicShapes/Cylinder.Cylinder", (4.0, 4.0, 0.2)),
    "water_access": ("/Engine/BasicShapes/Cylinder.Cylinder", (3.0, 3.0, 0.2)),
    "oxbow_lake": ("/Engine/BasicShapes/Cylinder.Cylinder", (6.0, 6.0, 0.1)),
    "pond": ("/Engine/BasicShapes/Cylinder.Cylinder", (6.0, 6.0, 0.1)),
    "backswamp": ("/Engine/BasicShapes/Cylinder.Cylinder", (6.0, 6.0, 0.1)),
    "exposure_site": ("/Engine/BasicShapes/Cube.Cube", (3.0, 1.0, 3.0)),
    "nesting_area": ("/Engine/BasicShapes/Cylinder.Cylinder", (1.0, 1.0, 0.05)),  # scaled to the zone radius
    "carcass_site": ("/Engine/BasicShapes/Sphere.Sphere", (2.0, 2.0, 1.0)),
    "camera_trap": ("/Engine/BasicShapes/Cube.Cube", (0.3, 0.3, 1.2)),
    "audio_recorder": ("/Engine/BasicShapes/Cylinder.Cylinder", (0.4, 0.4, 1.5)),
    "mic_array": ("/Engine/BasicShapes/Cylinder.Cylinder", (1.0, 1.0, 0.05)),  # scaled to the array radius
}
ZONE_TYPES = {"nesting_area", "mic_array"}  # marker footprint = radius_m
MARKER_COLORS = {  # linear RGB, used only when the basic shape material exposes a "Color" parameter
    "basecamp": (1.0, 1.0, 1.0), "flood_refuge": (0.6, 0.6, 0.6), "overlook": (1.0, 0.75, 0.05),
    "blind": (0.2, 0.8, 0.15), "ford": (0.1, 0.8, 1.0), "water_access": (0.3, 0.6, 1.0), "oxbow_lake": (0.1, 0.25, 1.0),
    "pond": (0.1, 0.25, 1.0), "backswamp": (0.05, 0.6, 0.45), "exposure_site": (0.8, 0.3, 0.05),
    "nesting_area": (1.0, 0.3, 0.6), "carcass_site": (0.6, 0.02, 0.02), "camera_trap": (0.8, 0.1, 0.8),
    "audio_recorder": (0.45, 0.25, 1.0), "mic_array": (0.6, 0.5, 1.0),
}
# PCG volumes: graph asset name -> texture masks the graph samples (documentation tags on the volume).
PCG_GRAPHS = {
    "PCG_Noctis_Foliage": ["foliage_*", "weight_channel", "weight_oxbow_lake"],
    "PCG_Noctis_Trails": ["mask_corridor_use"],
    "PCG_Noctis_Exposures": ["weight_cutbank_cliff"],
    "PCG_Noctis_RiverDebris": ["weight_sandbar", "mask_flood_risk"],
}
# Editor-only data layers (world_partition.data_layers of the level JSON). There is no runtime layer: POI markers are
# editor-only, and in game the speculative nesting zones follow profile_toggle read from the JSON by the game code.
DATA_LAYERS = {
    "DL_POI_Markers": "editor-only markers of every POI except the speculative nesting areas",
    "DL_Nesting_Speculative": "editor-only markers of the SPECULATIVE nesting areas (designers can hide them)",
}
DATA_LAYER_INSTANCES = {}  # name -> UDataLayerInstance, filled by stage_partition
RUNTIME_GRID = {"name": "MainGrid", "cell_size_cm": 25600, "loading_range_cm": 102400}


# ----------------------------------------------------------------------------------------------------------- logging
class BuildLog:
    """Logs to the Unreal output log (or stdout) and keeps every line for the log file."""

    def __init__(self):
        self.lines = []
        self.warnings = 0
        self.errors = 0

    def _emit(self, level, msg):
        stamp = datetime.datetime.now().strftime("%H:%M:%S")
        line = "[NoctisBuildLevel %s] %s %s" % (stamp, level, msg)
        self.lines.append(line)
        if IN_UNREAL:
            if level == "ERROR":
                unreal.log_error(line)
            elif level == "WARN ":
                unreal.log_warning(line)
            else:
                unreal.log(line)
        else:
            print(line)

    def info(self, msg):
        self._emit("INFO ", msg)

    def warn(self, msg):
        self.warnings += 1
        self._emit("WARN ", msg)

    def error(self, msg):
        self.errors += 1
        self._emit("ERROR", msg)

    def write(self, path):
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(self.lines) + "\n")
            self.info("Journal écrit : %s" % path)
        except OSError as exc:
            self.error("Impossible d'écrire le journal %s : %s" % (path, exc))


LOG = BuildLog()


def guarded(stage):
    """Decorator: a failing stage is logged with its exception and the build continues."""

    def wrap(fn):
        def run(*args, **kwargs):
            LOG.info("=== %s ===" % stage)
            try:
                return fn(*args, **kwargs)
            except Exception as exc:  # broad on purpose: one Unreal API mismatch must not stop the whole build
                LOG.error("%s : échec (%s: %s)" % (stage, type(exc).__name__, exc))
                return None

        return run

    return wrap


# ----------------------------------------------------------------------------------------------------------- inputs
def project_dir():
    if IN_UNREAL:
        return os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def parse_args(argv):
    root = project_dir()
    p = argparse.ArgumentParser(description="Build the Noctis Hell Creek level in the Unreal Editor.")
    p.add_argument("--terrain-dir", default=None, help="export-terrain output directory (Build/Terrain, else Output/terrain)")
    p.add_argument("--level-json", default=os.path.join(root, "Data", "Levels", "hell_creek_valley.json"))
    p.add_argument("--masks-dir", default=None, help="export-level masks directory (default: masks.directory of the JSON)")
    p.add_argument("--map", default="/Game/Maps/HellCreek", help="map asset path (EditorStartupMap in DefaultEngine.ini)")
    p.add_argument("--offset-cm", default="0,0,0", help="UNoctisSettings::WorldOffsetCm as X,Y,Z")
    p.add_argument("--skip", default="", help="comma-separated stages to skip (textures,landscape,partition,pois,trails,pcg,start)")
    p.add_argument("--generate-pcg", action="store_true", help="also generate the PCG components (slow; default: assign only)")
    p.add_argument("--replace-foreign-landscape", action="store_true",
                   help="delete a landscape that is not the Noctis import (e.g. an Open World template landscape) before importing")
    p.add_argument("--dry-run", action="store_true", help="parse inputs and print the plan without touching the editor")
    args = p.parse_args(argv)
    if args.terrain_dir is None:
        args.terrain_dir = os.path.join(root, "Build", "Terrain")
        if not os.path.isdir(args.terrain_dir):
            fallback = os.path.join(root, "Output", "terrain")
            LOG.warn("Dossier export-terrain %s absent : repli sur %s%s" % (args.terrain_dir, fallback, "" if os.path.isdir(fallback) else
                     " (absent lui aussi : lancer 'noctis_sim export-terrain --out Build/Terrain')"))
            args.terrain_dir = fallback
    elif not os.path.isdir(args.terrain_dir):
        LOG.warn("Dossier export-terrain introuvable : %s" % args.terrain_dir)
    args.offset = tuple(float(v) for v in args.offset_cm.split(","))
    args.skip = set(s.strip() for s in args.skip.split(",") if s.strip())
    if not IN_UNREAL:
        args.dry_run = True
    return args


def load_inputs(args):
    with open(args.level_json, encoding="utf-8") as f:
        level = json.load(f)
    if level.get("schema") != "noctis.level/1":
        raise ValueError("schéma inattendu : %s" % level.get("schema"))
    terrain_json = os.path.join(args.terrain_dir, "terrain.json")
    terrain = None
    if os.path.exists(terrain_json):
        with open(terrain_json, encoding="utf-8") as f:
            terrain = json.load(f)
    else:
        LOG.warn("terrain.json absent (%s) : lancer 'noctis_sim export-terrain --out Build/Terrain' ; valeurs du JSON de niveau utilisées" % terrain_json)
    if args.masks_dir is None:
        masks = level.get("masks", {}).get("directory", "Output/level")
        args.masks_dir = masks if os.path.isabs(masks) else os.path.join(project_dir(), masks)
    return level, terrain


def landscape_plan(level, terrain, offset):
    """Landscape transform from terrain.json (preferred) or the level JSON frame."""
    frame = level["frame"]
    size_m = float(frame["size_m"])
    if terrain:
        samples = int(terrain["samples"])
        lo, hi = float(terrain["min_height_m"]), float(terrain["max_height_m"])
        if terrain.get("seed") is not None and int(terrain["seed"]) != int(level["generation"]["seed"]):
            LOG.warn("Graine du terrain (%s) différente de celle du niveau (%s) : POI et relief ne correspondront pas" % (terrain["seed"], level["generation"]["seed"]))
    else:
        samples = int(frame["height_samples"])
        lo, hi = float(frame["min_height_m"]), float(frame["max_height_m"])
    quads = samples - 1
    scale_xy = size_m / quads * 100.0
    scale_z = (hi - lo) * 100.0 / 512.0
    loc = (0.0 + offset[0], -size_m * 100.0 + offset[1], (lo + (hi - lo) * 0.5) * 100.0 + offset[2])
    # Recommended component layout for 2017 samples: 2016 quads = 16 components x 2 sections x 63 quads.
    layout = None
    for section in (63, 127, 255, 31, 15, 7):
        for per in (2, 1):
            comp = section * per
            if quads % comp == 0 and quads // comp <= 32:
                layout = {"section_quads": section, "sections_per_component": per, "components": quads // comp}
                break
        if layout:
            break
    return {
        "samples": samples,
        "quads": quads,
        "size_m": size_m,
        "min_m": lo,
        "max_m": hi,
        "scale": (scale_xy, scale_xy, scale_z),
        "location": loc,
        "layout": layout,
        "z_step_mm": (hi - lo) / 65535.0 * 1000.0,
    }


def ue_location(poi, offset):
    x, y, z = poi["position_m"]
    return (x * 100.0 + offset[0], -y * 100.0 + offset[1], z * 100.0 + offset[2])


# ----------------------------------------------------------------------------------------------------------- editor helpers
def subsystem(cls_name):
    cls = getattr(unreal, cls_name)
    return unreal.get_editor_subsystem(cls)


def editor_world():
    try:
        return subsystem("UnrealEditorSubsystem").get_editor_world()
    except Exception:
        return unreal.EditorLevelLibrary.get_editor_world()  # deprecated in 5.x but still present


def all_actors():
    return subsystem("EditorActorSubsystem").get_all_level_actors()


def has_tag(actor, tag):
    try:
        return any(str(t) == tag for t in actor.tags)
    except Exception:
        return False


def set_tags(actor, tags):
    actor.set_editor_property("tags", [unreal.Name(t) for t in tags])


def spawn(cls, loc, yaw_deg=0.0):
    # Keyword arguments on purpose: the Python constructor order of unreal.Rotator is (roll, pitch, yaw), unlike FRotator.
    rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw_deg)
    return subsystem("EditorActorSubsystem").spawn_actor_from_class(cls, unreal.Vector(*loc), rot)


def destroy_generated(prefix_tag):
    doomed = [a for a in all_actors() if has_tag(a, GENERATED_TAG) and has_tag(a, prefix_tag)]
    eas = subsystem("EditorActorSubsystem")
    for a in doomed:
        eas.destroy_actor(a)
    if doomed:
        LOG.info("%d acteurs générés précédemment supprimés (%s)" % (len(doomed), prefix_tag))


# ----------------------------------------------------------------------------------------------------------- stages
@guarded("1. Niveau")
def stage_level(args):
    les = subsystem("LevelEditorSubsystem")
    if unreal.EditorAssetLibrary.does_asset_exist(args.map):
        les.load_level(args.map)
        LOG.info("Carte ouverte : %s" % args.map)
        return
    created = False
    try:
        # Empty World Partition map (= "Empty Open World"). Never the "Open World" template: its sample landscape would
        # be found by stage_landscape instead of the Noctis terrain.
        created = les.new_level(args.map, True)  # VERIFY: is_partitioned_world argument of NewLevel (UE 5.1+)
    except TypeError:
        created = False
    if not created:
        raise RuntimeError("création de %s impossible par script : File > New Level > Empty Open World, enregistrer sous %s, puis relancer "
                           "(pas le gabarit Open World : son paysage d'exemple n'est pas le terrain Noctis)" % (args.map, args.map))
    LOG.info("Carte créée (World Partition) : %s" % args.map)


def import_texture(path, dest_dir, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset_path = "%s/%s" % (dest_dir, name)
    tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if tex is None:
        raise RuntimeError("import échoué : %s" % path)
    # Masks are data: linear, no mips, no streaming; PCG samples them on the CPU.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    # VERIFY: PCG texture sampling in 5.4 reads CPU-available data; TC_VECTOR_DISPLACEMENTMAP keeps 8-bit RGBA
    # uncompressed (the usual recommendation for PCG / gameplay masks). TC_GRAYSCALE would also keep one 8-bit channel.
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return asset_path


@guarded("2. Textures (masques)")
def stage_textures(args, level):
    sources = []
    if os.path.isdir(args.terrain_dir):
        for fn in sorted(os.listdir(args.terrain_dir)):
            if fn.endswith(".png") and (fn.startswith("weight_") or fn.startswith("foliage_")):
                sources.append((os.path.join(args.terrain_dir, fn), TEXTURE_ROOT + "/Terrain", "T_" + fn[:-4].title().replace("_", "")))
    else:
        LOG.warn("Dossier export-terrain introuvable : %s" % args.terrain_dir)
    for fn in level.get("masks", {}).get("files", []):
        if fn.startswith("mask_"):
            path = os.path.join(args.masks_dir, fn)
            if os.path.exists(path):
                sources.append((path, TEXTURE_ROOT + "/Level", "T_" + fn[:-4].title().replace("_", "")))
            else:
                LOG.warn("Masque absent : %s (relancer export-level)" % path)
    if args.dry_run:
        for s in sources:
            LOG.info("[simulation] importer %s -> %s/%s" % s)
        return
    for path, dest, name in sources:
        asset = import_texture(path, dest, name)
        LOG.info("Texture importée : %s" % asset)
    LOG.info("%d textures importées (linéaires, sans mips)" % len(sources))


def find_landscape():
    found = []
    for a in all_actors():
        cls = a.get_class().get_name()
        if cls in ("Landscape", "LandscapeStreamingProxy", "LandscapeProxy"):
            found.append(a)
    main = [a for a in found if a.get_class().get_name() == "Landscape"]
    return (main[0] if main else None), found


def print_manual_landscape_steps(plan, heightmap):
    lay = plan["layout"] or {}
    LOG.warn("Import manuel du paysage (Landscape Mode > Manage > New > Import from File) :")
    LOG.warn("  Heightmap File          : %s" % heightmap)
    LOG.warn("  Section Size            : %dx%d quads" % (lay.get("section_quads", 63), lay.get("section_quads", 63)))
    LOG.warn("  Sections Per Component  : %dx%d" % (lay.get("sections_per_component", 2), lay.get("sections_per_component", 2)))
    LOG.warn("  Number of Components    : %dx%d  (résolution %dx%d)" % (lay.get("components", 16), lay.get("components", 16), plan["samples"], plan["samples"]))
    LOG.warn("  Location (cm)           : X=%.1f Y=%.1f Z=%.1f" % plan["location"])
    LOG.warn("  Scale                   : X=%.4f Y=%.4f Z=%.5f" % plan["scale"])
    LOG.warn("  World Partition         : activé, Grid Size = 1 composant par proxy de streaming")
    LOG.warn("  Puis relancer ce script : il reconnaîtra le paysage (transformation exacte), le marquera '%s' et poursuivra." % LANDSCAPE_TAG)


def landscape_transform_gap(landscape, plan):
    """Position (cm) and scale differences between an existing landscape and the Noctis plan."""
    loc = landscape.get_actor_location()
    scl = landscape.get_actor_scale3d()
    want_loc, want_scl = plan["location"], plan["scale"]
    dl = math.sqrt((loc.x - want_loc[0]) ** 2 + (loc.y - want_loc[1]) ** 2 + (loc.z - want_loc[2]) ** 2)
    ds = max(abs(scl.x - want_scl[0]), abs(scl.y - want_scl[1]), abs(scl.z - want_scl[2]))
    return dl, ds


def loaded_landscape_footprint(actors):
    """Landscape components and XY bounds (cm) of the LOADED landscape actors (World Partition may keep proxies
    unloaded, so a partial count is not an error by itself)."""
    comps = 0
    box = None
    for a in actors:
        try:
            comps += len(a.get_components_by_class(unreal.LandscapeComponent))  # VERIFY: AActor::K2_GetComponentsByClass
            origin, extent = a.get_actor_bounds(False)  # VERIFY: returns (origin, box_extent)
        except Exception:
            continue
        if extent.x <= 0.0 and extent.y <= 0.0:
            continue
        b = (origin.x - extent.x, origin.x + extent.x, origin.y - extent.y, origin.y + extent.y)
        box = b if box is None else (min(box[0], b[0]), max(box[1], b[1]), min(box[2], b[2]), max(box[3], b[3]))
    return comps, box


@guarded("3. Paysage")
def stage_landscape(args, plan):
    """Returns 'ok', 'manual' (import still to do) or 'foreign' (a landscape that is not the Noctis terrain is present)."""
    heightmap = os.path.join(args.terrain_dir, "heightmap.png")
    LOG.info("Échantillons %d (quads %d), échelle XY %.4f cm, échelle Z %.5f, position %s, pas vertical %.2f mm"
             % (plan["samples"], plan["quads"], plan["scale"][0], plan["scale"][2], tuple(round(v, 1) for v in plan["location"]), plan["z_step_mm"]))
    if plan["layout"]:
        LOG.info("Découpage : %(components)dx%(components)d composants, %(sections_per_component)dx%(sections_per_component)d sections de %(section_quads)d quads" % plan["layout"])
    if not os.path.exists(heightmap):
        LOG.warn("Heightmap absente : %s (lancer 'noctis_sim export-terrain --out Build/Terrain')" % heightmap)
    if args.dry_run:
        LOG.info("[simulation] paysage : %s ; un paysage existant sans le tag '%s' et dont la transformation diffère sera refusé (gabarit)" % (heightmap, LANDSCAPE_TAG))
        return "manual"
    try:
        # Same computation in C++ (NoctisEditor module, FNoctisSpace): returns (ok, info) or info depending on the version.
        result = unreal.NoctisEditorLibrary.compute_terrain_info()
        info = (result[-1] if result and result[0] else None) if isinstance(result, tuple) else result
        if info is not None:
            loc, scl = info.landscape_location, info.landscape_scale
            d = max(abs(loc.x - plan["location"][0]), abs(loc.y - plan["location"][1]), abs(loc.z - plan["location"][2]))
            ds = max(abs(scl.x - plan["scale"][0]), abs(scl.z - plan["scale"][2]))
            (LOG.info if d < 1.0 and ds < 1e-3 else LOG.warn)("Contrôle NoctisEditorLibrary : écart position %.2f cm, échelle %.5f" % (d, ds))
    except Exception as exc:
        LOG.info("NoctisEditorLibrary.compute_terrain_info indisponible (%s) : contrôle croisé ignoré" % exc)
    landscape, proxies = find_landscape()
    if landscape is not None:
        dl, ds = landscape_transform_gap(landscape, plan)
        comps, box = loaded_landscape_footprint(proxies)
        want = (plan["layout"] or {}).get("components", 16) ** 2
        size_cm = plan["size_m"] * 100.0
        box_ok = box is None or (box[0] > -plan["scale"][0] and box[1] < size_cm + plan["scale"][0] and box[2] > -size_cm - plan["scale"][0] and box[3] < plan["scale"][0])
        tagged = has_tag(landscape, LANDSCAPE_TAG)
        matches = dl <= 1.0 and ds <= 1e-3 and comps <= want and box_ok
        if not tagged and not matches:
            msg = ("Paysage étranger détecté (%s : écart position %.0f cm, échelle %.4f, %d composants chargés, emprise %s) — probablement le paysage "
                   "d'exemple du gabarit Open World. Ce n'est pas le terrain Noctis : supprimer ce paysage et ses proxys puis importer heightmap.png, "
                   "ou relancer avec --replace-foreign-landscape."
                   % (landscape.get_actor_label(), dl, ds, comps, tuple(round(v) for v in box) if box else "inconnue"))
            if not args.replace_foreign_landscape:
                LOG.error(msg)
                return "foreign"
            LOG.warn(msg + " Suppression demandée (--replace-foreign-landscape).")
            eas = subsystem("EditorActorSubsystem")
            for a in proxies:
                eas.destroy_actor(a)
            landscape = None
        elif tagged and not matches:
            # Never move / rescale: under World Partition the streaming proxies would not follow, and a heightmap imported
            # with wrong settings cannot be repaired by scaling the actor.
            LOG.error("Le paysage Noctis '%s' n'a pas la transformation attendue (écart position %.1f cm, échelle %.4f, %d composants chargés) : "
                      "le supprimer et le réimporter avec les réglages ci-dessous (aucune correction automatique)." % (landscape.get_actor_label(), dl, ds, comps))
            print_manual_landscape_steps(plan, heightmap)
            return "foreign"
        elif not tagged:
            set_tags(landscape, [str(t) for t in landscape.tags] + [LANDSCAPE_TAG])
            LOG.info("Paysage importé à la main reconnu (transformation exacte, %d composants chargés) : marqué '%s'" % (comps, LANDSCAPE_TAG))
        else:
            LOG.info("Paysage Noctis conforme (%d acteurs de paysage/proxys chargés, %d composants)" % (len(proxies), comps))
    if landscape is None:
        helper = getattr(unreal, "NoctisEditorLibrary", None)
        if helper is not None and hasattr(helper, "import_landscape_from_heightmap"):
            # Project helper (C++ UBlueprintFunctionLibrary in the NoctisEditor module, wrapping ALandscape::Import).
            # Expected signature: import_landscape_from_heightmap(path, section_quads, sections_per_component,
            #                     components, location: Vector, scale: Vector) -> Landscape   — not implemented yet.
            lay = plan["layout"]
            landscape = helper.import_landscape_from_heightmap(heightmap, lay["section_quads"], lay["sections_per_component"], lay["components"],
                                                               unreal.Vector(*plan["location"]), unreal.Vector(*plan["scale"]))
            set_tags(landscape, [LANDSCAPE_TAG])
            LOG.info("Paysage importé par NoctisEditorLibrary et marqué '%s'" % LANDSCAPE_TAG)
        else:
            print_manual_landscape_steps(plan, heightmap)
            return "manual"
    if unreal.EditorAssetLibrary.does_asset_exist(LANDSCAPE_MATERIAL):
        landscape.set_editor_property("landscape_material", unreal.EditorAssetLibrary.load_asset(LANDSCAPE_MATERIAL))
        LOG.info("Matériau du paysage : %s" % LANDSCAPE_MATERIAL)
    else:
        LOG.warn("Matériau %s absent : couches à créer (une par habitat weight_*.png)" % LANDSCAPE_MATERIAL)
    LOG.info("Couches de peinture : Landscape Mode > Paint > Target Layers > Import pour chaque weight_<habitat>.png de %s" % args.terrain_dir)
    return "ok"


@guarded("4. World Partition et couches de données")
def stage_partition(args):
    manual_grid = ("World Settings > World Partition Setup > Runtime Settings > Grids[0] : Cell Size %(cell_size_cm)d, Loading Range "
                   "%(loading_range_cm)d (grille %(name)s)" % RUNTIME_GRID)
    if args.dry_run:
        # AWorldSettings::WorldPartition is not an editor-visible property: in the editor this step is expected to be manual.
        LOG.warn("[simulation] grille d'exécution : étape manuelle probable — %s" % manual_grid)
        for name, desc in DATA_LAYERS.items():
            LOG.info("[simulation] couche de données éditeur %s (%s)" % (name, desc))
        return
    world = editor_world()
    partitioned = None
    try:
        # VERIFY: UWorldPartitionBlueprintLibrary::GetEditorWorldBounds (5.1+) returns an empty box on a non-partitioned world.
        bounds = unreal.WorldPartitionBlueprintLibrary.get_editor_world_bounds()
        partitioned = bool(bounds.is_valid) if hasattr(bounds, "is_valid") else None
    except Exception:
        partitioned = None
    grid_set = False
    getter = getattr(world, "get_world_partition", None)  # VERIFY: UWorld::GetWorldPartition is normally NOT exposed to Python
    if getter is not None:
        try:
            wp = getter()
            runtime_hash = wp.get_editor_property("runtime_hash")  # VERIFY
            grids = list(runtime_hash.get_editor_property("grids"))  # VERIFY: TArray<FSpatialHashRuntimeGrid>
            if grids:
                g = grids[0]
                g.set_editor_property("cell_size", RUNTIME_GRID["cell_size_cm"])
                g.set_editor_property("loading_range", float(RUNTIME_GRID["loading_range_cm"]))
                grids[0] = g
                runtime_hash.set_editor_property("grids", grids)
                grid_set = True
                LOG.info("Grille d'exécution réglée : cellules %d cm, chargement %d cm" % (RUNTIME_GRID["cell_size_cm"], RUNTIME_GRID["loading_range_cm"]))
        except Exception as exc:
            LOG.info("Réglage de la grille par script impossible (%s)" % exc)
    if not grid_set:
        LOG.warn("Grille d'exécution : étape manuelle — %s" % manual_grid)
    if partitioned is False:
        LOG.warn("World Partition non actif sur cette carte : Tools > Convert Level (le niveau de 8 km en a besoin)")
    elif partitioned is None:
        LOG.warn("World Partition non vérifiable par script : contrôler World Settings > Enable World Partition")
    # Editor data layers (5.1+ asset-based). Creation by script is best-effort; the tags noctis.data_layer:<name> carry the
    # same information, and stage_pois assigns the markers when the instances exist.
    try:
        dle = subsystem("DataLayerEditorSubsystem")
        for name in DATA_LAYERS:
            asset_path = "/Game/Noctis/Level/DataLayers/%s" % name
            if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
                unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Noctis/Level/DataLayers", unreal.DataLayerAsset, unreal.DataLayerFactory())  # VERIFY factory class
            asset = unreal.EditorAssetLibrary.load_asset(asset_path)
            try:
                asset.set_editor_property("data_layer_type", unreal.DataLayerType.EDITOR)  # VERIFY enum / property name
            except Exception as exc:
                LOG.warn("Type de %s à régler à la main sur Editor (%s)" % (name, exc))
            instance = dle.get_data_layer_instance(asset)  # VERIFY
            if instance is None:
                params = unreal.DataLayerCreationParameters()  # VERIFY struct and field names
                params.set_editor_property("data_layer_asset", asset)
                instance = dle.create_data_layer_instance(params)  # VERIFY
            DATA_LAYER_INSTANCES[name] = instance
            LOG.info("Couche de données éditeur prête : %s" % name)
    except Exception as exc:
        LOG.warn("Couches de données non créées (%s) : les créer à la main (%s, type Editor) ; les marqueurs portent le tag noctis.data_layer:<nom>"
                 % (exc, ", ".join(DATA_LAYERS)))


def make_marker_material(poi_type):
    """Material instance of the basic shape material with a per-type colour (optional)."""
    path = "/Game/Noctis/Level/Materials/MI_Poi_%s" % poi_type
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    parent = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_Poi_%s" % poi_type, "/Game/Noctis/Level/Materials", unreal.MaterialInstanceConstant,
                                                                unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    r, g, b = MARKER_COLORS.get(poi_type, (0.7, 0.7, 0.7))
    # VERIFY: BasicShapeMaterial exposes a vector parameter named "Color"; if not, the call returns False and the
    # marker keeps the default grey (harmless).
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(r, g, b, 1.0))
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mi


def poi_label(poi):
    return "%s %s" % (poi["id"], poi["name"]["fr"])


@guarded("5. Points d'intérêt")
def stage_pois(args, level):
    pois = level["pois"]
    counts = {}
    for p in pois:
        counts[p["type"]] = counts.get(p["type"], 0) + 1
    LOG.info("POI : %d (%s)" % (len(pois), ", ".join("%s=%d" % kv for kv in sorted(counts.items()))))
    if args.dry_run:
        for p in pois:
            loc = ue_location(p, args.offset)
            LOG.info("[simulation] %-5s %-15s UE (%.0f, %.0f, %.0f) cm  confiance=%s  placement=%s%s"
                     % (p["id"], p["type"], loc[0], loc[1], loc[2], p["confidence"], p["placement"],
                        ("  profil:" + p["profile_toggle"]) if p.get("profile_toggle") else ""))
        return
    destroy_generated("noctis.poi")
    materials = {}
    by_layer = {}
    with unreal.ScopedEditorTransaction("Noctis : POI"):
        for p in pois:
            loc = ue_location(p, args.offset)
            mesh_path, scale = MARKERS.get(p["type"], ("/Engine/BasicShapes/Sphere.Sphere", (1.0, 1.0, 1.0)))
            if p["type"] in ZONE_TYPES:
                d = max(1.0, p["radius_m"] * 2.0)  # 100 cm basic shape -> diameter in metres
                scale = (d, d, scale[2])
            yaw = -p.get("heading_deg", 0.0)  # core heading (CCW from east) -> Unreal yaw (clockwise)
            actor = spawn(unreal.StaticMeshActor, loc, yaw)
            smc = actor.get_editor_property("static_mesh_component")
            smc.set_static_mesh(unreal.EditorAssetLibrary.load_asset(mesh_path))
            smc.set_collision_profile_name("NoCollision")
            if p["type"] not in materials:
                try:
                    materials[p["type"]] = make_marker_material(p["type"])
                except Exception as exc:
                    materials[p["type"]] = None
                    LOG.warn("Matériau de marqueur %s non créé (%s)" % (p["type"], exc))
            if materials[p["type"]] is not None:
                smc.set_material(0, materials[p["type"]])
            actor.set_actor_scale3d(unreal.Vector(*scale))
            actor.set_actor_label(poi_label(p))
            actor.set_folder_path("%s/%s" % (POI_FOLDER, p["type"]))
            actor.set_actor_hidden_in_game(True)  # no quest markers in game: the player builds the map from data
            try:
                actor.set_editor_property("is_editor_only_actor", True)  # VERIFY property name (bIsEditorOnlyActor)
            except Exception:
                pass
            layer = "DL_Nesting_Speculative" if p.get("profile_toggle") == "nesting_grounds" else "DL_POI_Markers"
            by_layer.setdefault(layer, []).append(actor)
            set_tags(actor, [GENERATED_TAG, "noctis.poi", "noctis.poi.id:" + p["id"], "noctis.poi.type:" + p["type"],
                             "noctis.confidence:" + p["confidence"], "noctis.placement:" + p["placement"], "noctis.data_layer:" + layer]
                     + (["noctis.profile_toggle:" + p["profile_toggle"]] if p.get("profile_toggle") else [])
                     + (["noctis.clearing"] if p["type"] in ("basecamp", "blind", "flood_refuge") else []))
            # Floating label (editor aid).
            text = spawn(unreal.TextRenderActor, (loc[0], loc[1], loc[2] + 600.0), 0.0)
            tr = text.get_editor_property("text_render")  # VERIFY component property name on ATextRenderActor
            tr.set_text("%s\n%s" % (p["id"], p["name"]["fr"]))
            tr.set_editor_property("world_size", 220.0)
            tr.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)  # (sic) Unreal enum name
            text.set_actor_label("LBL_" + p["id"])
            text.set_folder_path("%s/%s" % (POI_FOLDER, p["type"]))
            text.set_actor_hidden_in_game(True)
            set_tags(text, [GENERATED_TAG, "noctis.poi", "noctis.poi.label"])
            by_layer.setdefault("DL_Nesting_Speculative" if p.get("profile_toggle") == "nesting_grounds" else "DL_POI_Markers", []).append(text)
    for layer, actors in by_layer.items():
        instance = DATA_LAYER_INSTANCES.get(layer)
        if instance is None:
            LOG.warn("Couche %s indisponible : %d marqueurs à y assigner à la main (tag noctis.data_layer:%s)" % (layer, len(actors), layer))
            continue
        try:
            subsystem("DataLayerEditorSubsystem").add_actors_to_data_layer(actors, instance)  # VERIFY: AddActorsToDataLayer(actors, instance)
            LOG.info("%d marqueurs assignés à %s" % (len(actors), layer))
        except Exception as exc:
            LOG.warn("Assignation à %s impossible (%s) : %d marqueurs à assigner à la main" % (layer, exc, len(actors)))
    LOG.info("%d marqueurs de POI placés (cachés en jeu ; données lues à l'exécution depuis %s)" % (len(pois), os.path.basename(args.level_json)))


def trail_points_ue(trail, args, plan):
    """Trail polyline in Unreal space. Points are [x, y, z] (terrain height at export); older files with [x, y]
    fall back to the middle of the relief (PCG projects the spline on the landscape anyway)."""
    z_mid = (plan["min_m"] + plan["max_m"]) * 0.5
    out = []
    for q in trail["points"]:
        z = q[2] if len(q) > 2 else z_mid
        out.append((q[0] * 100.0 + args.offset[0], -q[1] * 100.0 + args.offset[1], z * 100.0 + args.offset[2]))
    return out


@guarded("6. Corridors prédits")
def stage_trails(args, level, plan):
    trails = level.get("trails", [])
    LOG.info("Corridors : %d (%s)" % (len(trails), ", ".join("%s %.0f m %s" % (t["id"], t["length_m"], t["evidence"]) for t in trails)))
    if args.dry_run:
        return
    destroy_generated("noctis.trail")
    bp_class = None
    if unreal.EditorAssetLibrary.does_asset_exist(TRAIL_BLUEPRINT):
        bp = unreal.EditorAssetLibrary.load_asset(TRAIL_BLUEPRINT)
        bp_class = unreal.load_class(None, TRAIL_BLUEPRINT + ".BP_NoctisTrail_C")  # VERIFY generated class path
        if bp_class is None and bp is not None:
            bp_class = bp.generated_class()  # VERIFY
    else:
        LOG.warn("%s absent (Blueprint Actor avec un SplineComponent) : corridors matérialisés par des jalons tous les 100 m" % TRAIL_BLUEPRINT)
    with unreal.ScopedEditorTransaction("Noctis : corridors"):
        for t in trails:
            pts = trail_points_ue(t, args, plan)
            tags = [GENERATED_TAG, "noctis.trail", "noctis.trail.id:" + t["id"], "noctis.trail.kind:" + t["kind"],
                    "noctis.trail.evidence:" + t["evidence"], "wear:%.3f" % t["observed_wear_mean"], "noctis.confidence:" + t["confidence"]]
            if bp_class is not None:
                actor = spawn(bp_class, pts[0], 0.0)
                spline = actor.get_component_by_class(unreal.SplineComponent)
                spline.clear_spline_points(False)
                for q in pts:
                    spline.add_spline_point(unreal.Vector(*q), unreal.SplineCoordinateSpace.WORLD, False)
                spline.update_spline()
                actor.set_actor_label("TRAIL_%s_%s" % (t["id"], t["kind"]))
                actor.set_folder_path(TRAIL_FOLDER)
                set_tags(actor, tags)
            else:
                cube = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
                dist = 0.0
                for i in range(1, len(pts)):
                    a, b = pts[i - 1], pts[i]
                    seg = math.hypot(b[0] - a[0], b[1] - a[1])
                    while dist <= seg:
                        f = dist / seg if seg > 0 else 0.0
                        q = (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2])
                        m = spawn(unreal.StaticMeshActor, q, 0.0)
                        m.get_editor_property("static_mesh_component").set_static_mesh(cube)
                        m.set_actor_scale3d(unreal.Vector(0.4, 0.4, 0.4))
                        m.set_actor_label("TRAIL_%s_%04d" % (t["id"], int(dist)))
                        m.set_folder_path("%s/%s" % (TRAIL_FOLDER, t["id"]))
                        m.set_actor_hidden_in_game(True)
                        set_tags(m, tags)
                        dist += 10000.0
                    dist -= seg
    LOG.info("Corridors placés (altitude = terrain à l'export ; PCG projette les splines sur le paysage)")


def check_pcg_masks(args):
    """Warn about every mask a PCG graph expects that is missing on disk (e.g. weight_oxbow_lake.png when the seed has no
    oxbow lake): the corresponding graph branch must then be skipped, not silently sample an empty texture."""
    import glob
    for graph, masks in PCG_GRAPHS.items():
        for m in masks:
            folder = args.masks_dir if m.startswith("mask_") else args.terrain_dir
            if not glob.glob(os.path.join(folder, m + ".png")):
                hint = " (aucun bras mort pour cette graine : branche d'exclusion à ignorer dans le graphe)" if m == "weight_oxbow_lake" else ""
                LOG.warn("PCG %s : masque %s.png absent de %s%s" % (graph, m, folder, hint))


@guarded("7. Volumes PCG")
def stage_pcg(args, plan):
    size_cm = plan["size_m"] * 100.0
    center = (size_cm * 0.5 + args.offset[0], -size_cm * 0.5 + args.offset[1], plan["location"][2])
    height_cm = (plan["max_m"] - plan["min_m"]) * 100.0 + 20000.0
    check_pcg_masks(args)
    if args.dry_run:
        for g, masks in PCG_GRAPHS.items():
            LOG.info("[simulation] volume %s centré %s, %.0f x %.0f x %.0f cm, masques %s" % (g, tuple(round(c) for c in center), size_cm, size_cm, height_cm, masks))
        return
    destroy_generated("noctis.pcg")
    for graph_name, masks in PCG_GRAPHS.items():
        vol = spawn(unreal.PCGVolume, center, 0.0)
        # A spawned volume carries the default 200 cm cube brush (VERIFY in 5.4): scale it to the landscape bounds.
        vol.set_actor_scale3d(unreal.Vector(size_cm / 200.0, size_cm / 200.0, height_cm / 200.0))
        vol.set_actor_label("PCGV_" + graph_name)
        vol.set_folder_path(PCG_FOLDER)
        set_tags(vol, [GENERATED_TAG, "noctis.pcg", "noctis.pcg.graph:" + graph_name] + ["noctis.mask:" + m for m in masks])
        comp = vol.get_editor_property("pcg_component")  # VERIFY property name (APCGVolume::PCGComponent)
        graph_path = "%s/%s" % (PCG_ROOT, graph_name)
        if unreal.EditorAssetLibrary.does_asset_exist(graph_path):
            comp.set_graph(unreal.EditorAssetLibrary.load_asset(graph_path))  # VERIFY: UPCGComponent::SetGraph is BlueprintCallable (5.2+)
            LOG.info("Graphe assigné : %s" % graph_path)
        else:
            LOG.warn("Graphe %s absent : le créer selon la description en tête de ce script, puis relancer" % graph_path)
        try:
            comp.set_editor_property("is_partitioned", True)  # VERIFY (bIsPartitioned)
            comp.set_editor_property("generation_trigger", unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)  # VERIFY enum name
        except Exception as exc:
            LOG.warn("Réglages PCG à faire à la main sur %s : Is Partitioned, Generation Trigger = On Demand (%s)" % (graph_name, exc))
        if args.generate_pcg and unreal.EditorAssetLibrary.does_asset_exist(graph_path):
            comp.generate_local(True)  # VERIFY: GenerateLocal(bool bForce)
            LOG.info("Génération PCG lancée : %s" % graph_name)
    LOG.info("Grille de partition PCG recommandée : 25600 cm (PCGWorldActor > Partition Grid Size)")


@guarded("8. Départ du joueur")
def stage_start(args, level):
    camps = [p for p in level["pois"] if p["type"] == "basecamp"]
    if not camps:
        LOG.warn("Aucun camp de base dans le JSON")
        return
    loc = ue_location(camps[0], args.offset)
    loc = (loc[0], loc[1], loc[2] + 200.0)
    t = level.get("time", {})
    LOG.info("Départ : %s jour %s (indice %s en base 0) à %sh, profil %s" % (camps[0]["id"], t.get("start_day_display", "?"), t.get("start_day_of_year"),
                                                                           t.get("start_hour"), level["generation"]["profile"]))
    if args.dry_run:
        LOG.info("[simulation] PlayerStart en %s" % (tuple(round(v) for v in loc),))
        return
    # Reuse the PlayerStart that setup_project.py may already have placed (label PlayerStart_Basecamp) or any other.
    starts = [a for a in all_actors() if a.get_class().get_name() == "PlayerStart"]
    starts.sort(key=lambda a: 0 if a.get_actor_label() == "PlayerStart_Basecamp" else 1)
    if starts:
        ps = starts[0]
        ps.set_actor_location(unreal.Vector(*loc), False, True)
        LOG.info("PlayerStart existant déplacé au camp de base (%s)" % ps.get_actor_label())
    else:
        ps = spawn(unreal.PlayerStart, loc, 0.0)
        ps.set_actor_label("PlayerStart_Basecamp")
        LOG.info("PlayerStart créé au camp de base")
    ps.set_folder_path("Noctis")
    set_tags(ps, ["noctis.start"])


def summarize(level, plan):
    s = level.get("summary", {})
    LOG.info("Résumé du niveau %s : %s" % (level["id"], json.dumps(s.get("poi_counts", {}), ensure_ascii=False)))
    LOG.info("Corridors %s, liaisons %s, composantes à pied %s, POI accessibles en véhicule %s, couverture radio %s"
             % (s.get("trails"), s.get("connections"), s.get("foot_components"), s.get("rover_reachable_pois"), s.get("radio_coverage_fraction")))
    rc = level.get("river_crossing", {}).get("main_river", {})
    if rc:
        LOG.info("Rivière principale : jamais guéable %s (débit minimal simulé %s, niveau >= %s m) ; traversée en packraft possible maintenant aux points %s"
                 % (rc.get("never_fordable"), rc.get("baseflow_fraction_min"), rc.get("stage_min_simulated_m"), ", ".join(rc.get("packraft_crossings_now", [])) or "aucun"))


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    LOG.info("Noctis build_level — %s" % ("éditeur Unreal" if IN_UNREAL else "simulation hors Unreal (--dry-run)"))
    level, terrain = load_inputs(args)
    plan = landscape_plan(level, terrain, args.offset)
    LOG.info("Niveau %s, environnement %s, graine %s, profil %s" % (level["id"], level["environment"], level["generation"]["seed"], level["generation"]["profile"]))
    if not args.dry_run:
        stage_level(args)
    if "textures" not in args.skip:
        stage_textures(args, level)
    landscape = "skipped"
    if "landscape" not in args.skip:
        landscape = stage_landscape(args, plan)
    if landscape == "foreign":
        # Content placed now would sit on the wrong terrain: stop before POIs, trails, PCG and the start point.
        LOG.error("Paysage non conforme : étapes POI, corridors, PCG et départ annulées (voir l'étape 3)")
        args.skip |= {"pois", "trails", "pcg", "start"}
    if "partition" not in args.skip:
        stage_partition(args)
    if "pois" not in args.skip:
        stage_pois(args, level)
    if "trails" not in args.skip:
        stage_trails(args, level, plan)
    if "pcg" not in args.skip:
        stage_pcg(args, plan)
    if "start" not in args.skip:
        stage_start(args, level)
    summarize(level, plan)
    if not args.dry_run:
        try:
            subsystem("LevelEditorSubsystem").save_current_level()
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
            LOG.info("Carte et paquets enregistrés")
        except Exception as exc:
            LOG.error("Sauvegarde : %s" % exc)
    saved = os.path.join(unreal.Paths.project_saved_dir(), "Logs") if IN_UNREAL else os.path.join(project_dir(), "Saved", "Logs")
    LOG.info("Terminé : %d avertissement(s), %d erreur(s)" % (LOG.warnings, LOG.errors))
    LOG.write(os.path.join(saved, "NoctisBuildLevel_%s.log" % datetime.datetime.now().strftime("%Y%m%d_%H%M%S")))
    return 0 if LOG.errors == 0 else 1


if __name__ == "__main__":
    status = main()
    if not IN_UNREAL:
        sys.exit(status)
