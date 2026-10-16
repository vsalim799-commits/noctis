#!/usr/bin/env python3
"""Noctis AudioFactory — reproducible procedural sound library.

    python Tools/AudioFactory/generate.py                 # everything
    python Tools/AudioFactory/generate.py --only vocal footsteps
    python Tools/AudioFactory/generate.py --jobs 4 --no-png

Writes 48 kHz / 24-bit WAV files under Build/Audio/ (git-ignored), spectrogram PNGs under Output/audio/
and the committed manifest Data/Audio/manifest.json.
"""
from __future__ import annotations

import argparse
import importlib
import json
import math
import multiprocessing as mp
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from audiofactory import common as C  # noqa: E402
from audiofactory import data as D  # noqa: E402
from audiofactory import manifest as M  # noqa: E402

MODULES = ["vocal", "footsteps", "foley", "ambience", "equipment"]
ULTRASONIC_LP_HZ = 20000.0      # finalize(): zero-phase low-pass (18-20 kHz roll-off) of every non-vocal sound
TAIL_FLOOR_DB = -60.0           # finalize(): one-shot tails are trimmed below this level re the loudest frame
_DS: D.Dataset | None = None
_PNG = True
_REUSE_PNG = False


def _init(png: bool, reuse_png: bool = False):
    global _DS, _PNG, _REUSE_PNG
    _DS = D.load()
    _PNG = png
    _REUSE_PNG = reuse_png


