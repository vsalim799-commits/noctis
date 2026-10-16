# -*- coding: utf-8 -*-
"""Imports the sounds produced by Tools/AudioFactory (Build/Audio/**.wav) as described by Data/Audio/manifest.json,
and creates the sound classes, submixes, attenuation and concurrency presets the manifest declares.

Usage:
    py "Content/Python/import_audio.py"                       # whole manifest
    py "Content/Python/import_audio.py" --species triceratops_horridus --skip-existing
    py "Content/Python/import_audio.py" --prefer-baked        # species visual data play baked calls instead of SignalSynth
    py "Content/Python/import_audio.py" --dry-run             # report what would be imported

Calibration (shared with the live synthesis, see ANoctisAudioDirector and UNoctisSettings::AudioFullScaleDbSpl):
    the core reasons in dB SPL; Unreal plays digital levels. A procedural call at L dB SPL is rendered at
    L - AudioFullScaleDbSpl dBFS. For a baked file the manifest gives level.calibration_spl_db_at_0dbfs_rms (the SPL that
    0 dBFS RMS of the file stands for), so the asset volume is 10^((calibration - AudioFullScaleDbSpl) / 20): at volume
    multiplier 1 a baked call then sounds like its source level at 1 m, and the director multiplies by the propagation
    loss computed by the core (spreading, ISO 9613-1 absorption, foliage, wind, terrain).
    Distance variants (_d50_open, _d200_forest...) already contain propagation: they are imported for the field journal
    reference library and are never bound to live calls.

Asset layout:
    /Game/Audio/Classes/SC_*            sound classes (manifest ue5.sound_classes, parent links)
    /Game/Audio/Submixes/SM_*           submixes named by the sounds
    /Game/Audio/Attenuation/ATT_*       manifest presets + ATT_Noctis_SpatialOnly (spatialisation only, no distance model)
    /Game/Audio/Concurrency/CON_*       manifest presets + SCON_Noctis_Calls
    /Game/Audio/Creatures/<species>/voc_*   vocalisations (metadata tags Noctis.* carry confidence and hypothesis flags)
    /Game/Audio/<Category>/...          other categories when the factory produces them
    /Game/Audio/Cues/RC_<random_group>  Random cues of the director's event libraries (thunder by distance class, splash /
                                        vegetation crash by mass class); the UNoctisSettings cues (Weather/SC_Thunder,
                                        Water/SC_Splash, Foley/SC_VegetationCrash) are built from the same variations
"""
from __future__ import annotations

import argparse
import os
import re
import sys

import unreal

import noctis_ue_common as nc

AUDIO = "/Game/Audio"


def _enum(enum_type, camel: str):
    """"StopFarthestThenOldest" -> enum_type.STOP_FARTHEST_THEN_OLDEST (Unreal Python enum naming)."""
    name = re.sub(r"(?<!^)(?=[A-Z])", "_", camel).upper()
    return getattr(enum_type, name)


# ----------------------------------------------------------------------------------------------- presets

def create_sound_classes(manifest: dict) -> dict:
    classes = {}
    spec = (manifest.get("ue5") or {}).get("sound_classes") or {}
    for name in spec:
        classes[name] = nc.create_asset(f"{AUDIO}/Classes/{name}", unreal.SoundClass, unreal.SoundClassFactory())
    for name, entry in spec.items():
        parent_name = (entry or {}).get("parent")
        if parent_name and parent_name in classes:
            child, parent = classes[name], classes[parent_name]
            child.set_editor_property("parent_class", parent)
            children = list(parent.get_editor_property("child_classes") or [])
            if child not in children:
                children.append(child)
                parent.set_editor_property("child_classes", children)
            nc.save(child)
            nc.save(parent)
    nc.info(f"{len(classes)} classes de sons.")
    return classes


def create_attenuations(manifest: dict) -> dict:
    result = {}
    presets = dict((manifest.get("ue5") or {}).get("attenuation_presets") or {})
    # The live path: the core already applied every propagation loss; Unreal only spatialises (HRTF / panning).
    presets["ATT_Noctis_SpatialOnly"] = {"spatialize": True, "attenuate": False, "attenuate_with_lpf": False}
    for name, p in presets.items():
        def configure(asset, p=p):
            settings = asset.get_editor_property("attenuation")
            # Manifest presets without a distance model carry attenuate = false; `or` defaults also tolerate null values.
            attenuate = bool(p.get("attenuate", True))
            nc.set_props(settings, spatialize=bool(p.get("spatialize", True)), attenuate=attenuate,
                         attenuate_with_lpf=bool(p.get("attenuate_with_lpf") or False))
            if attenuate:
                nc.set_props(settings,
                             attenuation_shape=_enum(unreal.AttenuationShape, p.get("shape") or "Sphere"),
                             distance_algorithm=_enum(unreal.AttenuationDistanceModel, p.get("distance_algorithm") or "NaturalSound"),
                             attenuation_shape_extents=unreal.Vector(float(p.get("inner_radius_cm") or 100.0), 0.0, 0.0),
                             falloff_distance=float(p.get("falloff_distance_cm") or 10000.0))
                # dBAttenuationAtMax: Python snake-cases the "dB" prefix differently across versions (VERIFY).
                db_max = float(p.get("db_attenuation_at_max") or -60.0)
                try:
                    settings.set_editor_property("d_b_attenuation_at_max", db_max)
                except Exception:  # noqa: BLE001
                    nc.set_props(settings, db_attenuation_at_max=db_max)
            asset.set_editor_property("attenuation", settings)
        result[name] = nc.step(name, lambda name=name, configure=configure: nc.create_asset(
            f"{AUDIO}/Attenuation/{name}", unreal.SoundAttenuation, unreal.SoundAttenuationFactory(), configure, overwrite=True))
    return result


