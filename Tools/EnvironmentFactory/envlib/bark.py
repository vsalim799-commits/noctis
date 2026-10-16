"""Tileable bark / wood / stem texture sets (BaseColor, Normal (DirectX), Roughness, Height).

All maps tile exactly (periodic FFT noise, periodic Worley). U runs around the stem, V along it, so
fibrous patterns are elongated along V (rows of the image). tile_m gives the physical size of one tile
used by the mesh generators to set UV density.
"""
from __future__ import annotations

import numpy as np

from . import texgen as tg

SIZE = 1024


def _finish(col, height, rough, strength):
    return dict(basecolor=np.clip(col, 0, 1), height=np.clip(height, 0, 1), roughness=np.clip(rough, 0, 1),
                normal=tg.normal_from_height(height, strength, wrap=True))


def cupressaceae(seed, n=SIZE):
    """Fibrous, stringy bark in long vertical strips (habit of modern Metasequoia / Taxodium bark)."""
    s = (n, n)
    strips = tg.fft_noise(s, seed, beta=2.2, lo=3, hi=90, aniso=(1.0, 14.0))  # long along V
    fibres = tg.fft_noise(s, seed + 1, beta=1.2, lo=40, hi=400, aniso=(1.0, 20.0))
    plates = tg.fft_noise(s, seed + 2, beta=2.0, lo=2, hi=12)
    ridge = 1 - np.abs(strips - 0.5) * 2
    h = 0.55 * tg.smoothstep(0.25, 0.9, ridge) + 0.25 * fibres + 0.2 * plates
    h = (h - h.min()) / (h.max() - h.min())
    furrow = 1 - tg.smoothstep(0.15, 0.55, h)
    red = np.array([0.40, 0.25, 0.17])
    grey = np.array([0.46, 0.41, 0.37])
    dark = np.array([0.15, 0.09, 0.06])
    weather = tg.smoothstep(0.55, 0.9, h)[..., None] * (0.4 + 0.6 * plates[..., None])
    col = red * (1 - weather) + grey * weather
    col = col * (1 - furrow[..., None] * 0.75) + dark * furrow[..., None] * 0.75
    col *= (0.85 + 0.3 * fibres[..., None])
    rough = 0.82 + 0.12 * fibres - 0.1 * furrow
    return _finish(col, h, rough, 10.0)


def angio_smooth(seed, n=SIZE):
    """Smooth grey-brown bark with horizontal lenticels and faint wrinkles."""
    s = (n, n)
    base = tg.fbm(s, seed, octaves=((3, 1.0), (12, 0.5), (48, 0.3)))
    wr = tg.fft_noise(s, seed + 3, beta=1.5, lo=20, hi=200, aniso=(6.0, 1.0))  # horizontal wrinkles
    rng = np.random.default_rng(seed)
    len_ = np.zeros(s)
    for _ in range(420):
        y, x = rng.integers(0, n, 2)
        w = rng.integers(6, 22)
        hh = rng.integers(2, 4)
        ys = (np.arange(y, y + hh) % n)[:, None]
        xs = (np.arange(x, x + w) % n)[None, :]
        len_[ys, xs] = 1.0
    len_ = tg.blur(len_, 1.0)
    h = 0.5 * base + 0.3 * wr + 0.4 * len_
    h = (h - h.min()) / (h.max() - h.min())
    c0 = np.array([0.42, 0.40, 0.35])
    c1 = np.array([0.30, 0.27, 0.22])
    lich = tg.smoothstep(0.62, 0.8, tg.fft_noise(s, seed + 5, beta=2.0, lo=4, hi=40))
    col = c0 * (1 - base[..., None] * 0.4) + c1 * base[..., None] * 0.4
    col = col * (1 - lich[..., None] * 0.3) + np.array([0.48, 0.50, 0.40]) * lich[..., None] * 0.3
    col = col * (1 - 0.35 * len_[..., None]) + np.array([0.52, 0.45, 0.36]) * 0.35 * len_[..., None]
    rough = 0.72 + 0.15 * base
    return _finish(col, h, rough, 5.0)


def angio_rough(seed, n=SIZE):
    """Blocky, deeply fissured bark: vertically elongated plates separated by furrows."""
    s = (n, n)
    cells = tg.worley(s, seed, 140, periodic=True, metric="f2-f1", jitter_aspect=0.3)
    warp = tg.fft_noise(s, seed + 1, beta=2.0, lo=2, hi=30)
    plates = tg.smoothstep(0.5, 7.0, cells + 4 * (warp - 0.5))
    detail = tg.fft_noise(s, seed + 2, beta=1.0, lo=60, hi=500, aniso=(1.0, 4.0))
    h = 0.75 * plates + 0.25 * detail
    h = (h - h.min()) / (h.max() - h.min())
    furrow = 1 - plates
    c_top = np.array([0.38, 0.33, 0.27])
    c_fur = np.array([0.12, 0.09, 0.07])
    col = c_top * (0.8 + 0.4 * detail[..., None]) * (1 - furrow[..., None]) + c_fur * furrow[..., None]
    rough = 0.85 - 0.05 * furrow
    return _finish(col, h, rough, 12.0)


