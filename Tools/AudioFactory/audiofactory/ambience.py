"""Ambiences: 60 s seamless stereo loops (separate layers + prebuilt day / night mixes) and thunder one-shots.

Every loop is rendered *circularly*: noise beds are synthesised in the frequency domain on an exactly
periodic STFT grid (3000 frames of 20 ms), slow controls (gusts, discharge pulsations) are periodic
Fourier series, and discrete events (leaf clicks, bubbles, drops, calls) are rendered into a buffer with a
tail that is folded back onto the start (circular convolution). The last sample therefore continues into the
first one without a cross-fade. qa.py measures the seam.

Physical models (game models built from textbook physics; the cited data only constrain the inputs):
  * wind in conifers: aeolian tones of needles / scale-leaf twigs, f = St U / d with St ~ 0.2 (vortex
    shedding behind a cylinder) and d = 0.7-3 mm (Cupressaceae / Metasequoia / Glyptostrobus foliage of the
    flora claim); dipole radiation, pressure ~ U^3. Gusts: periodic turbulence, intensity 0.3.
  * wind in broadleaf canopy (angiosperm trees): leaf-leaf collisions (impulses through leaf-plate modes,
    rate ~ U^3) + low 'whoosh' (~ U^2.5): rustle rather than hiss.
  * river: line source of turbulent roar + clustered surface bubbles at the Minnaert frequency + lapping on the
    bank; level = core WaterSystem::noiseLevelDb (48 dB at 5 m for the mean discharge, +10 log10(Q/Qmean)).
  * rain: Marshall-Palmer drop-size distribution N(D) = 8000 exp(-Lambda D) m^-3 mm^-1, Lambda = 4.1 R^-0.21
    mm^-1; drop impacts on leaves / litter / water; on water the 'type I' regular entrainment (D ~ 0.8-1.1 mm)
    gives ~0.2 mm bubbles (~14 kHz, Prosperetti & Oguz 1993); level = core rainNoiseDb 38 + 16 log10(1 + 4R).
  * thunder: tortuous lightning channel (random walk of 5-20 m segments, ~16 deg tortuosity, branches); each
    segment radiates an N-wave mostly perpendicular to itself; N-wave duration lengthens as r^1/4 (weak-shock
    theory); arrival r/c, spherical spreading and ISO 9613-1 absorption per distance band (propagation.py).
  * biophony (labelled HYPOTHESES): katydid-like pure-tone stridulation (wing stridulation with a ~6.4 kHz pure
    tone is reconstructed for the Jurassic katydid Archaboilus musicus, Gu et al. 2012; nothing is known for Hell
    Creek), flying-insect wingbeat buzz with Doppler, anuran pulsed calls (anurans are part of the Hell Creek
    lissamphibian assemblage; calls unknown), bird calls from hell_creek_avifauna sim.vocal through the vocal
    module (syrinx attested in the Maastrichtian Vegavis, Clarke et al. 2016; calls unknown).
"""
from __future__ import annotations

import math
from functools import lru_cache

import numpy as np

from . import common as C
from . import data as D
from . import manifest as M
from . import propagation as P
from . import vocal as V

LOOP_S = 60.0
N = C.ns(LOOP_S)                 # 2 880 000 samples
HOP = 960                        # 20 ms STFT synthesis hop
WIN = 4 * HOP                    # 80 ms frames, 75 % overlap
NFR = N // HOP                   # 3000 frames, exactly periodic
FB = np.fft.rfftfreq(WIN, 1.0 / C.SR)
TAIL = C.ns(4.0)
assert N % HOP == 0

GU = "gu2012_katydid_stridulation"
MP = "marshall1948_raindrop_size"
PROSP = "prosperetti1993_rain_noise"
MINN = "minnaert1933_musical_air_bubbles"
ISO1 = "iso1993_9613_1"
ISO2 = "iso1996_9613_2"
CLARKE = "clarke2016_mesozoic_syrinx"
GARDNER = "gardner2013_lissamphibians"

U_GUST_FACTOR = 0.35             # gust term of the core wind-noise formula: gust ~ 0.35 U (game assumption)
RIVER_MEAN_Q = 60.0              # m^3/s, TerrainGenerator main river (game assumption of the core)
SALTATION_U_MS = 6.5             # wind speed at ~1.5 m above which fine sand saltates (game assumption)


# ------------------------------------------------------------------------------------------------
# Periodic building blocks
# ------------------------------------------------------------------------------------------------
def ctl(n: int, r: np.random.Generator, cycles: int, slope: float = 0.5) -> np.ndarray:
    """Exactly periodic smooth control signal (zero mean, unit std) with at most `cycles` cycles per period."""
    nb = n // 2 + 1
    cycles = max(1, min(cycles, nb - 1))
    spec = np.zeros(nb, dtype=np.complex128)
    k = np.arange(1, cycles + 1)
    spec[k] = (r.standard_normal(cycles) + 1j * r.standard_normal(cycles)) * k ** -slope
    y = np.fft.irfft(spec, n)
    return (y - y.mean()) / (y.std() + 1e-12)


def frames_to_samples(v: np.ndarray) -> np.ndarray:
    """Periodic linear interpolation of a per-frame control to the sample grid."""
    return np.interp(np.arange(N), np.arange(NFR + 1) * HOP, np.append(v, v[0]))


_WIN = np.hanning(WIN + 1)[:-1]  # periodic Hann: sum of squares at 75 % overlap is constant (1.5)


def stft_noise(mag: np.ndarray, r: np.random.Generator) -> np.ndarray:
    """Exactly periodic noise of length N whose short-time magnitude spectrum follows mag (NFR x bins).
    Weighted overlap-add of random-phase frames; the frame index wraps around (circular)."""
    z = (r.standard_normal(mag.shape) + 1j * r.standard_normal(mag.shape)) * mag
    z[:, 0] = 0.0
    fr = np.fft.irfft(z, WIN, axis=1) * _WIN
    out = np.zeros((NFR, HOP))
    for k in range(4):
        out += np.roll(fr[:, k * HOP:(k + 1) * HOP], k, axis=0)
    y = out.reshape(-1)
    return y / (np.std(y) + 1e-12)


def unit_rms(x: np.ndarray) -> np.ndarray:
    return x / (C.rms(x) + 1e-15)


CREST_DB = 17.0                  # mastering: peak-to-RMS ceiling of the ambience loops (game choice)


def limit_crest(y: np.ndarray, crest_db: float = CREST_DB, window_s: float = 0.01) -> tuple[np.ndarray, dict]:
    """Transparent circular peak limiter. Threshold = crest_db above the *typical* level (median of the 50 ms frame
    RMS, robust to the rare loud events that dominate a plain RMS). Gain = moving average (window L) of the moving
    minimum (window L) of the required gain: no sample exceeds the threshold and the gain changes smoothly; 'wrap'
    boundaries keep the loop seamless. Physical event levels (a rain drop at 40 cm, a frog at 3 m) otherwise give
    crest factors of 30-40 dB that make a bed unusable in a mix; the reduction is reported in the manifest."""
    from scipy.ndimage import minimum_filter1d, uniform_filter1d
    L = max(3, C.ns(window_s))
    a = np.max(np.abs(y), axis=1) if y.ndim == 2 else np.abs(y)
    mono = y.mean(axis=1) if y.ndim == 2 else y
    w = C.ns(0.05)
    nfr = mono.size // w
    fr = np.sqrt(np.mean(mono[: nfr * w].reshape(nfr, w) ** 2, axis=1))
    typical = float(np.median(fr[fr > 0])) if np.any(fr > 0) else C.rms(mono)
    thr = typical * 10.0 ** (crest_db / 20.0)
    info = {"crest_ceiling_db_above_median_frame_rms": crest_db}
    if a.max() > thr:
        g = np.minimum(1.0, thr / np.maximum(a, 1e-15))
        g = uniform_filter1d(minimum_filter1d(g, L, mode="wrap"), L, mode="wrap")
        y = y * (g[:, None] if y.ndim == 2 else g)
        info.update({"max_gain_reduction_db": round(float(-20.0 * np.log10(max(g.min(), 1e-12))), 2),
                     "fraction_limited": round(float(np.mean(g < 0.999)), 5)})
    else:
        info.update({"max_gain_reduction_db": 0.0, "fraction_limited": 0.0})
    a = np.max(np.abs(y), axis=1) if y.ndim == 2 else np.abs(y)
    info["crest_db_after"] = round(float(20.0 * np.log10(a.max() / (C.rms(y) + 1e-15))), 2)
    return y, info


def expected_rms_dbfs(lim: dict, target: float = -20.0, ceiling: float = -1.0) -> float:
    """RMS after generate.py's normalisation (RMS target with a peak ceiling) given the crest factor of the layer."""
    return round(min(target, ceiling - lim["crest_db_after"]), 2)


