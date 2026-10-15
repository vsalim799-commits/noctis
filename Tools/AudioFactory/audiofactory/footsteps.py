"""Footfalls by body mass, gait and substrate, for every legged species of both formations and for the
player (field researcher in boots).

Kinematics mirror the core (Source/NoctisCore/Private/Creatures/Locomotion.cpp):
  * Froude number Fr = v^2 / (g h); duty factor = lerp(walk, run, saturate((Fr - 0.15) / 0.85));
  * stride / hip = max(0.5, 2.3 Fr^0.3) (dynamic similarity, Alexander), contact time = duty x stride period;
  * peak foot force = m g share (1 + 0.6 min(Fr, 2)), share = 1 (biped) or 0.55 (hind foot of a quadruped);
  * source level (dB SPL at 1 m) = 35 + 12 log10 m + 6 sqrt(Fr), -6 dB mud, +4 dB vegetation, +4 dB water
    (footfallSound), and the core's 'thump' frequency 600 m^-0.25 + 30 Hz is used as the silt reference.

Synthesis (physically informed, not random noise):
  * thump: the foot + yielding ground is a damped mass-spring system. Natural frequency = core frequency x
    sqrt(relative contact stiffness of the substrate); damping ratio per substrate (game assumptions).
    The m^-0.25 law is close to what a mass-spring contact predicts (k ~ contact radius ~ m^1/3 -> f ~ m^-1/3).
  * substrate texture: sand = thousands of grain micro-impacts (impulse train through grain-cluster modes);
    silt = dull crumbling; mud = wet slap then suction release when the foot is lifted at the end of stance
    (cavity 'bubble' at the Minnaert frequency of a cavity ~0.15 x foot width); shallow water = slap, crown
    splash, entrained-bubble cloud (Minnaert) and droplets falling back after 2 v_up / g; leaf litter = brittle
    leaf fractures (impulses through leaf-plate modes); fern cover = fronds brushing the leg during the end
    of the swing phase, then frond snaps for large animals.
"""
from __future__ import annotations

import math

import numpy as np

from . import common as C
from . import data as D
from . import manifest as M

G = C.G
VARIATIONS = 4
GAITS = ("walk", "run")
NO_FEET = ("fish", "invertebrate")
FOOT_AREA_FILL = 0.55          # SpeciesRuntime::footAreaFill
A_REF = 0.05                   # m, reference contact radius for event counts (game assumption)

# k_rel: contact stiffness relative to damp silt; zeta: damping ratio; level_adj: core footfallSound terms.
SUBSTRATES = {
    "mud": dict(core="mud", k_rel=0.25, zeta=0.75, level_adj=-6.0, thump=0.8, tex=1.0, fr="boue", en="mud"),
    "silt": dict(core="silt", k_rel=1.0, zeta=0.35, level_adj=0.0, thump=1.0, tex=0.45, fr="limon", en="silt"),
    "sand": dict(core="sand", k_rel=0.7, zeta=0.45, level_adj=0.0, thump=0.9, tex=0.8, fr="sable", en="sand"),
    "shallow_water": dict(core="silt", k_rel=0.5, zeta=0.6, level_adj=4.0, thump=0.35, tex=1.0, water=True,
                          fr="eau peu profonde", en="shallow water"),
    "leaf_litter": dict(core="forest_soil", k_rel=0.6, zeta=0.5, level_adj=4.0, thump=0.6, tex=1.0,
                        fr="litière de feuilles", en="leaf litter"),
    "fern_cover": dict(core="forest_soil", k_rel=0.6, zeta=0.5, level_adj=4.0, thump=0.55, tex=1.0,
                       fr="couvert de fougères", en="fern cover"),
}
REGION_SUBSTRATES = {
    # Hell Creek: channel margins, sandbars, backswamps, forest floor and fern-rich floodplain.
    "hell_creek": ["mud", "silt", "sand", "shallow_water", "leaf_litter", "fern_cover"],
    # Djadochta: dunes and interdune flats, ephemeral ponds; no ferns nor broadleaf litter (environment JSON).
    "djadochta": ["sand", "silt", "shallow_water"],
}
GAIT_FR = {"walk": "marche", "run": "allure rapide"}
GAIT_EN = {"walk": "walk", "run": "fast gait"}
ALEXANDER = "alexander1976_dinosaur_speeds"
ALEXANDER83 = "alexander1983_dynamic_similarity"
MINNAERT = "minnaert1933_musical_air_bubbles"
PROSPERETTI = "prosperetti1993_rain_noise"

