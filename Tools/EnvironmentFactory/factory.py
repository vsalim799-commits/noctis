#!/usr/bin/env python
"""Noctis environment factory: Hell Creek flora, deadwood and ground materials for UE5, plus valley renders.

    /home/user/.venvs/noctis-tools/bin/python Tools/EnvironmentFactory/factory.py \
        [--stages sim,flora,ground,landscape,manifest,sheet,aerial,ground_shot] [--assets id1,id2] [--force-textures]
        [--samples N] [--preview]

Stages (each re-runnable alone, outputs cached in Build/Environment/):
  sim         noctis_sim export-terrain + export-ground (seed 7) -> Build/Environment/sim
  flora       leaf / bark textures, LOD0-2 meshes, UCX collision, FBX (cm), library .blend, metadata JSON
  ground      tileable 2K ground texture sets -> Build/Environment/ground
  landscape   UE5 paint-layer weightmaps at heightmap resolution (from the simulation masks) -> Build/Environment/landscape
  manifest    Build/Environment/manifest.json (assets, textures, scatter rules, landscape layers) read by the UE scripts
  sheet       Output/environment/assets_sheet.png (--samples 0: re-compose from cached thumbnails)
  aerial      Output/environment/valley_aerial.png
  ground_shot Output/environment/valley_ground.png
See README.md (French).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from envlib import common  # noqa: E402

DEFAULT = ["sim", "flora", "ground", "landscape", "manifest", "sheet", "aerial", "ground_shot"]


def stage_sim(args):
    os.makedirs(common.SIM_DIR, exist_ok=True)
    for cmd in ("export-terrain", "export-ground"):
        subprocess.run([common.NOCTIS_SIM, cmd, "--seed", str(args.seed), "--out", common.SIM_DIR], check=True)


def stage_flora(args):
    from envlib import assets
    ids = args.assets.split(",") if args.assets else None
    return assets.run(ids, force_textures=args.force_textures)


def stage_ground(args):
    from envlib import ground
    return ground.run(force=args.force_textures)


def stage_landscape(args):
    from envlib import landscape
    return landscape.run()


def stage_manifest(args):
    from envlib import manifest
    return manifest.run()


def stage_sheet(args):
    from envlib import sheet
    return sheet.run(samples=20 if args.samples is None else args.samples, ids=args.assets.split(",") if args.assets else None)


def stage_aerial(args):
    from envlib import scenes
    return scenes.render_aerial(samples=args.samples, preview=args.preview)


def stage_ground_shot(args):
    from envlib import scenes
    return scenes.render_ground(samples=args.samples, preview=args.preview)


STAGES = dict(sim=stage_sim, flora=stage_flora, ground=stage_ground, landscape=stage_landscape, manifest=stage_manifest,
              sheet=stage_sheet, aerial=stage_aerial, ground_shot=stage_ground_shot)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stages", default=",".join(DEFAULT))
    ap.add_argument("--assets", default=None, help="flora: comma-separated asset ids (default: all)")
    ap.add_argument("--force-textures", action="store_true", help="regenerate cached textures")
    ap.add_argument("--samples", type=int, default=None, help="Cycles samples override (renders, sheet)")
    ap.add_argument("--preview", action="store_true", help="renders at 25 %% resolution, few samples")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args(argv)
    common.ensure_dirs()
    timings = common.read_json(os.path.join(common.OUT_DIR, "timings.json"), {})
    for s in [x.strip() for x in args.stages.split(",") if x.strip()]:
        if s not in STAGES:
            ap.error("unknown stage %r (known: %s)" % (s, ", ".join(STAGES)))
        common.log("=== stage %s ===" % s)
        t0 = time.time()
        STAGES[s](args)
        dt = time.time() - t0
        key = s + ("_preview" if args.preview and s in ("aerial", "ground_shot") else "")
        timings[key] = round(dt, 1)
        common.write_json(os.path.join(common.OUT_DIR, "timings.json"), timings)
        common.log("=== stage %s done in %.1f s ===" % (s, dt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
