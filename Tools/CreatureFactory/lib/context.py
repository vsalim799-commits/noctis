"""Shared stage context: paths, quality presets, logging, timings, species/anatomy data."""
from __future__ import annotations

import glob
import json
import os
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
STAGES = ["anatomy", "sdf", "detail", "retopo", "uv", "bake", "rig", "anim", "export", "render", "video"]

# Quality presets. Lengths in metres.
QUALITY = {
    "hero": dict(
        h_base=0.012,          # marching-cubes voxel of the smooth sculpt (retopo source)
        h_high=0.006,          # voxel of the displaced high-poly (detail stage)
        retopo_faces=31500,    # quad target of the voxel remesh -> ~68k body tris (+claws, teeth, eyes: LOD0 < 80k)
        lods=[0.5, 0.25, 0.12],
        tex=4096,              # texture size (px)
        bake_ss=1,             # texel supersampling (1 = centre only)
        render_samples=64,
        render_res=(1600, 900),
    ),
    "standard": dict(
        h_base=0.016,
        h_high=0.010,
        retopo_faces=7500,
        lods=[0.5, 0.25],
        tex=2048,
        bake_ss=1,
        render_samples=48,
        render_res=(1600, 900),
    ),
}


class Ctx:
    def __init__(self, species, quality="hero", options=None):
        self.species = species
        self.quality = quality
        self.q = dict(QUALITY[quality])
        self.options = options or {}
        self.repo = REPO
        self.build = os.path.join(REPO, "Build", "Creatures", species)
        self.out = os.path.join(REPO, "Output", "factory", species)
        os.makedirs(self.build, exist_ok=True)
        os.makedirs(self.out, exist_ok=True)
        self._logf = open(os.path.join(self.build, "factory.log"), "a")

    # ------------------------------------------------------------------ io
    def path(self, *p):
        return os.path.join(self.build, *p)

    def opath(self, *p):
        return os.path.join(self.out, *p)

    def log(self, *a):
        msg = " ".join(str(x) for x in a)
        line = f"[{time.strftime('%H:%M:%S')}] {msg}"
        print(line, flush=True)
        self._logf.write(line + "\n")
        self._logf.flush()

    def record_timing(self, stage, dt):
        p = self.path("timings.json")
        d = {}
        if os.path.exists(p):
            with open(p) as f:
                d = json.load(f)
        d.setdefault(self.quality, {})[stage] = round(dt, 1)
        with open(p, "w") as f:
            json.dump(d, f, indent=1)

    def save_json(self, name, obj):
        with open(self.path(name), "w") as f:
            json.dump(obj, f, indent=1, ensure_ascii=False)

    def load_json(self, name):
        with open(self.path(name)) as f:
            return json.load(f)

    # ------------------------------------------------------------------ data
    def anatomy(self):
        p = os.path.join(REPO, "Data", "Anatomy", f"{self.species}.json")
        with open(p) as f:
            return json.load(f)

    def species_data(self):
        for p in glob.glob(os.path.join(REPO, "Data", "Species", "*", f"{self.species}.json")):
            with open(p) as f:
                return json.load(f)
        raise FileNotFoundError(self.species)

    def bibliography(self):
        ids = {}
        for p in glob.glob(os.path.join(REPO, "Data", "Sources", "*.json")):
            with open(p) as f:
                j = json.load(f)
            ents = j if isinstance(j, list) else j.get("entries", j.get("sources", []))
            if isinstance(ents, dict):
                ents = list(ents.values())
            for e in ents:
                if isinstance(e, dict) and "id" in e:
                    ids[e["id"]] = e
        return ids
