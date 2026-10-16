"""World-space procedural fields shared by the Blender renders and the UE bakes.

Every field is a deterministic function of the simulation coordinates (metres): the scatter helper
(Tools/CreatureFactory/scenes/vegetation_library.py), the terrain micro-relief of the renders and the PCG spawn
probability textures baked for Unreal (envlib/pcgbake.py) evaluate the SAME functions, so the renders show the
distribution the game will get.

Noise model: isotropic, band-limited Gaussian noise defined on a periodic grid (period = 64 x the feature scale,
so the repetition is never visible) and sampled bilinearly. Unlike a sum of a few plane waves (the earlier
implementation), it has no preferred direction, hence no parallel stripes ("corduroy") in plant density or in the
terrain shading.
"""
from __future__ import annotations

import math

import numpy as np
from scipy import ndimage

_GRIDS = {}
_GRID_N = 256  # grid cells per period; cell = scale / 4  ->  period = 64 x scale


def _grid(scale_m, seed, band=(1.0, 3.0)):
    """Unit-variance periodic noise grid whose energy lies between wavelengths band[0]*scale and band[1]*scale."""
    key = (round(float(scale_m), 4), int(seed), band)
    g = _GRIDS.get(key)
    if g is not None:
        return g
    n = _GRID_N
    cell = float(scale_m) / 4.0
    rng = np.random.default_rng(int(seed) * 7919 + int(scale_m * 1000) % 100003)
    fy = np.fft.fftfreq(n)[:, None] * n
    fx = np.fft.rfftfreq(n)[None, :] * n
    f = np.hypot(fx, fy)  # cycles per period; wavelength = n * cell / f = 64 scale / f
    f_hi = n / (4.0 * band[0])  # wavelength = band[0] * scale
    f_lo = n / (4.0 * band[1])
    fc = math.sqrt(f_lo * f_hi)
    sig = 0.5 * math.log(f_hi / f_lo) + 1e-6
    amp = np.exp(-0.5 * ((np.log(np.maximum(f, 1e-6)) - math.log(fc)) / sig) ** 2) * (f > 0)
    spec = amp * (rng.normal(size=f.shape) + 1j * rng.normal(size=f.shape))
    a = np.fft.irfft2(spec, s=(n, n))
    a = (a - a.mean()) / max(a.std(), 1e-12)
    _GRIDS[key] = (a.astype(np.float64), cell)
    return _GRIDS[key]


def gauss_noise(x, y, scale_m, seed, band=(1.0, 3.0)):
    """Zero-mean, unit-variance isotropic noise at world coordinates (any array shape)."""
    a, cell = _grid(scale_m, seed, band)
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    out = ndimage.map_coordinates(a, [(y / cell).ravel(), (x / cell).ravel()], order=1, mode="grid-wrap")
    return out.reshape(x.shape)


def cluster_noise(x, y, scale_m, seed, contrast=0.32):
    """Patchiness field in [0, 1] (mean 0.5) for plant clusters of size ~scale_m."""
    return np.clip(0.5 + contrast * gauss_noise(x, y, scale_m, seed), 0.0, 1.0)


def micro_relief(x, y, amp=0.15, min_wavelength=0.0, seed=3):
    """Isotropic, domain-warped fbm (metres, RMS ~ amp / 2) with wavelengths 3-48 m. Octaves shorter than
    min_wavelength are dropped (a mesh of spacing r cannot carry wavelengths below ~4 r without aliasing)."""
    x = np.asarray(x, np.float64)
    y = np.asarray(y, np.float64)
    wx = x + 5.0 * gauss_noise(x, y, 30.0, seed + 11)
    wy = y + 5.0 * gauss_noise(x, y, 30.0, seed + 12)
    h = np.zeros_like(x)
    norm = 0.0
    for k, lam in enumerate((3.0, 6.0, 12.0, 24.0, 48.0)):
        w = lam ** 0.8  # amplitude ~ wavelength^0.8: slopes decrease towards the short wavelengths
        norm += w * w
        if lam < min_wavelength:
            continue
        h += w * gauss_noise(wx, wy, lam, seed + 31 * k, band=(0.8, 1.6))
    return amp * 0.5 * h / math.sqrt(norm)