def create_concurrency(manifest: dict) -> dict:
    result = {}
    presets = dict((manifest.get("ue5") or {}).get("concurrency_presets") or {})
    max_calls = int(nc.read_ini("DefaultEngine.ini", "/Script/Noctis.NoctisSettings", "MaxProceduralCalls", "12") or 12)
    presets["SCON_Noctis_Calls"] = {"max_count": max_calls, "resolution_rule": "StopQuietest", "limit_to_owner": False}
    folder = f"{AUDIO}/Concurrency"
    for name, p in presets.items():
        def configure(asset, p=p):
            settings = asset.get_editor_property("concurrency")
            nc.set_props(settings, max_count=int(p.get("max_count", 8)), limit_to_owner=bool(p.get("limit_to_owner", False)),
                         resolution_rule=_enum(unreal.MaxConcurrentResolutionRule, p.get("resolution_rule", "StopQuietest")))
            asset.set_editor_property("concurrency", settings)
        result[name] = nc.step(name, lambda name=name, configure=configure: nc.create_asset(
            f"{folder}/{name}", unreal.SoundConcurrency, unreal.SoundConcurrencyFactory(), configure, overwrite=True))
    return result


def create_submix(name: str):
    return nc.create_asset(f"{AUDIO}/Submixes/{name}", unreal.SoundSubmix, unreal.SoundSubmixFactory())


# UNoctisSettings sound paths -> the manifest random group whose variations feed them (fallback cues; the audio director
# itself picks the class closest to the event: thunder by distance, splash / crash by mass, see ANoctisAudioDirector).
SETTINGS_CUES = {
    "Weather/SC_Thunder": "thunder_2000m",
    "Water/SC_Splash": "fol_water_stroke_medium",
    "Foley/SC_VegetationCrash": "fol_vegetation_crash_large",
    "Foley/SC_Impact": None,
}
# Random groups turned into Random cues (/Game/Audio/Cues/RC_<group>): the director's event libraries.
CUE_GROUP_PATTERNS = (r"^thunder_\d+m$", r"^fol_water_(enter|stroke|exit)_[a-z]+$", r"^fol_vegetation_crash_[a-z]+$")


def _random_cue(path: str, waves) -> object:
    """SoundCue with a Random node over `waves` (SoundCueFactoryNew.InitialSoundWaves builds the Random node when it is
    given several waves; VERIFY on 5.4: if the property is not exposed to Python, fall back to a single-wave cue)."""
    factory = unreal.SoundCueFactoryNew()
    try:
        factory.set_editor_property("initial_sound_waves", list(waves))
    except Exception:  # noqa: BLE001 - property not exposed in this engine version
        try:
            factory.set_editor_property("initial_sound_wave", waves[0])
            nc.warn(f"{path} : nœud Random indisponible depuis Python, une seule variation branchée (compléter à la main).")
        except Exception:  # noqa: BLE001
            nc.warn(f"{path} : impossible de pré-remplir le cue (créer le nœud Random à la main).")
    if nc.load_asset(path):
        # create_asset() keeps an existing asset as is: delete it so that the factory rebuilds the Random node.
        unreal.EditorAssetLibrary.delete_asset(path)
    return nc.create_asset(path, unreal.SoundCue, factory)


def build_random_cues(manifest: dict) -> dict:
    groups = {}
    for entry in manifest.get("sounds") or []:
        g = (entry.get("ue5") or {}).get("random_group")
        if g and any(re.match(pat, g) for pat in CUE_GROUP_PATTERNS):
            wave = nc.load_asset(f"{destination(entry)}/{entry['id']}")
            if wave:
                groups.setdefault(g, []).append(wave)
    cues = {}
    for g, waves in sorted(groups.items()):
        cues[g] = nc.step(f"RC_{g}", lambda g=g, waves=waves: _random_cue(f"{AUDIO}/Cues/RC_{g}", waves))
    nc.info(f"{len(cues)} cue(s) aléatoire(s) d'événements.")
    return groups


