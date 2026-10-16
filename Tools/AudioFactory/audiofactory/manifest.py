"""Sound records and Unreal Engine 5 import hints."""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from . import data as D

# --------------------------------------------------------------------------------------------
# UE5 presets (names are the assets the UE5 layer is expected to create; values in metres and cm)
# --------------------------------------------------------------------------------------------
SOUND_CLASSES = {
    "SC_Noctis_Master": {"parent": None},
    "SC_Creatures": {"parent": "SC_Noctis_Master"},
    "SC_Creature_Vocal_Hypothesis": {
        "parent": "SC_Creatures",
        "note_fr": "Vocalisations hypothétiques. Classe séparée pour pouvoir les couper ou les signaler selon le profil de reconstruction actif.",
    },
    "SC_Creature_Footstep": {"parent": "SC_Creatures"},
    "SC_Creature_Foley": {"parent": "SC_Creatures"},
    "SC_Ambience": {"parent": "SC_Noctis_Master"},
    "SC_Ambience_Bed": {"parent": "SC_Ambience"},
    "SC_Ambience_Layer": {"parent": "SC_Ambience"},
    "SC_Ambience_OneShot": {"parent": "SC_Ambience"},
    "SC_Weather": {"parent": "SC_Ambience"},
    "SC_Equipment": {"parent": "SC_Noctis_Master"},
    "SC_Equipment_Vehicle": {"parent": "SC_Equipment"},
    "SC_Equipment_Drone": {"parent": "SC_Equipment"},
    "SC_Equipment_Handheld": {"parent": "SC_Equipment"},
    "SC_Equipment_Radio": {"parent": "SC_Equipment"},
    "SC_Player": {"parent": "SC_Noctis_Master"},
    "SC_Player_Footstep": {"parent": "SC_Player"},
    "SC_Player_Foley": {"parent": "SC_Player"},
}


def _att(inner_m, falloff_m, lpf=True, note=None):
    """Spatialised preset with a distance model (Sphere / NaturalSound). Every numeric field is set: the UE importer
    (Content/Python/import_audio.py) configures the attenuation from them when attenuate is true."""
    d = {
        "spatialize": True,
        "attenuate": True,
        "shape": "Sphere",
        "distance_algorithm": "NaturalSound",
        "inner_radius_m": inner_m,
        "falloff_distance_m": falloff_m,
        "inner_radius_cm": round(inner_m * 100.0),
        "falloff_distance_cm": round(falloff_m * 100.0),
        "db_attenuation_at_max": -60.0,
        "attenuate_with_lpf": bool(lpf),
    }
    if note:
        d["note_fr"] = note
    return d


def _att_no_distance(spatial: bool, note: str) -> dict:
    """No distance model (attenuate = false): 2D beds and first-person sounds (spatialize = false), or distance-baked
    files that UE5 only spatialises (direction) because the propagation loss is already in the file / in the level."""
    return {"spatialize": spatial, "attenuate": False, "attenuate_with_lpf": False, "note_fr": note}


_BAKED = ("Variante de distance : divergence, absorption ISO 9613-1 (et feuillage) déjà dans le fichier et dans "
          "level.received_spl_db (calibration) — UE5 ne fait que spatialiser (direction), sans modèle de distance, "
          "pour ne compter ni la perte de niveau ni l'absorption deux fois.")

