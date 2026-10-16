"""Numpy texture toolkit: periodic (tileable) noise, Worley cells, normals from height, PNG writers.

Every generator is deterministic for a given seed. "Periodic" functions tile exactly at the image border,
which is what ground and bark textures need; leaf cards use the non-periodic helpers.
Conventions: arrays are (rows, cols) = (v, u) with row 0 at the TOP of the image (PNG order).
"""
from __future__ import annotations

import os

import numpy as np
from PIL import Image
from scipy import ndimage
from scipy.spatial import cKDTree


# ----------------------------------------------------------------------------------------------- noise
def fft_noise(shape, seed, beta=2.0, lo=1.0, hi=None, aniso=(1.0, 1.0)):
    """Periodic coloured noise (power spectrum ~ 1/f^beta), normalised to [0, 1].

    lo / hi: band limits in cycles per image; aniso stretches frequencies (u, v) for fibrous patterns
    (aniso=(1, 6) gives features elongated along v)."""
    rng = np.random.default_rng(seed)
    h, w = shape
    fy = np.fft.fftfreq(h)[:, None] * h * aniso[1]
    fx = np.fft.rfftfreq(w)[None, :] * w * aniso[0]
    f = np.sqrt(fx * fx + fy * fy)
    amp = np.where(f >= lo, 1.0 / np.maximum(f, 1e-6) ** (beta * 0.5), 0.0)
    if hi is not None:
        amp *= np.exp(-(f / hi) ** 2)
    spec = amp * (rng.normal(size=f.shape) + 1j * rng.normal(size=f.shape))
    n = np.fft.irfft2(spec, s=(h, w))
    n -= n.min()
    return n / max(n.max(), 1e-9)


def fbm(shape, seed, octaves=((4, 1.0), (16, 0.5), (64, 0.25)), beta=1.0):
    """Sum of band-limited periodic noises; octaves = ((cycles per image, weight), ...)."""
    out = np.zeros(shape)
    tot = 0.0
    for i, (c, wgt) in enumerate(octaves):
        out += wgt * fft_noise(shape, seed + 101 * i, beta=beta, lo=c * 0.5, hi=c * 1.5)
        tot += wgt
    out /= tot
    out -= out.min()
    return out / max(out.max(), 1e-9)


def worley(shape, seed, n_points, periodic=True, metric="f1", jitter_aspect=1.0, return_ids=False):
    """Worley (cellular) distances in pixel units. periodic=True tiles exactly.
    metric: 'f1' nearest distance, 'f2-f1' cell borders."""
    rng = np.random.default_rng(seed)
    h, w = shape
    pts = rng.random((n_points, 2)) * np.array([h, w])
    yy, xx = np.mgrid[0:h, 0:w]
    q = np.stack([yy.ravel() + 0.5, xx.ravel() + 0.5], 1)
    # jitter_aspect < 1 compresses distances along rows (v), i.e. cells elongated vertically (along stems).
    scale = np.array([jitter_aspect, 1.0])
    if periodic:
        tree = cKDTree(pts * scale, boxsize=np.array([h, w]) * scale)
    else:
        tree = cKDTree(pts * scale)
    k = 2 if metric != "f1" else 1
    d, idx = tree.query(q * scale, k=k, workers=2)
    if k == 1:
        res = d.reshape(h, w)
        ids = idx.reshape(h, w)
    else:
        res = (d[:, 1] - d[:, 0]).reshape(h, w) if metric == "f2-f1" else d[:, 0].reshape(h, w)
        ids = idx[:, 0].reshape(h, w)
    return (res, ids) if return_ids else res


def remap(a, lo, hi, clip=True):
    r = (a - lo) / max(hi - lo, 1e-9)
    return np.clip(r, 0.0, 1.0) if clip else r


def smoothstep(lo, hi, a):
    t = np.clip((a - lo) / np.maximum(hi - lo, 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def blur(a, sigma, wrap=True):
    mode = "wrap" if wrap else "reflect"
    if a.ndim == 3:
        return np.stack([ndimage.gaussian_filter(a[..., i], sigma, mode=mode) for i in range(a.shape[2])], -1)
    return ndimage.gaussian_filter(a, sigma, mode=mode)


# ----------------------------------------------------------------------------------------------- maps
def normal_from_height(height, strength, wrap=True, directx=True):
    """Tangent-space normal map from a height map (height in [0, 1], strength = slope gain in px units).
    Returns uint8 RGB. directx=True writes the green channel for Unreal (Y down / -Y)."""
    mode = "wrap" if wrap else "nearest"
    dx = ndimage.sobel(height, axis=1, mode=mode) / 8.0
    dy = ndimage.sobel(height, axis=0, mode=mode) / 8.0
    nx = -dx * strength
    ny = dy * strength  # image rows go down; +V (OpenGL) is up
    nz = np.ones_like(height)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / l, ny / l, nz / l
    if directx:
        ny = -ny
    rgb = np.stack([nx, ny, nz], -1) * 0.5 + 0.5
    return to_u8(rgb)


def to_u8(a):
    return (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def to_u16(a):
    return (np.clip(a, 0.0, 1.0) * 65535.0 + 0.5).astype(np.uint16)


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def save_png(path, arr, bits=8):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    a = np.asarray(arr)
    if bits == 16:
        if a.dtype != np.uint16:
            a = to_u16(a)
        Image.fromarray(a, mode="I;16").save(path)
        return path
    if a.dtype != np.uint8:
        a = to_u8(a)
    if a.ndim == 2:
        Image.fromarray(a, "L").save(path, optimize=False, compress_level=6)
    elif a.shape[2] == 3:
        Image.fromarray(a, "RGB").save(path, compress_level=6)
    else:
        Image.fromarray(a, "RGBA").save(path, compress_level=6)
    return path


def downsample(a, factor):
    """Box-filter downsampling by an integer factor (supersampling resolve)."""
    if factor == 1:
        return a
    h, w = a.shape[:2]
    h2, w2 = h // factor, w // factor
    a = a[: h2 * factor, : w2 * factor]
    if a.ndim == 2:
        return a.reshape(h2, factor, w2, factor).mean((1, 3))
    return a.reshape(h2, factor, w2, factor, a.shape[2]).mean((1, 3))


def dilate_color(rgb, alpha, iterations=24):
    """Bleeds colour outside the alpha mask (prevents dark fringes in mipmaps of alpha-tested cards)."""
    mask = alpha > 0.5
    if mask.all() or not mask.any():
        return rgb
    _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
    return rgb[iy, ix]


def hsv_jitter(rgb, rng, dh=0.02, ds=0.08, dv=0.08):
    """Small random hue/saturation/value change of one RGB colour (tuple in [0, 1])."""
    import colorsys
    h, s, v = colorsys.rgb_to_hsv(*[float(x) for x in rgb])
    h = (h + rng.uniform(-dh, dh)) % 1.0
    s = float(np.clip(s * (1 + rng.uniform(-ds, ds)), 0, 1))
    v = float(np.clip(v * (1 + rng.uniform(-dv, dv)), 0, 1))
    return np.array(colorsys.hsv_to_rgb(h, s, v))


def lerp(a, b, t):
    t = np.asarray(t)
    if t.ndim == 2 and np.asarray(a).ndim == 3 or (np.asarray(b).ndim == 3 and t.ndim == 2):
        t = t[..., None]
    return a + (b - a) * t
