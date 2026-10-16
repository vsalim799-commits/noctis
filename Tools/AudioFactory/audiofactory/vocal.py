"""Vocal hypotheses for every species call type in Data/Species/*/*.json (sim.vocal.call_types).

The synthesis mirrors the C++ SignalSynth (Source/NoctisCore/Private/Research/Acoustics.cpp) so that
what the player hears and what the in-game recorder analyses are the same signal model:
  * f(t) = f0_json * pitchShift * contour(context, u) with the core's contours;
  * closed-mouth calls: 8 harmonics weighted h^-2.5 (near-sinusoidal 'coo/boom' hypothesis);
  * threat calls: 12 Hz amplitude modulation; distress calls: 4 % jitter (0.8 % otherwise);
  * envelope: 12 % attack, 25 % release;
  * pitchShift = sizeRatio^-0.4 * (0.95 + 0.1 * markingSeed), level += 5 log10(sizeRatio) (CreatureSystem::emitCall).
Distance variants use the ISO 9613-1 absorption and the foliage model of SoundField.cpp (see propagation.py).
Every call is a labelled HYPOTHESIS: no recording of a non-avian dinosaur exists.
"""
from __future__ import annotations

import math

import numpy as np

from . import common as C
from . import data as D
from . import manifest as M
from . import propagation as P

# Marking seeds -> individual voices. voiceD (added in 1.2) completes the 4 variations per one-shot; A-C are unchanged.
VOICES = [("voiceA", 0.15), ("voiceB", 0.50), ("voiceC", 0.85), ("voiceD", 0.35)]
INFRASONIC_HZ = 20.0
LOW_REPRO_HZ = 30.0         # below ~30 Hz most speakers and headphones reproduce almost nothing
LOW_REPRO_FLAG = 0.25       # energy fraction below 30 Hz above which a visual cue is mandatory (accessibility)
SUBADULT_SIZE_RATIO = 0.10                                          # subadult variant: 10 % of adult mass
DISTANCES = {
    "hell_creek": [("d50_open", 50.0, 0.0), ("d200_open", 200.0, 0.0), ("d800_open", 800.0, 0.0), ("d200_forest", 200.0, 60.0)],
    "djadochta": [("d50_open", 50.0, 0.0), ("d200_open", 200.0, 0.0), ("d800_open", 800.0, 0.0)],
}
AUDIBLE_FLOOR_DB = 15.0     # distance variants below this received SPL at f0 are not generated
RANGE_FLOOR_DB = 25.0       # attenuation falloff: distance where the call reaches a quiet natural ambience
RIEDE = "riede2016_closed_mouth_vocalization"
FLETCHER = "fletcher2004_frequency_scaling"
ISO1 = "iso1993_9613_1"
ISO2 = "iso1996_9613_2"
ARCHOSAUR_PLANS = ("biped_", "facultative_quadruped_hadrosaur", "quadruped_ceratopsian", "quadruped_ankylosaur",
                   "quadruped_crocodylian", "bird_flyer")
CONTEXT_FR = {"contact": "contact", "alarm": "alarme", "distress": "détresse", "threat": "menace",
              "courtship": "parade", "begging": "juvénile / quémande", "territorial": "territorial", "other": "autre"}
CONTEXT_EN = {"contact": "contact", "alarm": "alarm", "distress": "distress", "threat": "threat",
              "courtship": "courtship", "begging": "juvenile / begging", "territorial": "territorial", "other": "other"}
STYLE_FR = {
    "closed": "vocalisation à bouche fermée (quasi sinusoïdale, harmoniques en h^-2,5)",
    "open": "vocalisation à bouche ouverte (source harmonique filtrée par des formants de conduit vocal)",
    "hiss": "souffle / sifflement non tonal (bruit turbulent filtré)",
    "huff": "expiration forcée par des cavités nasales (bruit résonant)",
}


def call_style(ds: D.Dataset, sp: D.Species, call: dict) -> str:
    cid = call["id"]
    f0 = float(call["f0_hz"])
    if f0 <= 0.0 or "hiss" in cid:
        return "hiss"
    if "huff" in cid:
        return "huff"
    closed = bool(sp.sim["vocal"].get("closed_mouth", False))
    # Closed-mouth model only for archosaurs and only while its supporting reference is in Data/Sources.
    if closed and sp.body_plan.startswith(ARCHOSAUR_PLANS) and ds.has_source(RIEDE):
        return "closed"
    return "open"


