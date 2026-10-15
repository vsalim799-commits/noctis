"""Body foley: breathing, skin / scale / armour contact, tail through vegetation, feeding, drinking, water
splashes, vegetation crashes and the gar's air gulp.

Breathing (documented scaling law)
  * rate: the core's law (Body.cpp) f = 0.89 M^-0.26 Hz x (0.8 + 1.6 load), i.e. 53.5 M^-0.26 breaths/min
    (Stahl 1967, mammals) for every species, so the sound stays in sync with the animated chest.
    The avian law (Calder 1968, 182 W^-0.33 /min, W in g) is given in the manifest as an alternative.
  * tidal volume: V_T = 7.69 M^1.04 mL (Stahl 1967; exponent checked, constant not re-verified -> flagged).
  * airflow noise: peak flow Q = (pi/2) V_T / T_phase through a nasal airway of area A = 1.5 cm^2 (M/70)^(2/3)
    (isometric, game assumption); turbulent (dipole) radiation, intensity ~ A U^6 (game calibration:
    25 dB SPL at 1 m for a resting 70 kg endotherm); spectrum shaped by the vocal / nasal tract resonances
    (2n-1) c / 4L with L from the vocal module (isometric 0.17 m at 70 kg).
  For non-avian dinosaurs, pterosaurs and ectotherms the rate is a SPECULATIVE extrapolation.

Everything else uses physical building blocks from common.py (damped modes, impulse trains, Minnaert bubble
clouds, band-limited noise) with size scaling documented per sound. Levels are game assumptions.
"""
from __future__ import annotations

import math

import numpy as np

from . import common as C
from . import data as D
from . import manifest as M
from . import propagation as P
from . import vocal as V

VAR = 4
STAHL = "stahl1967_respiratory_scaling"
CALDER = "calder1968_bird_respiration"
MINNAERT = "minnaert1933_musical_air_bubbles"
PROSPERETTI = "prosperetti1993_rain_noise"
NO_BODY = ("fish", "invertebrate")

# Integument class per species (from each species' science.integument claim; confidence taken from that claim).
INTEGUMENT = {
    "velociraptor_mongoliensis": "feathers", "acheroraptor_temertyorum": "feathers", "anzu_wyliei": "feathers",
    "citipati_osmolskae": "feathers", "struthiomimus_sedens": "feathers", "hell_creek_avifauna": "feathers",
    "tyrannosaurus_rex": "scales", "nanotyrannus_lancensis": "scales", "edmontosaurus_annectens": "scales",
    "triceratops_horridus": "scales", "protoceratops_andrewsi": "scales", "pachycephalosaurus_wyomingensis": "scales",
    "thescelosaurus_neglectus": "scales", "isodontosaurus_gracilis": "scales", "champsosaurus": "scales",
    "ankylosaurus_magniventris": "osteoderms", "pinacosaurus_grangeri": "osteoderms", "borealosuchus_sternbergii": "osteoderms",
    "basilemys_sinuosa": "shell", "didelphodon_vorax": "fur", "meniscoessus_robustus": "fur", "kryptobaatar_dashzevegi": "fur",
    "hell_creek_azhdarchid": "pycnofibres", "scapherpeton_tectum": "naked",
}
INTEG_FR = {"feathers": "plumes", "scales": "écailles", "osteoderms": "ostéodermes", "shell": "carapace", "fur": "pelage",
            "pycnofibres": "pycnofibres", "naked": "peau nue"}
INTEG_EN = {"feathers": "feathers", "scales": "scales", "osteoderms": "osteoderms", "shell": "shell", "fur": "fur",
            "pycnofibres": "pycnofibres", "naked": "bare skin"}

# Feeding actions per species (diet / feeding_strategy claims of each species file).
FEEDING = {
    "tyrannosaurus_rex": ["tear", "crunch"], "nanotyrannus_lancensis": ["tear"], "acheroraptor_temertyorum": ["tear"],
    "velociraptor_mongoliensis": ["tear"], "borealosuchus_sternbergii": ["snap", "tear"], "champsosaurus": ["snap"],
    "didelphodon_vorax": ["crunch"], "hell_creek_azhdarchid": ["snap"], "edmontosaurus_annectens": ["crop", "grind"],
    "triceratops_horridus": ["crop", "shear"], "protoceratops_andrewsi": ["crop", "shear"],
    "ankylosaurus_magniventris": ["crop", "pulp"], "pinacosaurus_grangeri": ["crop", "pulp"],
    "pachycephalosaurus_wyomingensis": ["crop", "pulp"], "thescelosaurus_neglectus": ["crop", "pulp"],
    "basilemys_sinuosa": ["crop"], "anzu_wyliei": ["crop"], "citipati_osmolskae": ["crop"], "struthiomimus_sedens": ["crop"],
    "hell_creek_avifauna": ["peck"], "meniscoessus_robustus": ["gnaw"], "kryptobaatar_dashzevegi": ["gnaw"],
    "isodontosaurus_gracilis": ["snap"], "scapherpeton_tectum": ["snap"],
}
FEED_FR = {"tear": "arrachage de chair", "crunch": "os / coquilles broyés", "snap": "claquement de mâchoires", "crop": "cueillette de végétaux",
           "grind": "broyage (batterie dentaire)", "shear": "cisaillement (batterie dentaire)", "pulp": "mastication simple",
           "peck": "coups de bec", "gnaw": "rongement"}
FEED_EN = {"tear": "flesh tearing", "crunch": "crunching bone or shell", "snap": "jaw snap", "crop": "cropping plants",
           "grind": "grinding (dental battery)", "shear": "shearing (dental battery)", "pulp": "simple chewing", "peck": "pecking",
           "gnaw": "gnawing"}
FEED_SOURCES = {"crunch": {"tyrannosaurus_rex": ["gignac2017_trex_osteophagy"], "didelphodon_vorax": ["wilson2016_didelphodon"]},
                "grind": {"edmontosaurus_annectens": ["erickson2012_hadrosaur_dentition", "williams2009_hadrosaur_microwear"]},
                "shear": {"triceratops_horridus": ["erickson2015_triceratops_dentition"]}}
ARCHOSAUR = ("biped_", "facultative_quadruped_hadrosaur", "quadruped_ceratopsian", "quadruped_ankylosaur", "quadruped_crocodylian",
             "bird_flyer", "quadruped_azhdarchid")


def mouth_scale(mass: float) -> float:
    """Linear size relative to a 10 kg animal (isometry)."""
    return (max(mass, 1e-4) / 10.0) ** (1.0 / 3.0)


def fscale(mass: float) -> float:
    """Frequency factor of structural modes (inverse of linear size), clipped."""
    return float(np.clip(mouth_scale(mass) ** -1.0, 0.25, 6.0))


def hump(n: int, i0: int, m: int) -> np.ndarray:
    e = np.zeros(n)
    m = max(2, min(m, n - i0))
    e[i0:i0 + m] = np.sin(np.linspace(0.0, math.pi, m)) ** 2
    return e