def _active_rms_dbfs(x: np.ndarray) -> float:
    mono = x.mean(axis=1) if x.ndim == 2 else x
    w = C.ns(0.05)
    nfr = max(1, mono.shape[0] // w)
    fr = mono[: nfr * w].reshape(nfr, w) if mono.shape[0] >= w else mono.reshape(1, -1)
    e = np.sqrt(np.mean(fr * fr, axis=1))
    act = e >= e.max() * C.db2lin(-20.0)
    return float(C.lin2db(math.sqrt(float(np.mean(e[act] ** 2)))))


def _ultrasonic_gain(f):
    """Zero-phase low-pass: flat to 18 kHz, raised-cosine roll-off to zero at 20 kHz (nothing inaudible above)."""
    lo, hi = ULTRASONIC_LP_HZ - 2000.0, ULTRASONIC_LP_HZ
    return np.where(f <= lo, 1.0, np.where(f >= hi, 0.0, 0.5 + 0.5 * np.cos(np.pi * (f - lo) / (hi - lo))))


def finalize(s: M.Sound) -> tuple[dict, tuple]:
    x = np.asarray(s.audio, dtype=np.float64)
    # Band guard: DC / sub-audio high-pass (5 Hz) for everything; low-pass at 20 kHz for every module except the vocal
    # library (unchanged since 1.1.0, its partials stop at 0.45 SR by construction): no inaudible ultrasonic noise.
    lp = s.category != "vocal"
    if s.loop:
        # Circular filtering: filter one wrapped copy first so the output is in periodic steady state (seamless).
        k = min(x.shape[0], C.ns(2.0))
        x = C.highpass(np.concatenate([x[-k:], x], axis=0), 5.0, order=2)[k:]
        if lp:
            x = C.circular_filter(x, _ultrasonic_gain)      # zero-phase and exactly periodic
    else:
        x = C.highpass(x, 5.0, order=2)
        if lp:
            x = C.fft_filter(x, _ultrasonic_gain)
        if lp and not s.transition:
            # (not the vocal library: its calls end with 0.15-0.2 s of rendered silence and stay bit-identical to 1.1.0)
            # Buffers are rendered long enough for the exponential / Poisson tails of the events; cut the file once
            # the envelope stays below -60 dB re its loudest 5 ms frame, so the end fade never cuts a sound still ringing.
            x = C.trim_tail(x, TAIL_FLOOR_DB)
    if s.fade and not s.loop:
        x = C.fade(x, 0.002, 0.02)
    mode = s.normalize[0]
    if mode == "peak":
        x, g = C.normalize_peak(x, s.normalize[1])
    elif mode == "rms":
        x, g = C.normalize_rms(x, s.normalize[1], s.normalize[2] if len(s.normalize) > 2 else -1.0)
    else:
        g = 0.0
    wav = C.BUILD_DIR / s.group / f"{s.id}.wav"
    C.write_wav(wav, x)
    rec = s.record(_DS)
    act = _active_rms_dbfs(x)
    level = dict(s.level)
    level["normalization"] = {"mode": mode, "target_dbfs": s.normalize[1] if len(s.normalize) > 1 else None,
                              "gain_db": round(g, 3)}
    level["active_rms_dbfs"] = round(act, 3)
    if level.get("received_spl_db") is not None:
        # Distance baked into the file (thunder): calibrate on the received level.
        level["calibration_spl_db_at_0dbfs_rms"] = round(level["received_spl_db"] - act, 2)
    elif level.get("ambient_spl_db") is not None:
        # Diffuse beds / loops: the whole file represents the ambient level at the listener.
        full = float(C.lin2db(C.rms(x)))
        level["rms_dbfs"] = round(full, 3)
        level["calibration_spl_db_at_0dbfs_rms"] = round(level["ambient_spl_db"] - full, 2)
    elif "source_spl_db_1m" in level:
        # SPL (dB re 20 µPa at 1 m) that a full-scale RMS signal (0 dBFS RMS) of this file represents.
        level["calibration_spl_db_at_0dbfs_rms"] = round(level["source_spl_db_1m"] - act, 2)
    rec["level"] = level
    ch = 1 if x.ndim == 1 else x.shape[1]
    rec["file"] = C.rel(wav)
    rec["audio"] = {"sample_rate": C.SR, "bit_depth": 24, "format": "WAV PCM", "channels": ch, "frames": int(x.shape[0]),
                    "duration_s": round(x.shape[0] / C.SR, 6), "loop": s.loop}
    rec["qa"] = dict(s.qa, duration_s=round(x.shape[0] / C.SR, 6), channels=ch, loop=s.loop)
    if s.transition:
        rec["qa"]["transition"] = True
    thumb = None
    png = C.PREVIEW_DIR / s.group / f"{s.id}.png"
    if _PNG and _REUSE_PNG and png.exists():
        rec["preview_png"] = C.rel(png)          # audio unchanged: keep the existing spectrogram
    elif _PNG:
        low = s.params.get("f0_effective_median_hz", 1000.0) < 150.0 or s.category in ("footstep",) or "thunder" in s.id
        from audiofactory import spectro
        thumb = spectro.render(png, x, s.id, f"{s.label_fr}  [{D.CONFIDENCE_LABEL_FR[s.confidence]}]", low_content=low)
        rec["preview_png"] = C.rel(png)
    return rec, (s.group, s.id, thumb, D.CONFIDENCE_LABEL_FR[s.confidence])


def run_job(job: tuple) -> list:
    mod, fn, kw = job
    m = importlib.import_module(f"audiofactory.{mod}")
    sounds = getattr(m, fn)(_DS, **kw)
    out = []
    for s in sounds:
        rec, thumb = finalize(s)
        rec["module"] = mod
        out.append((rec, thumb))
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", choices=MODULES, help="generate only these categories (manifest is merged)")
    ap.add_argument("--jobs", type=int, default=max(1, mp.cpu_count()))
    ap.add_argument("--no-png", action="store_true", help="skip spectrogram previews")
    ap.add_argument("--prune-only", action="store_true", help="only delete WAV / PNG files that are not in the manifest")
    ap.add_argument("--reuse-png", action="store_true",
                    help="keep existing spectrograms (metadata-only changes); contact sheets are rebuilt only for new previews")
    args = ap.parse_args(argv)
    t0 = time.time()
    if args.prune_only:
        prev = json.loads(C.MANIFEST_PATH.read_text(encoding="utf-8"))
        prune(prev["sounds"], png=True)
        return 0
    ds = D.load()
    mods = args.only or MODULES
    joblist = []
    for m in mods:
        joblist += importlib.import_module(f"audiofactory.{m}").jobs(ds)
    print(f"[audiofactory] {len(joblist)} jobs ({', '.join(mods)}) on {args.jobs} processes")
    results = []
    if args.jobs > 1:
        with mp.get_context("fork").Pool(args.jobs, initializer=_init, initargs=(not args.no_png, args.reuse_png)) as pool:
            for k, res in enumerate(pool.imap(run_job, joblist, chunksize=1)):
                results.extend(res)
                if (k + 1) % 25 == 0:
                    print(f"  {k + 1}/{len(joblist)} jobs, {len(results)} sounds, {time.time() - t0:.0f} s", flush=True)
    else:
        _init(not args.no_png, args.reuse_png)
        for job in joblist:
            results.extend(run_job(job))
    records = [r for r, _ in results]
    ids = [r["id"] for r in records]
    dup = [k for k, v in Counter(ids).items() if v > 1]
    if dup:
        raise SystemExit(f"duplicate sound ids: {dup[:10]}")

    # Merge with the existing manifest when only some modules were regenerated.
    old = []
    if args.only and C.MANIFEST_PATH.exists():
        prev = json.loads(C.MANIFEST_PATH.read_text(encoding="utf-8"))
        old = [r for r in prev.get("sounds", []) if r.get("module") not in set(mods)]
    all_records = sorted(old + records, key=lambda r: (r["category"], r["id"]))
    write_manifest(ds, all_records)
    prune(all_records, png=not args.no_png)

    if not args.no_png:
        from audiofactory import spectro
        groups = defaultdict(list)
        for _, (g, sid, th, conf) in results:
            groups[g].append((sid, th, conf))
        for g, items in sorted(groups.items()):
            if any(th is None for _, th, _ in items):
                continue                          # some previews were reused (--reuse-png): keep the existing sheet
            for part in range(0, len(items), 48):
                chunk = items[part:part + 48]
                suffix = "" if len(items) <= 48 else f"_{part // 48 + 1}"
                spectro.contact_sheet(C.PREVIEW_DIR / "sheets" / f"{g.replace('/', '__')}{suffix}.png", g + suffix, chunk)
    by_cat = Counter(r["category"] for r in all_records)
    print(f"[audiofactory] {len(all_records)} sounds in manifest {dict(by_cat)}; {time.time() - t0:.0f} s")
    return 0


def prune(records: list[dict], png: bool) -> None:
    """Delete generated files that are no longer described by the manifest (renamed or removed sounds)."""
    listed = {r["file"] for r in records}
    n = 0
    for p in C.BUILD_DIR.rglob("*.wav"):
        if C.rel(p) not in listed:
            p.unlink()
            n += 1
    if png:
        pngs = {r.get("preview_png") for r in records}
        for p in C.PREVIEW_DIR.rglob("*.png"):
            if "sheets" not in p.relative_to(C.PREVIEW_DIR).parts and C.rel(p) not in pngs:
                p.unlink()
                n += 1
    if n:
        print(f"[audiofactory] pruned {n} stale files")


def _aliases() -> dict:
    """Sound ids that are deliberately not rendered because they would duplicate another file (single-layer mixes)."""
    from audiofactory import ambience
    return dict(ambience.ALIASES)


def write_manifest(ds: D.Dataset, records: list[dict]) -> None:
    used = sorted({s for r in records for s in r["sources"]})
    by_cat = Counter(r["category"] for r in records)
    by_conf = Counter(r["confidence"] for r in records)
    by_model = Counter(r.get("model_confidence", r["confidence"]) for r in records)
    doc = {
        "schema": "noctis.audio_manifest/1",
        "generator": "Tools/AudioFactory/generate.py",
        "generator_version": C.GENERATOR_VERSION,
        "master_seed": C.MASTER_SEED,
        "audio_format": {"sample_rate": C.SR, "bit_depth": 24, "container": "WAV PCM", "root": "Build/Audio"},
        "conventions_fr": {
            "niveaux": "level.source_spl_db_1m : niveau de source en dB SPL (re 20 µPa) à 1 m, même convention que SoundSource::levelDb du cœur. "
                       "Les fichiers sont normalisés (voir level.normalization) ; level.calibration_spl_db_at_0dbfs_rms permet de recalibrer dans UE5.",
            "confiance": "Deux niveaux par son, chacun égal à l'hypothèse la plus faible qui le fonde (règle Data/SCHEMA.md : une référence non vérifiée ne peut soutenir plus qu'INFÉRÉ ; sans référence : SPÉCULATIF au mieux). "
                         "confidence porte sur l'existence de l'événement et ses grandeurs documentées (masse, cinématique, débit, intensité de pluie...) ; "
                         "model_confidence porte sur le modèle de synthèse (timbre, loi de niveau), le plus souvent une hypothèse de jeu ; "
                         "overall_confidence = min des deux, à afficher quand une seule valeur est montrée.",
            "hypotheses": "Toutes les vocalisations de dinosaures non aviens, d'oiseaux, d'amphibiens et d'insectes sont des HYPOTHÈSES (hypothesis = true). Le jeu ne prétend jamais connaître la signification d'un cri.",
            "distance": "Les variantes de distance appliquent l'absorption ISO 9613-1 et l'atténuation par le feuillage du cœur (SoundField.cpp) ; le niveau reçu est dans parameters.propagation et dans level.received_spl_db, sur lequel la calibration est faite (préréglages *_DistanceBaked : spatialisation seule).",
            "fins": "Les one-shots sont rendus dans un tampon assez long pour les queues exponentielles de leurs événements puis coupés à -60 dB du maximum (sauf transitions vers une boucle, qa.transition) ; passe-bas 20 kHz sur tout sauf les vocalisations.",
            "qa": "Le bloc qa contient les valeurs attendues vérifiées par Tools/AudioFactory/qa.py.",
        },
        "ue5": {
            "sound_classes": M.SOUND_CLASSES,
            "attenuation_presets": M.ATTENUATION_PRESETS,
            "concurrency_presets": M.CONCURRENCY_PRESETS,
            "aliases": _aliases(),
        },
        "summary": {
            "sounds": len(records),
            "by_category": dict(sorted(by_cat.items())),
            "by_confidence": dict(sorted(by_conf.items(), key=lambda kv: D.conf_rank(kv[0]))),
            "by_model_confidence": dict(sorted(by_model.items(), key=lambda kv: D.conf_rank(kv[0]))),
            "hypotheses": sum(1 for r in records if r.get("hypothesis")),
            "total_duration_s": round(sum(r["audio"]["duration_s"] for r in records), 2),
        },
        "sources_used": {s: {"authors": ds.sources[s].get("authors"), "year": ds.sources[s].get("year"),
                             "title": ds.sources[s].get("title"), "verified": bool(ds.sources[s].get("verified"))} for s in used},
        "sounds": records,
    }
    C.MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
    C.MANIFEST_PATH.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