def vocal_tract_length_m(mass_kg: float) -> float:
    """Isometric scaling from a 70 kg mammal (~0.17 m): L = 0.17 (M/70)^(1/3). Game assumption."""
    return 0.17 * (mass_kg / 70.0) ** (1.0 / 3.0)


def synth_call(style: str, f0: float, dur: float, ctx: str, pitch: float, mass: float, r: np.random.Generator,
               timbre: float = 0.0):
    """Returns (signal, f0 trajectory, base envelope, call start sample, call length)."""
    pre, post = 0.02, (0.2 if style == "closed" else 0.15)
    n_call = C.ns(dur)
    i0 = C.ns(pre)
    n = i0 + n_call + C.ns(post)
    k = np.arange(n)
    u = np.clip((k - i0) / n_call, 0.0, 1.0)
    t_call = (k - i0) / C.SR
    jitter_rel = 0.04 if ctx == "distress" else 0.008
    f0_eff = max(5.0, f0) * pitch
    f = f0_eff * D.contour(ctx, u) * (1.0 + jitter_rel * C.smooth_noise(n, r, 6.0))
    base_env = np.zeros(n)
    if style == "hiss":
        base_env[i0:i0 + n_call] = C.ramp_env(n_call, int(0.06 * n_call), int(0.30 * n_call))
    elif style == "huff":
        e = np.exp(-np.arange(n_call) / (0.45 * n_call))
        base_env[i0:i0 + n_call] = e * C.ramp_env(n_call, int(0.04 * n_call), int(0.25 * n_call))
    else:
        base_env[i0:i0 + n_call] = C.ramp_env(n_call, int(0.12 * n_call), int(0.25 * n_call))
    env = base_env.copy()
    if ctx == "threat":
        env *= 0.65 + 0.35 * np.sin(2.0 * math.pi * 12.0 * t_call)
    env *= 1.0 + 0.04 * C.smooth_noise(n, r, 10.0)       # shimmer
    phase = 2.0 * math.pi * np.cumsum(f) / C.SR

    if style == "closed":
        expo = 2.5 + timbre * 0.15
        s = np.zeros(n)
        for h in range(1, 9):
            s += h ** -expo * np.sin(h * phase + r.uniform(0, 2 * math.pi)) * (h * f < C.NYQ_SAFE)
        breath = C.bandpass(C.white(n, r), f0_eff, min(8.0 * f0_eff, 12000.0))
        s += C.db2lin(-32.0) * breath / (np.std(breath) + 1e-12) * np.std(s)
    elif style == "open":
        H = int(min(120, C.NYQ_SAFE / max(f.min(), 20.0)))
        src = np.zeros(n)
        for h in range(1, H + 1):
            src += (1.0 / h) * np.sin(h * phase + r.uniform(0, 2 * math.pi)) * (h * f < C.NYQ_SAFE)
        L = vocal_tract_length_m(mass) * (1.0 + 0.03 * timbre)
        c = P.speed_of_sound(20.0)
        formants = [(2 * j - 1) * c / (4.0 * L) for j in range(1, 5)]
        asp = C.white(n, r) * np.std(src) * C.db2lin(-24.0)
        x = src + asp
        s = 0.25 * x + C.resonator_bank(x, formants, [6.0] * 4, [1.0, 0.7, 0.45, 0.3])
    elif style == "hiss":
        if f0 > 0.0:
            lo = f0_eff
            hi = min(8.0 * lo, 12000.0)
        else:
            lo, hi = 1500.0, 9000.0          # small-lizard defensive hiss (game assumption)
        s = C.bandpass(C.white(n, r), lo, hi, order=2) * (1.0 + 0.3 * C.smooth_noise(n, r, 30.0))
        s /= np.std(s) + 1e-12
        if f0 > 0.0:
            ton = sum(h ** -2.0 * np.sin(h * phase) for h in range(1, 4))
            s += C.db2lin(-12.0) * ton / (np.std(ton) + 1e-12)
    else:  # huff
        nz = C.white(n, r)
        res = [(2 * j - 1) * f0_eff for j in range(1, 5)]
        s = C.resonator_bank(nz, res, [4.0] * 4, [1.0, 0.6, 0.4, 0.25])
        s /= np.std(s) + 1e-12
        ton = np.sin(phase)
        s += C.db2lin(-8.0) * ton / (np.std(ton) + 1e-12)
    return s * env, f, base_env, i0, n_call


def _expected_f0(f, base_env) -> float:
    sel = base_env > 0.5
    return float(np.median(f[sel])) if np.any(sel) else float(np.median(f))


