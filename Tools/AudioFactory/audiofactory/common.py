"""Shared constants, deterministic random streams and DSP helpers for the Noctis AudioFactory.

Everything here is plain numpy/scipy so that the whole library can be regenerated bit-for-bit
from the repository data (same numpy/scipy versions -> same samples).
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48000                      # output sample rate (Hz)
SUBTYPE = "PCM_24"              # 24-bit integer PCM
NYQ_SAFE = 0.45 * SR            # highest partial we let through (same margin as the C++ SignalSynth)
REPO = Path(__file__).resolve().parents[3]
TOOL_DIR = REPO / "Tools" / "AudioFactory"
BUILD_DIR = REPO / "Build" / "Audio"
MANIFEST_PATH = REPO / "Data" / "Audio" / "manifest.json"
PREVIEW_DIR = REPO / "Output" / "audio"
GENERATOR_VERSION = "1.0.0"
MASTER_SEED = 20261015          # changing it changes every random variation of the library

C_AIR_REF = 343.0               # m/s, only used where temperature does not matter
RHO_WATER = 998.0               # kg/m^3
P_ATM = 101325.0                # Pa
GAMMA_AIR = 1.4
G = 9.81


# --------------------------------------------------------------------------------------------
# Deterministic randomness
# --------------------------------------------------------------------------------------------
def stable_seed(*parts) -> int:
    """64-bit seed derived from a key (independent of Python's hash randomisation)."""
    key = "|".join(str(p) for p in parts) + f"|{MASTER_SEED}"
    return int.from_bytes(hashlib.sha256(key.encode("utf-8")).digest()[:8], "little")


def rng(*parts) -> np.random.Generator:
    return np.random.default_rng(stable_seed(*parts))


# --------------------------------------------------------------------------------------------
# Basic helpers
# --------------------------------------------------------------------------------------------
def ns(duration_s: float) -> int:
    return max(1, int(round(duration_s * SR)))


def tvec(n: int) -> np.ndarray:
    return np.arange(n, dtype=np.float64) / SR


def db2lin(db):
    return np.power(10.0, np.asarray(db, dtype=np.float64) / 20.0)


def lin2db(x, floor: float = 1e-12):
    return 20.0 * np.log10(np.maximum(np.abs(x), floor))


def rms(x) -> float:
    x = np.asarray(x, dtype=np.float64)
    return float(np.sqrt(np.mean(x * x))) if x.size else 0.0


def place(dst: np.ndarray, src: np.ndarray, start: int, gain: float = 1.0) -> None:
    """Add src into dst at sample offset start (clipped to dst bounds)."""
    if start >= dst.shape[0] or start + src.shape[0] <= 0:
        return
    a = max(0, start)
    b = min(dst.shape[0], start + src.shape[0])
    dst[a:b] += gain * src[a - start:b - start]


# --------------------------------------------------------------------------------------------
# Noise
# --------------------------------------------------------------------------------------------
def white(n: int, r: np.random.Generator) -> np.ndarray:
    return r.standard_normal(n)


def colored(n: int, r: np.random.Generator, exponent: float = 1.0) -> np.ndarray:
    """Gaussian noise with power spectral density ~ f^-exponent (1 = pink, 2 = brown), unit std."""
    spec = np.fft.rfft(r.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    f[0] = f[1] if n > 1 else 1.0
    spec *= f ** (-exponent / 2.0)
    y = np.fft.irfft(spec, n)
    return y / (np.std(y) + 1e-12)


def smooth_noise(n: int, r: np.random.Generator, cutoff_hz: float) -> np.ndarray:
    """Slowly varying zero-mean unit-std control signal (for jitter, gusts, shimmer)."""
    n_ctl = max(8, int(math.ceil(n * cutoff_hz * 4.0 / SR)) + 4)
    ctl = r.standard_normal(n_ctl)
    ctl = np.convolve(ctl, np.hanning(5) / np.hanning(5).sum(), mode="same")
    x = np.interp(np.linspace(0, n_ctl - 1, n), np.arange(n_ctl), ctl)
    return (x - x.mean()) / (x.std() + 1e-12)


# --------------------------------------------------------------------------------------------
# Filters
# --------------------------------------------------------------------------------------------
def _clamp_hz(f: float) -> float:
    return float(min(max(f, 1.0), NYQ_SAFE))


def lowpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    sos = signal.butter(order, _clamp_hz(fc), "lowpass", fs=SR, output="sos")
    return signal.sosfilt(sos, x, axis=0)


def highpass(x: np.ndarray, fc: float, order: int = 2) -> np.ndarray:
    sos = signal.butter(order, _clamp_hz(fc), "highpass", fs=SR, output="sos")
    return signal.sosfilt(sos, x, axis=0)


def bandpass(x: np.ndarray, lo: float, hi: float, order: int = 2) -> np.ndarray:
    lo = _clamp_hz(lo)
    hi = _clamp_hz(hi)
    if hi <= lo * 1.05:
        hi = min(NYQ_SAFE, lo * 1.1)
    sos = signal.butter(order, [lo, hi], "bandpass", fs=SR, output="sos")
    return signal.sosfilt(sos, x, axis=0)


def resonator(x: np.ndarray, f0: float, q: float) -> np.ndarray:
    """RBJ constant-0-dB-peak band-pass biquad (a single damped mode driven by x)."""
    f0 = _clamp_hz(f0)
    w0 = 2.0 * math.pi * f0 / SR
    alpha = math.sin(w0) / (2.0 * max(q, 0.05))
    b = np.array([alpha, 0.0, -alpha])
    a = np.array([1.0 + alpha, -2.0 * math.cos(w0), 1.0 - alpha])
    return signal.lfilter(b / a[0], a / a[0], x, axis=0)


def resonator_bank(x: np.ndarray, freqs, qs, gains) -> np.ndarray:
    y = np.zeros_like(x, dtype=np.float64)
    for f, q, g in zip(freqs, qs, gains):
        if f < NYQ_SAFE:
            y += g * resonator(x, f, q)
    return y


def one_pole_lowpass(x: np.ndarray, fc: float) -> np.ndarray:
    a = math.exp(-2.0 * math.pi * _clamp_hz(fc) / SR)
    return signal.lfilter([1.0 - a], [1.0, -a], x, axis=0)


def fft_filter(x: np.ndarray, gain_fn) -> np.ndarray:
    """Zero-phase filtering by an arbitrary magnitude response gain_fn(freqs_hz) (linear)."""
    n = x.shape[0]
    nfft = int(2 ** math.ceil(math.log2(max(16, 2 * n))))   # 2x padding: no circular wrap of the response
    spec = np.fft.rfft(x, nfft, axis=0)
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    g = gain_fn(f)
    if spec.ndim == 2:
        g = g[:, None]
    return np.fft.irfft(spec * g, nfft, axis=0)[:n]


# --------------------------------------------------------------------------------------------
# Envelopes and elementary sources
# --------------------------------------------------------------------------------------------
def ramp_env(n: int, attack: int, release: int) -> np.ndarray:
    """Raised-cosine attack/release envelope over n samples."""
    e = np.ones(n)
    attack = max(1, min(attack, n))
    release = max(1, min(release, n))
    e[:attack] = 0.5 - 0.5 * np.cos(np.linspace(0.0, math.pi, attack))
    e[n - release:] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, math.pi, release))
    return e