def band(f, lo, hi, order=2.0):
    return C.band_gain(lo, hi, order)(f)


def gusts(r: np.random.Generator, u_mean: float, ti: float = 0.3, floor: float = 0.45) -> np.ndarray:
    """Per-frame wind speed: slow gusts (periods >= 7.5 s) + faster turbulence; periodic. Lulls are floored at
    `floor` x mean so that the U^3 radiation law does not silence the canopy completely (game choice)."""
    g = 0.8 * ctl(NFR, r, 8, 0.3) + 0.45 * ctl(NFR, r, 60, 0.8)
    g /= g.std()
    return u_mean * np.maximum(floor, 1.0 + ti * g)


def pan_gains(pans):
    a = (np.clip(np.asarray(pans, dtype=np.float64), -1.0, 1.0) + 1.0) * math.pi / 4.0
    return np.cos(a), np.sin(a)


def modal_crackle_st(n, r, times, amps, pans, lo, hi, groups=6, nmodes=3, tau=(0.0008, 0.003), broad=0.5,
                     broad_band=None):
    """Stereo brittle-contact texture: each event is an impulse through one of `groups` random modal sets
    (leaf / twig plates), plus a broadband click. Same modes in both channels."""
    times = np.asarray(times, dtype=np.float64)
    out = np.zeros((n, 2))
    if times.size == 0:
        return out
    gl, gr = pan_gains(pans)
    grp = r.integers(0, groups, times.size)
    for g in range(groups):
        sel = grp == g
        if not np.any(sel):
            continue
        fm = np.exp(r.uniform(math.log(lo), math.log(hi), nmodes))
        tm = r.uniform(tau[0], tau[1], nmodes)
        for ch, gg in enumerate((gl, gr)):
            x = C.impulses(n, times[sel], amps[sel] * gg[sel])
            out[:, ch] += C.modal_bank(x, fm, tm, np.ones(nmodes))
    if broad > 0:
        b_lo, b_hi = broad_band or (lo, min(2.0 * hi, 18000.0))
        for ch, gg in enumerate((gl, gr)):
            out[:, ch] += broad * C.bandpass(C.impulses(n, times, amps * gg), b_lo, b_hi)
    return out


def bubbles_st(n, times, radii, amps, pans, r, depth_m=0.0):
    seed = int(r.integers(1 << 62))
    gl, gr = pan_gains(pans)
    amps = np.asarray(amps, dtype=np.float64)
    return np.stack([C.bubble_cloud(n, times, radii, amps * gl, np.random.default_rng(seed), depth_m),
                     C.bubble_cloud(n, times, radii, amps * gr, np.random.default_rng(seed), depth_m)], axis=1)


def wrap(buf: np.ndarray) -> np.ndarray:
    return C.wrap_circular(buf, N)


def stereo_noise(r, mag_l, mag_r=None, coherence=0.2):
    a = stft_noise(mag_l, r)
    b = stft_noise(mag_l if mag_r is None else mag_r, r)
    c = stft_noise(mag_l, r)
    k1, k2 = math.sqrt(coherence), math.sqrt(1.0 - coherence)
    return np.stack([k1 * c + k2 * a, k1 * c + k2 * b], axis=1)


# ------------------------------------------------------------------------------------------------
# Wind
# ------------------------------------------------------------------------------------------------
NEEDLES = [(0.0007, 0.5), (0.001, 1.0), (0.0014, 1.0), (0.002, 0.7), (0.003, 0.4)]   # diameter (m), weight
TWIGS_XERIC = [(0.002, 0.6), (0.003, 1.0), (0.005, 0.8), (0.008, 0.4)]


def _aeolian_mag(U, u_ref, elements, width=0.5):
    f = np.maximum(FB[None, :], 1.0)
    mag = np.zeros((U.size, FB.size))
    for d, w in elements:
        fc = 0.2 * U[:, None] / d
        mag += w * np.exp(-0.5 * (np.log(f / fc) / width) ** 2)
    return mag * (U[:, None] / u_ref) ** 3


def _low_turbulence_mag(U, u_ref, hi=300.0, expo=2.0):
    f = np.maximum(FB[None, :], 1.0)
    return (f / 60.0) ** -0.8 * band(f, 18.0, hi) * (U[:, None] / u_ref) ** expo


def wind_conifer(r, u_mean):
    U = gusts(r, u_mean)
    out = []
    for ch in range(2):
        Uc = np.roll(U, 12 * ch)                     # the gust front reaches the second side ~0.24 s later
        mag = _aeolian_mag(Uc, u_mean, NEEDLES) + 0.12 * _low_turbulence_mag(Uc, u_mean)
        out.append(stft_noise(mag, r))
    y = np.stack(out, axis=1)
    fc = [round(0.2 * u_mean / d, 1) for d, _ in NEEDLES]
    return y, {"wind_mean_ms": u_mean, "turbulence_intensity": 0.3, "strouhal": 0.2,
               "needle_diameters_m": [d for d, _ in NEEDLES], "aeolian_tone_hz_at_mean": fc,
               "wind_speed_p05_p95_ms": [round(float(np.percentile(U, 5)), 2), round(float(np.percentile(U, 95)), 2)]}


def wind_broadleaf(r, u_mean):
    U = gusts(r, u_mean)
    f = np.maximum(FB[None, :], 1.0)
    whoosh = []
    for ch in range(2):
        Uc = np.roll(U, 12 * ch)
        mag = (f / 400.0) ** -0.5 * band(f, 90.0, 2500.0) * (Uc[:, None] / u_mean) ** 2.5 + \
            0.1 * _low_turbulence_mag(Uc, u_mean)
        whoosh.append(stft_noise(mag, r))
    whoosh = np.stack(whoosh, axis=1)
    Us = frames_to_samples(U)
    lam0 = 500.0 * (u_mean / 3.0) ** 2              # leaf collisions per second at the mean speed (game assumption)
    umax = float(U.max())

    def rate(t):
        return lam0 * (Us[np.minimum((t * C.SR).astype(np.int64), N - 1)] / u_mean) ** 3

    times = C.poisson_times(r, rate, 0.0, LOOP_S, lam0 * (umax / u_mean) ** 3 * 1.01)
    ui = Us[np.minimum((times * C.SR).astype(np.int64), N - 1)] / u_mean
    amps = r.lognormal(0.0, 0.7, times.size) * r.choice([-1.0, 1.0], times.size) * ui
    # Many leaf sizes: 32 modal sets with low Q, so no single leaf mode is heard as a steady pitch.
    clicks = modal_crackle_st(N + TAIL, r, times, amps, r.uniform(-1, 1, times.size), 600.0, 6000.0,
                              groups=32, nmodes=2, tau=(0.0003, 0.0009), broad=1.0, broad_band=(500.0, 12000.0))
    rustle = wrap(clicks)
    y = 0.45 * unit_rms(whoosh) + unit_rms(rustle)
    return y, {"wind_mean_ms": u_mean, "turbulence_intensity": 0.3, "leaf_collisions": int(times.size),
               "leaf_collision_rate_law": "500 (U/3)^2 /s × (u(t)/U)^3 (hypothèse de jeu)",
               "leaf_mode_range_hz": [700.0, 5000.0]}


def wind_desert(r, u_mean):
    """Open dune field: low turbulence roar, aeolian whistles of sparse xeric shrubs, and sand saltation hiss
    (many grain impacts) whenever the gusting wind exceeds the saltation threshold."""
    U = gusts(r, u_mean, ti=0.33)
    out = []
    for ch in range(2):
        Uc = np.roll(U, 12 * ch)
        mag = 0.35 * _aeolian_mag(Uc, u_mean, TWIGS_XERIC, 0.45) + 0.5 * _low_turbulence_mag(Uc, u_mean, 400.0, 2.0)
        out.append(stft_noise(mag, r))
    y = unit_rms(np.stack(out, axis=1))
    Us = frames_to_samples(U)
    excess = np.maximum(0.0, Us - SALTATION_U_MS)
    info = {"wind_mean_ms": u_mean, "turbulence_intensity": 0.33, "saltation_threshold_ms": SALTATION_U_MS,
            "twig_diameters_m": [d for d, _ in TWIGS_XERIC], "saltation_fraction_of_time": round(float(np.mean(excess > 0)), 3)}
    if excess.max() > 0:
        lam_max = 4000.0 * (float(excess.max()) / 3.0) ** 3 + 1.0      # grain impacts / s (game assumption, ~ (u - ut)^3)

        def rate(t):
            return 4000.0 * (excess[np.minimum((t * C.SR).astype(np.int64), N - 1)] / 3.0) ** 3

        times = C.poisson_times(r, rate, 0.0, LOOP_S, lam_max)
        if times.size:
            amps = r.lognormal(0.0, 0.5, times.size) * r.choice([-1.0, 1.0], times.size)
            pans = r.uniform(-1, 1, times.size)
            gl, gr = pan_gains(pans)
            sal = np.stack([C.bandpass(C.impulses(N + TAIL, times, amps * gl), 2500.0, 14000.0),
                            C.bandpass(C.impulses(N + TAIL, times, amps * gr), 2500.0, 14000.0)], axis=1)
            sal = wrap(sal)
            # Scale: saltation hiss relative to the wind bed grows with the excess speed (game calibration).
            w = 0.25 * min(4.0, float(np.mean(excess)) / 0.5 + 0.3)
            y = y + w * unit_rms(sal)
            info.update({"grain_impacts": int(times.size), "saltation_rate_law": "4000 ((u - u_t)/3)^3 impacts/s (hypothèse de jeu)"})
    return y, info


