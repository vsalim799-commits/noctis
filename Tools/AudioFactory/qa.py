#!/usr/bin/env python3
"""Noctis AudioFactory — QA of the generated sound library.

    python Tools/AudioFactory/qa.py                  # every sound of Data/Audio/manifest.json
    python Tools/AudioFactory/qa.py --only vocal     # one category (vocal, footstep, foley, ambience, weather, equipment)
    python Tools/AudioFactory/qa.py --no-core        # skip the C++ core comparison

For every manifest entry the WAV file is read and measured (duration, RMS, active RMS, peak, spectral
centroid, spectral peak, band energy ratio, f0 by YIN for tonal calls, loop seam, distance transfer function)
and compared with the manifest (audio block, level block, qa block). The propagation model of the library
(audiofactory/propagation.py) is also checked against ISO 9613-2 Table 2 and against the compiled C++ core
(validation/soundfield_probe.cpp linked with build/libnoctis_core.a).

Writes Output/audio/qa_report.json; exit code 1 when a check fails.
"""
from __future__ import annotations

import argparse
import json
import math
import multiprocessing as mp
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))

from audiofactory import common as C  # noqa: E402
from audiofactory import propagation as P  # noqa: E402

REPORT = C.PREVIEW_DIR / "qa_report.json"
PROBE_SRC = C.TOOL_DIR / "validation" / "soundfield_probe.cpp"
PROBE_BIN = C.REPO / "Build" / "AudioFactory" / "soundfield_probe"
CORE_LIB = C.REPO / "build" / "libnoctis_core.a"

# ISO 9613-2:1996 Table 2 — atmospheric attenuation coefficient (dB/km) at the octave mid-band frequencies.
ISO_TABLE2 = [
    (10.0, 0.70, [0.1, 0.4, 1.0, 1.9, 3.7, 9.7, 32.8, 117.0]),
    (20.0, 0.70, [0.1, 0.3, 1.1, 2.8, 5.0, 9.0, 22.9, 76.6]),
    (30.0, 0.70, [0.1, 0.3, 1.0, 3.1, 7.4, 12.7, 23.1, 59.3]),
    (15.0, 0.20, [0.3, 0.6, 1.2, 2.7, 8.2, 28.2, 88.8, 202.0]),
    (15.0, 0.50, [0.1, 0.5, 1.2, 2.2, 4.2, 10.8, 36.2, 129.0]),
    (15.0, 0.80, [0.1, 0.3, 1.1, 2.4, 4.1, 8.3, 23.7, 82.8]),
]
ISO_MIDBAND = [1000.0 * 10.0 ** (0.3 * k) for k in range(-4, 4)]     # exact mid-band frequencies 63.1 Hz .. 7943 Hz


# --------------------------------------------------------------------------------------------
# Measurements
# --------------------------------------------------------------------------------------------
def db(x, floor=1e-12):
    return float(20.0 * math.log10(max(float(x), floor)))


