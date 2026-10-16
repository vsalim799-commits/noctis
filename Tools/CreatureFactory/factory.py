#!/usr/bin/env python
"""Noctis creature factory: anatomy spec -> sculpted high-poly -> game-ready skinned, animated asset.

    /home/user/.venvs/noctis-tools/bin/python Tools/CreatureFactory/factory.py <species_id> \
        [--stages anatomy,sdf,detail,retopo,uv,bake,rig,anim,export,render,video] [--quality hero|standard]

Every stage reads the cached outputs of the previous ones from Build/Creatures/<species_id>/ and can be
re-run alone. See README.md (French) for the description of each stage and of the outputs.
"""
from __future__ import annotations

import argparse
import importlib
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from lib.context import Ctx, STAGES  # noqa: E402

STAGE_MODULES = {
    "anatomy": "lib.anatomy",
    "sdf": "lib.sdf_stage",
    "detail": "lib.detail",
    "retopo": "lib.retopo",
    "uv": "lib.uv",
    "bake": "lib.bake",
    "rig": "lib.rig",
    "anim": "lib.anim",
    "export": "lib.export",
    "render": "lib.render",
    "video": "lib.video",
}
DEFAULT = ["anatomy", "sdf", "detail", "retopo", "uv", "bake", "rig", "anim", "export", "render"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("species")
    ap.add_argument("--stages", default=",".join(DEFAULT), help="comma separated, in order (default: all but video)")
    ap.add_argument("--quality", default="hero", choices=["hero", "standard"])
    ap.add_argument("--shots", default=None, help="render stage: comma separated subset of shots")
    ap.add_argument("--samples", type=int, default=None, help="render stage: override Cycles samples")
    ap.add_argument("--no-preview", action="store_true", help="skip the clay preview renders of the sdf/detail/retopo stages")
    args = ap.parse_args(argv)
    stages = [s.strip() for s in args.stages.split(",") if s.strip()]
    for s in stages:
        if s not in STAGE_MODULES:
            ap.error(f"unknown stage {s!r}; known: {', '.join(STAGES)}")
    ctx = Ctx(args.species, args.quality, options=vars(args))
    for s in stages:
        mod = importlib.import_module(STAGE_MODULES[s])
        ctx.log(f"=== stage {s} ({args.species}, {args.quality}) ===")
        t0 = time.time()
        mod.run(ctx)
        dt = time.time() - t0
        ctx.record_timing(s, dt)
        ctx.log(f"=== stage {s} done in {dt:.1f} s ===")
    return 0


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    # bpy (module build) can segfault while tearing down after heavy scenes: skip interpreter teardown
    os._exit(code or 0)
