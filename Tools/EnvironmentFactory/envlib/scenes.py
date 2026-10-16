"""Stages 'aerial' and 'ground_shot': Cycles renders of the simulated valley (seed 7) with the generated assets.

Both scenes use the public helper Tools/CreatureFactory/scenes/vegetation_library.py (terrain crop from the
simulation heightmap, habitat-blended ground material, simulated river level, mask-driven scatter with
camera-distance LOD), i.e. exactly what a creature beauty shot would use.
  valley_aerial.png   low aerial over the meanders at golden hour (sun ~6 deg), atmospheric haze (mist pass)
  valley_ground.png   eye level (1.6 m) on a riverbank among horsetails and ferns, morning mist (height fog + mist)
"""
from __future__ import annotations

import json
import math
import os
import sys
import time

import numpy as np

from . import common

HELPER_DIR = os.path.join(common.ROOT, "Tools", "CreatureFactory", "scenes")


def _veg():
    if HELPER_DIR not in sys.path:
        sys.path.insert(0, HELPER_DIR)
    import vegetation_library as veg
    return veg


# ----------------------------------------------------------------------------------------------- scene setup
def setup_render(sc, samples, res=(1920, 1080), preview=False, threads=3):
    sc.render.engine = "CYCLES"
    cy = sc.cycles
    cy.device = "CPU"
    cy.samples = samples
    cy.use_adaptive_sampling = True
    cy.adaptive_threshold = 0.03
    cy.use_denoising = True
    try:
        cy.denoiser = "OPENIMAGEDENOISE"
    except Exception:
        pass
    cy.max_bounces = 5
    cy.diffuse_bounces = 2
    cy.glossy_bounces = 2
    cy.transmission_bounces = 2
    cy.volume_bounces = 0
    cy.transparent_max_bounces = 24
    cy.caustics_reflective = False
    cy.caustics_refractive = False
    cy.sample_clamp_indirect = 8.0
    cy.volume_step_rate = 4.0
    cy.volume_max_steps = 128
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = int(os.environ.get("NOCTIS_PREVIEW_PCT", "25")) if preview else 100
    sc.render.threads_mode = "FIXED"
    sc.render.threads = threads
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    sc.view_settings.view_transform = "AgX"
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass
    sc.render.use_persistent_data = False


def sun_rotation(elev_deg, az_deg):
    """Blender sun lamp Euler for a sun at compass azimuth az (0 = north, 90 = east) and elevation."""
    return (math.radians(90 - elev_deg), 0.0, math.radians(180 - az_deg))