def exp_decay(n: int, tau_s: float) -> np.ndarray:
    return np.exp(-tvec(n) / max(tau_s, 1e-5))


def damped_sine(freq: float, tau_s: float, dur_s: float | None = None, phase: float = 0.0) -> np.ndarray:
    dur = dur_s if dur_s is not None else min(6.0 * tau_s, 4.0)
    t = tvec(ns(dur))
    return np.sin(2.0 * math.pi * freq * t + phase) * np.exp(-t / max(tau_s, 1e-5))


def half_sine_pulse(dur_s: float) -> np.ndarray:
    n = max(3, ns(dur_s))
    return np.sin(np.linspace(0.0, math.pi, n))


def impulses(n: int, times_s, amps) -> np.ndarray:
    """Sparse impulse train (fractional placement by linear split)."""
    x = np.zeros(n)
    times = np.asarray(times_s, dtype=np.float64) * SR
    amps = np.asarray(amps, dtype=np.float64)
    i0 = np.floor(times).astype(np.int64)
    frac = times - i0
    ok = (i0 >= 0) & (i0 + 1 < n)
    np.add.at(x, i0[ok], amps[ok] * (1.0 - frac[ok]))
    np.add.at(x, i0[ok] + 1, amps[ok] * frac[ok])
    return x