# ------------------------------------------------------------------------------------------------
# River
# ------------------------------------------------------------------------------------------------
RIVERS = {
    # q: discharge (m^3/s); roar weight; gurgle cluster rate (/s); bubbles per cluster; median bubble radius (m); lapping /s
    "creek": dict(q=1.5, roar=0.25, clusters=6.0, per=8, rmed=0.002, lap=0.3, flood=False,
                  fr="ruisseau de plaine d'inondation (1,5 m³/s)", en="floodplain creek"),
    "low": dict(q=18.0, roar=0.6, clusters=1.6, per=6, rmed=0.003, lap=1.0, flood=False,
                fr="rivière, étiage (18 m³/s)", en="river, low flow"),
    "mean": dict(q=60.0, roar=1.0, clusters=3.0, per=10, rmed=0.0035, lap=1.2, flood=False,
                 fr="rivière, débit moyen (60 m³/s)", en="river, mean flow"),
    "flood": dict(q=300.0, roar=2.2, clusters=10.0, per=18, rmed=0.005, lap=2.2, flood=True,
                  fr="rivière en crue (300 m³/s)", en="river in flood"),
}


def river(r, key):
    p = RIVERS[key]
    f = np.maximum(FB[None, :], 1.0)
    pulse = 1.0 + 0.15 * ctl(NFR, r, 20, 0.5)[:, None]
    roar_mag = (f / 80.0) ** -0.5 * band(f, 45.0, 600.0 * (p["q"] / RIVER_MEAN_Q) ** 0.15) * pulse
    roar = stereo_noise(r, roar_mag, coherence=0.5)
    n = N + TAIL
    # Gurgles: Poisson clusters of surface bubbles (Minnaert).
    nc = int(r.poisson(p["clusters"] * LOOP_S))
    tc = r.uniform(0.0, LOOP_S, nc)
    k = r.poisson(p["per"], nc) + 1
    tb = np.repeat(tc, k) + r.exponential(0.02, int(k.sum()))
    nb = tb.size
    rad = np.clip(p["rmed"] * r.lognormal(0.0, 0.5, nb), 0.0005, 0.02)
    amps = np.sqrt(rad / rad.max()) * r.lognormal(0.0, 0.4, nb)
    pans = np.repeat(r.uniform(-0.9, 0.9, nc), k)
    gurgle = wrap(bubbles_st(n, tb, rad, amps, pans, r))
    # Lapping of small waves on the bank.
    nl = int(r.poisson(p["lap"] * LOOP_S))
    lap = np.zeros((n, 2))
    for t0 in r.uniform(0.0, LOOP_S, nl):
        i0 = C.ns(t0)
        m = C.ns(0.6)
        seg = C.bandpass(r.standard_normal(m), 80.0, 1200.0) * C.env_ad(m, 0.03, 0.15)
        gl, gr = C.pan_gains(r.uniform(-0.8, 0.8))
        a = r.lognormal(0.0, 0.4)
        lap[i0:i0 + m, 0] += a * gl * seg
        lap[i0:i0 + m, 1] += a * gr * seg
    lap = wrap(lap)
    y = p["roar"] * unit_rms(roar) + 0.9 * unit_rms(gurgle) + (0.5 * unit_rms(lap) if nl else 0.0)
    info = {"discharge_m3s": p["q"], "discharge_ratio": round(p["q"] / RIVER_MEAN_Q, 4), "gurgle_clusters": nc,
            "bubbles": int(nb), "bubble_median_radius_m": p["rmed"], "bubble_median_minnaert_hz": round(float(C.minnaert_hz(p["rmed"])), 1),
            "lapping_events": nl}
    if p["flood"]:
        hiss_mag = band(f, 900.0, 7000.0) * (1.0 + 0.3 * ctl(NFR, r, 30, 0.5)[:, None])
        hiss = stereo_noise(r, hiss_mag, coherence=0.3)
        nk = int(r.poisson(0.2 * LOOP_S))
        knocks = np.zeros((n, 2))
        for t0 in r.uniform(0.0, LOOP_S, nk):
            x = C.impulses(C.ns(1.0), [0.005], [1.0])
            f1 = r.uniform(110.0, 320.0)
            s = C.modal_bank(x, [f1, 2.7 * f1, 5.1 * f1], [0.05, 0.025, 0.012], [1.0, 0.4, 0.2])
            gl, gr = C.pan_gains(r.uniform(-0.8, 0.8))
            i0 = C.ns(t0)
            knocks[i0:i0 + s.size, 0] += gl * s
            knocks[i0:i0 + s.size, 1] += gr * s
        y = y + 0.6 * unit_rms(hiss) + (0.35 * unit_rms(wrap(knocks)) if nk else 0.0)
        info.update({"standing_wave_hiss": True, "debris_knocks": nk})
    return y, info


def river_level_db(q: float, dist_m: float = 5.0) -> float:
    """Core WaterSystem::noiseLevelDb: 48 dB at 5 m for the mean discharge, line source (-3 dB / doubling)."""
    return max(0.0, 48.0 + 10.0 * math.log10(max(0.05, q / RIVER_MEAN_Q)) - 10.0 * math.log10(max(5.0, dist_m) / 5.0))


# ------------------------------------------------------------------------------------------------
# Rain
# ------------------------------------------------------------------------------------------------
def terminal_velocity(d_mm):
    """Empirical fit v(D) = 9.65 - 10.3 exp(-0.6 D) m/s, D in mm (game assumption, standard meteorological fit)."""
    return np.maximum(0.1, 9.65 - 10.3 * np.exp(-0.6 * np.asarray(d_mm)))


def rain_drops(r, rate_mm_h, area_m2, d_min=0.5, d_max=6.0):
    lam = 4.1 * rate_mm_h ** -0.21
    grid = np.linspace(d_min, d_max, 2000)
    flux_pdf = 8000.0 * np.exp(-lam * grid) * terminal_velocity(grid)          # drops m^-2 s^-1 mm^-1
    flux = float(np.trapezoid(flux_pdf, grid))
    count = int(r.poisson(flux * area_m2 * LOOP_S))
    cdf = np.cumsum(flux_pdf)
    cdf /= cdf[-1]
    d = np.interp(r.uniform(0, 1, count), cdf, grid)
    t = r.uniform(0.0, LOOP_S, count)
    rmax = math.sqrt(area_m2 / math.pi)
    dist = np.maximum(0.4, rmax * np.sqrt(r.uniform(0, 1, count)))
    amp = d ** 3 * terminal_velocity(d) / dist                                    # momentum / distance
    amp = amp / np.median(amp) * r.lognormal(0.0, 0.3, count)
    return t, d, amp, {"lambda_per_mm": round(lam, 4), "flux_drops_m2_s_above_dmin": round(flux, 1), "drops_rendered": count,
                       "near_field_area_m2": area_m2, "d_min_mm": d_min}