def jobs(ds: D.Dataset) -> list[tuple]:
    out = []
    for sp in ds.species.values():
        for ci, _ in enumerate((sp.sim.get("vocal") or {}).get("call_types") or []):
            out.append(("vocal", "job", {"species": sp.id, "ci": ci}))
    return out


def job(ds: D.Dataset, species: str, ci: int) -> list[M.Sound]:
    sp = ds.species[species]
    env = ds.env(sp.region)
    call = sp.sim["vocal"]["call_types"][ci]
    return _build_call(ds, sp, call, ci, sp.claim("communication"), env.temperature_c, env.humidity)


def _build_call(ds, sp, call, ci, comm, T, RH):
    style = call_style(ds, sp, call)
    ctx = D.parse_call_context(call.get("context", "other"))
    f0 = float(call["f0_hz"])
    dur = float(call["duration_s"])
    lvl = float(call["source_level_db"])
    srcs = list(comm.get("sources", []))
    if style == "closed":
        srcs.append(RIEDE)
    srcs.append(FLETCHER)
    srcs = [s for s in dict.fromkeys(srcs) if ds.has_source(s)]
    conf = ds.capped(call.get("confidence", "speculative"), srcs)
    if style == "closed":
        # Project rule: the closed-mouth production model is itself a hypothesis (Riede et al. 2016 reconstruct the
        # behaviour's evolution, no non-avian call is known), so the synthesised sound stays SPECULATIVE even when
        # the existence of the call type is INFERRED (e.g. juvenile crocodyliform distress calls by phylogenetic bracket).
        conf = D.conf_min(conf, "speculative")
    toggle = {"contact": "contact_calls", "alarm": "alarm_calls"}.get(ctx)
    basis = (f"Paramètres sim.vocal ({sp.basis('vocal.call_types')}). "
             f"Communication [{D.CONFIDENCE_LABEL_FR[comm['confidence']]}] : {comm['summary']} "
             f"Modèle de production : {STYLE_FR[style]}. Contour de fréquence et enveloppe du cœur (SignalSynth).")
    group = f"vocal/{sp.id}"
    stem = f"voc_{sp.id}_{call['id']}"
    fr_ctx = CONTEXT_FR[ctx]
    out = []
    voice_A = None
    for vname, mseed in VOICES:
        r = C.rng(stem, vname)
        pitch = 0.95 + 0.1 * mseed
        dur_v = dur * (1.0 + 0.1 * (mseed - 0.5))     # +/-4 % individual duration
        timbre = (mseed - 0.5) * 2.0
        x, f, benv, i0, ncall = synth_call(style, f0, dur_v, ctx, pitch, sp.mass, r, timbre)
        f0_exp = _expected_f0(f, benv)
        if vname == "voiceA":
            voice_A = (x, f0_exp, pitch, dur_v)
        out.append(_sound(ds, sp, call, stem + "_" + vname, group, x, style, ctx, f0, f0_exp, pitch, 1.0, dur_v, lvl, T, RH,
                          conf, srcs, basis, toggle, {"voice": vname, "marking_seed": mseed, "size_ratio": 1.0},
                          f"{_label_head(sp, call, fr_ctx)} — voix {vname[-1]}", random_group=f"{stem}_adult"))
    # Subadult voice: only for the first contact-type call of species heavier than 20 kg.
    if ctx == "contact" and sp.mass >= 20.0 and ci == next((i for i, c in enumerate(sp.sim["vocal"]["call_types"])
                                                         if D.parse_call_context(c.get("context", "")) == "contact"), -1):
        r = C.rng(stem, "subadult")
        pitch = SUBADULT_SIZE_RATIO ** D.VOCAL_PITCH_EXPONENT * (0.95 + 0.1 * 0.5)
        lvl_s = lvl + D.VOCAL_LEVEL_SLOPE_DB * math.log10(SUBADULT_SIZE_RATIO)
        x, f, benv, i0, ncall = synth_call(style, f0, dur, ctx, pitch, sp.mass * SUBADULT_SIZE_RATIO, r, 0.0)
        out.append(_sound(ds, sp, call, stem + "_subadult", group, x, style, ctx, f0, _expected_f0(f, benv), pitch,
                          SUBADULT_SIZE_RATIO, dur, lvl_s, T, RH, conf, srcs, basis, toggle,
                          {"voice": "subadult", "marking_seed": 0.5, "size_ratio": SUBADULT_SIZE_RATIO},
                          f"{_label_head(sp, call, fr_ctx)} — subadulte (10 % de la masse adulte)"))
    # Distance variants from voice A.
    xA, f0A, pitchA, durA = voice_A
    for dname, dist, fol in DISTANCES.get(sp.region, []):
        rec = P.received_level_db(lvl, f0A, dist, T, RH, foliage_m=fol)
        if rec < AUDIBLE_FLOOR_DB:
            continue
        y = P.apply_propagation(xA, dist, T, RH, foliage_m=fol, ref_hz=f0A)
        s = _sound(ds, sp, call, f"{stem}_{dname}", group, y, style, ctx, f0, f0A, pitchA, 1.0, durA, lvl, T, RH, conf,
                   list(dict.fromkeys(srcs + [ISO1] + ([ISO2] if fol > 0 else []))), basis, toggle,
                   {"voice": "voiceA", "distance_m": dist, "foliage_equivalent_m": fol},
                   f"{_label_head(sp, call, fr_ctx)} — entendu à {dist:.0f} m"
                   + (" à travers la forêt riveraine" if fol > 0 else " en terrain découvert"),
                   distance_m=dist)
        # Calibrate the file on the level received at that distance (finalize: level.received_spl_db), not at 1 m.
        s.level["received_spl_db"] = round(rec, 2)
        s.level["reference"] = ("Niveau reçu à f0 (dB SPL) à la distance de la variante : divergence sphérique + ISO 9613-1"
                                + (" + feuillage" if fol > 0 else "") + " ; source_spl_db_1m = niveau de la voix à 1 m")
        s.params["propagation"] = {
            "distance_m": dist, "temperature_c": T, "relative_humidity": RH, "pressure_kpa": 101.325,
            "foliage_equivalent_m": fol, "reference_hz": round(f0A, 3),
            "absorption_db_per_m_at_f0": float(P.absorption_db_per_m(f0A, T, RH)),
            "received_spl_db_at_f0": round(rec, 2),
            "model": "SoundField.cpp : divergence sphérique + ISO 9613-1 + feuillage ISO 9613-2 (vent et relief non inclus)",
            "file_normalisation": "réponse normalisée à 0 dB à f0 ; la perte de niveau est dans received_spl_db_at_f0",
            "foliage_note_fr": ("Équivalent de 60 m de feuillage dense sur un trajet de 200 m (hypothèse de jeu)" if fol > 0 else None),
        }
        s.qa["transfer"] = {"base_id": f"{stem}_voiceA", "distance_m": dist, "temperature_c": T, "relative_humidity": RH,
                            "foliage_m": fol, "ref_hz": round(f0A, 3),
                            "check_hz": [round(f0A * h, 3) for h in (2, 3, 4) if f0A * h < 20000.0] if style in ("closed", "open")
                            else [round(v, 1) for v in np.geomspace(max(200.0, f0A), 9000.0, 4)]}
        acc = s.ue5.get("accessibility")
        s.ue5 = M.ue5("SC_Creature_Vocal_Hypothesis", "ATT_Vocal_DistanceBaked", False, "CON_Vocal_PerSpecies",
                      priority=0.6, falloff_m=None, extra={"distance_variant_m": dist, **({"accessibility": acc} if acc else {})})
        out.append(s)
    return out