def poisson_times(r: np.random.Generator, rate_fn, t0: float, t1: float, rate_max: float) -> np.ndarray:
    """Inhomogeneous Poisson process by thinning; rate_fn(t) in events/s."""
    if t1 <= t0 or rate_max <= 0:
        return np.zeros(0)
    count = r.poisson(rate_max * (t1 - t0))
    t = np.sort(r.uniform(t0, t1, count))
    keep = r.uniform(0.0, rate_max, count) < rate_fn(t)
    return t[keep]


def minnaert_hz(radius_m: np.ndarray | float, depth_m: float = 0.0) -> np.ndarray:
    """Natural frequency of a gas bubble in water (Minnaert 1933): f = sqrt(3 gamma p0 / rho) / (2 pi R)."""
    p0 = P_ATM + RHO_WATER * G * depth_m
    return np.sqrt(3.0 * GAMMA_AIR * p0 / RHO_WATER) / (2.0 * math.pi * np.asarray(radius_m))


def bubble(radius_m: float, amp: float, r: np.random.Generator, rising: bool = True) -> np.ndarray:
    """Damped bubble 'plink' at the Minnaert frequency with the small upward chirp of a rising bubble.
    Total damping constant delta ~0.02-0.05 (radiation + thermal + viscous; order of magnitude for
    millimetric bubbles, game assumption)."""
    f = float(minnaert_hz(radius_m))
    delta = 0.02 + 0.006 * math.sqrt(max(f, 1.0) / 1000.0)     # total damping (dimensionless)
    tau = 1.0 / (math.pi * f * delta)
    dur = min(6.0 * tau, 0.6)
    t = tvec(ns(dur))
    chirp = 1.0 + (0.1 * (1.0 - np.exp(-t / (2.0 * tau))) if rising else 0.0)
    ph = 2.0 * math.pi * np.cumsum(f * chirp) / SR + r.uniform(0, 2 * math.pi)
    return amp * np.sin(ph) * np.exp(-t / tau) * (1.0 - np.exp(-t / 0.0004))


# --------------------------------------------------------------------------------------------
# Levels, loops, file output
# --------------------------------------------------------------------------------------------
def normalize_peak(x: np.ndarray, target_dbfs: float = -1.0) -> tuple[np.ndarray, float]:
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    if peak <= 0.0:
        return x, 0.0
    gain = float(db2lin(target_dbfs)) / peak
    return x * gain, 20.0 * math.log10(gain)


def normalize_rms(x: np.ndarray, target_dbfs: float, peak_ceiling_dbfs: float = -1.0) -> tuple[np.ndarray, float]:
    r = rms(x)
    if r <= 0.0:
        return x, 0.0
    gain = float(db2lin(target_dbfs)) / r
    peak = float(np.max(np.abs(x))) * gain
    ceiling = float(db2lin(peak_ceiling_dbfs))
    if peak > ceiling:
        gain *= ceiling / peak
    return x * gain, 20.0 * math.log10(gain)


def seamless_loop(x: np.ndarray, loop_n: int, xfade_n: int) -> np.ndarray:
    """x must hold loop_n + xfade_n samples. The overhang is equal-power cross-faded into the head so
    the last sample of the loop continues into the first one without a seam."""
    assert x.shape[0] >= loop_n + xfade_n
    y = np.array(x[:loop_n], dtype=np.float64, copy=True)
    k = np.linspace(0.0, 1.0, xfade_n, endpoint=False)
    g_in = np.sin(0.5 * math.pi * k)
    g_out = np.cos(0.5 * math.pi * k)
    if y.ndim == 2:
        g_in = g_in[:, None]
        g_out = g_out[:, None]
    y[:xfade_n] = x[:xfade_n] * g_in + x[loop_n:loop_n + xfade_n] * g_out
    return y


def fade(x: np.ndarray, fade_in_s: float = 0.002, fade_out_s: float = 0.01) -> np.ndarray:
    n = x.shape[0]
    e = ramp_env(n, ns(fade_in_s), ns(fade_out_s))
    return x * (e[:, None] if x.ndim == 2 else e)


def remove_dc(x: np.ndarray) -> np.ndarray:
    return highpass(x, 8.0, order=2)


def write_wav(path: Path, x: np.ndarray) -> None:
    x = np.asarray(x, dtype=np.float64)
    if not np.all(np.isfinite(x)):
        raise ValueError(f"non-finite samples in {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    full = 1.0 - 2.0 ** -23
    sf.write(str(path), np.clip(x, -full, full), SR, subtype=SUBTYPE)


def rel(path: Path) -> str:
    return path.resolve().relative_to(REPO).as_posix()