def envelope_db(mono, frame_s=0.005):
    w = max(1, C.ns(frame_s))
    nfr = max(1, mono.size // w)
    fr = mono[: nfr * w].reshape(nfr, w) if mono.size >= w else mono.reshape(1, -1)
    e = np.sqrt(np.mean(fr * fr, axis=1))
    return 20.0 * np.log10(np.maximum(e, 1e-12)), w


def active_rms_dbfs(mono):
    """Same definition as generate.py: RMS over the 50 ms frames within 20 dB of the loudest frame."""
    e, _ = envelope_db(mono, 0.05)
    lin = 10.0 ** (e / 20.0)
    act = e >= e.max() - 20.0
    return db(math.sqrt(float(np.mean(lin[act] ** 2))))


def active_duration(mono, below_db=20.0):
    e, w = envelope_db(mono, 0.005)
    idx = np.nonzero(e >= e.max() - below_db)[0]
    return float((idx[-1] - idx[0] + 1) * w / C.SR) if idx.size else 0.0


def psd(mono, res_hz=None):
    if res_hz is None:
        res_hz = 6.0
    nper = int(2 ** math.ceil(math.log2(max(256.0, C.SR / max(res_hz, 0.5)))))
    nper = min(nper, 2 ** int(math.floor(math.log2(max(256, mono.size)))))
    f, p = signal.welch(mono, fs=C.SR, nperseg=nper, noverlap=nper // 2, window="hann", detrend=False)
    return f, p


def centroid(f, p):
    sel = (f >= 20.0) & (f <= 20000.0)
    return float(np.sum(f[sel] * p[sel]) / max(np.sum(p[sel]), 1e-30))


def band_ratio(f, p, lo, hi):
    tot = (f >= 20.0) & (f <= 20000.0)
    sel = (f >= lo) & (f <= hi)
    return float(np.sum(p[sel]) / max(np.sum(p[tot]), 1e-30))


def spectral_peak(f, p, lo, hi):
    sel = np.nonzero((f >= lo) & (f <= hi))[0]
    if sel.size == 0:
        return float("nan")
    i = sel[np.argmax(p[sel])]
    if 0 < i < f.size - 1:
        a, b, c = np.log(p[i - 1] + 1e-30), np.log(p[i] + 1e-30), np.log(p[i + 1] + 1e-30)
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        return float(f[i] + d * (f[1] - f[0]))
    return float(f[i])


def yin_f0(mono, f_lo, f_hi, threshold=0.15):
    """Median YIN estimate over the active frames of a call. Returns (f0, voiced_fraction)."""
    sr = C.SR
    dec = int(max(1, min(12, sr // max(4000.0, 16.0 * f_hi))))
    x = signal.decimate(mono, dec, ftype="fir", zero_phase=True) if dec > 1 else mono
    sr = sr / dec
    tau_min = max(2, int(sr / f_hi))
    tau_max = int(math.ceil(sr / f_lo))
    W = int(max(2.5 * tau_max, 0.03 * sr))
    e, w = envelope_db(x, 0.01 * dec)
    # envelope_db uses C.SR frames; recompute on the decimated grid
    wf = max(1, int(0.01 * sr))
    nfr = max(1, x.size // wf)
    ee = np.sqrt(np.mean(x[: nfr * wf].reshape(nfr, wf) ** 2, axis=1))
    act = np.nonzero(20 * np.log10(ee + 1e-12) >= 20 * np.log10(ee.max() + 1e-12) - 12.0)[0]
    if act.size == 0:
        return float("nan"), 0.0
    a0, a1 = act[0] * wf, (act[-1] + 1) * wf
    if a1 - a0 < W + tau_max:
        W = max(4 * tau_min, (a1 - a0) - tau_max)
    if W <= 2 * tau_min or a0 + W + tau_max > x.size:
        W = max(2 * tau_min + 1, x.size - a0 - tau_max - 1)
        if W <= 2 * tau_min:
            return float("nan"), 0.0
    starts = np.linspace(a0, max(a0, a1 - W - tau_max), num=min(25, max(1, (a1 - a0) // max(1, W // 2))), dtype=int)
    f0s, voiced = [], 0
    for s in starts:
        seg = x[s:s + W + tau_max + 1]
        if seg.size < W + tau_max + 1:
            continue
        ref = seg[:W]
        nfft = int(2 ** math.ceil(math.log2(seg.size + W)))
        corr = np.fft.irfft(np.fft.rfft(seg, nfft) * np.conj(np.fft.rfft(ref, nfft)), nfft)[: tau_max + 1]
        c2 = np.concatenate([[0.0], np.cumsum(seg * seg)])
        e0 = c2[W] - c2[0]
        taus = np.arange(tau_max + 1)
        et = c2[taus + W] - c2[taus]
        d = e0 + et - 2.0 * corr
        d[0] = 0.0
        cm = np.cumsum(d[1:])
        dn = np.ones_like(d)
        dn[1:] = d[1:] * np.arange(1, tau_max + 1) / np.maximum(cm, 1e-30)
        cand = np.nonzero(dn[tau_min:] < threshold)[0]
        if cand.size:
            t = tau_min + cand[0]
            while t + 1 <= tau_max and dn[t + 1] < dn[t]:
                t += 1
        else:
            t = tau_min + int(np.argmin(dn[tau_min:]))
            if dn[t] > 0.45:
                continue
        voiced += 1
        if 0 < t < tau_max:
            a, b, c = dn[t - 1], dn[t], dn[t + 1]
            den = a - 2 * b + c
            t = t + (0.5 * (a - c) / den if den != 0 else 0.0)
        f0s.append(sr / t)
    if not f0s:
        return float("nan"), 0.0
    return float(np.median(f0s)), voiced / max(1, len(starts))


def seam_check(x):
    """Loop seam: the step from the last to the first sample and the high-frequency energy around the junction
    must look like any other point of the file."""
    rng = np.random.default_rng(1)
    chans = x if x.ndim == 2 else x[:, None]
    out = {"ok": True}
    worst = 0.0
    for ch in range(chans.shape[1]):
        y = chans[:, ch]
        d = np.abs(np.diff(y))
        p999 = float(np.percentile(d, 99.9))
        step = float(abs(y[0] - y[-1]))
        j = np.concatenate([y[-240:], y[:240]])

        def hf(seg):
            return float(np.sqrt(np.mean(np.diff(seg, 2) ** 2)))

        m_seam = hf(j)
        pos = rng.integers(240, y.size - 240, 400)
        ref = np.array([hf(y[p - 240:p + 240]) for p in pos])
        thr = float(np.percentile(ref, 99.5)) * 1.25 + 1e-9
        ok = step <= max(p999, 1e-4) and m_seam <= thr
        worst = max(worst, m_seam / thr)
        out[f"ch{ch}"] = {"step": round(step, 6), "diff_p999": round(p999, 6), "hf_seam": round(m_seam, 6), "hf_threshold": round(thr, 6)}
        out["ok"] = out["ok"] and ok
    out["hf_ratio_worst"] = round(worst, 3)
    return out


def tail_level_db(mono):
    """Level of the 20-50 ms before the end (just before the 20 ms end fade) re the loudest 5 ms frame."""
    e, w = envelope_db(mono, 0.005)
    a, b = mono.size - C.ns(0.05), mono.size - C.ns(0.02)
    if a <= 0:
        return -200.0
    return db(math.sqrt(float(np.mean(mono[a:b] ** 2)))) - float(e.max())


def ultrasonic_fraction_db(mono):
    """Energy above 20 kHz re the total (dB)."""
    spec = np.abs(np.fft.rfft(mono)) ** 2
    f = np.fft.rfftfreq(mono.size, 1.0 / C.SR)
    tot = float(spec[1:].sum())
    return 10.0 * math.log10(max(float(spec[f > 20000.0].sum()), 1e-30) / max(tot, 1e-30))


def hf_frame_energy(mono):
    hf = C.bandpass(mono, 2000.0, 12000.0, order=4)
    w = C.ns(0.001)
    nfr = hf.size // w
    return np.mean(hf[: nfr * w].reshape(nfr, w) ** 2, axis=1)


def ensemble_gate_db(monos, peak_window_db=30.0, smooth_ms=3):
    """Hard-gate detector for dense granular textures, on the mean 2-12 kHz energy envelope of the variations of a
    group (1 ms frames, 3 ms smoothing; averaging removes the random gaps of single renders). drop(k) = median level of
    the 5 ms before k minus the maximum level of the 5 ms starting 2 ms after k, over the k whose preceding median is
    within 30 dB of the peak. A texture cut by a gate falls and stays low; a smooth relaxation, or a loud grain falling
    back onto the ongoing texture, gives a few dB. (A truncated texture of 1.1 gave 13-28 dB.)"""
    from numpy.lib.stride_tricks import sliding_window_view
    envs = [hf_frame_energy(m) for m in monos]
    L = max(e.size for e in envs)
    E = np.zeros(L)
    for e in envs:
        E[: e.size] += e
    E = np.convolve(E / len(envs), np.ones(smooth_ms) / smooth_ms, mode="same")
    lv = 10.0 * np.log10(E + 1e-30)
    if lv.size < 14:
        return 0.0
    k = np.arange(5, lv.size - 7)
    before = np.median(sliding_window_view(lv, 5)[k - 5], axis=1)
    after = sliding_window_view(lv, 5)[k + 2].max(axis=1)
    cand = before >= lv.max() - peak_window_db
    return float((before - after)[cand].max()) if np.any(cand) else 0.0


TAIL_MAX_DB = -40.0          # pre-fade level of non-transition one-shots re their maximum
ULTRASONIC_MAX_DB = -30.0    # energy above 20 kHz re total
DC_MAX_DBFS = -80.0
GATE_MAX_DB = 12.0
LIMITER_MAX_GR_DB = 6.0
LIMITER_MAX_FRACTION = 0.01


def transfer_check(base, var, q):
    """Distance variant = base x |G(f)| (ISO 9613-1 absorption + foliage), normalised at ref_hz.
    Compare band-power ratios variant/base at each check frequency with the prediction."""
    fb, pb = psd(base, 3.0)
    fv, pv = psd(var, 3.0)
    T, RH, dist, fol, ref = q["temperature_c"], q["relative_humidity"], q["distance_m"], q.get("foliage_m", 0.0), q["ref_hz"]

    def g2(f):
        return 10.0 ** (-(P.excess_attenuation_db(f, dist, T, RH, fol) - P.excess_attenuation_db(ref, dist, T, RH, fol)) / 10.0)

    def band(fc):
        half = max(0.03 * fc, 4.0 * (fb[1] - fb[0])) if q.get("tonal", True) else fc * (2 ** (1 / 6) - 1)
        return (fb >= fc - half) & (fb <= fc + half)

    out = []
    sel_r = band(ref)
    for fc in q["check_hz"]:
        sel = band(fc)
        if not np.any(sel) or not np.any(sel_r):
            continue
        if np.sum(pb[sel]) < 1e-9 * np.max(pb):          # nothing to measure in the base file there
            continue
        meas = 10 * math.log10((np.sum(pv[sel]) / np.sum(pv[sel_r])) / (np.sum(pb[sel]) / np.sum(pb[sel_r])))
        pred = 10 * math.log10((np.sum(pb[sel] * g2(fb[sel])) / np.sum(pb[sel_r] * g2(fb[sel_r]))) / (np.sum(pb[sel]) / np.sum(pb[sel_r])))
        tol = 1.0 + 0.1 * abs(pred)
        e = {"hz": fc, "measured_db": round(meas, 2), "predicted_db": round(pred, 2), "ok": abs(meas - pred) <= tol}
        if pred < -90.0:
            # Below the dynamic range of a 24-bit file normalised at the reference band: only require a large loss.
            e.update(ok=meas <= -80.0, note="sous le plancher de mesure (24 bits)")
        out.append(e)
    return out


def load(path):
    x, sr = sf.read(str(path), dtype="float64", always_2d=False)
    info = sf.info(str(path))
    return x, sr, info


def measure(rec):
    path = C.REPO / rec["file"]
    res = {"id": rec["id"], "category": rec["category"], "checks": [], "metrics": {}}

    def check(name, ok, detail=None):
        res["checks"].append({"name": name, "ok": bool(ok), **({"detail": detail} if detail is not None else {})})

    if not path.exists():
        check("file_exists", False, rec["file"])
        return res
    x, sr, info = load(path)
    ch = 1 if x.ndim == 1 else x.shape[1]
    mono = x if x.ndim == 1 else x.mean(axis=1)
    q = rec.get("qa", {})
    a = rec["audio"]
    m = res["metrics"]
    m["duration_s"] = round(x.shape[0] / sr, 6)
    m["channels"] = ch
    m["peak_dbfs"] = round(db(np.max(np.abs(x))), 3)
    m["rms_dbfs"] = round(db(C.rms(x)), 3)
    m["active_rms_dbfs"] = round(active_rms_dbfs(mono), 3)
    f, p = psd(mono, 2.0 if rec["category"] in ("weather", "ambience", "equipment") else 6.0)
    m["spectral_centroid_hz"] = round(centroid(f, p), 1)
    check("format", sr == C.SR and info.subtype == "PCM_24" and ch == a["channels"] and x.shape[0] == a["frames"],
          f"{sr} Hz {info.subtype} {ch} ch {x.shape[0]} frames")
    check("duration_matches_manifest", abs(m["duration_s"] - q.get("duration_s", a["duration_s"])) <= 1.0 / sr)
    check("finite_nonsilent", np.all(np.isfinite(x)) and m["peak_dbfs"] > -60.0, m["peak_dbfs"])
    if "peak_dbfs_max" in q:
        check("peak", m["peak_dbfs"] <= q["peak_dbfs_max"] + 1e-3, f"{m['peak_dbfs']} <= {q['peak_dbfs_max']}")
    lv = rec.get("level", {})
    if "active_rms_dbfs" in lv:
        check("level_manifest_consistent", abs(m["active_rms_dbfs"] - lv["active_rms_dbfs"]) <= 0.05,
              f"{m['active_rms_dbfs']} vs {lv['active_rms_dbfs']}")
    if "rms_dbfs" in q:
        tol = q.get("rms_tolerance_db", 0.5)
        check("rms_target", abs(m["rms_dbfs"] - q["rms_dbfs"]) <= tol, f"{m['rms_dbfs']} vs {q['rms_dbfs']} ± {tol}")
    if "active_duration_s" in q:
        m["active_duration_s"] = round(active_duration(mono), 4)
        lo, hi = q["active_duration_s"]
        check("active_duration", lo <= m["active_duration_s"] <= hi, f"{m['active_duration_s']} in [{lo}, {hi}]")
    if "f0_hz" in q:
        f0e = q["f0_hz"]
        am = (rec.get("parameters") or {}).get("amplitude_modulation_hz")
        if am and am >= 0.25 * f0e:
            # Strong amplitude modulation close to f0 (threat rumbles: 12 Hz AM on a ~20 Hz carrier) creates sidebands
            # that make the waveform periodicity ambiguous (YIN reports ~f0/2): measure the carrier as the spectral peak.
            fp, pp = psd(mono, max(0.25, 0.02 * f0e))
            f0, vf = spectral_peak(fp, pp, 0.45 * f0e, 2.4 * f0e), 1.0
            m["f0_method"] = "spectral_peak (porteuse modulée en amplitude)"
        else:
            f0, vf = yin_f0(mono, 0.45 * f0e, min(2.4 * f0e, 0.45 * C.SR / 2))
            m["f0_method"] = "yin"
        m["f0_hz"] = None if math.isnan(f0) else round(f0, 3)
        m["voiced_fraction"] = round(vf, 3)
        tol = q.get("f0_tolerance", 0.1)
        check("f0", (not math.isnan(f0)) and abs(f0 - f0e) <= tol * f0e, f"{m['f0_hz']} vs {f0e} ± {100 * tol:g} %")
    if "centroid_hz" in q:
        lo, hi = q["centroid_hz"]
        check("centroid", lo <= m["spectral_centroid_hz"] <= hi, f"{m['spectral_centroid_hz']} in [{lo}, {hi}]")
    if "spectral_peak_hz" in q:
        lo, hi = q["spectral_peak_hz"]
        slo, shi = q.get("spectral_peak_search_hz", [20.0, 20000.0])
        fp, pp = psd(mono, max(0.5, 0.02 * lo))
        pk = spectral_peak(fp, pp, slo, shi)
        m["spectral_peak_hz"] = round(pk, 2)
        check("spectral_peak", lo <= pk <= hi, f"{m['spectral_peak_hz']} in [{lo}, {hi}] (search {slo}-{shi})")
    if "low_band_centroid_hz" in q:
        b = q["low_band_centroid_hz"]
        lo, hi = b["band_hz"]
        fl, pl = psd(mono, max(0.5, 0.03 * hi))
        sel = (fl >= lo) & (fl <= hi)
        c = float(np.sum(fl[sel] * pl[sel]) / max(np.sum(pl[sel]), 1e-30))
        m["low_band_centroid_hz"] = round(c, 2)
        rlo, rhi = b["range_hz"]
        check("low_band_centroid", rlo <= c <= rhi, f"{m['low_band_centroid_hz']} in [{rlo}, {rhi}] (bande {lo}-{hi} Hz)")
    if "band_energy_ratio" in q:
        b = q["band_energy_ratio"]
        br = band_ratio(f, p, b["lo_hz"], b["hi_hz"])
        m["band_energy_ratio"] = round(br, 4)
        check("band_energy_ratio", br >= b["min"], f"{b['lo_hz']}-{b['hi_hz']} Hz: {br:.3f} >= {b['min']}")
    if q.get("loop_seam") or a.get("loop"):
        sc = seam_check(x)
        m["loop_seam"] = sc
        check("loop_seam", sc["ok"], f"hf ratio {sc['hf_ratio_worst']}")
        check("loop_flags", bool(rec["ue5"].get("looping")) and bool(a.get("loop")))
    if "transfer" in q:
        t = q["transfer"]
        base = SOUNDS.get(t["base_id"])
        if base is None:
            check("transfer", False, f"base {t['base_id']} missing")
        else:
            xb, _, _ = load(C.REPO / base["file"])
            mb = xb if xb.ndim == 1 else xb.mean(axis=1)
            tonal = rec.get("parameters", {}).get("production_model") in (None, "closed", "open")
            tr = transfer_check(mb, mono, dict(t, tonal=tonal))
            m["transfer"] = tr
            check("transfer", len(tr) > 0 and all(e["ok"] for e in tr),
                  "; ".join(f"{e['hz']:.0f} Hz {e['measured_db']}/{e['predicted_db']} dB" for e in tr) or "no measurable band")
    # Signal hygiene: DC, ultrasonic energy, truncated tails.
    dc = max(db(abs(float(np.mean(x[:, c] if x.ndim == 2 else x)))) for c in range(ch))
    m["dc_dbfs"] = round(dc, 1)
    check("dc_offset", dc < DC_MAX_DBFS, f"{m['dc_dbfs']} dBFS < {DC_MAX_DBFS}")
    us = ultrasonic_fraction_db(mono)
    m["energy_above_20khz_db"] = round(us, 1)
    if rec["category"] != "vocal":
        # (Vocal hypotheses are exempt: the harmonics above 20 kHz of high-pitched small-mammal calls, e.g. a ~10.5 kHz
        # distress call, belong to the call model analysed by the in-game recorder; the 1.1 vocal library is unchanged.)
        check("ultrasonic_energy", us < ULTRASONIC_MAX_DB, f"{m['energy_above_20khz_db']} dB < {ULTRASONIC_MAX_DB}")
    if not a.get("loop") and not q.get("transition"):
        tl = tail_level_db(mono)
        m["pre_fade_tail_db"] = round(tl, 1)
        check("tail_not_truncated", tl < TAIL_MAX_DB, f"{m['pre_fade_tail_db']} dB re max < {TAIL_MAX_DB} (20-50 ms avant la fin)")
    if "rms_dbfs" in lv:
        check("rms_manifest_consistent", abs(m["rms_dbfs"] - lv["rms_dbfs"]) <= 0.05, f"{m['rms_dbfs']} vs {lv['rms_dbfs']}")
    # Mastering limiter of the loops: only true outliers may be touched.
    params = rec.get("parameters") or {}
    lim = params.get("mastering_limiter") or (params.get("synthesis") or {}).get("mastering_limiter")
    if lim is not None:
        gr, fr = float(lim.get("max_gain_reduction_db", 0.0)), float(lim.get("fraction_limited", 0.0))
        m["limiter"] = {"max_gain_reduction_db": gr, "fraction_limited": fr}
        check("limiter_transparent", gr < LIMITER_MAX_GR_DB and fr < LIMITER_MAX_FRACTION,
              f"réduction max {gr} dB < {LIMITER_MAX_GR_DB}, fraction {fr} < {LIMITER_MAX_FRACTION}")
    if "thunder" in q:
        t = q["thunder"]
        check("thunder_level_geometry_vs_core", abs(t["geometry_db"] - t["core_db"]) <= t["max_difference_db"],
              f"{t['geometry_db']} vs {t['core_db']} dB ± {t['max_difference_db']}")
    # Metadata checks.
    check("confidence_label", rec.get("confidence") in ("established", "strongly_supported", "inferred", "speculative", "unknown"))
    check("model_confidence_label", rec.get("model_confidence") in ("established", "strongly_supported", "inferred", "speculative", "unknown"))
    cap = rec.get("caption") or {}
    check("caption_fr_en", bool(cap.get("fr")) and bool(cap.get("en")), cap or "missing")
    if rec.get("hypothesis"):
        check("caption_says_hypothesis", "hypoth" in (cap.get("fr") or "").lower() and "hypoth" in (cap.get("en") or "").lower(),
              cap.get("fr"))
        check("caption_no_double_hypothesis", (cap.get("fr") or "").lower().count("(hypoth") <= 1
              and (cap.get("en") or "").lower().count("(hypoth") <= 1, cap.get("fr"))
    if "distance_variant_m" in (rec.get("ue5") or {}):
        check("distance_variant_calibrated_at_distance", (rec.get("level") or {}).get("received_spl_db") is not None,
              (rec.get("level") or {}).get("received_spl_db"))
    check("sources_known", all(s in SOURCES for s in rec.get("sources", [])), [s for s in rec.get("sources", []) if s not in SOURCES])
    if rec["category"] == "vocal":
        check("vocal_is_hypothesis", rec.get("hypothesis") is True)
        # The UI label must not name the call by a function (data ids such as 'threat_rumble', 'alarm_bellow').
        # (The supposed-context clause itself is allowed: a call whose data id is just 'contact' would otherwise fail.)
        ct = (rec.get("parameters") or {}).get("call_type") or ""
        lab = rec.get("label_fr") or ""
        lab_wo_ctx = re.sub(r"contexte supposé : [^)]*", "", lab)
        check("vocal_label_no_function_id", bool(ct) and ct not in lab_wo_ctx and "contexte supposé" in lab, lab)
    ue = rec.get("ue5", {})
    check("ue5_hints", all(k in ue for k in ("sound_class", "attenuation_preset", "looping", "concurrency")))
    if "preview_png" in rec:
        check("preview_png", (C.REPO / rec["preview_png"]).exists())
    return res


SOUNDS: dict = {}
SOURCES: set = set()


def _init(sounds, sources):
    global SOUNDS, SOURCES
    SOUNDS = sounds
    SOURCES = sources


# --------------------------------------------------------------------------------------------
# Propagation validation
# --------------------------------------------------------------------------------------------
def check_iso_table():
    rows = []
    ok = True
    for T, h, ref in ISO_TABLE2:
        calc = P.absorption_db_per_m(np.array(ISO_MIDBAND), T, h) * 1000.0
        err = [round(float(c - r), 3) for c, r in zip(calc, ref)]
        good = all(abs(c - r) <= 0.06 + 0.02 * r for c, r in zip(calc, ref))
        ok = ok and good
        rows.append({"temperature_c": T, "relative_humidity": h, "calc_db_km": [round(float(c), 2) for c in calc], "iso_db_km": ref,
                     "error_db_km": err, "ok": good})
    return ok, rows


def check_core(skip: bool):
    if skip:
        return None, {"skipped": True}
    if not CORE_LIB.exists():
        return None, {"skipped": True, "reason": f"{CORE_LIB} absent (cmake --build build --target noctis_core)"}
    PROBE_BIN.parent.mkdir(parents=True, exist_ok=True)
    if not PROBE_BIN.exists() or PROBE_BIN.stat().st_mtime < max(PROBE_SRC.stat().st_mtime, CORE_LIB.stat().st_mtime):
        cmd = ["g++", "-DNOCTIS_STANDALONE=1", "-I", str(C.REPO / "Source/NoctisCore/Public"), "-O2", "-std=c++20", "-fno-exceptions",
               "-fno-rtti", "-Wall", "-Wextra", "-Wshadow", "-Werror", str(PROBE_SRC), str(CORE_LIB), "-lpthread", "-o", str(PROBE_BIN)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return False, {"compile_error": r.stderr[-2000:]}
    out = subprocess.run([str(PROBE_BIN)], capture_output=True, text=True, check=True).stdout.split("\n")
    worst = {"abs_rel": 0.0, "fol_abs": 0.0, "rcv_db": 0.0}
    n = 0
    for line in out:
        p = line.split()
        if not p:
            continue
        if p[0] == "abs":
            T, h, f, v = map(float, p[1:])
            py = float(P.absorption_db_per_m(f, T, h))
            worst["abs_rel"] = max(worst["abs_rel"], abs(py - v) / max(abs(v), 1e-12))
        elif p[0] == "fol":
            f, v = map(float, p[1:])
            worst["fol_abs"] = max(worst["fol_abs"], abs(float(P.foliage_db_per_m(f)) - v))
        elif p[0] == "rcv":
            d, f, v = map(float, p[1:])
            worst["rcv_db"] = max(worst["rcv_db"], abs(P.received_level_db(100.0, f, d, 12.0, 0.75) - v))
        n += 1
    ok = worst["abs_rel"] < 2e-4 and worst["fol_abs"] < 1e-5 and worst["rcv_db"] < 0.02
    return ok, {"values_compared": n, "worst": {k: float(f"{v:.3g}") for k, v in worst.items()},
                "tolerance": {"abs_rel": 2e-4, "fol_abs": 1e-5, "rcv_db": 0.02}, "note": "C++ en float32, Python en float64"}


# --------------------------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", help="categories to check")
    ap.add_argument("--jobs", type=int, default=max(1, mp.cpu_count()))
    ap.add_argument("--no-core", action="store_true")
    args = ap.parse_args(argv)
    t0 = time.time()
    man = json.loads(C.MANIFEST_PATH.read_text(encoding="utf-8"))
    sounds = {r["id"]: r for r in man["sounds"]}
    from audiofactory import data as D
    ds = D.load()
    recs = [r for r in man["sounds"] if not args.only or r["category"] in args.only]
    with mp.get_context("fork").Pool(args.jobs, initializer=_init, initargs=(sounds, set(ds.sources))) as pool:
        results = pool.map(measure, recs, chunksize=4)
    iso_ok, iso_rows = check_iso_table()
    core_ok, core = check_core(args.no_core)
    # Library-level checks.
    lib = []
    wavs = {p.relative_to(C.REPO).as_posix() for p in C.BUILD_DIR.rglob("*.wav")}
    listed = {r["file"] for r in man["sounds"]}
    lib.append({"name": "no_orphan_wav", "ok": not (wavs - listed), "detail": sorted(wavs - listed)[:20]})
    lib.append({"name": "all_listed_exist", "ok": not (listed - wavs), "detail": sorted(listed - wavs)[:20]})
    lib.append({"name": "iso9613_table2", "ok": iso_ok})
    if core_ok is not None:
        lib.append({"name": "python_port_matches_cpp_core", "ok": core_ok, "detail": core.get("worst", core.get("compile_error"))})
    # One-shots must come in >= 4 variations per random group.
    groups = defaultdict(int)
    for r in man["sounds"]:
        g = r["ue5"].get("random_group")
        if g:
            groups[g] += 1
    few = {g: n for g, n in groups.items() if n < 4}
    lib.append({"name": "variations_per_random_group_ge_4", "ok": not few, "detail": few})
    # Every vocal one-shot voice belongs to a random group (4 voices).
    lone = [r["id"] for r in man["sounds"] if r["category"] == "vocal" and (r.get("variant") or {}).get("voice", "").startswith("voice")
            and "distance_m" not in (r.get("variant") or {}) and not r["ue5"].get("random_group")]
    lib.append({"name": "vocal_voices_in_random_groups", "ok": not lone, "detail": lone[:20]})
    # UE5 presets: importer-compatible attenuation (attenuate=false or every numeric field set).
    att = man["ue5"]["attenuation_presets"]
    bad_att = [n for n, p in att.items()
               if not (p.get("attenuate") is False or (p.get("attenuate") is True and all(
                   p.get(k) is not None for k in ("shape", "distance_algorithm", "inner_radius_cm", "falloff_distance_cm", "db_attenuation_at_max"))))
               or any(v is None for v in p.values())]
    lib.append({"name": "attenuation_presets_importable", "ok": not bad_att, "detail": bad_att})
    # Concurrency: a sound that crossfades with others must not sit in a single-slot group.
    con = man["ue5"]["concurrency_presets"]
    by_id = {r["id"]: r for r in man["sounds"]}
    bad_con = []
    for r in man["sounds"]:
        ue = r["ue5"]
        mates = [by_id[k] for k in ue.get("crossfade_with") or [] if k in by_id]
        if ue.get("crossfade_parameter") or mates:
            if con[ue["concurrency"]]["max_count"] < 2:
                bad_con.append(r["id"])
            bad_con += [f"{r['id']}~{o['id']}" for o in mates if o["ue5"]["concurrency"] == ue["concurrency"]
                        and con[ue["concurrency"]]["max_count"] < 2]
    lib.append({"name": "concurrency_allows_crossfades", "ok": not bad_con, "detail": bad_con[:20]})
    # crossfade_with must name sound ids of the manifest (otherwise the check above, the importer and the director miss them).
    dangling = sorted({f"{r['id']}->{k}" for r in man["sounds"] for k in (r["ue5"].get("crossfade_with") or []) if k not in by_id})
    lib.append({"name": "crossfade_targets_resolve", "ok": not dangling, "detail": dangling[:20]})
    # Aliases (single-layer mixes) point at an existing sound.
    aliases = man["ue5"].get("aliases") or {}
    bad_alias = [a for a, v in aliases.items() if v.get("target") not in by_id]
    lib.append({"name": "aliases_resolve", "ok": not bad_alias, "detail": bad_alias})
    # Thunder within 2 km is louder than the heavy-rain bed (internal consistency of the absolute levels).
    rain_db = max([r["level"].get("ambient_spl_db") or 0.0 for r in man["sounds"] if r["category"] == "weather" and r["subcategory"] == "rain"]
                  or [0.0])
    quiet = [r["id"] for r in man["sounds"] if r["subcategory"] == "thunder" and r["variant"].get("distance_m", 1e9) <= 2000.0
             and r["level"]["received_spl_db"] <= rain_db]
    lib.append({"name": "thunder_2km_louder_than_heavy_rain", "ok": not quiet, "detail": {"heavy_rain_db": rain_db, "too_quiet": quiet}})
    # Dense granular footstep textures fade out instead of stopping as a gate (mean envelope of the variations).
    gate_groups = defaultdict(list)
    for r in man["sounds"]:
        if r.get("qa", {}).get("texture_gate_check") and r["ue5"].get("random_group"):
            gate_groups[r["ue5"]["random_group"]].append(r)
    gates = {}
    for g, rs in gate_groups.items():
        monos = []
        for r in rs:
            xx, _, _ = load(C.REPO / r["file"])
            monos.append(xx if xx.ndim == 1 else xx.mean(axis=1))
        gates[g] = round(ensemble_gate_db(monos), 2)
    bad_gate = {g: v for g, v in gates.items() if v >= GATE_MAX_DB}
    lib.append({"name": "texture_no_hard_gate", "ok": not bad_gate,
                "detail": {"groups": len(gates), "worst_db": max(gates.values()) if gates else None, "failing": bad_gate}})

    fails = [(r["id"], c) for r in results for c in r["checks"] if not c["ok"]]
    by_check = Counter(c["name"] for _, c in fails)
    by_cat = Counter(r["category"] for r in results)
    fail_cat = Counter(r["category"] for r in results if any(not c["ok"] for c in r["checks"]))
    summary = {
        "sounds_checked": len(results), "by_category": dict(by_cat), "sounds_failing": sum(fail_cat.values()),
        "failing_by_category": dict(fail_cat), "failing_checks": dict(by_check),
        "library_checks": {c["name"]: c["ok"] for c in lib}, "elapsed_s": round(time.time() - t0, 1),
    }
    # Category statistics.
    stats = {}
    for cat in sorted(by_cat):
        rs = [r for r in results if r["category"] == cat and r["metrics"]]
        if rs:
            stats[cat] = {
                "count": len(rs), "total_duration_s": round(sum(r["metrics"]["duration_s"] for r in rs), 1),
                "peak_dbfs_max": max(r["metrics"]["peak_dbfs"] for r in rs),
                "active_rms_dbfs_median": round(float(np.median([r["metrics"]["active_rms_dbfs"] for r in rs])), 2),
                "centroid_hz_median": round(float(np.median([r["metrics"]["spectral_centroid_hz"] for r in rs])), 1),
            }
    summary["stats"] = stats
    report = {"summary": summary, "propagation": {"iso9613_2_table2": iso_rows, "cpp_core": core}, "library": lib, "sounds": results}
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=1))
    for sid, c in fails[:40]:
        print(f"  FAIL {sid}: {c['name']} {c.get('detail', '')}")
    if len(fails) > 40:
        print(f"  ... {len(fails) - 40} more (see {C.rel(REPORT)})")
    lib_fail = [c for c in lib if not c["ok"]]
    for c in lib_fail:
        print(f"  LIBRARY FAIL {c['name']}: {c.get('detail')}")
    print(f"[qa] report: {C.rel(REPORT)}")
    return 1 if fails or lib_fail else 0


if __name__ == "__main__":
    sys.exit(main())