ATTENUATION_PRESETS = {
    "ATT_Vocal_Short": _att(2, 150),
    "ATT_Vocal_Medium": _att(5, 600),
    "ATT_Vocal_Long": _att(10, 2000),
    "ATT_Vocal_VeryLong": _att(20, 5000),
    "ATT_Vocal_DistanceBaked": _att_no_distance(True, _BAKED),
    "ATT_Footstep_Small": _att(1, 40),
    "ATT_Footstep_Medium": _att(2, 150),
    "ATT_Footstep_Large": _att(5, 600),
    "ATT_Foley_Close": _att(1, 30),
    "ATT_Foley_Medium": _att(2, 120),
    "ATT_Foley_Large": _att(4, 400),
    "ATT_Ambience_Point": _att(10, 400),
    "ATT_Ambience_Bed2D": _att_no_distance(False, "Lit d'ambiance stéréo non spatialisé."),
    "ATT_Thunder_DistanceBaked": _att_no_distance(
        True, "Tonnerre : retard, absorption et durée du roulement intégrés au fichier ; niveau reçu dans level.received_spl_db. "
              "Le directeur audio (ANoctisAudioDirector) place le son au point d'impact (direction de l'éclair) et corrige le "
              "niveau d'après le cœur : UE5 ne fait que spatialiser."),
    "ATT_Vehicle": _att(3, 1500),
    "ATT_Drone": _att(2, 1000),
    "ATT_Drone_DistanceBaked": _att_no_distance(True, _BAKED),
    "ATT_Handheld_FirstPerson": _att_no_distance(False, "Matériel tenu par le joueur : 2D, sans atténuation."),
    "ATT_Generator_DistanceBaked": _att_no_distance(True, _BAKED),
    "ATT_Water_Line": _att(5, 400, note="Rivière : source linéique ; placer plusieurs émetteurs le long du chenal (-3 dB par doublement de distance)."),
}