def _label_head(sp, call, fr_ctx) -> str:
    """UI label: the call is named as a hypothesis with its *supposed* context (never as a known function)."""
    return f"{sp.name_fr} — cri hypothétique [{call['id']}] (contexte supposé : {fr_ctx})"


def low_fractions(x: np.ndarray) -> tuple[float, float]:
    """Fractions of the signal energy below 20 Hz (infrasound) and below 30 Hz (closed-mouth rumbles of the largest
    species): the second one decides the accessibility flag, the first one is reported."""
    spec = np.abs(np.fft.rfft(np.asarray(x, dtype=np.float64))) ** 2
    f = np.fft.rfftfreq(x.shape[0], 1.0 / C.SR)
    tot = float(spec[1:].sum())
    if tot <= 0:
        return 0.0, 0.0
    return float(spec[1:][f[1:] < INFRASONIC_HZ].sum() / tot), float(spec[1:][f[1:] < LOW_REPRO_HZ].sum() / tot)


def _sound(ds, sp, call, sid, group, x, style, ctx, f0, f0_exp, pitch, size_ratio, dur, lvl, T, RH, conf, srcs, basis, toggle,
           variant, label, random_group=None, distance_m=None):
    rng_m = P.audible_range_m(lvl, f0_exp, T, RH, RANGE_FLOOR_DB)
    att = M.vocal_attenuation_for(rng_m)
    qa = {"active_duration_s": [round(dur * 0.72, 3), round(dur * 1.12 + 0.03, 3)], "peak_dbfs_max": -0.5}
    if style in ("closed", "open"):
        qa["f0_hz"] = round(f0_exp, 3)
        qa["f0_tolerance"] = 0.10
    elif style == "hiss":
        lo = f0 * pitch if f0 > 0 else 1500.0
        qa["centroid_hz"] = [round(lo * 0.8, 1), round(min(8.0 * lo, 12000.0) * 1.1, 1)]
    else:
        qa["spectral_peak_hz"] = [round(f0_exp * 0.75, 2), round(f0_exp * 1.35, 2)]
    params = {
        "call_type": call["id"],
        "context_json": call.get("context"),
        "call_context_core": ctx,
        "production_model": style,
        "f0_json_hz": f0,
        "pitch_shift": round(pitch, 5),
        "size_ratio": size_ratio,
        "f0_effective_median_hz": round(f0_exp, 3),
        "duration_json_s": float(call["duration_s"]),
        "duration_s": round(dur, 4),
        "f0_range_json_hz": [sp.sim["vocal"].get("f0_min_hz"), sp.sim["vocal"].get("f0_max_hz")],
        "contour": "acousticsimpl::contour (Acoustics.cpp)",
        "jitter_rel": 0.04 if ctx == "distress" else 0.008,
        "amplitude_modulation_hz": 12.0 if ctx == "threat" else None,
        "envelope": {"attack_frac": 0.12, "release_frac": 0.25} if style in ("closed", "open") else None,
        "frequency_scaling": "pitchShift = sizeRatio^-0.4 × (0.95 + 0.1 × markingSeed) — CreatureSystem::emitCall",
        "audible_range_m": round(rng_m, 1),
        "audible_range_floor_db": RANGE_FLOOR_DB,
        "environment": {"region": sp.region, "temperature_c": T, "relative_humidity": RH},
    }
    if style == "open":
        params["vocal_tract_length_m"] = round(vocal_tract_length_m(sp.mass * size_ratio), 4)
    infra, low = low_fractions(x)
    params["infrasonic_energy_fraction"] = round(infra, 4)
    params["below_30hz_energy_fraction"] = round(low, 4)
    extra = {"random_group": random_group} if random_group else {}
    name_en = sp.raw.get("common_names", {}).get("en", sp.name)
    ctx_fr, ctx_en = CONTEXT_FR[ctx], CONTEXT_EN[ctx]
    cap_fr = f"Cri hypothétique {D.de_fr(sp.name_fr)} (contexte supposé : {ctx_fr})"
    cap_en = f"Hypothetical call of {name_en} (supposed context: {ctx_en})"
    if distance_m is not None:
        cap_fr += f", entendu à {distance_m:.0f} m"
        cap_en += f", heard at {distance_m:.0f} m"
    if low >= LOW_REPRO_FLAG:
        cap_fr += " — très grave" + (", en partie infrasonore" if infra >= 0.05 else "")
        cap_en += " — very low" + (", partly infrasonic" if infra >= 0.05 else "")
        extra["accessibility"] = {
            "visual_cue_required": True,
            "note_fr": (f"{100 * low:.0f} % de l'énergie sous 30 Hz (dont {100 * infra:.0f} % sous 20 Hz) : presque inaudible sur la "
                        "plupart des haut-parleurs et casques. "
                        "Toujours afficher la légende et le spectrogramme ; une couche « proxy audible » éventuelle devra être étiquetée "
                        "comme telle (à trancher lors de la passe d'écoute)."),
        }
    return M.Sound(
        id=sid, category="vocal", subcategory=f"call_{ctx}", group=group, label_fr=label, audio=x,
        confidence=conf, basis_fr=basis, sources=srcs, params=params, qa=qa, region=sp.region, species=sp.id,
        hypothesis=True, profile_toggle=toggle, variant=variant,
        level={"source_spl_db_1m": round(lvl, 2), "reference": "RMS de la partie tenue du cri, dB SPL re 20 µPa à 1 m (convention SoundSource::levelDb)"},
        ue5=M.ue5("SC_Creature_Vocal_Hypothesis", att, False, "CON_Vocal_PerSpecies", priority=min(1.0, 0.4 + lvl / 200.0),
                  falloff_m=rng_m, extra=extra or None),
        normalize=("peak", -1.0),
        caption={"fr": cap_fr, "en": cap_en},
        model_confidence=D.conf_min(conf, "speculative"),
        model_basis_fr="Timbre et contour = modèle de production hypothétique (SignalSynth) ; aucun cri de dinosaure non avien n'est connu.",
    )