def crackle(n, r, times, lo, hi, nmodes=4, tau=(0.0005, 0.002), sigma=0.7, groups=4):
    """Brittle fracture events: impulses through random plate/fibre modes plus a broadband part."""
    times = np.asarray(times)
    if times.size == 0:
        return np.zeros(n)
    amps = C.lognormal_amps(r, times.size, sigma)
    grp = r.integers(0, groups, times.size)
    y = np.zeros(n)
    for g in range(groups):
        sel = grp == g
        if np.any(sel):
            x = C.impulses(n, times[sel], amps[sel])
            fm = np.exp(r.uniform(math.log(lo), math.log(hi), nmodes))
            y += C.modal_bank(x, fm, r.uniform(tau[0], tau[1], nmodes), np.ones(nmodes))
    y += 0.5 * C.bandpass(C.impulses(n, times, amps), lo, min(hi * 2.0, 18000.0))
    return y


# ------------------------------------------------------------------------------------------------
# Breathing
# ------------------------------------------------------------------------------------------------
def resp_rate_hz(mass, load):
    return 0.89 * mass ** -0.26 * (0.8 + 1.6 * load)


def tidal_volume_m3(mass):
    return 7.69 * mass ** 1.04 * 1e-6


def airway_area_m2(mass):
    return 1.5e-4 * (mass / 70.0) ** (2.0 / 3.0)


def breath_params(mass, state):
    load = 0.1 if state == "rest" else 1.0
    f = resp_rate_hz(mass, load)
    T = 1.0 / f
    vt = tidal_volume_m3(mass) * (1.0 if state == "rest" else 1.6)
    ti, te = 0.42 * T, 0.48 * T
    A = airway_area_m2(mass)
    u_in = (math.pi / 2.0) * vt / ti / A
    u_ex = (math.pi / 2.0) * vt / te / A
    return {"load": load, "rate_hz": f, "rate_per_min": 60.0 * f, "cycle_s": T, "inspiration_s": ti, "expiration_s": te,
            "pause_s": 0.1 * T, "tidal_volume_l": vt * 1e3, "airway_area_m2": A, "peak_velocity_in_ms": u_in,
            "peak_velocity_ex_ms": u_ex}


_REF = breath_params(70.0, "rest")


def breath_level_db(mass, bp):
    u = 0.5 * (bp["peak_velocity_in_ms"] + bp["peak_velocity_ex_ms"])
    u0 = 0.5 * (_REF["peak_velocity_in_ms"] + _REF["peak_velocity_ex_ms"])
    return 25.0 + 10.0 * math.log10(bp["airway_area_m2"] / _REF["airway_area_m2"]) + 60.0 * math.log10(u / u0)


def synth_breath(mass, bp, r):
    L = V.vocal_tract_length_m(mass)
    c = P.speed_of_sound(20.0)
    formants = [(2 * j - 1) * c / (4.0 * L) for j in range(1, 5)]
    d = 2.0 * math.sqrt(bp["airway_area_m2"] / math.pi)
    pre = 0.02
    n_in, n_ex = C.ns(bp["inspiration_s"]), C.ns(bp["expiration_s"])
    gap = C.ns(0.04 * bp["cycle_s"])
    n = C.ns(pre) + n_in + gap + n_ex + C.ns(0.05)
    y = np.zeros(n)
    f_hi = float(np.clip(6000.0 * (70.0 / mass) ** (1.0 / 6.0), 1500.0, 14000.0))
    i = C.ns(pre)
    parts = []
    for phase, m, u in (("in", n_in, bp["peak_velocity_in_ms"]), ("ex", n_ex, bp["peak_velocity_ex_ms"])):
        f_lo = float(np.clip(0.2 * u / d, 40.0, 800.0))
        src = C.bandpass(r.standard_normal(m), f_lo, f_hi, order=1)
        res = C.resonator_bank(src, formants, [3.5] * 4, [1.0, 0.6, 0.4, 0.25])
        if phase == "in":
            x = 0.8 * src / (np.std(src) + 1e-12) + 0.5 * res / (np.std(res) + 1e-12)
            x += 0.4 * C.highpass(r.standard_normal(m), 2000.0) * (u / 10.0 if u < 10 else 1.0)   # nostril hiss
        else:
            x = 0.5 * src / (np.std(src) + 1e-12) + res / (np.std(res) + 1e-12)
        flow = np.sin(np.linspace(0.0, math.pi, m))
        env = flow ** 3 * (1.0 + 0.15 * C.smooth_noise(m, r, 3.0))      # amplitude ~ U^3 (dipole)
        y[i:i + m] += x * env * (1.0 if phase == "ex" else 0.85)
        parts.append({"phase": phase, "start_s": round(i / C.SR, 4), "duration_s": round(m / C.SR, 4),
                      "turbulence_onset_hz": round(f_lo, 1)})
        i += m + gap
    return y, {"tract_resonances_hz": [round(f, 1) for f in formants], "airway_diameter_m": round(d, 4),
               "noise_upper_hz": round(f_hi, 1), "phases": parts}