def palm_trunk(seed, n=SIZE):
    """Persistent split leaf bases in a criss-cross pattern (habit of modern Sabal trunks)."""
    s = (n, n)
    yy, xx = np.mgrid[0:n, 0:n] / n
    k = 4.0
    wu = (tg.fft_noise(s, seed + 7, beta=2.0, lo=1, hi=6) - 0.5) * 0.25
    a = (xx * k + yy * k * 2 + wu) % 1.0
    b = (xx * k - yy * k * 2 + wu) % 1.0
    diamond = np.minimum(np.abs(a - 0.5), np.abs(b - 0.5)) * 2
    fib = tg.fft_noise(s, seed, beta=1.0, lo=40, hi=500, aniso=(1.0, 8.0))
    bases = tg.smoothstep(0.08, 0.5, diamond)
    h = 0.7 * bases + 0.3 * fib
    h = (h - h.min()) / (h.max() - h.min())
    c0 = np.array([0.40, 0.33, 0.24])
    c1 = np.array([0.18, 0.14, 0.10])
    col = c0 * (0.75 + 0.5 * fib[..., None]) * bases[..., None] + c1 * (1 - bases[..., None])
    rough = 0.85 + 0.1 * fib
    return _finish(col, h, rough, 9.0)


def deadwood(seed, n=SIZE):
    """Weathered, barkless wood: longitudinal checks, silvering, dark decay patches."""
    s = (n, n)
    grain = tg.fft_noise(s, seed, beta=1.6, lo=6, hi=300, aniso=(1.0, 25.0))
    checks = tg.worley(s, seed + 1, 60, periodic=True, metric="f2-f1", jitter_aspect=0.08)
    crack = 1 - tg.smoothstep(0.0, 3.0, checks)
    blot = tg.fft_noise(s, seed + 2, beta=2.2, lo=2, hi=20)
    h = 0.6 * grain + 0.4 * (1 - crack)
    h = (h - h.min()) / (h.max() - h.min())
    silver = np.array([0.50, 0.47, 0.42])
    brown = np.array([0.36, 0.28, 0.20])
    rot = np.array([0.17, 0.13, 0.09])
    t = tg.smoothstep(0.35, 0.75, blot)[..., None]
    col = silver * (1 - t) + brown * t
    col = col * (0.8 + 0.35 * grain[..., None])
    col = col * (1 - crack[..., None] * 0.7) + rot * crack[..., None] * 0.7
    moss = tg.smoothstep(0.7, 0.85, tg.fft_noise(s, seed + 4, beta=2.0, lo=3, hi=30))[..., None]
    col = col * (1 - moss * 0.6) + np.array([0.22, 0.28, 0.10]) * moss * 0.6
    rough = 0.8 + 0.15 * grain
    return _finish(col, h, rough, 9.0)


def horsetail_stem(seed, n=SIZE):
    """Equisetum stem, one internode per tile: longitudinal ridges, toothed dark node sheath at the base."""
    s = (n, n)
    yy, xx = np.mgrid[0:n, 0:n] / n
    ridges = 0.5 + 0.5 * np.cos(2 * np.pi * xx * 18)
    v = 1 - yy  # V up (row 0 = top of tile)
    sheath = (v < 0.11).astype(float)
    teeth = (v < 0.11 + 0.05 * np.clip(1 - np.abs(((xx * 18) % 1.0) - 0.5) * 4, 0, 1)).astype(float)
    sheath = np.maximum(sheath, teeth)
    sheath = tg.blur(sheath, 1.2)
    band = np.exp(-((v - 0.115) / 0.008) ** 2)
    noise = tg.fft_noise(s, seed, beta=1.2, lo=10, hi=300, aniso=(1.0, 6.0))
    h = 0.6 * ridges + 0.3 * sheath + 0.1 * noise
    # dark blue-green stem and pale ash-grey sheath with a black rim: the banded look that identifies Equisetum
    green = np.array([0.19, 0.31, 0.16]) * (0.85 + 0.25 * noise[..., None]) * (0.85 + 0.15 * ridges[..., None])
    sh = np.array([0.50, 0.48, 0.40])
    col = green * (1 - sheath[..., None]) + sh * sheath[..., None]
    col = col * (1 - 0.7 * band[..., None])
    tips = (v > 0.11) & (v < 0.16) & (teeth > 0.5)
    col[tips] = col[tips] * 0.4 + np.array([0.10, 0.08, 0.06]) * 0.6
    rough = 0.45 + 0.2 * sheath
    return _finish(col, h, rough, 6.0)


def green_stem(seed, n=512):
    """Fibrous green-brown stem for petioles, rachises and herbaceous stalks."""
    s = (n, n)
    fib = tg.fft_noise(s, seed, beta=1.2, lo=8, hi=200, aniso=(1.0, 12.0))
    h = fib
    col = np.array([0.34, 0.38, 0.17]) * (0.8 + 0.35 * fib[..., None])
    return _finish(col, h, 0.55 + 0.1 * fib, 3.0)


def soil_clod(seed, n=512):
    """Silty soil clinging to a root plate: lumpy, cracked, with rootlets."""
    s = (n, n)
    lump = tg.fbm(s, seed, octaves=((4, 1.0), (16, 0.6), (64, 0.3)))
    cr = tg.worley(s, seed + 1, 50, periodic=True, metric="f2-f1")
    crack = 1 - tg.smoothstep(0.0, 2.5, cr)
    h = 0.8 * lump + 0.2 * (1 - crack)
    col = np.array([0.33, 0.28, 0.21]) * (0.75 + 0.45 * lump[..., None])
    col = col * (1 - 0.5 * crack[..., None])
    return _finish(col, h, 0.9 - 0.1 * lump, 6.0)


# name -> (painter, tile size in metres (u, v))
BARKS = {
    "cupressaceae": (cupressaceae, (0.9, 1.6)),
    "angio_smooth": (angio_smooth, (0.8, 0.8)),
    "angio_rough": (angio_rough, (0.9, 1.4)),
    "palm_trunk": (palm_trunk, (0.9, 1.2)),
    "deadwood": (deadwood, (0.8, 2.0)),
    "horsetail_stem": (horsetail_stem, (0.035, 0.09)),
    "green_stem": (green_stem, (0.06, 0.5)),
    "soil_clod": (soil_clod, (1.5, 1.5)),
}