RESEARCHER = {
    "id": "researcher", "name_fr": "Chercheur (bottes de terrain)", "name_en": "Researcher (field boots)",
    "mass": 80.0, "hip": 0.9, "foot_length": 0.30, "foot_width": 0.11, "dfw": 0.62, "dfr": 0.38, "vw": 1.3, "vr": 3.5,
    "biped": True,
}


def legged(sp: D.Species) -> bool:
    return sp.body_plan not in NO_FEET and float(sp.sim.get("foot_length_m", 0) or 0) > 0


def gait_state(mass, hip, dfw, dfr, v, biped):
    """Mirror of the gait block of integrateLocomotion (Locomotion.cpp)."""
    fr = v * v / (G * hip)
    run_blend = float(C.saturate((fr - 0.15) / 0.85))
    df = dfw + (dfr - dfw) * run_blend
    stride = max(0.5, 2.3 * max(fr, 1e-4) ** 0.3) * hip
    period = stride / max(v, 1e-3)
    dyn = 1.0 + 0.6 * min(fr, 2.0)
    share = 1.0 if biped else 0.55
    core_gait = "walk" if fr < 0.15 else ("fast_walk" if df >= 0.5 else "run")
    return {
        "speed_ms": v, "froude": fr, "duty_factor": df, "stride_length_m": stride, "stride_period_s": period,
        "contact_time_s": df * period, "swing_time_s": (1.0 - df) * period, "dynamic_factor": dyn, "load_share": share,
        "peak_force_n": mass * G * share * dyn, "core_gait": core_gait,
        # Vertical touchdown velocity (game assumption): 8 % of sqrt(g h) at a slow walk, 25 % at Fr >= 1.
        "touchdown_velocity_ms": math.sqrt(G * hip) * (0.08 + 0.17 * float(C.saturate(fr))),
    }


def core_level_db(mass, fr, sub):
    return 35.0 + 12.0 * math.log10(max(0.01, mass)) + 6.0 * math.sqrt(max(0.0, fr)) + SUBSTRATES[sub]["level_adj"]


def core_thump_hz(mass):
    return 600.0 * max(0.01, mass) ** -0.25 + 30.0


# ------------------------------------------------------------------------------------------------
# Synthesis
# ------------------------------------------------------------------------------------------------
def _thump(n, i0, f, zeta, r, amp=1.0):
    w = 2.0 * math.pi * f
    wd = w * math.sqrt(max(1e-3, 1.0 - zeta * zeta))
    t = C.tvec(n - i0)
    y = np.zeros(n)
    y[i0:] = np.exp(-zeta * w * t) * np.sin(wd * t + r.uniform(-0.2, 0.2)) * (1.0 - np.exp(-t * f * 6.0))
    return amp * C.unit(y)