def sky(sc, elev, az, sun_strength=4.0, sky_strength=0.35, air=1.0, dust=2.0, sun_color=(1.0, 0.82, 0.62), sun_angle=0.6):
    import bpy
    w = bpy.data.worlds.new("noctis_sky")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes["Background"]
    st = nt.nodes.new("ShaderNodeTexSky")
    st.sky_type = "NISHITA"
    st.sun_disc = False
    st.sun_elevation = math.radians(elev)
    # Nishita: rotation 0 puts the sun towards +Y? Calibrated: sun_rotation = azimuth measured from north, clockwise.
    st.sun_rotation = math.radians(az)
    st.altitude = 200.0
    st.air_density = air
    st.dust_density = dust
    st.ozone_density = 1.0
    nt.links.new(st.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = sky_strength
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = sun_strength
    sun.data.color = sun_color
    sun.data.angle = math.radians(sun_angle)
    sun.rotation_euler = sun_rotation(elev, az)
    return sun


def mist_compositor(sc, color, amount, start, depth, falloff="QUADRATIC", glare=True, height_fog=None):
    """Distance haze from the Mist pass, optionally preceded by an analytic height fog (noise-free, no volume):
    height_fog = dict(z0=water level (Blender Z), cam_z=camera Z, density=1/m, height=m, color=(r, g, b)).
    Optical depth along a view ray ~ density * distance * mean(exp(-h/H)) with h the height above z0, averaged
    between the camera and the hit point (distance from the Mist pass, height from the Position pass); the sky
    (Depth pass = no hit) is left to the mist. The distance saturates at start + depth of the mist settings."""
    import math as _m
    import bpy

    def despeckle(sock):
        # The Mist / Depth / Position passes are not denoised: where some samples of a pixel pass through the holes
        # of alpha cards to a far surface, the pass is locally much "further" than its neighbours and the haze
        # colour shows up as bright specks over the foliage. A 1 px morphological opening (erode then dilate)
        # removes these isolated high values; unlike a plain erode it keeps silhouettes in place (no dark blocks
        # around small far objects).
        er = nt.nodes.new("CompositorNodeDilateErode")
        er.mode = "STEP"
        er.distance = -1
        nt.links.new(sock, er.inputs["Mask"])
        di = nt.nodes.new("CompositorNodeDilateErode")
        di.mode = "STEP"
        di.distance = 1
        nt.links.new(er.outputs["Mask"], di.inputs["Mask"])
        return di.outputs["Mask"]

    vl = sc.view_layers[0]
    vl.use_pass_mist = True
    ms = sc.world.mist_settings
    ms.start = start
    ms.depth = depth
    ms.falloff = falloff
    sc.use_nodes = True
    nt = sc.node_tree
    nt.nodes.clear()
    rl = nt.nodes.new("CompositorNodeRLayers")
    image = rl.outputs["Image"]
    if height_fog:
        vl.use_pass_z = True
        vl.use_pass_position = True
        nt.nodes.remove(rl)
        rl = nt.nodes.new("CompositorNodeRLayers")  # sockets of the passes enabled above
        image = rl.outputs["Image"]
        hf = height_fog

        def math(op, a, b=None):
            n = nt.nodes.new("CompositorNodeMath")
            n.operation = op
            for i, v in enumerate((a, b)):
                if v is None:
                    continue
                if isinstance(v, (int, float)):
                    n.inputs[i].default_value = float(v)
                else:
                    nt.links.new(v, n.inputs[i])
            return n.outputs[0]

        sep = nt.nodes.new("CompositorNodeSeparateColor")
        nt.links.new(rl.outputs["Position"], sep.inputs["Image"])
        H = float(hf.get("height", 2.5))
        hp = math("MAXIMUM", math("SUBTRACT", sep.outputs["Blue"], float(hf["z0"])), 0.0)
        fp = math("EXPONENT", math("MULTIPLY", hp, -1.0 / H))
        fc = _m.exp(-max(float(hf["cam_z"]) - float(hf["z0"]), 0.0) / H)
        favg = math("MULTIPLY", math("ADD", fp, fc), 0.5)
        # Distance from the Mist pass (averaged over the samples, hence anti-aliased; the Depth pass holds one sample
        # per pixel and makes blocky fog edges around thin stems and far silhouettes). Inverse of the mist falloff.
        mist_d = despeckle(rl.outputs["Mist"])
        if falloff == "QUADRATIC":
            mist_d = math("SQRT", mist_d)
        elif falloff == "INVERSE_QUADRATIC":
            mist_d = math("POWER", mist_d, 0.25)
        dist = math("ADD", math("MULTIPLY", mist_d, float(depth)), float(start))
        od = math("MULTIPLY", math("MULTIPLY", dist, favg), float(hf.get("density", 0.006)))
        fog = math("SUBTRACT", 1.0, math("EXPONENT", math("MULTIPLY", od, -1.0)))
        fog = math("MULTIPLY", fog, math("LESS_THAN", rl.outputs["Depth"], 1.0e6))  # no fog on the sky
        fog = math("MULTIPLY", fog, float(hf.get("max", 0.92)))
        hmix = nt.nodes.new("CompositorNodeMixRGB")
        hmix.blend_type = "MIX"
        nt.links.new(fog, hmix.inputs[0])
        nt.links.new(image, hmix.inputs[1])
        hmix.inputs[2].default_value = (*hf.get("color", color), 1.0)
        image = hmix.outputs[0]
    mul = nt.nodes.new("CompositorNodeMath")
    mul.operation = "MULTIPLY"
    mul.inputs[1].default_value = amount
    nt.links.new(despeckle(rl.outputs["Mist"]), mul.inputs[0])
    mix = nt.nodes.new("CompositorNodeMixRGB")
    mix.blend_type = "MIX"
    nt.links.new(mul.outputs[0], mix.inputs[0])
    nt.links.new(image, mix.inputs[1])
    mix.inputs[2].default_value = (*color, 1.0)
    last = mix.outputs[0]
    if glare:
        gl = nt.nodes.new("CompositorNodeGlare")
        gl.glare_type = "FOG_GLOW"
        gl.quality = "MEDIUM"
        gl.mix = -0.85
        gl.threshold = 0.8
        gl.size = 8
        nt.links.new(last, gl.inputs["Image"])
        last = gl.outputs["Image"]
    comp = nt.nodes.new("CompositorNodeComposite")
    nt.links.new(last, comp.inputs["Image"])


def far_terrain(sim, exclude_center, exclude_size, res=16.0, canopy=True, name="FarTerrain"):
    """Whole 8 km valley at coarse resolution around the detailed crop: canopy relief and colour from the
    simulation's tree masks (reads as forest under haze), water from the simulated river level."""
    import bpy
    from . import terrain as T
    from scipy import ndimage
    size = sim.size
    n = int(size / res) + 1
    xs = np.arange(n) * res
    X, Y = np.meshgrid(xs, xs)
    Z = sim.sample("height", X, Y, order=1)
    trees = np.clip(sim.sample("foliage_conifer", X, Y) + sim.sample("foliage_angio_tree", X, Y) + 0.5 * sim.sample("foliage_ginkgo", X, Y), 0, 1)
    trees = ndimage.gaussian_filter(trees, 1.0)
    water = sim.sample("water_mask", X, Y)
    rng = np.random.default_rng(5)
    bump = ndimage.gaussian_filter(rng.random(X.shape), 1.6)
    bump = (bump - bump.mean()) / bump.std()
    canopy_h = ndimage.gaussian_filter(np.clip(trees * 2.2, 0, 1), 1.5) * (22 + 1.5 * bump) * (water < 0.2)
    if canopy:
        Z = Z + canopy_h
    keep = np.ones((n - 1, n - 1), bool)
    if exclude_center is not None:
        h = exclude_size / 2 - res
        qx = (xs[:-1] + xs[1:]) / 2
        QX, QY = np.meshgrid(qx, qx)
        keep = ~((np.abs(QX - exclude_center[0]) < h) & (np.abs(QY - exclude_center[1]) < h))
    ox, oy = exclude_center
    ob = T._grid_object(name, X - ox, Y - oy, Z - 0.3, keep, (ox, oy, size), None)
    # vertex colours: forest canopy / open floodplain / sand / water
    col = np.zeros(X.shape + (3,))
    open_c = np.array([0.20, 0.22, 0.10])
    forest = np.array([0.045, 0.075, 0.03]) * (0.8 + 0.25 * bump[..., None])
    sand = np.array([0.42, 0.38, 0.28])
    wcol = np.array([0.03, 0.04, 0.035])
    sb = sim.sample("weight_sandbar", X, Y)
    t = np.clip(trees * 2.0, 0, 1)[..., None]
    col = open_c * (1 - t) + forest * t
    col = col * (1 - sb[..., None]) + sand * sb[..., None]
    col = col * (1 - water[..., None]) + wcol * water[..., None]
    me = ob.data
    attr = me.color_attributes.new("FarCol", "FLOAT_COLOR", "POINT")
    attr.data.foreach_set("color", np.concatenate([col.reshape(-1, 3), np.ones((n * n, 1))], 1).astype(np.float32).ravel())
    mat = bpy.data.materials.new("M_FarTerrain")
    mat.use_nodes = True
    a = mat.node_tree.nodes.new("ShaderNodeAttribute")
    a.attribute_name = "FarCol"
    p = mat.node_tree.nodes["Principled BSDF"]
    mat.node_tree.links.new(a.outputs["Color"], p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = 0.85
    me.materials.append(mat)
    return ob


def tune_leaf_materials(roughness=None, specular=None):
    """Scene-level tweak of the appended leaf materials (M_Leaf_*): at aerial distances the glossy highlights of
    millions of sub-pixel cards read as glitter under a grazing sun, so the aerial shot uses rougher leaves."""
    import bpy
    n = 0
    for m in bpy.data.materials:
        if not m.name.startswith("M_Leaf_") or not m.use_nodes:
            continue
        for node in m.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                if roughness is not None:
                    node.inputs["Roughness"].default_value = roughness
                if specular is not None:
                    node.inputs["Specular IOR Level"].default_value = specular
                n += 1
    return n


def _camera(sc, loc, target, lens=35.0, name="Camera", dof=None):
    import bpy
    from mathutils import Vector
    cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.location = Vector(loc)
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cam.data.lens = lens
    cam.data.sensor_width = 36.0
    cam.data.clip_start = 0.05
    cam.data.clip_end = 20000
    if dof:
        cam.data.dof.use_dof = True
        cam.data.dof.focus_distance = dof[0]
        cam.data.dof.aperture_fstop = dof[1]
    return cam


def _finish(sc, path, t_setup, extra):
    import bpy
    import resource
    sc.render.filepath = path
    t0 = time.time()
    r0 = resource.getrusage(resource.RUSAGE_SELF)
    bpy.ops.render.render(write_still=True)
    dt = time.time() - t0
    r1 = resource.getrusage(resource.RUSAGE_SELF)
    cpu = (r1.ru_utime - r0.ru_utime) + (r1.ru_stime - r0.ru_stime)
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = None
    # wall-clock time depends on the other jobs sharing the 4 CPUs: the CPU time and the load average are logged too
    info = dict(file=os.path.relpath(path, common.ROOT), render_seconds=round(dt, 1), setup_seconds=round(t_setup, 1),
                render_cpu_seconds=round(cpu, 1), load_average_at_end=load, threads=sc.render.threads,
                resolution=[sc.render.resolution_x * sc.render.resolution_percentage // 100,
                            sc.render.resolution_y * sc.render.resolution_percentage // 100],
                samples=sc.cycles.samples, **extra)
    common.write_json(os.path.splitext(path)[0] + ".json", info)
    common.log("render %s: %.1f s (setup %.1f s)" % (path, dt, t_setup))
    return info


# ----------------------------------------------------------------------------------------------- aerial
AERIAL = dict(center=(3600.0, 4800.0), size=3200.0, res=2.5,
              cam=(4420.0, 4300.0, 135.0), target=(3520.0, 4800.0, 0.0), lens=30.0,
              sun_elev=6.0, sun_az=250.0, exposure=-0.3, sun_strength=6.0, sky_strength=0.28, sun_color=(1.0, 0.64, 0.34),
              mist_color=(0.84, 0.58, 0.36), mist_amount=0.45, leaf_roughness=0.7, canopy_shade=0.6)


def render_aerial(samples=None, preview=False):
    import bpy
    from mathutils import Vector
    veg = _veg()
    t0 = time.time()
    common.reset_blender()
    sc = bpy.context.scene
    setup_render(sc, samples or (16 if preview else 24), preview=preview, threads=4)
    cfg = AERIAL
    sc.view_settings.exposure = cfg["exposure"]
    sc.cycles.diffuse_bounces = 1
    sc.cycles.glossy_bounces = 1
    sim = veg.sim_data()
    terrain = veg.build_terrain(cfg["center"], cfg["size"], res_m=cfg["res"], shading=True, water=True, mask_px=2048,
                                canopy_shade=cfg.get("canopy_shade", 0.0))
    far_terrain(sim, cfg["center"], cfg["size"])
    from . import terrain as T
    from . import terrain_shading as TS
    fw = T.build_water((4096.0, 4096.0), 8192.0, res_m=12.0, name="FarWater", sim=sim, material=TS.water_material("M_FarWater"))
    fw.location = (4096.0 - cfg["center"][0], 4096.0 - cfg["center"][1], -0.05)
    ox, oy = cfg["center"]
    cz = float(T.height_at(sim, np.array([cfg["cam"][0]]), np.array([cfg["cam"][1]]), 0)[0]) + cfg["cam"][2]
    tz = float(T.height_at(sim, np.array([cfg["target"][0]]), np.array([cfg["target"][1]]), 0)[0])
    cam = _camera(sc, (cfg["cam"][0] - ox, cfg["cam"][1] - oy, cz), (cfg["target"][0] - ox, cfg["target"][1] - oy, tz), lens=cfg["lens"])
    sky(sc, cfg["sun_elev"], cfg["sun_az"], sun_strength=cfg.get("sun_strength", 6.0), sky_strength=cfg.get("sky_strength", 0.28),
        air=1.3, dust=3.5, sun_color=cfg.get("sun_color", (1.0, 0.64, 0.34)))
    mist_compositor(sc, color=cfg.get("mist_color", (0.84, 0.58, 0.36)), amount=cfg.get("mist_amount", 0.45), start=200, depth=3800)
    stats = veg.scatter(terrain, camera=cam, seed=7, density=1.0, range_scale=1.0,
                        lod_distances={"tree": tuple(cfg.get("tree_lod", (90, 320)))}, budget=300000)
    # rough leaves: no sub-pixel specular glitter under the grazing sun
    tune_leaf_materials(roughness=cfg.get("leaf_roughness", 0.7), specular=0.2)
    sc.cycles.transparent_max_bounces = 16
    _cloud_layer(sc, altitude=1900.0, coverage=0.42)
    path = os.path.join(common.OUT_DIR, "valley_aerial%s.png" % ("_preview" if preview else ""))
    return _finish(sc, path, time.time() - t0, dict(scene="aerial", config=cfg, instances=stats))


# ----------------------------------------------------------------------------------------------- ground
# cam: explicit camera position (sim m) on the west riverbank of the meander at x~3850, y~4530 (riverbank habitat,
# horsetail + fern masks, 2.7 m above the simulated river level); yaw = compass azimuth of the view (deg),
# pitch < 0 looks down. cam=None falls back to the automatic search (_find_spot).
GROUND = dict(crop_size=900.0, res=1.5, detail_size=90.0, detail_res=0.1, sun_elev=8.0, sun_az=100.0, lens=26.0,
              eye=1.6, search_center=(3760.0, 4600.0), search_radius=450.0, spot=None,
              cam=(3851.0, 4528.0), yaw_deg=5.0, pitch_deg=-3.5, fog_density=0.0065, fog_height=2.5, mist_amount=0.45,
              mist_depth=650.0, fog_mode="analytic", fog_color=(0.86, 0.84, 0.80),
              fog_size=(320.0, 320.0, 8.0), volume_step_rate=16.0, volume_max_steps=32,  # fog_mode="volume" only
              transparent_max_bounces=16,
              # visible lateral half-width at distance d is ~0.69 d (26 mm lens); the frame bottom meets the ground at ~3.5 m
              fern_bed=((2.8, 1.3, "fern_tufted"), (3.5, 2.1, "fern_lace"), (4.5, 2.7, "fern_marsh"), (5.5, 3.3, "fern_tufted"),
                        (6.5, 2.2, "fern_lace"), (7.5, 4.4, "fern_tufted"), (4.0, 0.9, "fern_marsh"), (9.0, 5.2, "fern_marsh")))


def _find_spot(veg, sim, cfg):
    """Riverbank point with horsetails and ferns, trees within 30-120 m inland, on the camp side of the river."""
    from . import terrain as T
    from scipy import ndimage
    rng = np.random.default_rng(4)
    cx, cy = cfg["search_center"]
    R = cfg["search_radius"]
    pts = np.stack([rng.uniform(cx - R, cx + R, 6000), rng.uniform(cy - R, cy + R, 6000)], 1)
    rb = sim.sample("weight_riverbank", pts[:, 0], pts[:, 1])
    pts = pts[rb > 0.8]
    best, best_s = None, -1
    for p in pts[:600]:
        x, y = p
        hz = sim.sample("foliage_horsetail", np.array([x]), np.array([y]))[0]
        fe = sim.sample("foliage_fern", np.array([x]), np.array([y]))[0]
        # water direction: gradient of the water mask
        a = np.linspace(0, 2 * np.pi, 24, endpoint=False)
        ring = np.stack([x + 15 * np.cos(a), y + 15 * np.sin(a)], 1)
        wm = sim.sample("water_mask", ring[:, 0], ring[:, 1])
        if wm.max() < 0.5:
            continue
        wdir = np.array([np.sum(np.cos(a) * wm), np.sum(np.sin(a) * wm)])
        wdir /= np.linalg.norm(wdir) + 1e-9
        inland = p - wdir * 70
        ring2 = np.stack([inland[0] + 40 * np.cos(a), inland[1] + 40 * np.sin(a)], 1)
        trees = (sim.sample("foliage_conifer", ring2[:, 0], ring2[:, 1]) + sim.sample("foliage_angio_tree", ring2[:, 0], ring2[:, 1])).mean()
        s = hz * 1.5 + fe + trees * 1.2
        if s > best_s:
            best_s, best = s, (x, y, wdir)
    return best


def _detail_displacement(sim, crop_center, crop_size):
    """Micro-displacement of the near-camera patch from the ground height maps, blended by habitat."""
    from PIL import Image
    from . import ground as G
    from . import landscape as LS

    heights = {}
    for g in G.MATERIALS:
        p = os.path.join(common.GROUND_DIR, "T_Ground_%s_Height.png" % g)
        heights[g] = np.asarray(Image.open(p)).astype(np.float32) / 65535.0

    def fn(X, Y):
        acc = np.zeros_like(X)
        wsum = np.zeros_like(X) + 1e-4
        for layer, (g, habs) in LS.LAYERS.items():
            w = np.zeros_like(X)
            for h, k in habs.items():
                w += k * sim.sample("weight_" + h, X, Y)
            if not w.any():
                continue
            tile = G.MATERIALS[g][0]
            hm = heights[g]
            n = hm.shape[0]
            col = (np.mod(X / tile, 1.0) * n).astype(int) % n
            row = ((1 - np.mod(Y / tile, 1.0)) * n).astype(int) % n
            acc += w * (hm[row, col] - 0.5)
            wsum += w
        return 0.045 * acc / wsum
    return fn


def render_ground(samples=None, preview=False):
    import bpy
    from mathutils import Vector
    from . import terrain as T
    veg = _veg()
    t0 = time.time()
    common.reset_blender()
    sc = bpy.context.scene
    setup_render(sc, samples or (16 if preview else 20), preview=preview, threads=4)
    cfg = GROUND
    sim = veg.sim_data()
    if cfg.get("cam"):
        cam_xy = np.array(cfg["cam"], float)
        yaw = math.radians(cfg["yaw_deg"])
        view = np.array([math.sin(yaw), math.cos(yaw)])
        x, y = cam_xy
    else:
        x, y, wdir = cfg["spot"] or _find_spot(veg, sim, cfg)
        # camera 3 m inland from the bank point, looking along the bank, slightly towards the water
        tangent = np.array([-wdir[1], wdir[0]])
        view = tangent * math.cos(math.radians(28)) + wdir * math.sin(math.radians(28))
        cam_xy = np.array([x, y]) - wdir * 3.0
    center = (float(cam_xy[0] + view[0] * 250), float(cam_xy[1] + view[1] * 250))
    detail_c = (float(cam_xy[0] + view[0] * 30), float(cam_xy[1] + view[1] * 30))
    disp = _detail_displacement(sim, center, cfg["crop_size"])
    terrain = veg.build_terrain(center, cfg["crop_size"], res_m=cfg["res"], shading=True, water=True, mask_px=2048,
                                water_kw=dict(bump_strength=0.03, wave_scale=0.5),
                                detail=dict(center=detail_c, size=cfg["detail_size"], res=cfg["detail_res"], displace=disp))
    far_terrain(sim, center, cfg["crop_size"], res=16.0)
    ox, oy = center
    gz = float(T.height_at(sim, np.array([cam_xy[0]]), np.array([cam_xy[1]]))[0])
    cz = gz + cfg["eye"]
    pitch = math.radians(cfg.get("pitch_deg", 0.0))
    tgt = cam_xy + view * 60 * math.cos(pitch)
    tz = cz + 60 * math.sin(pitch)
    cam = _camera(sc, (cam_xy[0] - ox, cam_xy[1] - oy, cz), (tgt[0] - ox, tgt[1] - oy, tz), lens=cfg["lens"],
                  dof=(14.0, 8.0))
    sky(sc, cfg["sun_elev"], cfg["sun_az"], sun_strength=3.8, sky_strength=0.5, air=1.0, dust=4.0, sun_color=(1.0, 0.80, 0.58))
    # low morning fog lying on the river and the banks, height falloff from the water level.
    # "analytic" (default): compositor height fog from the Position / Depth passes, noise-free and free;
    # "volume": heterogeneous Cycles volume (wisps, light scattering) but slow and grainy at low sample counts.
    wl = float(sim.sample("water_surface", np.array([cam_xy[0] + view[0] * 40]), np.array([cam_xy[1] + view[1] * 40]))[0])
    fog_mode = cfg.get("fog_mode", "analytic")
    hfog = None
    if fog_mode == "analytic":
        hfog = dict(z0=wl - 0.3, cam_z=cz, density=cfg.get("fog_density", 0.0065), height=cfg.get("fog_height", 2.5),
                    color=cfg.get("fog_color", (0.86, 0.84, 0.80)))
    mist_compositor(sc, color=(0.85, 0.82, 0.77), amount=cfg.get("mist_amount", 0.5), start=8, depth=cfg.get("mist_depth", 650),
                    glare=True, height_fog=hfog)
    if fog_mode == "volume":
        fsx, fsy, fsz = cfg.get("fog_size", (560.0, 560.0, 12.0))
        _ground_fog(sc, center=(cam_xy[0] - ox + view[0] * (fsy / 2 - 30), cam_xy[1] - oy + view[1] * (fsy / 2 - 30)), z0=wl - 1.0,
                    size=(fsx, fsy, fsz), density=cfg.get("fog_density", 0.0065), height=cfg.get("fog_height", 2.5))
        # coarse volume marching: the fog is soft, the cost is in the long grazing rays through the layer
        sc.cycles.volume_step_rate = cfg.get("volume_step_rate", 4.0)
        sc.cycles.volume_max_steps = cfg.get("volume_max_steps", 128)
    sc.cycles.transparent_max_bounces = cfg.get("transparent_max_bounces", 24)
    # trees, palms and shrubs kept out of the first metres of the lens axis (ferns / horsetails may grow there)
    big = {"tree", "palm", "shrub", "deadwood"}
    lens_clear = [(float((cam_xy + view * d)[0]), float((cam_xy + view * d)[1]), r, big)
                  for d, r in ((3.0, 3.5), (8.0, 4.5), (15.0, 5.5), (24.0, 6.0))]
    lens_clear.append((float(cam_xy[0]), float(cam_xy[1]), 9.0, {"deadwood"}))  # no root plate in the lens
    # foreground art direction: a few hand-placed horsetail stands and ferns 2-7 m in front of the camera
    rng = np.random.default_rng(11)
    extra = []
    side = np.array([-view[1], view[0]])
    wl_here = float(sim.sample("water_surface", np.array([x]), np.array([y]))[0])
    for k in range(11):
        d = rng.uniform(2.5, 9.0)
        lat = rng.uniform(-2.0, 3.8) * (d / 6)  # side points left: mostly on the left (inland) side of the frame
        p = cam_xy + view * d + side * lat
        if float(T.height_at(sim, p[:1], p[1:])[0]) < wl_here + 0.3:
            continue  # not in the water
        extra.append(dict(asset=["horsetail_unbranched", "horsetail_branched", "fern_tufted", "fern_marsh"][k % 4],
                          x=float(p[0]), y=float(p[1]), scale=rng.uniform(0.9, 1.15)))
    # fern bed on the inland (left) side of the frame: (distance m, lateral m to the left, asset)
    for d, lat, aid in cfg.get("fern_bed", ()):
        p = cam_xy + view * d + side * lat
        if float(T.height_at(sim, p[:1], p[1:])[0]) < wl_here + 0.3:
            continue
        extra.append(dict(asset=aid, x=float(p[0]), y=float(p[1]), scale=rng.uniform(0.95, 1.25)))
    stats = veg.scatter(terrain, camera=cam, seed=7, density=1.0, range_scale=1.0, keep_clear=[(cam_xy[0], cam_xy[1], 1.2)] + lens_clear,
                        extra=extra, lod_distances={"tree": (80, 300), "fern": (25, 70), "horsetail": (25, 80)}, budget=250000)
    path = os.path.join(common.OUT_DIR, "valley_ground%s.png" % ("_preview" if preview else ""))
    return _finish(sc, path, time.time() - t0, dict(scene="ground", config={k: v for k, v in cfg.items()}, camera_sim_m=[float(cam_xy[0]), float(cam_xy[1])],
                                                    view_dir=[float(view[0]), float(view[1])], instances=stats,
                                                    note="%d plantes de premier plan placées à la main (direction artistique) ; le reste suit les masques." % len(extra)))


def _cloud_layer(sc, altitude=1900.0, coverage=0.4, size=40000.0):
    """Thin procedural cloud deck (one alpha layer): catches the low sun, cheap in Cycles."""
    import bpy
    import bmesh
    me = bpy.data.meshes.new("Clouds")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=size / 2)
    bm.to_mesh(me)
    ob = bpy.data.objects.new("Clouds", me)
    sc.collection.objects.link(ob)
    ob.location.z = altitude
    ob.visible_shadow = False
    mat = bpy.data.materials.new("M_Clouds")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 2.6, 1.0)  # streaky altocumulus
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 0.00045
    nz.inputs["Detail"].default_value = 8.0
    nz.inputs["Roughness"].default_value = 0.62
    nt.links.new(mp.outputs["Vector"], nz.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeMapRange")
    ramp.inputs["From Min"].default_value = 1.0 - coverage
    ramp.inputs["From Max"].default_value = 1.0 - coverage + 0.18
    nt.links.new(nz.outputs["Fac"], ramp.inputs["Value"])
    tr = nt.nodes.new("ShaderNodeBsdfTranslucent")
    tr.inputs["Color"].default_value = (1.0, 0.92, 0.85, 1)
    df = nt.nodes.new("ShaderNodeBsdfDiffuse")
    df.inputs["Color"].default_value = (0.95, 0.9, 0.88, 1)
    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.inputs[0].default_value = 0.5
    nt.links.new(df.outputs[0], mix.inputs[1])
    nt.links.new(tr.outputs[0], mix.inputs[2])
    tp = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix2 = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(ramp.outputs["Result"], mix2.inputs[0])
    nt.links.new(tp.outputs[0], mix2.inputs[1])
    nt.links.new(mix.outputs[0], mix2.inputs[2])
    nt.links.new(mix2.outputs[0], out.inputs["Surface"])
    me.materials.append(mat)
    return ob


def _ground_fog(sc, center, z0, size, density=0.03, height=5.0):
    import bpy
    import bmesh
    me = bpy.data.meshes.new("GroundFog")
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bm.to_mesh(me)
    ob = bpy.data.objects.new("GroundFog", me)
    sc.collection.objects.link(ob)
    ob.scale = size
    ob.location = (center[0], center[1], z0 + size[2] / 2)
    mat = bpy.data.materials.new("M_GroundFog")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (0.92, 0.92, 0.9, 1)
    vol.inputs["Anisotropy"].default_value = 0.6
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Object"], sep.inputs["Vector"])
    # object Z in [-0.5, 0.5] -> density falls off with height above the bottom
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value = -0.5
    mr.inputs["From Max"].default_value = -0.5 + height / size[2] + 0.2
    mr.inputs["To Min"].default_value = density
    mr.inputs["To Max"].default_value = 0.0
    nt.links.new(sep.outputs["Z"], mr.inputs["Value"])
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6.0
    nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
    mul = nt.nodes.new("ShaderNodeMath")
    mul.operation = "MULTIPLY"
    nt.links.new(mr.outputs["Result"], mul.inputs[0])
    nt.links.new(noise.outputs["Fac"], mul.inputs[1])
    mul2 = nt.nodes.new("ShaderNodeMath")
    mul2.operation = "MULTIPLY"
    mul2.inputs[1].default_value = 1.6
    nt.links.new(mul.outputs[0], mul2.inputs[0])
    nt.links.new(mul2.outputs[0], vol.inputs["Density"])
    nt.links.new(vol.outputs[0], out.inputs["Volume"])
    me.materials.append(mat)
    return ob
