"""Outdoor sound propagation, ported line-for-line from Source/NoctisCore/Private/World/SoundField.cpp.

* absorption_db_per_m : ISO 9613-1:1993 pure-tone atmospheric absorption (iso1993_9613_1)
* foliage_db_per_m    : ISO 9613-2 foliage attenuation, same linear-in-octaves fit as the core (iso1996_9613_2)
* received_level_db   : the core's SoundField::receivedLevelDb without the wind and terrain terms

The QA script compares these functions with the compiled C++ core (validation/soundfield_probe.cpp)
and with ISO 9613-2 Table 2.
"""
from __future__ import annotations

import math

import numpy as np

from . import common as C


def absorption_db_per_m(f, temperature_c: float, rh01: float, pressure_kpa: float = 101.325):
    """ISO 9613-1:1993 eqs. (3)-(5) and Annex B; identical to SoundField::absorptionDbPerM (computed in double)."""
    f = np.asarray(f, dtype=np.float64)
    T = float(temperature_c) + 273.15
    T0 = 293.15
    T01 = 273.16
    pr = 101.325
    pa = float(pressure_kpa)
    Cexp = -6.8346 * (T01 / T) ** 1.261 + 4.6151
    psat_ratio = 10.0 ** Cexp
    h = min(max(rh01, 0.0), 1.0) * 100.0 * psat_ratio / (pa / pr)
    frO = (pa / pr) * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h))
    frN = (pa / pr) * (T / T0) ** -0.5 * (9.0 + 280.0 * h * math.exp(-4.170 * ((T / T0) ** (-1.0 / 3.0) - 1.0)))
    ff = f * f
    return 8.686 * ff * (1.84e-11 * (pa / pr) ** -1.0 * (T / T0) ** 0.5
                         + (T / T0) ** -2.5 * (0.01275 * math.exp(-2239.1 / T) / (frO + ff / frO)
                                               + 0.1068 * math.exp(-3352.0 / T) / (frN + ff / frN)))


def foliage_db_per_m(f):
    """ISO 9613-2 Table A.1 fit used by the core: 0.02 dB/m at 63 Hz rising to 0.12 dB/m at 8 kHz."""
    f = np.asarray(f, dtype=np.float64)
    octaves = np.log2(np.maximum(63.0, f) / 63.0)
    return 0.02 + 0.1 * np.clip(octaves / 7.0, 0.0, 1.0)


def foliage_meters_from_transmittance(tr: float) -> float:
    """Core conversion of optical transmittance to an equivalent depth of dense foliage (game assumption tau=0.3/m)."""
    return min(200.0, -math.log(max(1e-4, tr)) / 0.3)


def speed_of_sound(temperature_c: float) -> float:
    return 331.3 + 0.606 * temperature_c


def received_level_db(level_db: float, f0_hz: float, distance_m: float, temperature_c: float, rh01: float,
                      pressure_kpa: float = 101.325, foliage_m: float = 0.0) -> float:
    """SPL at the listener: spherical spreading + ISO absorption at f0 + foliage at f0 (core model, no wind/terrain)."""
    r = max(1.0, distance_m)
    lvl = level_db - 20.0 * math.log10(r)
    lvl -= float(absorption_db_per_m(f0_hz, temperature_c, rh01, pressure_kpa)) * r
    if foliage_m > 0.0 and r > 10.0:
        lvl -= foliage_m * float(foliage_db_per_m(f0_hz))
    return lvl


def excess_attenuation_db(f, distance_m: float, temperature_c: float, rh01: float, foliage_m: float = 0.0,
                          pressure_kpa: float = 101.325):
    """Frequency-dependent part of the propagation loss (absorption + foliage), dB (positive = loss)."""
    a = absorption_db_per_m(f, temperature_c, rh01, pressure_kpa) * max(1.0, distance_m)
    if foliage_m > 0.0 and distance_m > 10.0:
        a = a + foliage_m * foliage_db_per_m(f)
    return a


def audible_range_m(level_db: float, f0_hz: float, temperature_c: float, rh01: float, floor_db: float = 25.0,
                    r_max: float = 6000.0) -> float:
    """Distance at which the core propagation model brings the source down to floor_db (bisection)."""
    if received_level_db(level_db, f0_hz, 1.0, temperature_c, rh01) <= floor_db:
        return 1.0
    lo, hi = 1.0, r_max
    if received_level_db(level_db, f0_hz, hi, temperature_c, rh01) > floor_db:
        return r_max
    for _ in range(60):
        mid = math.sqrt(lo * hi)
        if received_level_db(level_db, f0_hz, mid, temperature_c, rh01) > floor_db:
            lo = mid
        else:
            hi = mid
    return lo


def apply_propagation(x: np.ndarray, distance_m: float, temperature_c: float, rh01: float, foliage_m: float = 0.0,
                      ref_hz: float | None = None) -> np.ndarray:
    """Zero-phase spectral shaping of x by the excess attenuation over distance_m.
    With ref_hz the response is normalised to 0 dB at ref_hz (the broadband spreading loss and the loss at
    the call's f0 go into the manifest instead of the file, so UE5 attenuation keeps control of level)."""
    ref = 0.0 if ref_hz is None else float(excess_attenuation_db(ref_hz, distance_m, temperature_c, rh01, foliage_m))

    def gain(f):
        return C.db2lin(-(excess_attenuation_db(f, distance_m, temperature_c, rh01, foliage_m) - ref))

    return C.fft_filter(x, gain)