CONCURRENCY_PRESETS = {
    "CON_Vocal_PerSpecies": {"max_count": 4, "resolution_rule": "StopQuietest", "limit_to_owner": False},
    "CON_Footsteps": {"max_count": 24, "resolution_rule": "StopFarthestThenOldest", "limit_to_owner": False},
    "CON_Footsteps_PerOwner": {"max_count": 4, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Foley": {"max_count": 12, "resolution_rule": "StopQuietest", "limit_to_owner": False},
    "CON_Breathing_PerOwner": {"max_count": 1, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Player_Foley": {"max_count": 4, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Flight_PerOwner": {"max_count": 2, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Ambience_Bed": {"max_count": 4, "resolution_rule": "StopOldest", "limit_to_owner": False},
    "CON_Ambience_Layer": {"max_count": 8, "resolution_rule": "StopQuietest", "limit_to_owner": False},
    "CON_Weather_Thunder": {"max_count": 2, "resolution_rule": "StopOldest", "limit_to_owner": False},
    # Light / heavy and forest / water rain beds crossfade and overlap (riverbank under trees), interior + exterior.
    "CON_Weather_Rain": {"max_count": 4, "resolution_rule": "StopQuietest", "limit_to_owner": False},
    # Rover: two speed loops crossfade; the generator (with its start -> run hand-off), the doors and the drive are
    # separate groups so that a door never stops the engine and generator + electric drive play together.
    "CON_Vehicle_Drive": {"max_count": 2, "resolution_rule": "StopQuietest", "limit_to_owner": True},
    "CON_Vehicle_Generator": {"max_count": 2, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Vehicle_Foley": {"max_count": 2, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Drone_Rotor": {"max_count": 2, "resolution_rule": "StopQuietest", "limit_to_owner": True},
    "CON_Equipment_Unique": {"max_count": 1, "resolution_rule": "StopOldest", "limit_to_owner": True},
    "CON_Equipment_Handheld": {"max_count": 3, "resolution_rule": "StopOldest", "limit_to_owner": False},
    "CON_Water_Splash": {"max_count": 8, "resolution_rule": "StopQuietest", "limit_to_owner": False},
    "CON_Fire": {"max_count": 3, "resolution_rule": "StopQuietest", "limit_to_owner": False},
}


def ue5(sound_class: str, attenuation: str, looping: bool, concurrency: str, priority: float = 1.0,
        falloff_m: float | None = None, virtualize: bool = True, submix: str | None = None, extra: dict | None = None) -> dict:
    assert sound_class in SOUND_CLASSES, sound_class
    assert attenuation in ATTENUATION_PRESETS, attenuation
    assert concurrency in CONCURRENCY_PRESETS, concurrency
    d = {
        "sound_class": sound_class,
        "attenuation_preset": attenuation,
        "looping": looping,
        "concurrency": concurrency,
        "priority": priority,
        "virtualization": "PlayWhenSilent" if looping and virtualize else ("Restart" if looping else "Default"),
        "submix": submix or {"SC_Creature_Vocal_Hypothesis": "SM_Creatures_Hypotheses"}.get(sound_class, "SM_World"),
        "import": {"compression_quality": 80 if looping else 90, "loading_behavior": "RetainOnLoad" if not looping else "LoadOnDemand"},
    }
    if falloff_m is not None:
        d["falloff_distance_m_suggested"] = round(float(falloff_m), 1)
    if extra:
        d.update(extra)
    return d


def vocal_attenuation_for(range_m: float) -> str:
    if range_m <= 150:
        return "ATT_Vocal_Short"
    if range_m <= 600:
        return "ATT_Vocal_Medium"
    if range_m <= 2000:
        return "ATT_Vocal_Long"
    return "ATT_Vocal_VeryLong"


# --------------------------------------------------------------------------------------------
# Sound record
# --------------------------------------------------------------------------------------------
@dataclass
class Sound:
    id: str
    category: str                 # vocal | footstep | foley | ambience | weather | equipment
    subcategory: str
    group: str                    # folder under Build/Audio and contact-sheet key
    label_fr: str
    audio: np.ndarray             # float64, (n,) mono or (n, 2) stereo; not serialised
    confidence: str
    basis_fr: str
    sources: list
    params: dict
    ue5: dict
    qa: dict = field(default_factory=dict)
    loop: bool = False
    region: str | None = None
    species: str | None = None
    equipment: str | None = None
    hypothesis: bool = False
    profile_toggle: str | None = None
    level: dict = field(default_factory=dict)  # source_spl_db_1m / normalization
    normalize: tuple = ("peak", -1.0)
    fade: bool = True
    variant: dict = field(default_factory=dict)
    caption: dict = field(default_factory=dict)  # {"fr": ..., "en": ...} accessibility caption
    subject: str = "paleo"        # paleo | physical_environment | researcher | modern_equipment
    loop_xfade: bool = False      # loop already made seamless by construction (circular rendering)
    # Confidence scope: `confidence` covers the existence of the event and its documented inputs (mass, kinematics,
    # discharge, rain rate...); `model_confidence` covers the synthesis model (timbre, level law), usually a game model.
    model_confidence: str | None = None
    model_basis_fr: str | None = None
    transition: bool = False      # one-shot that intentionally ends at full level (hand-off to a loop)

    def record(self, ds: D.Dataset) -> dict:
        r = {
            "id": self.id,
            "category": self.category,
            "subcategory": self.subcategory,
            "label_fr": self.label_fr,
            "region": self.region,
            "species": self.species,
            "equipment": self.equipment,
            "variant": self.variant,
            "confidence": self.confidence,
            "confidence_label_fr": D.CONFIDENCE_LABEL_FR[self.confidence],
            "hypothesis": self.hypothesis,
            "basis_fr": self.basis_fr,
            "sources": ds.check_sources(self.sources),
            "parameters": self.params,
            "level": self.level,
            "ue5": self.ue5,
            "qa": self.qa,
        }
        r["subject"] = self.subject
        if self.subject in ("researcher", "modern_equipment"):
            r["confidence_note_fr"] = ("Son moderne de conception de jeu (equipment.json : hypothèses de conception) ; "
                                       "le niveau de confiance ne porte pas sur une question paléontologique.")
        mc = self.model_confidence or self.confidence
        r["model_confidence"] = mc
        r["model_confidence_label_fr"] = D.CONFIDENCE_LABEL_FR[mc]
        r["overall_confidence"] = D.conf_min(self.confidence, mc)
        if self.model_basis_fr:
            r["model_basis_fr"] = self.model_basis_fr
        if self.caption:
            cap = dict(self.caption)
            if self.hypothesis:
                # Append the label only when the caption does not already say it.
                if "hypoth" not in cap["fr"].lower():
                    cap["fr"] = cap["fr"] + " (hypothèse)"
                if "hypoth" not in cap["en"].lower():
                    cap["en"] = cap["en"] + " (hypothesis)"
            r["caption"] = cap
        if self.profile_toggle:
            r["profile_toggle"] = self.profile_toggle
            r["profile_states"] = ds.profile_toggle_states(self.profile_toggle)
        if self.hypothesis:
            r["disclaimer_fr"] = ("Hypothèse de reconstruction : aucun enregistrement ni preuve directe de ce son n'existe. "
                                  "Le jeu ne prétend connaître ni le timbre exact ni la signification de ce cri.")
        return r