def ensure_settings_sounds(groups: dict | None = None) -> None:
    """UNoctisSettings points at these USoundBase paths: each becomes a Random cue over the variations of its manifest
    group (no more silent placeholder cues). Impact has no baked library yet: an empty cue keeps the reference valid."""
    groups = groups or {}
    for path, group in SETTINGS_CUES.items():
        full = f"{AUDIO}/{path}"
        waves = groups.get(group) if group else None
        if waves:
            nc.step(path, lambda full=full, waves=waves: _random_cue(full, waves))
        elif not nc.load_asset(full):
            nc.create_asset(full, unreal.SoundCue, unreal.SoundCueFactoryNew())
            nc.warn(f"{full} créé vide (silencieux) : aucune variation « {group} » importée.")


def describe_footstep_metasound() -> None:
    nc.info("Pas (MetaSound) : créer une MetaSound Source par plan corporel (ex. /Game/Audio/Creatures/MS_Footstep_Large) avec les "
            "entrées : Footfall (Trigger), Mass (Float, kg), LevelDb (Float, dB SPL à 1 m calculé par le coeur), Substrate (Int32 : "
            "0 roche, 1 gravier, 2 sable, 3 limon, 4 boue, 5 argile, 6 sol forestier, 7 tourbe), InWater (Bool) ; puis la "
            "référencer dans FootstepSound de chaque DA_<espèce>. ANoctisCreatureActor::PlayFootfall règle ces paramètres.")


# ----------------------------------------------------------------------------------------------- import

def destination(entry: dict) -> str:
    category = (entry.get("category") or "misc").lower()
    if category == "vocal" and entry.get("species"):
        return f"{AUDIO}/Creatures/{entry['species']}"
    sub = entry.get("subcategory") or ""
    return f"{AUDIO}/{category.capitalize()}" + (f"/{sub}" if sub else "")


def configure_wave(wave, entry: dict, classes, attenuations, concurrency, submixes, full_scale_db: float) -> None:
    ue = entry.get("ue5") or {}
    level = entry.get("level") or {}
    audio = entry.get("audio") or {}
    sound_class = classes.get(ue.get("sound_class"))
    if sound_class:
        nc.set_props(wave, sound_class_object=sound_class)
    attenuation = attenuations.get(ue.get("attenuation_preset"))
    if attenuation:
        nc.set_props(wave, attenuation_settings=attenuation)
    conc = concurrency.get(ue.get("concurrency"))
    if conc:
        try:
            wave.set_editor_property("concurrency_set", {conc})
        except Exception:  # noqa: BLE001 - TSet conversion differs between versions
            nc.set_props(wave, concurrency_set=[conc])
    submix_name = ue.get("submix")
    if submix_name:
        if submix_name not in submixes:
            submixes[submix_name] = create_submix(submix_name)
        nc.set_props(wave, sound_submix_object=submixes[submix_name])
    if "priority" in ue:
        nc.set_props(wave, priority=float(ue["priority"]))
    nc.set_props(wave, looping=bool(ue.get("looping", audio.get("loop", False))))
    import_opts = ue.get("import") or {}
    if "compression_quality" in import_opts:
        nc.set_props(wave, compression_quality=int(import_opts["compression_quality"]))
    if import_opts.get("loading_behavior"):
        nc.set_props(wave, loading_behavior=_enum(unreal.SoundWaveLoadingBehavior, import_opts["loading_behavior"]))
    virtualization = ue.get("virtualization")
    if virtualization and virtualization != "Default":
        nc.set_props(wave, virtualization_mode=_enum(unreal.VirtualizationMode, virtualization))
    calibration = level.get("calibration_spl_db_at_0dbfs_rms")
    if calibration is not None:
        nc.set_props(wave, volume=float(10.0 ** ((float(calibration) - full_scale_db) / 20.0)))
    # Provenance travels with the asset (journal UI, review): confidence, hypothesis flag, sources.
    tags = {
        "Noctis.Id": entry.get("id", ""),
        "Noctis.Species": entry.get("species") or "",
        "Noctis.CallType": ((entry.get("parameters") or {}).get("call_type")) or "",
        "Noctis.Variant": ",".join(f"{k}={v}" for k, v in sorted((entry.get("variant") or {}).items())),
        "Noctis.Confidence": entry.get("confidence", ""),
        "Noctis.ModelConfidence": entry.get("model_confidence", ""),
        "Noctis.CaptionFr": (entry.get("caption") or {}).get("fr", ""),
        "Noctis.CaptionEn": (entry.get("caption") or {}).get("en", ""),
        "Noctis.AmbientSplDb": str(level.get("ambient_spl_db", "")),
        "Noctis.ReceivedSplDb": str(level.get("received_spl_db", "")),
        "Noctis.Hypothesis": "true" if entry.get("hypothesis") else "false",
        "Noctis.LabelFr": entry.get("label_fr", ""),
        "Noctis.Sources": ",".join(entry.get("sources") or []),
        "Noctis.SourceLevelDbSpl1m": str(level.get("source_spl_db_1m", "")),
    }
    for key, value in tags.items():
        unreal.EditorAssetLibrary.set_metadata_tag(wave, key, str(value))
    nc.save(wave)