def render_footfall(sub, mass, foot_len, foot_w, gs, r, boots=False):
    s = SUBSTRATES[sub]
    a = math.sqrt(max(1e-6, foot_len * foot_w * FOOT_AREA_FILL) / math.pi)      # contact radius (m)
    area_rel = a / A_REF
    v_td = gs["touchdown_velocity_ms"] * r.uniform(0.85, 1.15)
    t_c = gs["contact_time_s"]
    t_comp = float(np.clip(0.25 * t_c, 0.02, 0.25))
    pre = 0.03
    if sub == "fern_cover":
        pre = float(np.clip(0.3 * gs["swing_time_s"], 0.08, 0.45))
    tail = t_comp + 0.25
    t_release = min(t_c, 1.1)
    if sub == "mud":
        tail = max(tail, t_release + 0.35)
    v_up = 1.2 * v_td + 0.4
    t_fall = min(0.9, 2.0 * v_up / G)
    if sub == "shallow_water":
        tail = max(tail, t_fall + 0.15, t_release + 0.3)
    n = C.ns(min(2.6, pre + tail + 0.06))
    i0 = C.ns(pre)
    t0 = pre
    k_loc = s["k_rel"] * r.uniform(0.8, 1.2)                     # local heterogeneity of the ground
    f_t = core_thump_hz(mass) * math.sqrt(k_loc)
    zeta = s["zeta"] * r.uniform(0.9, 1.1)
    thump = _thump(n, i0, f_t, zeta, r)
    if boots:
        # Heel then forefoot contact of a plantigrade booted foot.
        thump += 0.5 * _thump(n, i0 + C.ns(0.35 * t_c), f_t * 1.15, zeta, r)
        thump = C.unit(thump)
    size_f = float(np.clip((mass / 70.0) ** -0.12, 0.5, 3.0))   # small animals: texture dominates
    fs = float(np.clip((A_REF / a) ** 0.35, 0.4, 2.5))            # frequency scale with contact size
    tex = np.zeros(n)
    meta = {}
    if sub == "sand":
        cnt = int(np.clip(250 * area_rel * math.sqrt(gs["dynamic_factor"]), 30, 5000))
        x = C.impulses(n, C.front_loaded_times(r, cnt, t0, 1.2 * t_comp, 1.4), C.lognormal_amps(r, cnt, 0.8))
        modes = np.exp(r.uniform(math.log(2500), math.log(8000), 5))
        grains = C.bandpass(x, 1800.0, 11000.0) + C.modal_bank(x, modes, r.uniform(0.0004, 0.001, 5), np.full(5, 0.5))
        shear = C.noise_burst(n, r, max(250.0, 2.5 * f_t), 2500.0, 0.004, 0.4 * t_comp, i0)
        tex = C.unit(grains) + 0.5 * C.unit(shear)
        meta = {"grain_events": cnt, "grain_modes_hz": [round(float(m), 1) for m in modes]}
    elif sub == "silt":
        cnt = int(np.clip(40 * area_rel, 8, 600))
        x = C.impulses(n, C.front_loaded_times(r, cnt, t0, 0.8 * t_comp, 1.5), C.lognormal_amps(r, cnt, 0.6))
        crumble = C.bandpass(x, 500.0, 4000.0)
        body = C.noise_burst(n, r, max(120.0, 2.5 * f_t), 1400.0 * fs, 0.003, 0.35 * t_comp, i0)
        tex = 0.6 * C.unit(body) + 0.35 * C.unit(crumble)
        meta = {"crumble_events": cnt}
    elif sub == "mud":
        tau_slap = float(np.clip(0.008 * area_rel ** 0.5, 0.004, 0.06))
        slap = C.noise_burst(n, r, 90.0, 1800.0, 0.002, tau_slap, i0)
        i_rel = i0 + C.ns(t_release)
        r_cav = float(np.clip(0.15 * foot_w, 0.002, 0.15))
        f_s = float(np.clip(C.minnaert_hz(r_cav), 30.0, 3000.0))
        tau_s = float(np.clip(4.0 / f_s + 0.02, 0.02, 0.2))
        m = n - i_rel
        tt = C.tvec(m)
        ph = 2.0 * math.pi * np.cumsum(f_s * (0.8 + 0.4 * (1.0 - np.exp(-tt / 0.02)))) / C.SR
        suction = np.zeros(n)
        suction[i_rel:] = np.sin(ph) * C.env_ad(m, 0.01, tau_s)
        visc = C.lowpass(r.standard_normal(n), 4.0 * f_s) * C.env_ad(n, 0.02, float(np.clip(0.1 * area_rel ** 0.3, 0.04, 0.3)), i_rel)
        nb = int(r.integers(3, 12))
        rad = np.clip(0.003 * area_rel ** 0.3 * r.lognormal(0.0, 0.4, nb), 0.0008, 0.03)
        bub = C.bubble_cloud(n, t0 + t_release + r.uniform(0.0, 0.12, nb), rad, rad / rad.max() * r.uniform(0.5, 1.0, nb), r)
        tex = 0.7 * C.unit(slap) + 0.9 * C.unit(suction) + 0.4 * C.unit(visc) + 0.4 * C.unit(bub)
        meta = {"suction_release_s": round(t_release, 4), "suction_cavity_radius_m": round(r_cav, 4),
                "suction_hz": round(f_s, 2), "release_bubbles": nb}
    elif sub == "shallow_water":
        tau_slap = float(np.clip(0.008 * area_rel ** 0.5, 0.004, 0.05))
        slap = C.noise_burst(n, r, 150.0, 4000.0, 0.001, tau_slap, i0)
        crown = C.noise_burst(n, r, 600.0, 9000.0, 0.004, float(np.clip(0.05 * area_rel ** 0.4, 0.02, 0.25)), i0)
        nb = int(np.clip(25 * area_rel * (v_td / 0.5), 6, 600))
        rad = np.clip(0.0015 * area_rel ** 0.25 * r.lognormal(0.0, 0.5, nb), 0.0003, 0.02)
        tb = t0 + r.exponential(0.04 * area_rel ** 0.4, nb)
        bubbles = C.bubble_cloud(n, tb, rad, np.sqrt(rad / rad.max()) * r.lognormal(0, 0.3, nb), r)
        nd = int(np.clip(30 * area_rel, 5, 800))
        td = t0 + 0.03 + r.uniform(0, 1, nd) ** 0.7 * t_fall
        ticks = C.bandpass(C.impulses(n, td, C.lognormal_amps(r, nd, 0.5)), 2000.0, 10000.0)
        ent = r.uniform(0, 1, nd) < 0.35
        drops = C.bubble_cloud(n, td[ent], r.uniform(0.0002, 0.0008, int(ent.sum())), r.uniform(0.3, 1.0, int(ent.sum())), r)
        i_rel = i0 + C.ns(t_release)
        slosh = C.noise_burst(n, r, 80.0, 1500.0, 0.03, float(np.clip(0.08 * area_rel ** 0.3, 0.03, 0.3)), i_rel)
        tex = (0.6 * C.unit(slap) + 0.5 * C.unit(crown) + 0.8 * C.unit(bubbles) + 0.35 * C.unit(ticks) + 0.3 * C.unit(drops)
               + 0.35 * C.unit(slosh))
        meta = {"water_depth_m": 0.15, "entrained_bubbles": nb, "bubble_median_radius_m": round(float(np.median(rad)), 5),
                "droplets": nd, "droplet_fallback_s": round(t_fall, 3), "withdrawal_s": round(t_release, 4)}
    elif sub == "leaf_litter":
        cnt = int(np.clip(80 * area_rel ** 1.2 * math.sqrt(gs["dynamic_factor"]), 15, 3000))
        k_main = int(0.85 * cnt)
        times = np.concatenate([C.front_loaded_times(r, k_main, t0, 1.5 * t_comp, 1.5),
                                t0 + t_comp + r.uniform(0.0, 0.25, cnt - k_main)])
        # each fracture is a burst of 1-4 sub-clicks within 3 ms
        reps = r.integers(1, 5, times.size)
        times = np.repeat(times, reps) + r.uniform(0.0, 0.003, int(reps.sum()))
        amps = C.lognormal_amps(r, times.size, 0.7)
        grp = r.integers(0, 6, times.size)
        modal = np.zeros(n)
        for gi in range(6):
            sel = grp == gi
            x = C.impulses(n, times[sel], amps[sel])
            fm = np.exp(r.uniform(math.log(1200), math.log(7000), 3))
            modal += C.modal_bank(x, fm, r.uniform(0.0006, 0.002, 3), np.ones(3))
        broad = C.bandpass(C.impulses(n, times, amps), 1000.0, 14000.0)
        tex = C.unit(modal) + 0.6 * C.unit(broad)
        meta = {"leaf_fractures": cnt}
    elif sub == "fern_cover":
        i_pre = 0
        e = np.zeros(n)
        rise = i0 - i_pre
        e[:i0] = 0.5 - 0.5 * np.cos(np.linspace(0.0, math.pi, rise))
        e[i0:] = np.exp(-C.tvec(n - i0) / 0.08)
        tt = C.tvec(n)
        fl = r.uniform(15.0, 35.0)
        flutter = 1.0 + 0.6 * np.sin(2.0 * math.pi * fl * tt + 0.8 * np.cumsum(C.smooth_noise(n, r, 4.0)) / C.SR * 2 * math.pi)
        swish = C.bandpass(r.standard_normal(n), 700.0 * fs ** 0.5, 7500.0) * e * flutter
        n_snap = int(r.poisson(0.3 + 3.0 * float(C.saturate(math.log10(max(mass, 1.0)) / 4.0))))
        snaps = np.zeros(n)
        if n_snap:
            x = C.impulses(n, t0 + r.uniform(0.0, t_comp, n_snap), C.lognormal_amps(r, n_snap, 0.4))
            snaps = C.modal_bank(x, np.exp(r.uniform(math.log(500), math.log(3000), 3)), r.uniform(0.005, 0.015, 3), np.ones(3))
        squish = C.noise_burst(n, r, 150.0, 1500.0, 0.005, 0.4 * t_comp, i0)
        tex = C.unit(swish) + (0.8 * C.unit(snaps) if n_snap else 0.0) + 0.4 * C.unit(squish)
        meta = {"brush_preroll_s": round(pre, 4), "frond_flutter_hz": round(fl, 2), "frond_snaps": n_snap}
    y = s["thump"] * thump + s["tex"] * size_f * C.unit(tex)
    if boots:
        y += 0.25 * C.unit(C.noise_burst(n, r, 1000.0, 4000.0, 0.0005, 0.004, i0))     # rubber lug click
    info = {"contact_radius_m": round(a, 4), "thump_hz": round(f_t, 2), "damping_ratio": round(zeta, 3),
            "touchdown_velocity_ms": round(v_td, 3), "compaction_time_s": round(t_comp, 4), "onset_s": round(pre, 4),
            "texture_gain": round(s["tex"] * size_f, 3), "texture": meta}
    return y, info