def rain(r, rate_mm_h, surface):
    n = N + TAIL
    f = np.maximum(FB[None, :], 1.0)
    area = 2.0 if rate_mm_h < 5 else 1.0
    t, d, amp, info = rain_drops(r, rate_mm_h, area)
    pans = r.uniform(-1, 1, t.size)
    sgn = r.choice([-1.0, 1.0], t.size)
    if surface == "forest":
        leaf = r.uniform(0, 1, t.size) < 0.7
        y_leaf = modal_crackle_st(n, r, t[leaf], amp[leaf] * sgn[leaf], pans[leaf], 600.0, 6000.0, groups=32, nmodes=2,
                                  tau=(0.0004, 0.0015), broad=0.8, broad_band=(800.0, 12000.0))
        g = ~leaf
        y_gnd = modal_crackle_st(n, r, t[g], amp[g] * sgn[g], pans[g], 250.0, 2000.0, groups=16, nmodes=2,
                                 tau=(0.001, 0.003), broad=0.4, broad_band=(150.0, 2500.0))
        # Canopy drip: large drops (4-5.5 mm) falling from leaves onto the understorey, intermittent.
        nd = int(r.poisson((3.0 + 0.6 * rate_mm_h) * LOOP_S))
        td = r.uniform(0.0, LOOP_S, nd)
        ad = r.lognormal(0.0, 0.4, nd) * 2.5 * r.choice([-1.0, 1.0], nd)
        y_drip = modal_crackle_st(n, r, td, ad, r.uniform(-1, 1, nd), 300.0, 2500.0, groups=16, nmodes=2,
                                  tau=(0.001, 0.004), broad=0.4)
        near = wrap(y_leaf + 0.6 * y_gnd + 0.5 * y_drip)
        bed_mag = band(f, 300.0, 9000.0) * (f / 2500.0) ** -0.3 * (1.0 + 0.12 * ctl(NFR, r, 6, 0.5)[:, None])
        info.update({"surface": "forest (70 % feuilles, 30 % litière) + égouttement de la canopée", "canopy_drips": nd})
    else:
        y_imp = modal_crackle_st(n, r, t, amp * sgn, pans, 1500.0, 9000.0, groups=4, nmodes=2, tau=(0.0002, 0.0006),
                                 broad=1.0, broad_band=(500.0, 12000.0))
        type1 = (d >= 0.8) & (d <= 1.1)
        big = (d > 2.2) & (r.uniform(0, 1, d.size) < 0.4)
        rb = np.concatenate([0.00022 * r.lognormal(0.0, 0.08, int(type1.sum())), r.uniform(0.0004, 0.002, int(big.sum()))])
        tb = np.concatenate([t[type1], t[big]]) + 0.0015
        ab = np.concatenate([1.2 * np.median(amp) * r.lognormal(0, 0.3, int(type1.sum())), amp[big] * 0.8])
        pb = np.concatenate([pans[type1], pans[big]])
        y_bub = bubbles_st(n, tb, rb, ab, pb, r)
        near = wrap(y_imp + 0.8 * y_bub)
        bed_mag = band(f, 500.0, 16000.0) * (f / 4000.0) ** -0.2 * (1.0 + 0.12 * ctl(NFR, r, 6, 0.5)[:, None])
        info.update({"surface": "eau libre (mare, chenal calme)", "type1_bubbles": int(type1.sum()), "large_drop_bubbles": int(big.sum()),
                     "type1_bubble_hz": round(float(C.minnaert_hz(0.00022)), 1)})
    bed = stereo_noise(r, bed_mag, coherence=0.1)
    w_bed = 0.6 if rate_mm_h < 5 else 1.0
    y = unit_rms(near) + w_bed * unit_rms(bed)
    info.update({"rain_rate_mm_h": rate_mm_h, "diffuse_bed_weight": w_bed})
    return y, info


def rain_level_db(rate_mm_h: float) -> float:
    """Core WeatherSystem: rainNoiseDb = 38 + 8 log10(1 + 4 R) x 2."""
    return 38.0 + 16.0 * math.log10(1.0 + 4.0 * rate_mm_h)


def wind_level_db(u: float) -> float:
    """Core WeatherSystem: windNoiseDb = 22 + 2.2 (U + gust), gust = 0.35 U here."""
    return 22.0 + 2.2 * u * (1.0 + U_GUST_FACTOR)


# ------------------------------------------------------------------------------------------------
# Biophony (hypotheses)
# ------------------------------------------------------------------------------------------------
def _periodic_hz(f: float) -> float:
    """Round a carrier frequency to a whole number of cycles per loop (exactly periodic carrier)."""
    return round(f * LOOP_S) / LOOP_S


def _schedule(r, period, jitter, t_first=None):
    """Quasi-periodic event times over one loop (an individual's call rhythm)."""
    t = r.uniform(0.0, period) if t_first is None else t_first
    out = []
    while t < LOOP_S:
        out.append(t)
        t += period * r.lognormal(0.0, jitter)
    return np.array(out)


def _pulse_env(n, starts, pulse_s, amps=None):
    """Envelope made of raised-sine pulses starting at `starts` (circularly wrapped)."""
    x = C.impulses(n, starts, np.ones(len(starts)) if amps is None else amps)
    m = max(4, C.ns(pulse_s))
    k = np.sin(np.linspace(0.0, math.pi, m)) ** 2
    from scipy.signal import oaconvolve
    return oaconvolve(x, k)[:n]


def insects_katydid(r, n_ind, d_range, T, RH, density_note):
    n = N + TAIL
    out = np.zeros((n, 2))
    inds = []
    tt = C.tvec(n)
    for i in range(n_ind):
        fc = _periodic_hz(float(np.clip(6400.0 * r.lognormal(0.0, 0.12), 4500.0, 9000.0)))
        pulses = int(r.integers(2, 7))
        pulse_s = r.uniform(0.008, 0.016)
        ipi = r.uniform(0.025, 0.045)
        period = r.uniform(0.6, 2.5)
        chirps = _schedule(r, period, 0.08)
        starts = (chirps[:, None] + ipi * np.arange(pulses)[None, :]).ravel()
        env = _pulse_env(n, starts, pulse_s)
        dist = float(np.sqrt(r.uniform(d_range[0] ** 2, d_range[1] ** 2)))
        att = 10.0 ** (-(float(P.absorption_db_per_m(fc, T, RH)) * dist + 0.3 * dist * float(P.foliage_db_per_m(fc))) / 20.0) / dist
        s = env * np.sin(2.0 * math.pi * fc * tt + r.uniform(0, 2 * math.pi)) * att
        gl, gr = C.pan_gains(r.uniform(-1, 1))
        out[:, 0] += gl * s
        out[:, 1] += gr * s
        inds.append({"carrier_hz": round(fc, 2), "pulses_per_chirp": pulses, "pulse_ms": round(1e3 * pulse_s, 1),
                     "chirp_period_s": round(period, 3), "distance_m": round(dist, 1)})
    return wrap(out), {"individuals": inds, "density": density_note, "temperature_c": T, "relative_humidity": RH,
                       "model": "tons purs pulsés (stridulation alaire de type Ensifera), porteuse ~6,4 kHz d'après Gu et al. 2012 (Jurassique)"}


def insects_flybys(r, T, RH):
    n = N + TAIL
    out = np.zeros((n, 2))
    times = C.poisson_times(r, lambda t: np.full(np.shape(t), 1.0 / 6.0), 0.0, LOOP_S, 1.0 / 6.0)
    c = P.speed_of_sound(T)
    info = []
    for t0 in times:
        fw = float(np.clip(180.0 * r.lognormal(0.0, 0.35), 90.0, 450.0))     # wingbeat frequency (Hz)
        v = r.uniform(1.0, 3.5)
        d0 = r.uniform(0.3, 3.0)
        dur = r.uniform(1.5, 4.0)
        m = C.ns(dur)
        tl = C.tvec(m) - dur / 2.0
        x = v * tl
        rr = np.sqrt(x * x + d0 * d0)
        vr = v * x / rr                                   # radial velocity (positive = receding)
        f_inst = fw * c / (c + vr)
        ph = 2.0 * math.pi * np.cumsum(f_inst) / C.SR
        s = sum(h ** -1.2 * np.sin(h * ph + r.uniform(0, 6.28)) for h in range(1, 9))
        s *= (1.0 + 0.3 * np.sin(ph / 2.0))                # down/up-stroke asymmetry
        a = (d0 / rr) * C.ramp_env(m, C.ns(0.2), C.ns(0.2))
        gl, gr = pan_gains(np.clip(x / 3.0, -1, 1))
        i0 = C.ns(t0)
        out[i0:i0 + m, 0] += a * gl * s
        out[i0:i0 + m, 1] += a * gr * s
        info.append({"t_s": round(float(t0), 2), "wingbeat_hz": round(fw, 1), "closest_m": round(d0, 2), "speed_ms": round(v, 2)})
    return wrap(out), {"flybys": info, "model": "bourdonnement d'ailes (harmoniques de la fréquence de battement) avec effet Doppler"}