def link_species_calls(species_ids, prefer_baked: bool) -> None:
    import import_creatures  # same folder; main() is guarded

    docs = nc.species_files()
    for species in sorted(species_ids):
        da = nc.load_asset(f"/Game/Creatures/{species}/DA_{species}")
        if not da:
            continue
        count = import_creatures.link_call_sounds(da, species, docs.get(species))
        if prefer_baked:
            nc.set_props(da, procedural_calls=False)
        nc.save(da)
        nc.info(f"DA_{species} : {count} cri(s) enregistré(s) lié(s)" + (" (lecture des sons enregistrés)" if prefer_baked else " (synthèse SignalSynth conservée)"))


def main(argv) -> int:
    parser = argparse.ArgumentParser(prog="import_audio.py")
    parser.add_argument("--species", nargs="*", default=[], help="limiter aux espèces données")
    parser.add_argument("--skip-existing", action="store_true", help="ne pas réimporter les sons déjà présents")
    parser.add_argument("--prefer-baked", action="store_true", help="DA_<espèce>.bProceduralCalls = false")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    manifest_path = os.path.join(nc.data_root(), "Audio", "manifest.json")
    manifest = nc.load_json(manifest_path)
    if not manifest:
        nc.error(f"Manifeste introuvable : {manifest_path}")
        return nc.summary("import_audio")
    sounds = manifest.get("sounds") or []
    if args.species:
        sounds = [s for s in sounds if s.get("species") in set(args.species)]
    full_scale_db = float(nc.read_ini("DefaultEngine.ini", "/Script/Noctis.NoctisSettings", "AudioFullScaleDbSpl", "110") or 110.0)
    nc.info(f"{len(sounds)} son(s) dans {manifest_path} ; pleine échelle {full_scale_db:.1f} dB SPL.")

    jobs = []
    missing = 0
    for entry in sounds:
        source = nc.repo_path(entry.get("file", "")) if entry.get("file") else ""
        if not source or not os.path.isfile(source):
            missing += 1
            continue
        dest = destination(entry)
        if args.skip_existing and nc.load_asset(f"{dest}/{entry['id']}"):
            continue
        jobs.append((entry, source, dest))
    if missing:
        nc.warn(f"{missing} fichier(s) du manifeste absents de Build/Audio : lancer Tools/AudioFactory/generate.py.")
    if args.dry_run:
        for entry, source, dest in jobs[:50]:
            nc.info(f"  {os.path.relpath(source, nc.project_dir())} -> {dest}/{entry['id']}")
        nc.info(f"{len(jobs)} import(s) prévus (--dry-run).")
        return nc.summary("import_audio")

    classes = nc.step("classes de sons", lambda: create_sound_classes(manifest)) or {}
    attenuations = nc.step("atténuations", lambda: create_attenuations(manifest)) or {}
    concurrency = nc.step("concurrence", lambda: create_concurrency(manifest)) or {}
    submixes = {}

    batch = 64
    imported = 0
    with unreal.ScopedSlowTask(len(jobs), "Import des sons Noctis") as task:
        task.make_dialog(True)
        for start in range(0, len(jobs), batch):
            if task.should_cancel():
                nc.warn("Import interrompu par l'utilisateur.")
                break
            chunk = jobs[start:start + batch]
            nc.import_files(nc.make_import_task(source, dest, entry["id"]) for entry, source, dest in chunk)
            for entry, _source, dest in chunk:
                wave = nc.load_asset(f"{dest}/{entry['id']}")
                if wave:
                    nc.step(entry["id"], lambda wave=wave, entry=entry: configure_wave(wave, entry, classes, attenuations, concurrency, submixes,
                                                                                      full_scale_db))
                    imported += 1
            task.enter_progress_frame(len(chunk))
    nc.info(f"{imported} son(s) importé(s).")
    groups = nc.step("cues aléatoires d'événements", lambda: build_random_cues(manifest)) or {}
    nc.step("sons des réglages", lambda: ensure_settings_sounds(groups))

    species_ids = {s.get("species") for s in sounds if s.get("species")}
    nc.step("liaison des cris aux fiches d'espèces", lambda: link_species_calls(species_ids, args.prefer_baked))
    describe_footstep_metasound()
    return nc.summary("import_audio")


if __name__ == "__main__":
    main(sys.argv[1:])