# ------------------------------------------------------------------------------------------------
# Body contact by integument
# ------------------------------------------------------------------------------------------------
def synth_body(mass, integ, r):
    dur = float(np.clip(0.6 + 0.25 * math.log10(max(mass, 0.01) + 1.0), 0.5, 1.8)) * r.uniform(0.85, 1.15)
    n = C.ns(dur + 0.1)
    i0 = C.ns(0.03)
    m = C.ns(dur)
    env = hump(n, i0, m)
    fs = fscale(mass)
    info = {"movement_s": round(dur, 3)}
    if integ in ("scales",):
        scale_size = float(np.clip(0.003 * mass ** (1.0 / 3.0), 0.0005, 0.03))
        v = 0.3 * mouth_scale(mass) ** 0.5
        rate = v / scale_size * (0.4 + 0.6 * env)                       # stick-slip at scale spacing
        ph = np.cumsum(rate) / C.SR
        idx = np.nonzero(np.diff(np.floor(ph)) > 0)[0]
        t = idx / C.SR + r.normal(0, 0.0004, idx.size)
        x = C.impulses(n, t, r.lognormal(0, 0.5, idx.size) * env[idx])
        rasp = C.bandpass(x, 250.0 * fs ** 0.5, 5000.0)
        fr = C.bandpass(r.standard_normal(n), 500.0 * fs ** 0.5, 6000.0) * env
        y = C.unit(rasp) + 0.4 * C.unit(fr)
        info.update({"scale_size_m": round(scale_size, 4), "slide_speed_ms": round(v, 3),
                     "stick_slip_hz": round(v / scale_size, 1)})
    elif integ == "osteoderms":
        plate = float(np.clip(0.02 * (mass / 10.0) ** (1.0 / 3.0), 0.01, 0.3))
        f1 = float(np.clip(120.0 / plate, 300.0, 6000.0))
        k = int(r.poisson(dur * 10.0)) + 2
        t = 0.03 + np.sort(r.uniform(0.0, dur, k))
        x = C.impulses(n, t, r.lognormal(0, 0.5, k))
        knocks = C.modal_bank(x, [f1, 2.3 * f1, 3.9 * f1], [0.008, 0.005, 0.003], [1.0, 0.5, 0.3])
        scrape = C.bandpass(r.standard_normal(n), 300.0, 4000.0) * env
        y = C.unit(knocks) + 0.5 * C.unit(scrape)
        info.update({"osteoderm_size_m": round(plate, 3), "plate_mode_hz": round(f1, 1), "knocks": k})
    elif integ == "shell":
        f1 = float(np.clip(900.0 * fs ** 0.5, 200.0, 3000.0))
        k = int(r.poisson(dur * 4.0)) + 1
        x = C.impulses(n, 0.03 + r.uniform(0.0, dur, k), r.lognormal(0, 0.4, k))
        y = C.unit(C.modal_bank(x, [f1, 2.1 * f1, 3.3 * f1], [0.012, 0.008, 0.004], [1, 0.6, 0.3])) + \
            0.7 * C.unit(C.bandpass(r.standard_normal(n), 400.0, 5000.0) * env * (1 + 0.5 * C.smooth_noise(n, r, 25.0)))
        info.update({"shell_mode_hz": round(f1, 1)})
    elif integ == "feathers":
        lo = float(np.clip(1500.0 * fs ** 0.3, 600.0, 3000.0))
        y = C.bandpass(r.standard_normal(n), lo, 12000.0) * env * (1.0 + 0.4 * C.smooth_noise(n, r, 12.0))
        info.update({"band_hz": [round(lo, 1), 12000.0]})
    elif integ in ("fur", "pycnofibres"):
        y = C.bandpass(r.standard_normal(n), 2000.0, 14000.0) * env * (1.0 + 0.5 * C.smooth_noise(n, r, 20.0))
    else:  # naked, wet glandular skin
        y = C.bandpass(r.standard_normal(n), 100.0, 2000.0) * env * (1.0 + 0.6 * C.smooth_noise(n, r, 8.0))
        k = int(r.integers(2, 6))
        y = C.unit(y) + 0.3 * C.unit(C.bubble_cloud(n, 0.03 + r.uniform(0, dur, k), r.uniform(0.0005, 0.0015, k), np.ones(k), r))
    return y, info


# ------------------------------------------------------------------------------------------------
# Tail through vegetation
# ------------------------------------------------------------------------------------------------
def synth_tail(mass, length_m, veg, r):
    lt = 0.45 * length_m
    dur = float(np.clip(0.5 * math.sqrt(lt), 0.3, 2.0)) * r.uniform(0.85, 1.15)
    n = C.ns(dur + 0.25)
    i0 = C.ns(0.03)
    env = hump(n, i0, C.ns(dur))
    fs = fscale(mass)
    tt = C.tvec(n)
    if veg == "ferns":
        fl = r.uniform(15.0, 35.0)
        swish = C.bandpass(r.standard_normal(n), 600.0 * fs ** 0.3, 8000.0) * env * (1.0 + 0.5 * np.sin(2 * math.pi * fl * tt))
        k = int(np.clip(30 * lt, 3, 400))
        clicks = crackle(n, r, 0.03 + dur * r.beta(2, 2, k), 2000.0, 7000.0, tau=(0.0004, 0.0012))
        y = C.unit(swish) + 0.35 * C.unit(clicks)
        n_snap = int(r.poisson(2.0 * float(C.saturate(math.log10(max(mass, 1.0)) / 4.0) ** 2)))
        if n_snap:
            y += 0.6 * C.unit(crackle(n, r, 0.03 + r.uniform(0, dur, n_snap), 500.0, 3000.0, nmodes=3, tau=(0.005, 0.015)))
        info = {"frond_flutter_hz": round(fl, 2), "frond_contacts": k, "frond_snaps": n_snap}
    else:  # xeric scrub twigs (Djadochta)
        k = int(np.clip(60 * lt, 5, 800))
        twigs = crackle(n, r, 0.03 + dur * r.beta(2, 2, k), 1500.0, 6000.0, tau=(0.0006, 0.002))
        rasp = C.bandpass(r.standard_normal(n), 800.0, 6000.0) * env
        y = C.unit(twigs) + 0.5 * C.unit(rasp)
        info = {"twig_contacts": k}
    info.update({"tail_length_m": round(lt, 3), "sweep_s": round(dur, 3)})
    return y, info


