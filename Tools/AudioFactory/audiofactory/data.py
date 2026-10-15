"""Access to the repository's scientific data (species, environments, equipment, bibliography, profiles)
and the small pieces of core logic the audio library must mirror exactly (call contexts, f0 contours,
confidence rules)."""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from . import common as C

CONFIDENCE_ORDER = ["unknown", "speculative", "inferred", "strongly_supported", "established"]
CONFIDENCE_LABEL_FR = {
    "established": "ÉTABLI",
    "strongly_supported": "FORTEMENT ÉTAYÉ",
    "inferred": "INFÉRÉ / PLAUSIBLE",
    "speculative": "SPÉCULATIF",
    "unknown": "INCONNU",
}
INSUFFICIENT_FR = "Information insuffisante — recherche nécessaire."

# Mirrors noctis::CallContext (Source/NoctisCore/Public/Noctis/Science/Taxonomy.h).
CALL_CONTEXTS = ["contact", "alarm", "distress", "threat", "courtship", "begging", "territorial", "other"]


def parse_call_context(s: str) -> str:
    """Mirror of noctis::parseCallContext (prefix match, same order)."""
    def starts(k: str) -> bool:
        return s.startswith(k)
    if starts("contact") or starts("social_contact"):
        return "contact"
    if starts("alarm"):
        return "alarm"
    if starts("distress"):
        return "distress"
    if starts("threat") or starts("agonistic") or starts("warning"):
        return "threat"
    if starts("courtship") or starts("mating"):
        return "courtship"
    if starts("begging") or starts("juvenile"):
        return "begging"
    if starts("territorial"):
        return "territorial"
    return "other"


def contour(ctx: str, u):
    """Mirror of acousticsimpl::contour in Source/NoctisCore/Private/Research/Acoustics.cpp:
    frequency contour of hypothetical call types, relative to f0, u in [0, 1]."""
    u = np.asarray(u, dtype=np.float64)
    pi = math.pi
    if ctx == "alarm":
        return 1.0 + 0.25 * np.sin(pi * u)
    if ctx == "distress":
        return 1.3 + 0.15 * np.sin(2 * pi * 3.0 * u)
    if ctx == "threat":
        return 0.85 - 0.1 * u
    if ctx == "courtship":
        return 1.0 + 0.08 * np.sin(2 * pi * 0.7 * u)
    if ctx == "begging":
        return 1.6 + 0.2 * u
    if ctx == "territorial":
        return 0.9 + 0.05 * np.sin(pi * u)
    return 1.0 + 0.06 * np.sin(pi * u)          # contact, other


# Core constants mirrored from CreatureSystem::emitCall.
VOCAL_PITCH_EXPONENT = -0.4       # pitchShift = sizeRatio^-0.4 (fletcher2004_frequency_scaling)
VOCAL_LEVEL_SLOPE_DB = 5.0        # levelDb += 5 log10(sizeRatio)


def conf_rank(c: str) -> int:
    return CONFIDENCE_ORDER.index(c) if c in CONFIDENCE_ORDER else 0


def conf_min(*levels: str) -> str:
    levels = [l for l in levels if l]
    return min(levels, key=conf_rank) if levels else "unknown"


@dataclass
class Species:
    id: str
    region: str
    path: Path
    raw: dict

    @property
    def sim(self) -> dict:
        return self.raw["sim"]

    @property
    def science(self) -> dict:
        return self.raw["science"]

    @property
    def name(self) -> str:
        return self.raw.get("scientific_name", self.id)

    @property
    def name_fr(self) -> str:
        return self.raw.get("common_names", {}).get("fr", self.name)

    @property
    def mass(self) -> float:
        return float(self.sim["adult_mass_kg"])

    @property
    def body_plan(self) -> str:
        return self.sim["body_plan"]

    @property
    def is_biped(self) -> bool:
        return self.body_plan.startswith("biped") or self.body_plan == "bird_flyer"

    def claim(self, topic: str) -> dict:
        return self.science.get(topic) or {"summary": INSUFFICIENT_FR, "confidence": "unknown", "sources": []}

    def basis(self, key: str) -> str:
        return self.raw.get("sim_basis", {}).get(key, "game_assumption: non documenté dans sim_basis")


@dataclass
class Environment:
    id: str
    raw: dict

    @property
    def sim(self) -> dict:
        return self.raw["sim"]

    @property
    def temperature_c(self) -> float:
        return float(self.sim["mean_annual_temp_c"])

    @property
    def humidity(self) -> float:
        return float(self.sim["relative_humidity_mean"])


@dataclass
class Dataset:
    species: dict[str, Species] = field(default_factory=dict)
    environments: dict[str, Environment] = field(default_factory=dict)
    equipment: dict = field(default_factory=dict)
    sources: dict[str, dict] = field(default_factory=dict)
    profiles: dict[str, dict] = field(default_factory=dict)

    # -- bibliography --------------------------------------------------------------------
    def has_source(self, sid: str) -> bool:
        return sid in self.sources

    def verified(self, sid: str) -> bool:
        return bool(self.sources.get(sid, {}).get("verified", False))

    def check_sources(self, ids) -> list[str]:
        missing = [s for s in ids if s not in self.sources]
        if missing:
            raise KeyError(f"unknown source ids (not in Data/Sources): {missing}")
        return list(dict.fromkeys(ids))

    def confidence_cap(self, ids) -> str:
        """Data/SCHEMA.md: an unverified reference cannot support more than 'inferred'.
        No reference at all: at most 'speculative' (game assumption)."""
        ids = list(ids)
        if not ids:
            return "speculative"
        return "established" if any(self.verified(s) for s in ids) else "inferred"

    def capped(self, level: str, ids) -> str:
        return conf_min(level, self.confidence_cap(ids))

    def env(self, region: str) -> Environment:
        return self.environments[region]

    def profile_toggle_states(self, toggle: str) -> dict[str, bool]:
        out = {}
        for pid, p in sorted(self.profiles.items()):
            for t in p.get("toggles", []):
                if t["id"] == toggle:
                    out[pid] = bool(t["enabled"])
        return out


def load(repo: Path = C.REPO) -> Dataset:
    ds = Dataset()
    data = repo / "Data"
    for f in sorted((data / "Species").glob("*/*.json")):
        raw = json.loads(f.read_text(encoding="utf-8"))
        ds.species[raw["id"]] = Species(raw["id"], f.parent.name, f, raw)
    for f in sorted((data / "Environments").glob("*.json")):
        raw = json.loads(f.read_text(encoding="utf-8"))
        ds.environments[raw["id"]] = Environment(raw["id"], raw)
    ds.equipment = json.loads((data / "Equipment" / "equipment.json").read_text(encoding="utf-8"))
    for f in sorted((data / "Sources").glob("*.json")):
        for e in json.loads(f.read_text(encoding="utf-8"))["entries"]:
            prev = ds.sources.get(e["id"])
            # The same id may appear in several thematic files; keep the verified copy if any.
            if prev is None or (not prev.get("verified") and e.get("verified")):
                ds.sources[e["id"]] = dict(e, _file=f.name)
    for f in sorted((data / "Profiles").glob("*.json")):
        raw = json.loads(f.read_text(encoding="utf-8"))
        ds.profiles[raw["id"]] = raw
    return ds
