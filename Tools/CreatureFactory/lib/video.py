"""Stage 'video': walk-cycle tracking shot (1280x720, 24 fps, 4 s, Cycles low spp + denoise) encoded
with ffmpeg to Output/factory/<sp>/<sp>_walk.mp4.

The walk Action (30 fps, one cycle with root motion) is sampled at t = k/24 s; cycles are chained by
offsetting the armature object by one stride per completed cycle (root motion accumulates). The
camera tracks the root at constant speed from a 3/4 side position."""
from __future__ import annotations

import math
import os
import shutil
import subprocess
import numpy as np


def run(ctx):
    import bpy
    from . import bl
    from .render import setup_body_material, studio
    sp = ctx.species
    clips = ctx.load_json("anim_clips.json")["clips"]
    walk = clips["walk"]
    bpy.ops.wm.open_mainfile(filepath=ctx.path("anim.blend"))
    sc = bpy.context.scene
    rig = bpy.data.objects["Armature"]
    for tr in rig.animation_data.nla_tracks:
        tr.mute = True
    rig.animation_data.action = bpy.data.actions["walk"]
    setup_body_material(bpy)
    bl.cycles(sc, samples=28, res=(1280, 720))
    sc.cycles.adaptive_threshold = 0.03
    studio(bpy, bl)
    fps_a = walk["fps"]
    nfa = walk["frames"]
    T = nfa / fps_a
    stride = walk["stride_m"]
    v = stride / T
    fdir = ctx.path("video_frames")
    shutil.rmtree(fdir, ignore_errors=True)
    os.makedirs(fdir)
    n = 4 * 24
    cam = bl.camera("cam_video", (0, 0, 0), (0, -1, 0), lens=40)
    sc.camera = cam
    for k in range(n):
        t = k / 24.0
        cyc = int(t // T)
        lt = t - cyc * T
        fr = 1 + lt * fps_a
        sc.frame_set(int(fr), subframe=fr - int(fr))
        rig.location = bl.to_bl(np.array([cyc * stride, 0.0, 0.0]))
        x = v * t
        eye = np.array([x + 3.0, 3.0, 14.5])
        tgt = np.array([x - 0.6, 2.1, 0.0])
        cam.location = bl.to_bl(eye)
        bl.look_at(cam, bl.to_bl(tgt))
        sc.render.filepath = os.path.join(fdir, f"f_{k:04d}.png")
        bpy.ops.render.render(write_still=True)
    out = ctx.opath(f"{sp}_walk.mp4")
    subprocess.run(["/usr/bin/ffmpeg", "-y", "-framerate", "24", "-i", os.path.join(fdir, "f_%04d.png"), "-c:v", "libx264",
                    "-pix_fmt", "yuv420p", "-crf", "18", out], check=True, capture_output=True)
    ctx.log(f"  video: {out}")