# ------------------------------------------------------------------------------------------------
# Feeding
# ------------------------------------------------------------------------------------------------
def synth_feed(mass, action, r):
    ms = mouth_scale(mass)
    fs = fscale(mass)
    info = {}
    if action == "crop":
        bites = int(r.integers(1, 4))
        period = float(np.clip(0.6 * (mass / 100.0) ** 0.15, 0.25, 1.6))
        n = C.ns(bites * period + 0.5)
        y = np.zeros(n)
        for b in range(bites):
            t0 = 0.03 + b * period * r.uniform(0.9, 1.1)
            tear = crackle(n, r, C.front_loaded_times(r, int(np.clip(40 * ms, 6, 600)), t0, 0.25 * period, 0.7),
                           900.0 * fs ** 0.5, 6000.0, tau=(0.0004, 0.0015))
            snap = crackle(n, r, [t0 + 0.25 * period], 700.0 * fs ** 0.5, 3500.0, nmodes=3, tau=(0.004, 0.012), groups=1)
            rustle = C.bandpass(r.standard_normal(n), 1500.0, 9000.0) * C.env_ad(n, 0.02, 0.15 * period, C.ns(t0 + 0.2 * period))
            y += C.unit(tear) + 0.9 * C.unit(snap) + 0.3 * C.unit(rustle)
        info = {"bites": bites, "bite_period_s": round(period, 3)}
    elif action in ("grind", "shear", "pulp"):
        f_chew = float(np.clip(1.6 * (mass / 100.0) ** -0.1, 0.6, 5.0))
        cycles = int(r.integers(3, 6))
        n = C.ns(cycles / f_chew + 0.4)
        y = np.zeros(n)
        for k in range(cycles):
            t0 = 0.03 + k / f_chew * r.uniform(0.95, 1.05)
            if action == "grind":
                # dental battery grinding stroke: stick-slip rasp of opposed tooth rows + fibre crushing
                m = C.ns(0.45 / f_chew)
                i0 = C.ns(t0)
                rate = float(np.clip(180.0 * fs ** 0.5, 60.0, 600.0))
                tt = np.arange(0, 0.45 / f_chew, 1.0 / rate)
                x = C.impulses(n, t0 + tt + r.normal(0, 0.0007, tt.size), r.lognormal(0, 0.4, tt.size))
                rasp = C.bandpass(x, 150.0 * fs ** 0.5, 3000.0) * hump(n, i0, m)
                crush = crackle(n, r, t0 + r.uniform(0, 0.45 / f_chew, int(np.clip(30 * ms, 5, 300))), 400.0 * fs ** 0.5, 3000.0)
                y += C.unit(rasp) + 0.5 * C.unit(crush)
                info["rasp_rate_hz"] = round(rate, 1)
            elif action == "shear":
                cut = crackle(n, r, C.front_loaded_times(r, int(np.clip(40 * ms, 6, 400)), t0, 0.15 / f_chew, 0.8),
                              600.0 * fs ** 0.5, 5000.0)
                clack = crackle(n, r, [t0 + 0.18 / f_chew], 800.0 * fs ** 0.5, 2500.0, nmodes=3, tau=(0.003, 0.008), groups=1)
                y += C.unit(cut) + 0.5 * C.unit(clack)
            else:
                mush = C.noise_burst(n, r, 150.0 * fs ** 0.5, 2500.0, 0.02, 0.12 / f_chew, C.ns(t0))
                crush = crackle(n, r, t0 + r.uniform(0, 0.25 / f_chew, int(np.clip(15 * ms, 3, 150))), 500.0 * fs ** 0.5, 3500.0)
                y += C.unit(mush) + 0.6 * C.unit(crush)
        info.update({"chew_hz": round(f_chew, 3), "cycles": cycles})
    elif action == "tear":
        pulls = int(r.integers(1, 3))
        dur = float(np.clip(0.4 * ms ** 0.5, 0.25, 1.2))
        n = C.ns(pulls * dur * 1.2 + 0.4)
        y = np.zeros(n)
        for k in range(pulls):
            t0 = 0.03 + k * dur * 1.2
            i0 = C.ns(t0)
            e = hump(n, i0, C.ns(dur))
            wet = C.bandpass(r.standard_normal(n), 120.0 * fs ** 0.5, 2500.0) * e * (1 + 0.7 * C.smooth_noise(n, r, 30.0))
            kk = int(np.clip(25 * ms, 5, 400))
            rips = C.bandpass(C.impulses(n, t0 + dur * r.beta(2, 1.5, kk), C.lognormal_amps(r, kk, 0.8)), 200.0 * fs ** 0.5, 3000.0)
            pops = C.bubble_cloud(n, t0 + r.uniform(0, dur, kk // 3 + 1), r.uniform(0.002, 0.008, kk // 3 + 1) * ms ** 0.3,
                                  np.ones(kk // 3 + 1), r)
            y += C.unit(wet) + 0.7 * C.unit(rips) + 0.3 * C.unit(pops)
        info = {"pulls": pulls, "pull_s": round(dur, 3)}
    elif action == "crunch":
        n = C.ns(1.4)
        t0 = 0.05
        crack = crackle(n, r, [t0], 300.0 * fs ** 0.5, 4000.0, nmodes=5, tau=(0.003, 0.012), groups=1)
        k = int(np.clip(60 * ms, 10, 800))
        frag = crackle(n, r, C.front_loaded_times(r, k, t0, 0.8, 1.3), 500.0 * fs ** 0.5, 6000.0, tau=(0.0008, 0.004))
        grind = C.noise_burst(n, r, 100.0 * fs ** 0.5, 2000.0, 0.02, 0.35, C.ns(t0))
        y = 1.2 * C.unit(crack) + 0.7 * C.unit(frag) + 0.4 * C.unit(grind)
        info = {"fragments": k}
    elif action == "snap":
        n = C.ns(0.5)
        t0 = 0.03
        f1 = float(np.clip(900.0 * fs ** 0.7, 150.0, 6000.0))
        x = C.impulses(n, [t0, t0 + 0.002 * ms ** 0.3], [1.0, 0.5])
        clap = C.modal_bank(x, [f1, 2.6 * f1, 4.1 * f1], [0.006, 0.003, 0.0015], [1, 0.5, 0.25])
        whoosh = C.bandpass(r.standard_normal(n), 300.0 * fs ** 0.5, 4000.0) * hump(n, 0, C.ns(t0 + 0.005))
        y = C.unit(clap) + 0.2 * C.unit(whoosh)
        info = {"jaw_mode_hz": round(f1, 1)}
    elif action == "peck":
        k = int(r.integers(3, 8))
        n = C.ns(0.18 * k + 0.2)
        t = 0.03 + np.cumsum(r.uniform(0.1, 0.25, k))
        y = crackle(n, r, t, 1500.0 * fs ** 0.3, 7000.0, nmodes=3, tau=(0.001, 0.003), groups=2)
        info = {"pecks": k}
    else:  # gnaw: rapid incisor strokes on a husk / nut
        rate = float(np.clip(7.0 * (mass / 0.1) ** -0.1, 3.0, 12.0))
        dur = r.uniform(1.0, 1.8)
        n = C.ns(dur + 0.2)
        t = 0.03 + np.arange(0.0, dur, 1.0 / rate) + r.normal(0, 0.006, int(math.ceil(dur * rate)))
        reps = r.integers(2, 6, t.size)
        tt = np.repeat(t, reps) + r.uniform(0.0, 0.02, int(reps.sum()))
        y = crackle(n, r, tt, 3000.0, 11000.0, nmodes=3, tau=(0.0003, 0.001))
        info = {"stroke_hz": round(rate, 2)}
    return y, info


# ------------------------------------------------------------------------------------------------
# Drinking
# ------------------------------------------------------------------------------------------------
def synth_drink(mass, mode, r):
    ms = mouth_scale(mass)
    fs = fscale(mass)
    if mode == "lap":
        rate = float(np.clip(4.0 * (mass / 10.0) ** -0.18, 2.0, 10.0))
        dur = r.uniform(1.5, 2.5)
        n = C.ns(dur + 0.4)
        t = 0.05 + np.arange(0, dur, 1.0 / rate) + r.normal(0, 0.01, int(math.ceil(dur * rate)))
        k = t.size
        y = C.unit(C.bubble_cloud(n, t, np.clip(0.002 * ms ** 0.5 * r.lognormal(0, 0.3, k), 0.0004, 0.02), np.ones(k), r))
        y += 0.5 * C.unit(C.bandpass(C.impulses(n, t, r.lognormal(0, 0.3, k)), 800.0, 8000.0))
        n_sw = max(1, int(dur / 1.2))
        for j in range(n_sw):
            y += 0.4 * C.unit(C.noise_burst(n, r, 60.0 * fs ** 0.5, 400.0 * fs ** 0.5, 0.02, 0.06, C.ns(0.05 + (j + 1) * dur / (n_sw + 1))))
        return y, {"mode": "lap", "lap_hz": round(rate, 2), "laps": k}
    if mode == "suction":
        gulps = int(r.integers(2, 5))
        per = float(np.clip(1.0 * (mass / 10.0) ** 0.12, 0.5, 2.5))
        n = C.ns(gulps * per + 0.6)
        y = np.zeros(n)
        y += 0.6 * C.unit(C.noise_burst(n, r, 200.0, 4000.0, 0.002, 0.03, C.ns(0.03)))       # lips touch the surface
        for g in range(gulps):
            t0 = 0.08 + g * per
            k = int(np.clip(25 * ms ** 0.5, 6, 120))
            rad = np.clip(0.004 * ms ** 0.5 * r.lognormal(0, 0.4, k), 0.0005, 0.04)
            y += C.unit(C.bubble_cloud(n, t0 + r.uniform(0, 0.35 * per, k), rad, np.sqrt(rad / rad.max()), r))
            y += 0.6 * C.unit(C.noise_burst(n, r, 50.0 * fs ** 0.5, 350.0 * fs ** 0.5, 0.02, 0.08 * ms ** 0.3, C.ns(t0 + 0.55 * per)))
        return y, {"mode": "suction", "gulps": gulps, "gulp_period_s": round(per, 3)}
    # scoop-and-tilt (archosaurs: birds and crocodilians, the extant bracket, raise the head to swallow)
    scoops = int(r.integers(2, 4))
    per = float(np.clip(1.5 * (mass / 10.0) ** 0.12, 0.8, 4.0))
    n = C.ns(scoops * per + 1.0)
    y = np.zeros(n)
    h = float(np.clip(0.4 * ms, 0.05, 2.5))                                 # mouth height when tilting (m)
    t_drop = math.sqrt(2.0 * h / C.G)
    for s in range(scoops):
        t0 = 0.05 + s * per
        k = int(np.clip(20 * ms, 5, 300))
        y += 0.8 * C.unit(C.noise_burst(n, r, 200.0 * fs ** 0.3, 5000.0, 0.003, 0.04 * ms ** 0.4, C.ns(t0)))
        y += 0.6 * C.unit(C.bubble_cloud(n, t0 + r.exponential(0.05, k), np.clip(0.003 * ms ** 0.3 * r.lognormal(0, 0.4, k), 3e-4, 0.03),
                                         np.ones(k), r))
        nd = int(np.clip(40 * ms, 5, 600))
        td = t0 + 0.35 * per + t_drop + r.exponential(0.15 * per, nd)
        ent = r.uniform(0, 1, nd) < 0.4
        y += 0.5 * C.unit(C.bandpass(C.impulses(n, td, C.lognormal_amps(r, nd, 0.5)), 1500.0, 9000.0))
        if ent.any():
            y += 0.5 * C.unit(C.bubble_cloud(n, td[ent], r.uniform(0.0003, 0.0012, int(ent.sum())), np.ones(int(ent.sum())), r))
        y += 0.4 * C.unit(C.noise_burst(n, r, 50.0 * fs ** 0.5, 350.0 * fs ** 0.5, 0.03, 0.1 * ms ** 0.3, C.ns(t0 + 0.6 * per)))
    return y, {"mode": "scoop_tilt", "scoops": scoops, "scoop_period_s": round(per, 3), "drip_height_m": round(h, 3)}


# ------------------------------------------------------------------------------------------------
# Generic water / vegetation events by mass class
# ------------------------------------------------------------------------------------------------
SPLASH_CLASSES = [("small", 1.0), ("medium", 30.0), ("large", 500.0), ("huge", 5000.0)]
CRASH_CLASSES = [("medium", 100.0), ("large", 1000.0), ("huge", 6000.0)]


def synth_splash(mass, action, r):
    rb = (3.0 * mass / (4.0 * math.pi * 1000.0)) ** (1.0 / 3.0)            # equivalent body radius (m)
    sc = rb / 0.062                                                          # relative to 1 kg
    if action == "enter":
        v = r.uniform(1.5, 3.0)
        v_up = 0.6 * v * sc ** 0.15
        t_fall = min(1.6, 2.0 * v_up / C.G)
        n = C.ns(t_fall + 1.2)
        t0 = 0.03
        i0 = C.ns(t0)
        f_cav = 3.26 / (0.5 * rb)                                            # Minnaert of the collapsing air cavity
        y = 0.8 * C.unit(C.noise_burst(n, r, 80.0, 6000.0, 0.002, float(np.clip(0.01 * sc ** 0.5, 0.005, 0.08)), i0))
        y += 0.6 * C.unit(C.noise_burst(n, r, 500.0, 10000.0, 0.01, float(np.clip(0.06 * sc ** 0.4, 0.03, 0.4)), i0))
        if f_cav >= 20.0:
            m = n - i0 - C.ns(0.05 * sc ** 0.5)
            cav = np.zeros(n)
            cav[n - m:] = np.sin(2 * math.pi * f_cav * C.tvec(m)) * C.env_ad(m, 0.01, 3.0 / f_cav)
            y += 0.7 * C.unit(cav)
        k = int(np.clip(80 * sc, 20, 3000))
        rad = np.clip(0.003 * sc ** 0.2 * r.lognormal(0, 0.5, k), 3e-4, 0.03)
        y += C.unit(C.bubble_cloud(n, t0 + r.exponential(0.08 * sc ** 0.4, k), rad, np.sqrt(rad / rad.max()), r))
        nd = int(np.clip(100 * sc, 20, 4000))
        td = t0 + 0.05 + r.uniform(0, 1, nd) ** 0.7 * t_fall
        ent = r.uniform(0, 1, nd) < 0.3
        y += 0.4 * C.unit(C.bandpass(C.impulses(n, td, C.lognormal_amps(r, nd, 0.6)), 1500.0, 10000.0))
        y += 0.4 * C.unit(C.bubble_cloud(n, td[ent], r.uniform(0.0003, 0.0015, int(ent.sum())), np.ones(int(ent.sum())), r))
        y += 0.3 * C.unit(C.noise_burst(n, r, 40.0, 600.0, 0.3, 0.4 * sc ** 0.3, C.ns(t0 + t_fall)))   # waves on the bank
        info = {"entry_speed_ms": round(v, 3), "cavity_hz": round(f_cav, 2), "bubbles": k, "droplets": nd, "fallback_s": round(t_fall, 3)}
    elif action == "stroke":
        dur = float(np.clip(0.4 * sc ** 0.3, 0.25, 1.5))
        n = C.ns(dur + 0.6)
        e = hump(n, C.ns(0.03), C.ns(dur))
        y = C.unit(C.bandpass(r.standard_normal(n), 150.0, 3000.0) * e * (1 + 0.6 * C.smooth_noise(n, r, 12.0)))
        k = int(np.clip(30 * sc, 8, 800))
        rad = np.clip(0.004 * sc ** 0.2 * r.lognormal(0, 0.5, k), 4e-4, 0.03)
        y += 0.8 * C.unit(C.bubble_cloud(n, 0.03 + dur * r.beta(2, 2, k), rad, np.sqrt(rad / rad.max()), r))
        nd = int(np.clip(30 * sc, 5, 600))
        y += 0.3 * C.unit(C.bandpass(C.impulses(n, 0.03 + dur * 0.5 + r.uniform(0, dur, nd), C.lognormal_amps(r, nd, 0.5)), 1500.0, 9000.0))
        info = {"stroke_s": round(dur, 3), "bubbles": k}
    else:  # exit: water pouring off the body
        dur = float(np.clip(0.8 * sc ** 0.3, 0.6, 3.0))
        n = C.ns(dur + 0.5)
        e = C.env_ad(n, 0.05, dur / 2.5, C.ns(0.03))
        pour = C.bandpass(r.standard_normal(n), 200.0, 6000.0) * e * (1 + 0.5 * C.smooth_noise(n, r, 20.0))
        nd = int(np.clip(150 * sc, 30, 6000))
        td = 0.03 + r.exponential(dur / 2.5, nd)
        ent = r.uniform(0, 1, nd) < 0.35
        drops = C.bubble_cloud(n, td[ent], r.uniform(0.0003, 0.002, int(ent.sum())), np.ones(int(ent.sum())), r)
        ticks = C.bandpass(C.impulses(n, td, C.lognormal_amps(r, nd, 0.5)), 1500.0, 10000.0)
        y = C.unit(pour) + 0.8 * C.unit(drops) + 0.4 * C.unit(ticks)
        info = {"pour_s": round(dur, 3), "drops": nd}
    info["equivalent_body_radius_m"] = round(rb, 3)
    return y, info


def synth_crash(mass, r):
    sc = mouth_scale(mass)
    dur = float(np.clip(0.8 * sc ** 0.3, 0.6, 2.5)) * r.uniform(0.85, 1.15)
    n = C.ns(dur + 0.8)
    snaps = int(r.integers(1, 3)) + int(r.poisson(1.0 + math.log10(mass / 100.0 + 1.0)))
    t_snap = 0.05 + np.sort(r.uniform(0.0, 0.6 * dur, snaps))
    y = np.zeros(n)
    for ts in t_snap:
        d_branch = float(np.clip(0.02 * sc ** 0.5 * r.lognormal(0, 0.4), 0.005, 0.15))   # branch diameter (m)
        f1 = float(np.clip(18.0 / d_branch, 120.0, 4000.0))                                # bending mode ~ 1/d (game assumption)
        x = C.impulses(n, [ts], [1.0])
        crack = C.modal_bank(x, [f1, 2.76 * f1, 5.4 * f1], [0.03, 0.015, 0.008], [1.0, 0.5, 0.3])
        crack += 0.6 * C.bandpass(C.impulses(n, ts + r.exponential(0.004, 20), C.lognormal_amps(r, 20, 0.6)), 800.0, 12000.0)
        y += r.uniform(0.6, 1.0) * C.unit(crack)
    rustle = crackle(n, r, 0.05 + dur * r.beta(1.5, 2.5, int(np.clip(200 * sc, 50, 3000))), 1500.0, 8000.0, tau=(0.0006, 0.002))
    y += 0.7 * C.unit(rustle) + 0.4 * C.unit(C.noise_burst(n, r, 60.0, 800.0, 0.05, 0.25, C.ns(0.05 + 0.7 * dur)))
    return y, {"branch_snaps": snaps, "movement_s": round(dur, 3)}


def synth_gar_gulp(r):
    n = C.ns(1.2)
    t0 = 0.05
    k = int(r.integers(5, 15))
    y = 0.8 * C.unit(C.bubble_cloud(n, t0 + r.exponential(0.05, k), r.uniform(0.002, 0.006, k), np.ones(k), r))   # air released
    t1 = t0 + r.uniform(0.15, 0.3)
    y += 0.7 * C.unit(C.noise_burst(n, r, 150.0, 3000.0, 0.003, 0.03, C.ns(t1)))                       # snout breaks surface
    rp = r.uniform(0.008, 0.015)
    f = float(C.minnaert_hz(rp))
    m = n - C.ns(t1 + 0.04)
    pop = np.zeros(n)
    pop[n - m:] = np.sin(2 * math.pi * f * C.tvec(m)) * C.env_ad(m, 0.003, 2.5 / f + 0.01)            # gulp
    y += 0.9 * C.unit(pop)
    nd = int(r.integers(10, 30))
    y += 0.3 * C.unit(C.bandpass(C.impulses(n, t1 + 0.05 + r.exponential(0.12, nd), C.lognormal_amps(r, nd, 0.5)), 1500.0, 9000.0))
    return y, {"released_bubbles": k, "gulp_cavity_radius_m": round(rp, 4), "gulp_hz": round(f, 1)}


# ------------------------------------------------------------------------------------------------
# Jobs and records
# ------------------------------------------------------------------------------------------------
def jobs(ds: D.Dataset) -> list[tuple]:
    out = []
    for sp in ds.species.values():
        if sp.body_plan not in NO_BODY:
            out.append(("foley", "job_species", {"species": sp.id}))
    out.append(("foley", "job_generic", {}))
    return out


def _att(mass):
    return "ATT_Foley_Close" if mass < 5 else ("ATT_Foley_Medium" if mass < 500 else "ATT_Foley_Large")


def _sound(ds, sp, sid, kind, y, label, cap_fr, cap_en, conf, srcs, basis, params, lvl, qa=None, concurrency="CON_Foley",
           hypothesis=False, region=None, species=None, att=None, subject="paleo", group=None):
    srcs = [s for s in dict.fromkeys(srcs) if ds.has_source(s)]
    conf = ds.capped(conf, srcs)
    q = {"peak_dbfs_max": -0.5}
    if qa:
        q.update(qa)
    return M.Sound(
        id=sid, category="foley", subcategory=kind, group=group or f"foley/{sp.id if sp else 'generic'}", label_fr=label, audio=y,
        confidence=conf, basis_fr=basis, sources=srcs, params=params, qa=q, region=region if region is not None else (sp.region if sp else None),
        species=species if species is not None else (sp.id if sp else None), hypothesis=hypothesis,
        level={"source_spl_db_1m": round(lvl, 2), "reference": "dB SPL re 20 µPa à 1 m (hypothèse de jeu sauf mention)"},
        ue5=M.ue5("SC_Creature_Foley", att or _att(sp.mass if sp else 100.0), False, concurrency,
                  priority=round(float(np.clip(0.15 + lvl / 200.0, 0.1, 0.8)), 3)),
        normalize=("peak", -1.0), caption={"fr": cap_fr, "en": cap_en}, subject=subject,
        variant=params.get("variant_info", {}))


def job_species(ds: D.Dataset, species: str) -> list[M.Sound]:
    sp = ds.species[species]
    m = sp.mass
    en = sp.raw.get("common_names", {}).get("en", sp.name)
    out = []
    archosaur = sp.body_plan.startswith(ARCHOSAUR)
    thermo = sp.sim["metabolism"]["thermo_class"]
    # --- breathing ---
    if sp.body_plan != "fish":
        is_mammal = sp.body_plan == "quadruped_small_mammal"
        conf = "inferred" if is_mammal else "speculative"
        for state in ("rest", "exerted"):
            bp = breath_params(m, state)
            lvl = breath_level_db(m, bp)
            avian = 182.0 * (m * 1000.0) ** -0.33
            for k in range(VAR):
                sid = f"fol_{sp.id}_breath_{state}_{k + 1:02d}"
                r = C.rng(sid)
                y, info = synth_breath(m, bp, r)
                params = {"action": f"breath_{state}", "variation": k + 1, "mass_kg": m, "thermo_class": thermo,
                          **{kk: round(vv, 5) if isinstance(vv, float) else vv for kk, vv in bp.items()}, **info,
                          "rate_law": "f = 0.89 M^-0.26 Hz × (0.8 + 1.6 charge) — Body.cpp (Stahl 1967, mammifères)",
                          "rate_avian_law_per_min": round(avian, 3),
                          "tidal_volume_law": "V_T = 7,69 M^1,04 mL (Stahl 1967 ; constante non revérifiée)",
                          "level_law": "25 dB à 1 m pour 70 kg au repos + 10 log10(A/A70) + 60 log10(U/U70) (rayonnement dipolaire, calibration de jeu)",
                          "variant_info": {"state": state, "variation": k + 1}}
                cyc = bp["cycle_s"]
                note = ("" if thermo == "endotherm" else
                        f" Animal {thermo} : la loi endotherme du cœur surestime probablement la fréquence (respiration réelle plus lente et intermittente).")
                basis = (f"Fréquence respiratoire {60 / cyc:.2f}/min ({state}), volume courant {bp['tidal_volume_l']:.3g} L, vitesse de pointe "
                         f"{bp['peak_velocity_ex_ms']:.2f} m/s. Loi allométrique des mammifères (Stahl 1967) utilisée par le cœur pour "
                         "toutes les espèces (synchronisation avec l'animation du thorax) ; pour un archosaure la loi aviaire (Calder 1968) "
                         f"donnerait {avian:.2f}/min au repos.{note}")
                out.append(_sound(ds, sp, sid, "breathing", y, f"{sp.name_fr} — respiration ({'repos' if state == 'rest' else 'effort'}) — variation {k + 1}",
                                  f"Respiration de {sp.name_fr}", f"{en} breathing", conf, [STAHL] + ([] if is_mammal else [CALDER]), basis,
                                  params, lvl, concurrency="CON_Breathing_PerOwner"))
                out[-1].ue5["schedule"] = {"cycle_s": round(cyc, 4), "loop_with_gap_s": round(max(0.0, cyc - len(y) / C.SR), 4),
                                           "note_fr": "Rejouer toutes les cycle_s secondes (phase = breathPhase du cœur)."}
    # --- body contact ---
    integ = INTEGUMENT.get(sp.id)
    if integ:
        claim = sp.claim("integument")
        for k in range(VAR):
            sid = f"fol_{sp.id}_body_{integ}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_body(m, integ, r)
            lvl = 25.0 + 8.0 * math.log10(max(m, 1e-3))
            params = {"action": "body_contact", "integument": integ, "variation": k + 1, "mass_kg": m, **info,
                      "level_law": "game_assumption: 25 + 8 log10(m) dB à 1 m", "variant_info": {"integument": integ, "variation": k + 1}}
            basis = (f"Tégument [{D.CONFIDENCE_LABEL_FR[claim['confidence']]}] : {claim['summary']} Frottement contre sol / tronc : "
                     "modèle physique de jeu (stick-slip à l'espacement des écailles, modes de plaques osseuses, souffle des plumes).")
            out.append(_sound(ds, sp, sid, "body_contact", y, f"{sp.name_fr} — contact corporel ({INTEG_FR[integ]}) — variation {k + 1}",
                              f"Frottement ({INTEG_FR[integ]}) de {sp.name_fr}", f"{en} body rub ({INTEG_EN[integ]})",
                              D.conf_min("inferred", claim["confidence"]) if claim["confidence"] != "unknown" else "speculative",
                              claim.get("sources", []), basis, params, lvl))
    # --- tail through vegetation ---
    tail = sp.body_plan.startswith(("biped_", "quadruped_ceratopsian", "quadruped_ankylosaur", "facultative_quadruped_hadrosaur",
                                    "quadruped_crocodylian", "aquatic_reptile")) or sp.id in ("isodontosaurus_gracilis", "scapherpeton_tectum")
    if tail:
        veg = "ferns" if sp.region == "hell_creek" else "xeric_scrub"
        for k in range(VAR):
            sid = f"fol_{sp.id}_tail_{veg}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_tail(m, float(sp.sim["adult_length_m"]), veg, r)
            lvl = 30.0 + 8.0 * math.log10(max(m, 1e-3))
            params = {"action": "tail_vegetation", "vegetation": veg, "variation": k + 1, "mass_kg": m, **info,
                      "tail_length_law": "game_assumption: 0,45 × longueur adulte", "level_law": "game_assumption: 30 + 8 log10(m)",
                      "variant_info": {"vegetation": veg, "variation": k + 1}}
            basis = ("Queue balayant " + ("des fougères (couvert de sous-bois de Hell Creek, environnement JSON)" if veg == "ferns" else
                                          "des arbustes xériques (seule végétation de la Djadochta, environnement JSON)") +
                     " ; longueur de queue et vitesse de balayage = hypothèses de jeu ; bruit de friction et ruptures de frondes/brindilles.")
            out.append(_sound(ds, sp, sid, "tail_vegetation", y, f"{sp.name_fr} — queue dans la végétation — variation {k + 1}",
                              f"Queue de {sp.name_fr} dans la végétation", f"{en} tail through vegetation", "speculative", [], basis, params, lvl))
    # --- feeding ---
    for action in FEEDING.get(sp.id, []):
        claims = [sp.claim("diet"), sp.claim("feeding_strategy")]
        known = [c["confidence"] for c in claims if c["confidence"] != "unknown"]
        conf = D.conf_min("inferred", *known) if known else "speculative"
        srcs = [s for c in claims for s in c.get("sources", [])] + FEED_SOURCES.get(action, {}).get(sp.id, [])
        base_lvl = {"crop": 35.0, "grind": 30.0, "shear": 32.0, "pulp": 28.0, "tear": 35.0, "crunch": 50.0, "snap": 40.0, "peck": 30.0,
                    "gnaw": 25.0}[action]
        for k in range(VAR):
            sid = f"fol_{sp.id}_feed_{action}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_feed(m, action, r)
            lvl = base_lvl + 7.0 * math.log10(max(m, 1e-3))
            params = {"action": f"feed_{action}", "variation": k + 1, "mass_kg": m, "mouth_scale": round(mouth_scale(m), 4), **info,
                      "level_law": f"game_assumption: {base_lvl:g} + 7 log10(m)", "variant_info": {"action": action, "variation": k + 1}}
            basis = (f"Régime [{D.CONFIDENCE_LABEL_FR[claims[0]['confidence']]}] : {claims[0]['summary']} "
                     f"Mode d'alimentation [{D.CONFIDENCE_LABEL_FR[claims[1]['confidence']]}]. Son : {FEED_FR[action]} — ruptures fragiles "
                     "(modes aléatoires), mise à l'échelle des fréquences par la taille de la bouche (isométrie) ; rythme = hypothèse de jeu.")
            out.append(_sound(ds, sp, sid, "feeding", y, f"{sp.name_fr} — alimentation : {FEED_FR[action]} — variation {k + 1}",
                              f"{sp.name_fr} se nourrit ({FEED_FR[action]})", f"{en} feeding ({FEED_EN[action]})", conf, srcs, basis, params, lvl))
    # --- drinking ---
    if sp.body_plan not in ("fish",) and sp.id not in ("hell_creek_insects", "hell_creek_unionids"):
        mode = "scoop_tilt" if archosaur else ("lap" if sp.body_plan == "quadruped_small_mammal" else "suction")
        for k in range(VAR):
            sid = f"fol_{sp.id}_drink_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_drink(m, mode if mode != "scoop_tilt" else "scoop", r)
            lvl = 25.0 + 7.0 * math.log10(max(m, 1e-3))
            params = {"action": "drink", "variation": k + 1, "mass_kg": m, **info, "level_law": "game_assumption: 25 + 7 log10(m)",
                      "variant_info": {"mode": info["mode"], "variation": k + 1}}
            why = {"scoop_tilt": "puiser puis relever la tête pour avaler (crochet phylogénétique : oiseaux et crocodiliens actuels)",
                   "lap": "lapement (mammifère ; mécanisme exact inconnu chez les multituberculés)",
                   "suction": "aspiration par gorgées (tortues, salamandres, squamates : analogues actuels)"}[info["mode"]]
            basis = (f"Boisson : {why}. Bulles à la fréquence de Minnaert, gouttes retombant de la gueule (temps de chute sqrt(2h/g)). "
                     "Rythme et niveaux : hypothèses de jeu.")
            out.append(_sound(ds, sp, sid, "drinking", y, f"{sp.name_fr} — boit ({info['mode']}) — variation {k + 1}",
                              f"{sp.name_fr} boit", f"{en} drinking", "speculative", [MINNAERT, PROSPERETTI], basis, params, lvl,
                              hypothesis=False))
    # --- gar air gulp ---
    if sp.id == "lepisosteus_occidentalis":
        claim = sp.claim("soft_tissue")
        for k in range(VAR):
            sid = f"fol_{sp.id}_air_gulp_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_gar_gulp(r)
            params = {"action": "air_gulp", "variation": k + 1, **info, "level_law": "game_assumption: 45 dB à 1 m",
                      "variant_info": {"variation": k + 1}}
            basis = (f"Respiration aérienne [{D.CONFIDENCE_LABEL_FR[claim['confidence']]}] : {claim['summary']} Expulsion de bulles, "
                     "museau crevant la surface et gorgée d'air (cavité ~1 cm : fréquence de Minnaert).")
            out.append(_sound(ds, sp, sid, "air_gulp", y, f"{sp.name_fr} — gorgée d'air en surface — variation {k + 1}",
                              f"{sp.name_fr} gobe de l'air en surface", f"{en} gulping air at the surface",
                              D.conf_min("inferred", claim["confidence"]), claim.get("sources", []) + [MINNAERT], basis, params, 45.0,
                              att="ATT_Foley_Medium"))
    return out


def job_generic(ds: D.Dataset) -> list[M.Sound]:
    out = []
    names_fr = {"small": "petit (1 kg)", "medium": "moyen (30 kg)", "large": "grand (500 kg)", "huge": "géant (5 t)"}
    act_fr = {"enter": "entrée dans l'eau", "stroke": "mouvement de nage", "exit": "sortie de l'eau (ruissellement)"}
    act_en = {"enter": "entering water", "stroke": "swimming stroke", "exit": "leaving water (dripping)"}
    for cname, mass in SPLASH_CLASSES:
        for action in ("enter", "stroke", "exit"):
            for k in range(VAR):
                sid = f"fol_water_{action}_{cname}_{k + 1:02d}"
                r = C.rng(sid)
                y, info = synth_splash(mass, action, r)
                lvl = {"enter": 50.0, "stroke": 40.0, "exit": 35.0}[action] + {"enter": 10.0, "stroke": 8.0, "exit": 8.0}[action] * math.log10(mass)
                params = {"action": f"water_{action}", "mass_class": cname, "mass_kg": mass, "variation": k + 1, **info,
                          "level_law": "game_assumption", "variant_info": {"mass_class": cname, "variation": k + 1}}
                basis = ("Physique des éclaboussures : impact, couronne, nuage de bulles entraînées (fréquence de Minnaert), cavité d'air "
                         "qui se referme (f = 3,26 / R), gouttes retombant après 2 v/g (bruit d'impact de gouttes, Prosperetti & Oguz 1993). "
                         "Indépendant de l'espèce : choisir la classe de masse la plus proche.")
                out.append(_sound(ds, None, sid, "water", y, f"Eau — {act_fr[action]} — animal {names_fr[cname]} — variation {k + 1}",
                                  f"Éclaboussure ({act_fr[action]})", f"Splash ({act_en[action]})", "inferred", [MINNAERT, PROSPERETTI], basis,
                                  params, lvl, concurrency="CON_Water_Splash", region=None, species=None, att=_att(mass),
                                  subject="physical_environment", group="foley/water"))
    for cname, mass in CRASH_CLASSES:
        for k in range(VAR):
            sid = f"fol_vegetation_crash_{cname}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = synth_crash(mass, r)
            lvl = 55.0 + 8.0 * math.log10(mass / 100.0)
            params = {"action": "vegetation_crash", "mass_class": cname, "mass_kg": mass, "variation": k + 1, **info,
                      "level_law": "game_assumption: 55 + 8 log10(m/100)", "variant_info": {"mass_class": cname, "variation": k + 1}}
            basis = ("Animal forçant le passage dans des arbustes (SoundKind::VegetationCrash du cœur) : ruptures de branches (mode de flexion "
                     "~ 1/diamètre, hypothèse de jeu) et froissement du feuillage.")
            out.append(_sound(ds, None, sid, "vegetation_crash", y, f"Végétation brisée — animal {cname} ({mass:g} kg) — variation {k + 1}",
                              "Branches brisées", "Branches snapping", "speculative", [], basis, params, lvl, region=None, species=None,
                              att="ATT_Foley_Large", subject="physical_environment", group="foley/vegetation"))
    return out