# ------------------------------------------------------------------------------------------------
# Jobs
# ------------------------------------------------------------------------------------------------
def jobs(ds: D.Dataset) -> list[tuple]:
    out = []
    for sp in ds.species.values():
        if legged(sp):
            for sub in REGION_SUBSTRATES.get(sp.region, []):
                out.append(("footsteps", "job", {"species": sp.id, "substrate": sub}))
    for sub in SUBSTRATES:
        out.append(("footsteps", "job", {"species": "researcher", "substrate": sub}))
    return out


def _confidence(ds: D.Dataset, sp: D.Species):
    """Footfall = body mass + foot size + gait (biomechanics) + substrate physics. Weakest of the mass,
    size and locomotion claims, at most INFERRED (the texture synthesis itself is a game model)."""
    topics = ["mass", "locomotion", "size"]
    claims = [sp.claim(t) for t in topics]
    srcs = [s for c in claims for s in c.get("sources", [])]
    level = D.conf_min("inferred", *[c["confidence"] for c in claims if c["confidence"] != "unknown"] or ["speculative"])
    return level, srcs, claims


def job(ds: D.Dataset, species: str, substrate: str) -> list[M.Sound]:
    out = []
    if species == "researcher":
        p = RESEARCHER
        mass, hip, fl, fw = p["mass"], p["hip"], p["foot_length"], p["foot_width"]
        dfw, dfr, vw, vr, biped = p["dfw"], p["dfr"], p["vw"], p["vr"], True
        name_fr, name_en, region = p["name_fr"], p["name_en"], None
        conf, srcs = "speculative", [ALEXANDER83]
        basis = ("Son du joueur (chercheur de 80 kg en bottes de terrain) : mêmes lois que les animaux (Locomotion.cpp), "
                 "appui talon puis avant-pied, crampons en caoutchouc. Conception de jeu : le niveau de confiance ne porte "
                 "pas sur une question paléontologique.")
        group = "footsteps/researcher"
        subject = "researcher"
    else:
        sp = ds.species[species]
        lo = sp.sim["locomotion"]
        mass, hip = sp.mass, float(sp.sim["hip_height_m"])
        fl, fw = float(sp.sim["foot_length_m"]), float(sp.sim["foot_width_m"])
        dfw, dfr = float(lo.get("duty_factor_walk", 0.6)), float(lo.get("duty_factor_run", 0.4))
        vw, vr, biped = float(lo["walk_speed_ms"]), float(lo["max_speed_ms"]), sp.is_biped
        name_fr, name_en, region = sp.name_fr, sp.raw.get("common_names", {}).get("en", sp.name), sp.region
        conf, csrc, claims = _confidence(ds, sp)
        srcs = csrc + [ALEXANDER, ALEXANDER83]
        basis = (f"Masse {mass:g} kg, hauteur de hanche {hip:g} m, pied {fl:g} × {fw:g} m, vitesses {vw:g}/{vr:g} m/s et "
                 f"facteurs de service {dfw:g}/{dfr:g} (sim de la fiche espèce). Masse [{D.CONFIDENCE_LABEL_FR[claims[0]['confidence']]}] ; "
                 f"locomotion [{D.CONFIDENCE_LABEL_FR[claims[1]['confidence']]}]. Cinématique et niveau identiques au cœur "
                 "(Locomotion.cpp : similitude dynamique, footfallSound) ; texture du substrat = modèle physique de jeu.")
        group = f"footsteps/{sp.id}"
        subject = "paleo"
    s = SUBSTRATES[substrate]
    if s.get("water"):
        srcs = srcs + [MINNAERT, PROSPERETTI]
    elif substrate == "mud":
        srcs = srcs + [MINNAERT]
    srcs = [x for x in dict.fromkeys(srcs) if ds.has_source(x)]
    conf = ds.capped(conf, srcs)
    for gait in GAITS:
        v = vw if gait == "walk" else vr
        gs = gait_state(mass, hip, dfw, dfr, v, biped)
        lvl = core_level_db(mass, gs["froude"], substrate)
        f_core = 400.0 if s.get("water") else core_thump_hz(mass)
        for k in range(VARIATIONS):
            sid = f"fs_{species}_{substrate}_{gait}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = render_footfall(substrate, mass, fl, fw, gs, r, boots=(species == "researcher"))
            params = {
                "source": species, "substrate": substrate, "substrate_core_key": s["core"], "gait": gait,
                "gait_core": gs["core_gait"], "variation": k + 1,
                "mass_kg": mass, "hip_height_m": hip, "foot_length_m": fl, "foot_width_m": fw,
                **{kk: (round(vv, 5) if isinstance(vv, float) else vv) for kk, vv in gs.items()},
                "foot_area_m2": round(fl * fw * FOOT_AREA_FILL, 6),
                "core_footfall_f0_hz": round(f_core, 2),
                **info,
                "level_formula": "35 + 12 log10(m) + 6 sqrt(Fr) + ajustement substrat (Locomotion.cpp footfallSound)",
                "model": "contact masse-ressort amorti + texture physique du substrat (voir docstring footsteps.py)",
            }
            qa = {"peak_dbfs_max": -0.5}
            if substrate in ("silt", "sand", "leaf_litter") and info["thump_hz"] <= 500.0:
                ft = info["thump_hz"]
                qa["spectral_peak_hz"] = [round(0.7 * ft, 2), round(1.4 * ft, 2)]
                qa["spectral_peak_search_hz"] = [round(0.4 * ft, 2), round(min(2.2 * ft, 1200.0), 2)]
            if substrate in ("sand", "leaf_litter", "fern_cover"):
                qa["band_energy_ratio"] = {"lo_hz": 1000.0, "hi_hz": 20000.0, "min": 0.01}
            if substrate == "mud":
                qa["band_energy_ratio"] = {"lo_hz": 20.0, "hi_hz": 1500.0, "min": 0.3}
            att = "ATT_Footstep_Small" if mass < 5 else ("ATT_Footstep_Medium" if mass < 500 else "ATT_Footstep_Large")
            label = f"{name_fr} — pas, {GAIT_FR[gait]}, {s['fr']} — variation {k + 1}"
            snd = M.Sound(
                id=sid, category="footstep", subcategory=f"footstep_{substrate}", group=group, label_fr=label, audio=y,
                confidence=conf, basis_fr=basis, sources=srcs, params=params, qa=qa, region=region,
                species=None if species == "researcher" else species, hypothesis=False,
                variant={"gait": gait, "substrate": substrate, "variation": k + 1},
                level={"source_spl_db_1m": round(lvl, 2),
                       "reference": "Niveau du pas à 1 m (dB SPL re 20 µPa), formule du cœur footfallSound"},
                ue5=M.ue5("SC_Player_Footstep" if species == "researcher" else "SC_Creature_Footstep",
                          "ATT_Handheld_FirstPerson" if species == "researcher" else att, False,
                          "CON_Footsteps_PerOwner", priority=round(float(np.clip(0.2 + lvl / 200.0, 0.1, 0.9)), 3),
                          extra={"random_group": f"{species}_{substrate}_{gait}",
                                 "manus_gain_db": None if biped else round(20 * math.log10(0.45 / 0.55), 2),
                                 "trigger": "AnimNotify au contact du pied (phase de gait du cœur)"}),
                normalize=("peak", -1.0),
                caption={"fr": f"Pas de {name_fr} ({s['fr']})", "en": f"{name_en} footstep ({s['en']})"},
                subject=subject,
            )
            out.append(snd)
    return out