def frogs(r, n_ind, d_range, T, RH):
    n = N + TAIL
    out = np.zeros((n, 2))
    tt = C.tvec(n)
    inds = []
    for i in range(n_ind):
        f0 = _periodic_hz(float(np.clip(700.0 * r.lognormal(0.0, 0.3), 300.0, 1800.0)))
        fd = float(np.clip(f0 * r.uniform(1.8, 3.5), 800.0, 3500.0))       # dominant (resonance) frequency
        prate = r.uniform(15.0, 60.0)
        cdur = r.uniform(0.15, 0.8)
        period = r.uniform(1.0, 4.5)
        calls = _schedule(r, period, 0.15)
        starts = (calls[:, None] + np.arange(0.0, cdur, 1.0 / prate)[None, :]).ravel()
        env = _pulse_env(n, starts, 0.55 / prate)
        H = int(min(12, 16000.0 // f0))
        car = np.zeros(n)
        for h in range(1, H + 1):
            g = 1.0 / (1.0 + ((h * f0 - fd) / (0.35 * fd)) ** 2)
            car += g * np.sin(2.0 * math.pi * h * f0 * tt + r.uniform(0, 6.28))
        dist = float(np.sqrt(r.uniform(d_range[0] ** 2, d_range[1] ** 2)))
        att = 10.0 ** (-float(P.absorption_db_per_m(fd, T, RH)) * dist / 20.0) / dist
        s = env * car * att
        gl, gr = C.pan_gains(r.uniform(-1, 1))
        out[:, 0] += gl * s
        out[:, 1] += gr * s
        inds.append({"f0_hz": round(f0, 2), "dominant_hz": round(fd, 1), "pulse_rate_hz": round(prate, 1), "call_s": round(cdur, 3),
                     "call_period_s": round(period, 3), "distance_m": round(dist, 1)})
    return wrap(out), {"individuals": inds, "temperature_c": T, "relative_humidity": RH,
                       "model": "appels pulsés d'anoures (série harmonique filtrée par une résonance) — structure empruntée aux anoures actuels"}


def birds(ds, r, n_ind, d_range, T, RH, foliage_frac):
    sp = ds.species["hell_creek_avifauna"]
    call = sp.sim["vocal"]["call_types"][0]
    style = V.call_style(ds, sp, call)
    ctx = D.parse_call_context(call.get("context", "other"))
    n = N + TAIL
    out = np.zeros((n, 2))
    inds = []
    for i in range(n_ind):
        pitch = float(np.clip(r.lognormal(0.0, 0.2), 0.6, 1.6))
        dscale = float(np.clip(r.lognormal(0.0, 0.25), 0.5, 1.8))
        dist = float(np.sqrt(r.uniform(d_range[0] ** 2, d_range[1] ** 2)))
        pan = r.uniform(-1, 1)
        gl, gr = C.pan_gains(pan)
        bout_period = r.uniform(5.0, 15.0)
        bouts = _schedule(r, bout_period, 0.25)
        ncalls = 0
        for tb in bouts:
            k = int(r.integers(2, 6))
            t = tb
            for _ in range(k):
                x, f, benv, i0, ncall = V.synth_call(style, float(call["f0_hz"]), float(call["duration_s"]) * dscale, ctx, pitch,
                                                     sp.mass, r, timbre=r.uniform(-1, 1))
                y = P.apply_propagation(x, dist, T, RH, foliage_m=foliage_frac * dist) / dist
                a = r.lognormal(0.0, 0.2)
                j = C.ns(t)
                m = min(y.size, n - j)
                if m > 0:
                    out[j:j + m, 0] += a * gl * y[:m]
                    out[j:j + m, 1] += a * gr * y[:m]
                ncalls += 1
                t += float(call["duration_s"]) * dscale + r.uniform(0.25, 0.7)
        inds.append({"pitch_factor": round(pitch, 3), "duration_factor": round(dscale, 3), "distance_m": round(dist, 1),
                     "bout_period_s": round(bout_period, 2), "calls": ncalls})
    return wrap(out), {"call_source": f"{sp.id} sim.vocal.call_types[0] ({call['id']}, f0 {call['f0_hz']} Hz, {call['duration_s']} s)",
                       "production_model": style, "individuals": inds, "temperature_c": T, "relative_humidity": RH,
                       "foliage_fraction_of_path": foliage_frac}


# ------------------------------------------------------------------------------------------------
# Thunder
# ------------------------------------------------------------------------------------------------
THUNDER_DISTANCES = [500.0, 2000.0, 5000.0, 12000.0]
THUNDER_LEVEL_1M = 125.0         # core WorldSimulation::handleLightning (equivalent SPL at 1 m, game assumption)
THUNDER_CENTROID_QA = {500.0: [50.0, 1500.0], 2000.0: [35.0, 900.0], 5000.0: [25.0, 500.0], 12000.0: [15.0, 300.0]}


def _walk(r, start, direction, length, bias, tort=0.35, step=(5.0, 20.0), zmin=0.0, zmax=None):
    pts = [np.asarray(start, dtype=np.float64)]
    d = np.asarray(direction, dtype=np.float64)
    d /= np.linalg.norm(d)
    total = 0.0
    while total < length:
        nd = 0.5 * np.asarray(bias) + 0.5 * d + r.normal(0.0, tort, 3)
        nd /= np.linalg.norm(nd) + 1e-12
        s = r.uniform(*step)
        p = pts[-1] + s * nd
        if p[2] < zmin or (zmax is not None and p[2] > zmax):
            break
        pts.append(p)
        d = nd
        total += s
    return np.array(pts)


def thunder(r, dist_m, T, RH):
    c = P.speed_of_sound(T)
    H = r.uniform(3000.0, 5000.0)
    az = r.uniform(-0.3, 0.3)
    ground = np.array([dist_m * math.cos(az), dist_m * math.sin(az), 0.0])
    main = _walk(r, ground, [0, 0, 1], 2.0 * H, [0, 0, 1], zmin=-1.0, zmax=H)
    paths = [(main, 1.0)]
    for _ in range(int(r.integers(2, 5))):
        k = int(r.integers(len(main) // 4, len(main) - 1))
        hdir = r.normal(0, 1, 3)
        hdir[2] = -abs(r.uniform(0.2, 0.8))
        paths.append((_walk(r, main[k], hdir, r.uniform(300.0, 1500.0), [hdir[0], hdir[1], -0.3], zmin=0.0), 0.5))
    L = np.array([0.0, 0.0, 1.7])
    segs = []
    for pts, w in paths:
        a, b = pts[:-1], pts[1:]
        mid = 0.5 * (a + b)
        u = b - a
        ln = np.linalg.norm(u, axis=1)
        u /= ln[:, None] + 1e-12
        rv = mid - L
        rr = np.linalg.norm(rv, axis=1)
        cosang = np.abs(np.sum(u * rv, axis=1)) / rr
        g = 0.15 + np.exp(-(cosang / 0.3) ** 2)                 # radiation mostly perpendicular to the segment
        e = r.lognormal(0.0, 0.5, rr.size) * w
        amp = g * np.sqrt(ln) * e / rr
        tn = np.clip(0.009 * r.lognormal(0.0, 0.35, rr.size) * (rr / 1000.0) ** 0.25, 0.002, 0.05)
        azm = np.arctan2(rv[:, 1], rv[:, 0])
        segs.append(np.column_stack([rr, amp, tn, azm]))
    S = np.vstack(segs)
    rr, amp, tn, azm = S[:, 0], S[:, 1], S[:, 2], S[:, 3]
    t_arr = rr / c
    t0 = float(t_arr.min()) - 0.08
    dur = float(t_arr.max() - t_arr.min()) + 3.0
    n = C.ns(min(dur, 24.0) + 0.1)
    edges = np.geomspace(rr.min() * 0.999, rr.max() * 1.001, 21)
    bins = np.clip(np.digitize(rr, edges) - 1, 0, 19)
    gl, gr = pan_gains(np.clip(azm / (math.pi / 2.0), -1.0, 1.0))
    y = np.zeros((n, 2))
    for b in range(20):
        sel = np.nonzero(bins == b)[0]
        if sel.size == 0:
            continue
        acc = np.zeros((n, 2))
        for i in sel:
            j = C.ns(t_arr[i] - t0)
            m = max(3, C.ns(tn[i]))
            if j >= n:
                continue
            k = 1.0 - 2.0 * np.arange(m) / (m - 1)              # N-wave (shock front then linear decay to the tail shock)
            mm = min(m, n - j)
            acc[j:j + mm, 0] += amp[i] * gl[i] * k[:mm]
            acc[j:j + mm, 1] += amp[i] * gr[i] * k[:mm]
        rb = float(math.sqrt(edges[b] * edges[b + 1]))
        y += P.apply_propagation(acc, rb, T, RH)                 # absolute ISO 9613-1 absorption over rb
    # Diffuse scattering / echoes from the valley and forest edges (game assumption, RT60 ~ 2.5 s, low-passed).
    m = C.ns(3.0)
    rev = C.lowpass(r.standard_normal((m, 2)), 900.0) * np.exp(-6.9 * C.tvec(m) / 2.5)[:, None]
    from scipy.signal import fftconvolve
    tail = np.stack([fftconvolve(y[:, ch], rev[:, ch])[:n] for ch in range(2)], axis=1)
    y = y + 0.3 * C.rms(y) / (C.rms(tail) + 1e-15) * tail
    r_core = math.sqrt(dist_m ** 2 + 500.0 ** 2)
    received = THUNDER_LEVEL_1M - 20.0 * math.log10(r_core) - float(P.absorption_db_per_m(60.0, T, RH)) * r_core
    info = {"strike_distance_m": dist_m, "channel_top_m": round(H, 1), "segments": int(S.shape[0]), "branches": len(paths) - 1,
            "delay_after_flash_s": round(float(t_arr.min()), 3), "geometric_rumble_s": round(float(t_arr.max() - t_arr.min()), 3),
            "n_wave_ms_median": round(float(np.median(tn)) * 1e3, 2), "speed_of_sound_ms": round(c, 2),
            "core_source_distance_m": round(r_core, 1), "received_spl_db_core": round(received, 2),
            "file_starts_s_after_flash": round(t0, 3)}
    return y, info, received


# ------------------------------------------------------------------------------------------------
# Layer table
# ------------------------------------------------------------------------------------------------
def _cond(ds, region, when):
    """Temperature / humidity for day and night of the warm (wet) season: T = mean + range/4 +/- diurnal/2
    (game assumption); RH 0.65 by day, 0.9 at night in Hell Creek, 0.25 / 0.45 in the Djadochta."""
    env = ds.env(region).sim
    t = env["mean_annual_temp_c"] + env["annual_temp_range_c"] / 4.0 + (0.5 if when == "day" else -0.5) * env["diurnal_temp_range_c"]
    rh = {"hell_creek": {"day": 0.65, "night": 0.9}, "djadochta": {"day": 0.25, "night": 0.45}}[region][when]
    return float(t), rh


LAYERS = {
    # key: (region, kind, params, label_fr, label_en)
    "hc_wind_conifer_calm": ("hell_creek", "wind_conifer", {"u": 1.5}, "Vent dans les conifères — calme (1,5 m/s)", "Wind in conifers — calm"),
    "hc_wind_conifer_moderate": ("hell_creek", "wind_conifer", {"u": 3.0}, "Vent dans les conifères — modéré (3 m/s, moyenne)", "Wind in conifers — moderate"),
    "hc_wind_conifer_strong": ("hell_creek", "wind_conifer", {"u": 8.0}, "Vent dans les conifères — fort (8 m/s)", "Wind in conifers — strong"),
    "hc_wind_broadleaf_calm": ("hell_creek", "wind_broadleaf", {"u": 1.5}, "Vent dans les feuillus — calme (1,5 m/s)", "Wind in broadleaf canopy — calm"),
    "hc_wind_broadleaf_moderate": ("hell_creek", "wind_broadleaf", {"u": 3.0}, "Vent dans les feuillus — modéré (3 m/s)", "Wind in broadleaf canopy — moderate"),
    "hc_wind_broadleaf_strong": ("hell_creek", "wind_broadleaf", {"u": 8.0}, "Vent dans les feuillus — fort (8 m/s)", "Wind in broadleaf canopy — strong"),
    "hc_river_creek": ("hell_creek", "river", {"key": "creek"}, "Eau courante — " + RIVERS["creek"]["fr"], "Running water — floodplain creek"),
    "hc_river_low": ("hell_creek", "river", {"key": "low"}, "Eau courante — " + RIVERS["low"]["fr"], "Running water — river, low flow"),
    "hc_river_mean": ("hell_creek", "river", {"key": "mean"}, "Eau courante — " + RIVERS["mean"]["fr"], "Running water — river, mean flow"),
    "hc_river_flood": ("hell_creek", "river", {"key": "flood"}, "Eau courante — " + RIVERS["flood"]["fr"], "Running water — river in flood"),
    "hc_rain_light_forest": ("hell_creek", "rain", {"rate": 2.0, "surface": "forest"}, "Pluie faible (2 mm/h) en forêt", "Light rain in forest"),
    "hc_rain_heavy_forest": ("hell_creek", "rain", {"rate": 25.0, "surface": "forest"}, "Pluie forte (25 mm/h) en forêt", "Heavy rain in forest"),
    "hc_rain_light_water": ("hell_creek", "rain", {"rate": 2.0, "surface": "water"}, "Pluie faible (2 mm/h) sur l'eau", "Light rain on water"),
    "hc_rain_heavy_water": ("hell_creek", "rain", {"rate": 25.0, "surface": "water"}, "Pluie forte (25 mm/h) sur l'eau", "Heavy rain on water"),
    "hc_insects_night_chorus": ("hell_creek", "katydid", {"n": 18, "d": (3.0, 45.0), "when": "night"},
                                "Chœur nocturne d'insectes stridulants (hypothèse)", "Night chorus of stridulating insects (hypothesis)"),
    "hc_insects_day_flybys": ("hell_creek", "flybys", {"when": "day"}, "Passages d'insectes volants (hypothèse)", "Flying insects passing (hypothesis)"),
    "hc_frogs_night_chorus": ("hell_creek", "frogs", {"n": 12, "d": (3.0, 40.0), "when": "night"},
                              "Chœur nocturne d'anoures près d'un bras mort (hypothèse)", "Night frog chorus by an oxbow (hypothesis)"),
    "hc_birds_day": ("hell_creek", "birds", {"n": 8, "d": (12.0, 120.0), "when": "day", "foliage": 0.3},
                     "Oiseaux du Maastrichtien, activité diurne (hypothèse)", "Maastrichtian birds, daytime activity (hypothesis)"),
    "dj_wind_calm": ("djadochta", "wind_desert", {"u": 2.0}, "Vent sur les dunes — calme (2 m/s)", "Wind over dunes — calm"),
    "dj_wind_moderate": ("djadochta", "wind_desert", {"u": 5.5}, "Vent sur les dunes — moyen (5,5 m/s), saltation intermittente",
                         "Wind over dunes — mean, intermittent saltation"),
    "dj_wind_strong": ("djadochta", "wind_desert", {"u": 11.0}, "Vent sur les dunes — fort (11 m/s), saltation du sable",
                       "Wind over dunes — strong, blowing sand"),
    "dj_insects_night_sparse": ("djadochta", "katydid", {"n": 4, "d": (10.0, 60.0), "when": "night"},
                                "Insectes stridulants épars, nuit (hypothèse)", "Sparse stridulating insects at night (hypothesis)"),
}

MIXES = {
    "hc_mix_day_riverbank": ("hell_creek", "day", "Mixage jour — berge de la rivière (forêt riveraine)", "Day mix — riverbank (riparian forest)",
                             [("hc_wind_broadleaf_moderate", None), ("hc_river_mean", river_level_db(60.0, 30.0)),
                              ("hc_birds_day", 38.0), ("hc_insects_day_flybys", 26.0)]),
    "hc_mix_night_riverbank": ("hell_creek", "night", "Mixage nuit — berge de la rivière", "Night mix — riverbank",
                               [("hc_wind_broadleaf_calm", None), ("hc_river_mean", river_level_db(60.0, 30.0)),
                                ("hc_insects_night_chorus", 38.0), ("hc_frogs_night_chorus", 32.0)]),
    "hc_mix_day_backswamp": ("hell_creek", "day", "Mixage jour — marais arrière à conifères", "Day mix — conifer backswamp",
                             [("hc_wind_conifer_moderate", None), ("hc_river_creek", river_level_db(1.5, 20.0) + 6.0),
                              ("hc_birds_day", 35.0), ("hc_insects_day_flybys", 28.0), ("hc_frogs_night_chorus", 24.0)]),
    "hc_mix_night_backswamp": ("hell_creek", "night", "Mixage nuit — marais arrière à conifères", "Night mix — conifer backswamp",
                               [("hc_wind_conifer_calm", None), ("hc_frogs_night_chorus", 44.0), ("hc_insects_night_chorus", 36.0)]),
    "dj_mix_day_dunes": ("djadochta", "day", "Mixage jour — champ de dunes", "Day mix — dune field", [("dj_wind_moderate", None)]),
    "dj_mix_night_dunes": ("djadochta", "night", "Mixage nuit — champ de dunes", "Night mix — dune field",
                           [("dj_wind_calm", None), ("dj_insects_night_sparse", 28.0)]),
}


def layer_level_db(key: str) -> float | None:
    region, kind, p, _, _ = LAYERS[key]
    if kind.startswith("wind"):
        return wind_level_db(p["u"])
    if kind == "river":
        return river_level_db(RIVERS[p["key"]]["q"], 5.0)
    if kind == "rain":
        return rain_level_db(p["rate"])
    return {"katydid": 40.0, "flybys": 26.0, "frogs": 42.0, "birds": 38.0}[kind] - (12.0 if key == "dj_insects_night_sparse" else 0.0)


@lru_cache(maxsize=8)
def render_layer(key: str):
    from . import data as _D
    ds = _D.load()
    region, kind, p, _, _ = LAYERS[key]
    r = C.rng("ambience", key)
    when = p.get("when", "day")
    T, RH = _cond(ds, region, when)
    if kind == "wind_conifer":
        y, info = wind_conifer(r, p["u"])
    elif kind == "wind_broadleaf":
        y, info = wind_broadleaf(r, p["u"])
    elif kind == "wind_desert":
        y, info = wind_desert(r, p["u"])
    elif kind == "river":
        y, info = river(r, p["key"])
    elif kind == "rain":
        y, info = rain(r, p["rate"], p["surface"])
    elif kind == "katydid":
        y, info = insects_katydid(r, p["n"], p["d"], T, RH, "chœur dense" if p["n"] > 8 else "individus épars")
    elif kind == "flybys":
        y, info = insects_flybys(r, T, RH)
    elif kind == "frogs":
        y, info = frogs(r, p["n"], p["d"], T, RH)
    elif kind == "birds":
        y, info = birds(ds, r, p["n"], p["d"], T, RH, p["foliage"])
    else:
        raise KeyError(kind)
    assert y.shape == (N, 2), (key, y.shape)
    return unit_rms(y), info


# ------------------------------------------------------------------------------------------------
# Science metadata per layer
# ------------------------------------------------------------------------------------------------
def _layer_science(ds, key):
    region, kind, p, _, _ = LAYERS[key]
    env = ds.env(region).raw["science"]
    hyp = kind in ("katydid", "flybys", "frogs", "birds")

    def cl(topic):
        return env.get(topic) or {"confidence": "unknown", "sources": [], "summary": D.INSUFFICIENT_FR}

    if kind in ("wind_conifer", "wind_broadleaf"):
        c = cl("flora")
        conf = D.conf_min("inferred", c["confidence"])
        what = ("conifères Cupressaceae s.l. (Metasequoia, Glyptostrobus, feuillages à écailles)" if kind == "wind_conifer"
                else "arbres angiospermes à feuilles larges (dominants)")
        basis = (f"Flore [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : {what}. Son = modèle physique : "
                 + ("tons éoliens des aiguilles et rameaux (f = 0,2 U/d, diamètres 0,7–3 mm, hypothèse de jeu), rayonnement dipolaire ~U³."
                    if kind == "wind_conifer" else "chocs feuille contre feuille (modes de plaques 0,7–5 kHz) dont le taux croît comme U³, et souffle ~U^2,5.")
                 + " Rafales périodiques (intensité de turbulence 0,3). Niveau : formule windNoiseDb du cœur.")
        return conf, list(c["sources"]), basis, hyp
    if kind == "wind_desert":
        c = cl("landscape")
        conf = D.conf_min("inferred", c["confidence"])
        basis = (f"Paysage [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : champ de dunes et broussailles clairsemées. Son : turbulence basse "
                 f"fréquence, tons éoliens de rameaux (2–8 mm), saltation du sable au-delà de {SALTATION_U_MS} m/s (seuil = hypothèse de jeu).")
        return conf, list(c["sources"]), basis, hyp
    if kind == "river":
        c = cl("hydrology")
        conf = D.conf_min("inferred", c["confidence"])
        basis = (f"Hydrologie [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : rivières à méandres, chenaux sableux. Débit moyen du chenal principal "
                 f"{RIVER_MEAN_Q:g} m³/s = hypothèse du générateur de terrain du cœur. Son : grondement turbulent, grappes de bulles de surface "
                 "à la fréquence de Minnaert, clapotis sur la berge ; niveau = WaterSystem::noiseLevelDb.")
        return conf, list(c["sources"]) + [MINN], basis, hyp
    if kind == "rain":
        c = cl("precipitation")
        conf = D.conf_min("inferred", c["confidence"])
        basis = (f"Précipitations [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : climat humide (~1 900 mm/an). Gouttes tirées de la distribution "
                 "de Marshall & Palmer (1948) ; vitesse terminale (ajustement empirique, hypothèse de jeu) ; impacts sur feuilles / litière / eau ; "
                 "sur l'eau, bulles d'entraînement régulier (type I, ~0,2 mm, ~14 kHz) et irrégulier (Prosperetti & Oguz 1993). "
                 "Niveau : rainNoiseDb du cœur.")
        return conf, list(c["sources"]) + [MP, PROSP, MINN], basis, hyp
    if kind == "katydid":
        sp = ds.species["hell_creek_insects"]
        basis = ("HYPOTHÈSE. La stridulation alaire d'Ensifera produisant un ton pur (~6,4 kHz) est reconstituée pour le Jurassique "
                 "(Archaboilus musicus, Gu et al. 2012). Aucun insecte chanteur n'est documenté dans la "
                 + ("Formation de Hell Creek" if region == "hell_creek" else "Formation de Djadochta")
                 + " ; communication des insectes : « " + sp.claim("communication")["summary"] + " ». Rythmes, densité et distances : "
                 "hypothèses de jeu ; absorption ISO 9613-1 sur la distance de chaque individu.")
        srcs = [GU] + list(sp.claim("taxonomy").get("sources", []))
        if region == "djadochta":
            srcs = [GU, "kirkland2010_insect_traces"]
        return "speculative", srcs + [ISO1], basis, hyp
    if kind == "flybys":
        sp = ds.species["hell_creek_insects"]
        c = sp.claim("taxonomy")
        basis = (f"Insectes [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : connus surtout par leurs traces d'alimentation sur les feuilles. "
                 "Le bourdonnement d'ailes (fréquence de battement 90–450 Hz) et l'effet Doppler sont physiques ; les taxons volants "
                 "et leurs fréquences réelles sont inconnus : HYPOTHÈSE.")
        return "speculative", list(c.get("sources", [])), basis, hyp
    if kind == "frogs":
        f = cl("fauna")
        basis = (f"Faune [{D.CONFIDENCE_LABEL_FR[f['confidence']]}] : amphibiens présents (assemblage de lissamphibiens incluant des anoures, "
                 "Gardner & DeMar 2013, non vérifié). Les appels des anoures du Crétacé sont inconnus : structure (trains d'impulsions "
                 "harmoniques 0,3–1,8 kHz) empruntée aux anoures actuels — HYPOTHÈSE.")
        return "speculative", list(f["sources"]) + [GARDNER, ISO1], basis, hyp
    if kind == "birds":
        sp = ds.species["hell_creek_avifauna"]
        c = sp.claim("taxonomy")
        basis = (f"Oiseaux [{D.CONFIDENCE_LABEL_FR[c['confidence']]}] : {c['summary']} Un syrinx est attesté chez Vegavis "
                 "(Maastrichtien, Antarctique ; Clarke et al. 2016) mais aucun cri n'est connu. Cris = paramètres sim.vocal de "
                 "hell_creek_avifauna synthétisés par le module vocal, variation individuelle de hauteur et de durée, "
                 "propagation ISO 9613-1 + feuillage — HYPOTHÈSE.")
        return "speculative", list(c.get("sources", [])) + [CLARKE, ISO1, ISO2], basis, hyp
    raise KeyError(kind)


# ------------------------------------------------------------------------------------------------
# Jobs
# ------------------------------------------------------------------------------------------------
def jobs(ds: D.Dataset) -> list[tuple]:
    out = [("ambience", "job_layer", {"key": k}) for k in LAYERS]
    out += [("ambience", "job_thunder", {"dist": d}) for d in THUNDER_DISTANCES]
    out += [("ambience", "job_mix", {"key": k}) for k in MIXES]
    return out


def _region_fr(region):
    return {"hell_creek": "Hell Creek", "djadochta": "Djadochta"}[region]


def job_layer(ds: D.Dataset, key: str) -> list[M.Sound]:
    region, kind, p, label_fr, label_en = LAYERS[key]
    y, info = render_layer(key)
    conf, srcs, basis, hyp = _layer_science(ds, key)
    # Sparse biophony keeps more of its natural dynamics (near vs far individuals) than wind / rain / water beds.
    y, lim = limit_crest(y, 22.0 if hyp else CREST_DB)
    info = dict(info, mastering_limiter=lim)
    srcs = [s for s in dict.fromkeys(srcs) if ds.has_source(s)]
    conf = ds.capped(conf, srcs)
    T, RH = _cond(ds, region, p.get("when", "day"))
    lvl = layer_level_db(key)
    weather = kind == "rain"
    ue = M.ue5("SC_Weather" if weather else "SC_Ambience_Layer",
               "ATT_Water_Line" if kind == "river" else "ATT_Ambience_Bed2D", True,
               "CON_Weather_Rain" if weather else "CON_Ambience_Layer", priority=0.5,
               extra={"layer_role": kind, "crossfade_with": [k for k in LAYERS if LAYERS[k][1] == kind and k != key and LAYERS[k][0] == region],
                      "drive_parameter": {"wind_conifer": "vent (m/s)", "wind_broadleaf": "vent (m/s)", "wind_desert": "vent (m/s)",
                                          "river": "débit (m³/s) et distance au chenal", "rain": "intensité (mm/h)"}.get(kind, "heure / saison / profil")})
    if hyp:
        ue["profile_note_fr"] = "Couche de biophonie hypothétique : peut être coupée (option d'accessibilité / profil prudent) ; légende « hypothèse »."
    params = {"layer": key, "kind": kind, **{k: v for k, v in p.items() if k != "d"}, "loop_s": LOOP_S,
              "environment": {"region": region, "temperature_c": T, "relative_humidity": RH, "time_of_day": p.get("when", "day")},
              "synthesis": info, "seamless_method": "rendu circulaire (STFT périodique, contrôles en série de Fourier, queues repliées)"}
    if "d" in p:
        params["distance_range_m"] = list(p["d"])
    qa = {"loop_seam": True, "rms_dbfs": expected_rms_dbfs(lim), "rms_tolerance_db": 0.6, "peak_dbfs_max": -0.9}
    if kind == "katydid":
        qa["spectral_peak_hz"] = [4400.0, 9200.0]
        qa["spectral_peak_search_hz"] = [200.0, 16000.0]
    if kind == "wind_conifer":
        fc = 0.2 * p["u"] / 0.0013
        qa["centroid_hz"] = [round(0.35 * fc, 1), round(min(3.0 * fc, 16000.0), 1)]
    if kind == "rain" and p["surface"] == "water":
        qa["band_energy_ratio"] = {"lo_hz": 4000.0, "hi_hz": 20000.0, "min": 0.1}
    if kind == "river":
        qa["band_energy_ratio"] = {"lo_hz": 20.0, "hi_hz": 3000.0, "min": 0.5}
    if kind == "frogs":
        qa["band_energy_ratio"] = {"lo_hz": 300.0, "hi_hz": 4000.0, "min": 0.6}
    s = M.Sound(id=f"amb_{key}", category="weather" if weather else "ambience", subcategory=kind, group=f"ambience/{region}",
                label_fr=f"{_region_fr(region)} — {label_fr}", audio=y, confidence=conf, basis_fr=basis, sources=srcs, params=params,
                ue5=ue, qa=qa, loop=True, region=region, hypothesis=hyp,
                level={"ambient_spl_db": round(lvl, 2) if lvl is not None else None,
                       "reference": ("Niveau sonore ambiant au point d'écoute (dB SPL re 20 µPa) pour la condition de la couche ; "
                                     "formules du cœur pour vent / pluie / rivière (5 m de la berge), hypothèse de jeu pour la biophonie")},
                normalize=("rms", -20.0, -1.0), fade=False, caption={"fr": label_fr, "en": label_en},
                subject="paleo" if hyp else "physical_environment", loop_xfade=True,
                variant={"layer": key})
    return [s]


def job_mix(ds: D.Dataset, key: str) -> list[M.Sound]:
    region, when, label_fr, label_en, parts = MIXES[key]
    out = []
    variants = [("full", parts)]
    hyp_parts = [k for k, _ in parts if _layer_science(ds, k)[3]]
    abiotic = [(k, l) for k, l in parts if k not in hyp_parts]
    if hyp_parts and len(abiotic) > 1:
        variants.append(("abiotic", abiotic))
    for vname, comp in variants:
        y = np.zeros((N, 2))
        recs, srcs, confs, hyp = [], [], [], False
        tot = 0.0
        for k, spl in comp:
            lvl = layer_level_db(k) if spl is None else spl
            x, _ = render_layer(k)
            y += x * 10.0 ** (lvl / 20.0)
            tot += 10.0 ** (lvl / 10.0)
            c, s, _, h = _layer_science(ds, k)
            s = [q for q in dict.fromkeys(s) if ds.has_source(q)]
            confs.append(ds.capped(c, s))
            srcs += s
            hyp = hyp or h
            recs.append({"layer": f"amb_{k}", "spl_db": round(lvl, 2), "hypothesis": h})
        total = 10.0 * math.log10(tot)
        y, lim = limit_crest(y)
        conf = D.conf_min(*confs)
        T, RH = _cond(ds, region, when)
        sid = f"amb_{key}" + ("" if vname == "full" else "_abiotic")
        lf = label_fr + ("" if vname == "full" else " — géophonie seule (sans biophonie hypothétique)")
        le = label_en + ("" if vname == "full" else " — geophony only (no hypothetical biophony)")
        basis = ("Mixage pré-calculé des couches listées dans parameters.layers, chacune à son niveau ambiant (dB SPL) : la balance "
                 "relative est physique (formules du cœur) pour le vent, la rivière et la pluie, hypothèse de jeu pour la biophonie. "
                 + ("Contient des couches HYPOTHÉTIQUES (biophonie)." if hyp else "Sans biophonie hypothétique."))
        ue = M.ue5("SC_Ambience_Bed", "ATT_Ambience_Bed2D", True, "CON_Ambience_Bed", priority=0.6,
                   extra={"time_of_day": when, "alternative_fr": "Pour un mixage dynamique, utiliser les couches séparées (amb_*)."})
        s = M.Sound(id=sid, category="ambience", subcategory="mix", group=f"ambience/{region}", label_fr=f"{_region_fr(region)} — {lf}",
                    audio=y, confidence=conf, basis_fr=basis, sources=list(dict.fromkeys(srcs)),
                    params={"mix": key, "variant": vname, "time_of_day": when, "layers": recs, "loop_s": LOOP_S,
                            "environment": {"region": region, "temperature_c": T, "relative_humidity": RH},
                            "mastering_limiter": lim,
                            "abiotic_equivalent": (f"amb_{abiotic[0][0]}" if hyp_parts and len(abiotic) == 1 and vname == "full" else None)},
                    ue5=ue, qa={"loop_seam": True, "rms_dbfs": expected_rms_dbfs(lim), "rms_tolerance_db": 0.6, "peak_dbfs_max": -0.9},
                    loop=True,
                    region=region, hypothesis=hyp,
                    level={"ambient_spl_db": round(total, 2), "reference": "Somme énergétique des niveaux des couches (dB SPL au point d'écoute)"},
                    normalize=("rms", -20.0, -1.0), fade=False, caption={"fr": lf, "en": le},
                    subject="paleo" if hyp else "physical_environment", loop_xfade=True, variant={"mix": key, "variant": vname})
        out.append(s)
    return out


def job_thunder(ds: D.Dataset, dist: float) -> list[M.Sound]:
    region = "hell_creek"
    T, RH = _cond(ds, region, "day")
    RH = 0.9                                           # storm: saturated air (game assumption)
    out = []
    srcs = [s for s in [ISO1] if ds.has_source(s)]
    for k in range(4):
        sid = f"amb_thunder_{int(dist)}m_{k + 1:02d}"
        r = C.rng(sid)
        y, info, received = thunder(r, dist, T, RH)
        basis = ("Orages : probabilité journalière du JSON d'environnement (sim.storm_probability_per_day) ; la physique du tonnerre est "
                 "indépendante de l'époque. Modèle : canal de foudre tortueux, onde en N par segment rayonnée perpendiculairement, "
                 "allongement r^1/4, absorption ISO 9613-1 par tranche de distance, diffusion par la vallée (hypothèse de jeu). "
                 "Niveau à 1 m équivalent : 125 dB (WorldSimulation::handleLightning).")
        dist_fr = f"{dist / 1000:g} km".replace(".", ",")
        s = M.Sound(id=sid, category="weather", subcategory="thunder", group="ambience/thunder",
                    label_fr=f"Tonnerre à {dist_fr} — variation {k + 1}", audio=y, confidence=ds.capped("inferred", srcs), basis_fr=basis,
                    sources=srcs, params={"variation": k + 1, "temperature_c": T, "relative_humidity": RH, **info},
                    ue5=M.ue5("SC_Weather", "ATT_Thunder_DistanceBaked", False, "CON_Weather_Thunder", priority=0.9,
                              extra={"random_group": f"thunder_{int(dist)}m", "schedule_delay_after_flash_s": info["delay_after_flash_s"],
                                     "note_fr": "Démarrer le son delay_after_flash_s après l'éclair (le silence initial n'est pas dans le fichier)."}),
                    qa={"peak_dbfs_max": -0.5, "centroid_hz": THUNDER_CENTROID_QA[dist]}, region=None,
                    level={"source_spl_db_1m": THUNDER_LEVEL_1M, "received_spl_db": round(received, 2),
                           "reference": "Niveau reçu (dB SPL) selon le modèle de propagation du cœur à 60 Hz ; fichier normalisé en crête"},
                    normalize=("peak", -1.0), caption={"fr": f"Tonnerre lointain ({dist_fr})" if dist > 1000 else "Coup de tonnerre proche",
                                                       "en": f"Thunder ({dist / 1000:g} km)"},
                    subject="physical_environment", variant={"distance_m": dist, "variation": k + 1})
        out.append(s)
    return out
