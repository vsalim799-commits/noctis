# -*- coding: utf-8 -*-
"""Shared helpers for the Noctis editor scripts (setup_project.py, import_creatures.py, import_audio.py).

Runs inside the Unreal Editor (Python Editor Script Plugin, enabled in Noctis.uproject). The editor adds
Content/Python to sys.path, so the scripts can `import noctis_ue_common`.

None of these scripts could be executed where they were written (no Unreal Editor available): every call into the
`unreal` module follows the UE 5.4/5.5 Python API as documented, and the places where the API is known to have moved
between versions are marked "VERIFY" and wrapped so that one failing step is reported without aborting the rest.
"""
from __future__ import annotations

import configparser
import json
import os
import traceback
from typing import Any, Callable, Iterable, Optional

import unreal

LOG_PREFIX = "[Noctis]"

# Counters reported by summary().
_report = {"ok": 0, "warnings": [], "errors": []}


# ----------------------------------------------------------------------------------------------- logging

def info(message: str) -> None:
    unreal.log(f"{LOG_PREFIX} {message}")


def warn(message: str) -> None:
    _report["warnings"].append(message)
    unreal.log_warning(f"{LOG_PREFIX} {message}")


def error(message: str) -> None:
    _report["errors"].append(message)
    unreal.log_error(f"{LOG_PREFIX} {message}")


def step(label: str, fn: Callable[[], Any]) -> Any:
    """Runs one independent step; an exception is logged (with traceback) and the script goes on."""
    try:
        result = fn()
        _report["ok"] += 1
        return result
    except Exception as exc:  # noqa: BLE001 - editor scripts must not stop at the first API difference
        error(f"{label} : {exc}")
        unreal.log_error(traceback.format_exc())
        return None


def summary(title: str) -> int:
    info(f"{title} : {_report['ok']} étape(s) réussie(s), {len(_report['warnings'])} avertissement(s), {len(_report['errors'])} erreur(s).")
    for w in _report["warnings"]:
        unreal.log_warning(f"{LOG_PREFIX}   avertissement : {w}")
    for e in _report["errors"]:
        unreal.log_error(f"{LOG_PREFIX}   erreur : {e}")
    return 1 if _report["errors"] else 0


# ----------------------------------------------------------------------------------------------- paths

def project_dir() -> str:
    """Absolute project directory (the repository root: Noctis.uproject, Data/, Build/, Tools/)."""
    return os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def repo_path(*parts: str) -> str:
    return os.path.join(project_dir(), *parts)


def data_root() -> str:
    """Same resolution as UNoctisSettings::ResolveDataRoot (override, <Project>/Data, Content/NoctisData)."""
    override = read_ini("DefaultEngine.ini", "/Script/Noctis.NoctisSettings", "DataRootOverride", "")
    if override:
        path = override if os.path.isabs(override) else os.path.join(project_dir(), override)
        return os.path.normpath(path)
    for candidate in (repo_path("Data"), os.path.join(project_dir(), "Content", "NoctisData")):
        if os.path.isdir(os.path.join(candidate, "Species")):
            return candidate
    return repo_path("Data")


def read_ini(file_name: str, section: str, key: str, default: str = "") -> str:
    """Reads Config/<file_name> without Unreal's ini hierarchy (good enough for the project's own keys)."""
    path = repo_path("Config", file_name)
    if not os.path.isfile(path):
        return default
    parser = configparser.ConfigParser(strict=False, interpolation=None, comment_prefixes=(";", "#"), inline_comment_prefixes=None)
    parser.optionxform = str  # keep key case
    try:
        with open(path, encoding="utf-8") as handle:
            parser.read_file(handle)
    except (configparser.Error, UnicodeDecodeError) as exc:
        warn(f"Lecture de {path} impossible ({exc}) : valeur par défaut pour {key}.")
        return default
    if parser.has_option(section, key):
        return parser.get(section, key).strip()
    return default


def load_json(path: str) -> Optional[dict]:
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def species_files() -> dict:
    """species id -> parsed JSON for every Data/Species/<env>/<id>.json."""
    result = {}
    root = os.path.join(data_root(), "Species")
    for env in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        env_dir = os.path.join(root, env)
        if not os.path.isdir(env_dir):
            continue
        for name in sorted(os.listdir(env_dir)):
            if name.endswith(".json"):
                doc = load_json(os.path.join(env_dir, name))
                if doc and doc.get("id"):
                    result[doc["id"]] = doc
    return result


# ----------------------------------------------------------------------------------------------- assets

def asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def object_path(package_path: str) -> str:
    """/Game/A/B -> /Game/A/B.B"""
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def load_asset(package_path: str):
    if unreal.EditorAssetLibrary.does_asset_exist(package_path):
        return unreal.EditorAssetLibrary.load_asset(package_path)
    return None


def create_asset(package_path: str, asset_class, factory, configure: Optional[Callable[[Any], None]] = None, overwrite: bool = False):
    """Returns the existing asset (configure() is applied again only when overwrite) or creates it."""
    existing = load_asset(package_path)
    if existing and not overwrite:
        return existing
    folder, name = package_path.rsplit("/", 1)
    ensure_directory(folder)
    asset = existing or asset_tools().create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"création de {package_path} impossible")
    if configure:
        configure(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def save(asset) -> None:
    if asset is not None:
        unreal.EditorAssetLibrary.save_loaded_asset(asset)


def set_props(obj, **props) -> None:
    """set_editor_property for each keyword; reports properties that do not exist on this engine version."""
    for key, value in props.items():
        try:
            obj.set_editor_property(key, value)
        except Exception as exc:  # noqa: BLE001
            warn(f"{obj.get_name()}.{key} non appliqué ({exc}) — VERIFY le nom de la propriété sur cette version du moteur.")


def import_files(tasks: Iterable["unreal.AssetImportTask"]) -> list:
    task_list = list(tasks)
    if not task_list:
        return []
    asset_tools().import_asset_tasks(task_list)
    imported = []
    for task in task_list:
        paths = list(task.get_editor_property("imported_object_paths") or [])
        if not paths:
            warn(f"Import sans résultat : {task.get_editor_property('filename')}")
        imported.extend(paths)
    return imported


def make_import_task(filename: str, destination: str, name: str = "", options=None) -> "unreal.AssetImportTask":
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    if name:
        task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    return task


def make_key(name: str):
    """FKey from its name ("W", "SpaceBar", "Mouse2D", "Gamepad_Left2D"...)."""
    # VERIFY: FKey exposes KeyName as a UPROPERTY, so the keyword constructor works in 5.4/5.5.
    return unreal.Key(key_name=name)
