"""Spectrogram previews (PNG) for every generated sound, plus per-group contact sheets."""
from __future__ import annotations

import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from scipy import signal  # noqa: E402

from . import common as C  # noqa: E402

F_MIN = 10.0
F_MAX = 24000.0
THUMB_F = 96     # thumbnail frequency rows (log spaced)
THUMB_T = 160    # thumbnail time columns


def _stft_db(x: np.ndarray, low_content: bool):
    nper = 8192 if low_content else 2048
    nper = min(nper, max(256, 2 ** int(math.floor(math.log2(max(256, x.shape[0]))))))
    hop = max(64, nper // 8)
    f, t, Z = signal.stft(x, fs=C.SR, nperseg=nper, noverlap=nper - hop, boundary=None, padded=False)
    S = 20.0 * np.log10(np.abs(Z) + 1e-9)
    return f, t, S


def render(path: Path, x: np.ndarray, title: str, subtitle: str, low_content: bool = False):
    """Writes a PNG and returns a small (THUMB_F x THUMB_T) dB thumbnail for contact sheets."""
    mono = x.mean(axis=1) if x.ndim == 2 else x
    f, t, S = _stft_db(mono, low_content)
    vmax = float(S.max())
    vmin = vmax - 80.0
    path.parent.mkdir(parents=True, exist_ok=True)
    fig = plt.figure(figsize=(9.0, 3.8), dpi=100)
    ax = fig.add_axes([0.08, 0.14, 0.78, 0.68])
    keep = f >= F_MIN
    ax.pcolormesh(t if t.size > 1 else np.array([0.0]), f[keep], S[keep, :], shading="auto", cmap="magma", vmin=vmin, vmax=vmax,
                  rasterized=True)
    ax.set_yscale("log")
    ax.set_ylim(F_MIN, F_MAX)
    ax.set_xlabel("temps (s)")
    ax.set_ylabel("fréquence (Hz)")
    ax.set_yticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000])
    ax.set_yticklabels(["20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"])
    # Waveform envelope strip on the right: level over time in dBFS.
    ax2 = fig.add_axes([0.88, 0.14, 0.1, 0.68])
    spec = np.mean(10 ** (S / 10.0), axis=1)
    ax2.semilogy(10.0 * np.log10(spec[keep] + 1e-18), f[keep], color="#e0a040", lw=0.8)
    ax2.set_ylim(F_MIN, F_MAX)
    ax2.set_yticks([])
    ax2.set_xlabel("dB moy.", fontsize=7)
    ax2.tick_params(labelsize=6)
    fig.text(0.08, 0.93, title, fontsize=9, family="monospace", weight="bold")
    fig.text(0.08, 0.87, subtitle, fontsize=8)
    fig.savefig(path)
    plt.close(fig)
    # Thumbnail on a log-frequency grid.
    fl = np.geomspace(20.0, 20000.0, THUMB_F)
    idx = np.clip(np.searchsorted(f, fl), 0, f.size - 1)
    Sl = S[idx, :]
    cols = np.linspace(0, Sl.shape[1] - 1, THUMB_T).astype(int) if Sl.shape[1] > 0 else np.zeros(THUMB_T, int)
    th = Sl[:, cols] - vmax
    return np.clip(th, -80.0, 0.0).astype(np.float16)


def contact_sheet(path: Path, title: str, items: list[tuple[str, np.ndarray, str]]):
    """items: (id, thumbnail, confidence label)."""
    n = len(items)
    if n == 0:
        return
    cols = 4 if n > 6 else min(n, 3)
    rows = int(math.ceil(n / cols))
    fig, axes = plt.subplots(rows, cols, figsize=(3.4 * cols, 1.9 * rows + 0.6), dpi=90, squeeze=False)
    for k, ax in enumerate(axes.flat):
        ax.set_xticks([])
        ax.set_yticks([])
        if k >= n:
            ax.axis("off")
            continue
        sid, th, conf = items[k]
        ax.imshow(th.astype(np.float32), origin="lower", aspect="auto", cmap="magma", vmin=-80, vmax=0)
        ax.set_title(sid if len(sid) < 46 else "…" + sid[-45:], fontsize=6, family="monospace")
        ax.text(2, 4, conf, fontsize=5, color="white")
        for fr in (100, 1000, 10000):
            y = (math.log(fr / 20.0) / math.log(1000.0)) * (THUMB_F - 1)
            ax.axhline(y, color="white", lw=0.3, alpha=0.4)
    fig.suptitle(title + "  (axe vertical log 20 Hz–20 kHz ; lignes : 100 Hz, 1 kHz, 10 kHz)", fontsize=8)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path)
    plt.close(fig)
