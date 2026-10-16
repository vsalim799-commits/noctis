"""Stage 'bake': texture maps of the low-poly body from the exact high-poly (implicit) surface.

For every texel covered by the UV layout (numba rasteriser of the low-poly triangles in UV space):
  1. low-poly position / smooth normal / MikkTSpace tangent + sign are interpolated (Blender
     calc_tangents, same convention as the FBX/GLB exports and UE5's MikkTSpace import);
  2. a ray from the texel along the low-poly normal is intersected with the sculpt SDF (cage-free:
     Newton/secant along the ray, both directions), giving the smooth high surface point x0 and n0;
  3. the skin relief of lib/skin.py is evaluated at x0 with its tangential gradient (finite
     differences at half a texel, filter width = texel size -> no aliasing): displaced normal
     n = normalize(n0 - grad_t disp);
  4. tangent-space normal = (n.T, n.B, n.N) with B = sign * N x T (OpenGL / +Y convention);
  5. AO = SDF ambient occlusion (macro) x relief cavities (micro); albedo, roughness, subsurface
     weight are synthesised from region scores, masks, relief channels and the palette (SPÉCULATIF).
Uncovered texels are filled with the nearest covered value (infinite padding, mip-safe).

Outputs (Build/Creatures/<sp>/textures/): T_<sp>_BaseColor.png (sRGB), _Normal.png (OpenGL, +Y),
_Normal_DX.png (DirectX, -Y, for UE5 if 'flip green' is not used), _ORM.png (R AO, G roughness,
B metallic=0), _Height.png (16 bit, range in the metadata), _SSS.png (subsurface weight),
_Cavity.png, _Masks.png (R scutes/plates, G rugose/feature scales, B soft skin lips/lids/throat),
T_<sp>_Eye_BaseColor.png (iris, 1024).
"""
from __future__ import annotations

import math
import os
import time
import numpy as np
from numba import njit, prange

from .sdf_stage import compile_body
from . import skin
from .skin import fbm as _fbm, vnoise as _vnoise
from .sdf import field_at


# ----------------------------------------------------------------------------------------------
@njit(cache=True)
def raster(UV, tri, size, tid, bu, bv):
    """UV in [0,1]^2 per corner (ntri,3,2). Writes the triangle id and barycentrics of each texel
    centre (with a 0.75 px tolerance outside the triangle; barycentrics clamped)."""
    for t in range(tri.shape[0]):
        x0 = UV[t, 0, 0] * size
        y0 = UV[t, 0, 1] * size
        x1 = UV[t, 1, 0] * size
        y1 = UV[t, 1, 1] * size
        x2 = UV[t, 2, 0] * size
        y2 = UV[t, 2, 1] * size
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-12:
            continue
        xmin = max(0, int(math.floor(min(x0, min(x1, x2)) - 1)))
        xmax = min(size - 1, int(math.ceil(max(x0, max(x1, x2)) + 1)))
        ymin = max(0, int(math.floor(min(y0, min(y1, y2)) - 1)))
        ymax = min(size - 1, int(math.ceil(max(y0, max(y1, y2)) + 1)))
        # tolerance in barycentric units ~ 0.75 px / triangle height
        e0 = math.sqrt((x1 - x2) ** 2 + (y1 - y2) ** 2)
        e1 = math.sqrt((x2 - x0) ** 2 + (y2 - y0) ** 2)
        e2 = math.sqrt((x0 - x1) ** 2 + (y0 - y1) ** 2)
        area2 = abs(den)
        tol0 = 0.75 * e0 / area2
        tol1 = 0.75 * e1 / area2
        tol2 = 0.75 * e2 / area2
        for py in range(ymin, ymax + 1):
            for px in range(xmin, xmax + 1):
                cx = px + 0.5
                cy = py + 0.5
                l0 = ((y1 - y2) * (cx - x2) + (x2 - x1) * (cy - y2)) / den
                l1 = ((y2 - y0) * (cx - x2) + (x0 - x2) * (cy - y2)) / den
                l2 = 1.0 - l0 - l1
                inside = l0 >= 0 and l1 >= 0 and l2 >= 0
                near = l0 >= -tol0 and l1 >= -tol1 and l2 >= -tol2
                if inside or (near and tid[py, px] < 0):
                    if not inside:
                        l0 = max(l0, 0.0)
                        l1 = max(l1, 0.0)
                        l2 = max(l2, 0.0)
                        s = l0 + l1 + l2
                        l0 /= s
                        l1 /= s
                    tid[py, px] = t
                    bu[py, px] = l0
                    bv[py, px] = l1


@njit(parallel=True, cache=True, fastmath=True)
def ray_to_surface(P, D, out_s, maxd, kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems):
    """Signed distance s along D (unit) from P to the SDF zero set: sample [-maxd, maxd], take the
    crossing nearest to s = 0, refine by secant."""
    n = P.shape[0]
    for j in prange(n):
        best = 1e9
        bs = 0.0
        ns = 24
        prev_s = -maxd
        prev_f = field_at(kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems, P[j, 0] - D[j, 0] * maxd, P[j, 1] - D[j, 1] * maxd, P[j, 2] - D[j, 2] * maxd)
        for i in range(1, ns + 1):
            s = -maxd + 2 * maxd * i / ns
            f = field_at(kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems, P[j, 0] + D[j, 0] * s, P[j, 1] + D[j, 1] * s, P[j, 2] + D[j, 2] * s)
            if (prev_f < 0) != (f < 0):
                a = prev_s
                b = s
                fa = prev_f
                fb = f
                for it in range(12):
                    m = b - fb * (b - a) / (fb - fa)
                    fm = field_at(kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems, P[j, 0] + D[j, 0] * m, P[j, 1] + D[j, 1] * m, P[j, 2] + D[j, 2] * m)
                    if (fm < 0) == (fa < 0):
                        a = m
                        fa = fm
                    else:
                        b = m
                        fb = fm
                    if abs(fm) < 1e-6:
                        break
                if abs(m) < abs(best):
                    best = m
                    bs = m
            prev_s = s
            prev_f = f
        out_s[j] = bs if best < 1e8 else 0.0


@njit(parallel=True, cache=True, fastmath=True)
def sdf_ao(P, N, out, kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems):
    for j in prange(P.shape[0]):
        occ = 0.0
        tot = 0.0
        for s in range(1, 7):
            h = 0.015 * s * s
            d = field_at(kind, sign, k, PP, LT, SW, g0, cell, dims, cstart, citems, P[j, 0] + N[j, 0] * h, P[j, 1] + N[j, 1] * h, P[j, 2] + N[j, 2] * h)
            w = 1.0 / s
            occ += w * max(0.0, (h - d) / h)
            tot += w
        out[j] = max(0.0, 1.0 - 1.1 * occ / tot)


@njit(parallel=True, cache=True, fastmath=True)
def noise_field(P, scale, seed, octaves, out):
    for j in prange(P.shape[0]):
        out[j] = _fbm(P[j, 0] / scale, P[j, 1] / scale, P[j, 2] / scale, seed, octaves)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# ----------------------------------------------------------------------------------------------
def lowpoly_data(me, bl):
    """Triangles with per-corner position (anatomy space), uv, normal, tangent, sign."""
    me.calc_tangents(uvmap=me.uv_layers.active.name)
    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    LI = np.empty(nt * 3, np.int32)
    me.loop_triangles.foreach_get("loops", LI)
    VI = np.empty(nt * 3, np.int32)
    me.loop_triangles.foreach_get("vertices", VI)
    nl = len(me.loops)
    Ln = np.empty(nl * 3, np.float32)
    me.loops.foreach_get("normal", Ln)
    Lt = np.empty(nl * 3, np.float32)
    me.loops.foreach_get("tangent", Lt)
    Ls = np.empty(nl, np.float32)
    me.loops.foreach_get("bitangent_sign", Ls)
    UV = np.empty(nl * 2, np.float32)
    me.uv_layers.active.data.foreach_get("uv", UV)
    Vb = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", Vb)
    V = bl.from_bl(Vb.reshape(-1, 3).astype(np.float64))
    Ln = bl.from_bl(Ln.reshape(-1, 3).astype(np.float64))
    Lt = bl.from_bl(Lt.reshape(-1, 3).astype(np.float64))
    return dict(LI=LI.reshape(-1, 3), VI=VI.reshape(-1, 3), V=V, N=Ln, T=Lt, S=Ls, UV=UV.reshape(-1, 2).astype(np.float64))


def palette_albedo(pal, P, N0, reg, M, ch, extra):
    """Linear albedo, roughness, SSS weight from the anatomy-space point data (all SPÉCULATIF)."""
    from .sdf import REGIONS
    R = {r: reg[:, i] for i, r in enumerate(REGIONS)}
    c = lambda k: np.asarray(pal[k], float)[None, :]
    ny = N0[:, 1]
    top, rnd, crease, feat, scute = ch[:, 1], ch[:, 2], ch[:, 3], ch[:, 4], ch[:, 5]
    rs = reg.sum(1) + 1e-6
    head = (R["head"] + R["jaw"]) / rs
    limb = (R["shin"] + R["foot"] + R["arm"]) / rs
    tail = R["tail"] / rs
    # countershading: ventral -> flank -> dorsal by normal and by height above the local belly line
    col = c("ventral") * (1 - smoothstep(-0.65, 0.05, ny))[:, None] + c("flank") * smoothstep(-0.65, 0.05, ny)[:, None]
    dor = smoothstep(-0.05, 0.6, ny)
    col = col * (1 - dor)[:, None] + (c("dorsal") * 0.88) * dor[:, None]
    # head: darker crown, lighter lips / jaw margin
    ht = head * smoothstep(0.2, 0.8, ny)
    col = col * (1 - ht)[:, None] + c("head_top") * ht[:, None]
    lip = np.clip(M[:, 8], 0, 1)
    col = col * (1 - 0.7 * lip)[:, None] + c("lip") * (0.7 * lip)[:, None]
    # mottling (two scales of fBm), stronger dorsally
    n1, n2 = extra["n1"], extra["n2"]
    mot = smoothstep(0.08, 0.42, n1 + 0.45 * n2) * (0.35 + 0.65 * smoothstep(-0.3, 0.6, ny)) * (1 - 0.6 * head)
    col = col * (1 - 0.65 * mot)[:, None] + c("mottle_dark") * (0.65 * mot)[:, None]
    # faint transverse dorsal bands on the tail and the back (speculative)
    x = P[:, 0]
    band = smoothstep(0.25, 0.75, 0.5 + 0.5 * np.sin(2 * np.pi * (x + 0.15 * n2) / 0.62)) * smoothstep(-1.0, -1.6, x) * smoothstep(0.0, 0.6, ny)
    col = col * (1 - 0.45 * band)[:, None] + c("band_dark") * (0.45 * band)[:, None]
    # limbs: darker toward the feet, scutes and pads
    lowleg = smoothstep(1.1, 0.4, P[:, 1]) * limb
    col = col * (1 - 0.4 * lowleg)[:, None] + c("scute") * (0.4 * lowleg)[:, None]
    col = col * (1 - scute)[:, None] + c("scute") * (scute * (0.85 + 0.3 * rnd))[:, None]
    pad = np.clip(M[:, 5], 0, 1) * smoothstep(0.2, -0.4, ny)
    col = col * (1 - pad)[:, None] + c("pad") * pad[:, None]
    rug = np.clip(M[:, 6], 0, 1)
    col = col * (1 - 0.6 * rug)[:, None] + c("rugose") * (0.6 * rug)[:, None]
    # per-scale variation, lighter worn tops, dark grooves and creases
    col *= (0.86 + 0.28 * rnd)[:, None]
    col *= (0.70 + 0.30 * top)[:, None]
    col *= (1 + 0.18 * feat)[:, None]
    col *= (1 - 0.3 * crease)[:, None]
    # dried mud on the feet (speculative grime)
    mud = smoothstep(0.55, 0.05, P[:, 1]) * smoothstep(-0.2, 0.3, extra["n3"])
    col = col * (1 - 0.35 * mud)[:, None] + np.array([[0.13, 0.11, 0.085]]) * (0.35 * mud)[:, None]
    col = np.clip(col, 0.01, 0.9)
    # roughness
    rough = 0.68 - 0.08 * top + 0.12 * (1 - top) + 0.1 * crease - 0.04 * scute - 0.1 * lip - 0.05 * np.clip(M[:, 7], 0, 1) + 0.06 * rug + 0.04 * extra["n2"]
    rough = np.clip(rough + 0.08 * mud, 0.3, 0.92)
    # subsurface weight (soft thin skin high, plates low)
    soft = np.clip(np.maximum.reduce([lip, np.clip(M[:, 7], 0, 1), np.clip(M[:, 1], 0, 1), np.clip(M[:, 11], 0, 1)]), 0, 1)
    sss = np.clip(0.35 + 0.5 * soft + 0.15 * smoothstep(0.0, -0.7, ny) - 0.3 * scute - 0.25 * rug - 0.2 * (1 - top), 0.05, 1.0)
    masks = np.stack([np.maximum(scute, pad), np.maximum(rug, feat), soft], 1)
    return col, rough, sss, masks


def to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def fill_uncovered(img, covered):
    from scipy.ndimage import distance_transform_edt
    _, (iy, ix) = distance_transform_edt(~covered, return_indices=True)
    return img[iy, ix]


def eye_texture(pal, size=1024, seed=3):
    """Iris on the polar eye UV layout (lib/accessories.eye_mesh): UV radius = 0.5 * polar angle / pi."""
    yy, xx = np.mgrid[0:size, 0:size]
    u = (xx + 0.5) / size - 0.5
    v = (yy + 0.5) / size - 0.5
    r = np.sqrt(u * u + v * v) * 2 * 180.0          # polar angle in degrees from the cornea centre
    ang = np.arctan2(v, u)
    rng = np.random.default_rng(seed)
    fib = np.zeros_like(r)
    for f, a in ((23, 0.5), (47, 0.3), (91, 0.2)):
        ph = rng.uniform(0, 2 * np.pi)
        fib += a * np.sin(f * ang + ph + 0.15 * r)
    iris = np.asarray(pal["iris"])[None, None, :] * (0.75 + 0.25 * fib[..., None])
    iris *= (0.65 + 0.5 * smoothstep(9, 30, r))[..., None]            # lighter towards the rim
    col = iris
    limbus = smoothstep(30, 38, r)
    sclera = np.array([0.09, 0.07, 0.05])
    col = col * (1 - limbus[..., None]) + sclera * limbus[..., None]
    pupil = smoothstep(10.5, 8.5, r)                                   # round pupil (speculative)
    col = col * (1 - pupil[..., None]) + np.array([0.004, 0.004, 0.004]) * pupil[..., None]
    from PIL import Image
    return Image.fromarray((to_srgb(col) * 255).astype(np.uint8)[::-1])


def run(ctx):
    import bpy
    from PIL import Image
    from . import bl
    t00 = time.time()
    doc = ctx.anatomy()
    comp = compile_body(doc)
    bpy.ops.wm.open_mainfile(filepath=ctx.path("uv.blend"))
    ob = bpy.data.objects["body"]
    me = ob.data
    L = lowpoly_data(me, bl)
    size = ctx.q["tex"]
    LI, VI = L["LI"], L["VI"]
    UVt = L["UV"][LI]                                   # (nt, 3, 2)
    tid = -np.ones((size, size), np.int32)
    bu = np.zeros((size, size), np.float32)
    bv = np.zeros((size, size), np.float32)
    raster(np.ascontiguousarray(UVt), VI, size, tid, bu, bv)
    cov = tid >= 0
    ctx.log(f"  raster {size}^2: {cov.mean()*100:.1f}% texels covered, {time.time()-t00:.1f}s")
    ys, xs = np.nonzero(cov)
    T = tid[ys, xs]
    w0 = bu[ys, xs].astype(np.float64)
    w1 = bv[ys, xs].astype(np.float64)
    w2 = 1 - w0 - w1
    W = np.stack([w0, w1, w2], 1)
    # texel world size per triangle
    Pt = L["V"][VI]
    a3 = 0.5 * np.linalg.norm(np.cross(Pt[:, 1] - Pt[:, 0], Pt[:, 2] - Pt[:, 0]), axis=1)
    du = UVt[:, 1] - UVt[:, 0]
    dv = UVt[:, 2] - UVt[:, 0]
    au = 0.5 * np.abs(du[:, 0] * dv[:, 1] - du[:, 1] * dv[:, 0])
    texel_m = np.sqrt(a3 / np.maximum(au * size * size, 1e-12))
    ctx.log(f"  texel size: median {np.median(texel_m)*1000:.2f} mm, p5 {np.percentile(texel_m,5)*1000:.2f}, p95 {np.percentile(texel_m,95)*1000:.2f} mm")
    n = len(T)
    chunk = 1_200_000
    out_n = np.zeros((n, 3), np.float32)
    out_col = np.zeros((n, 3), np.float32)
    out_r = np.zeros(n, np.float32)
    out_ao = np.zeros(n, np.float32)
    out_h = np.zeros(n, np.float32)
    out_sss = np.zeros(n, np.float32)
    out_cav = np.zeros(n, np.float32)
    out_m = np.zeros((n, 3), np.float32)
    pal = doc["palette"]
    for s0 in range(0, n, chunk):
        sl = slice(s0, min(n, s0 + chunk))
        t0 = time.time()
        tri = T[sl]
        Wc = W[sl]
        li = LI[tri]
        def interp(A):
            return (A[li[:, 0]] * Wc[:, 0:1] + A[li[:, 1]] * Wc[:, 1:2] + A[li[:, 2]] * Wc[:, 2:3])
        vi = VI[tri]
        P = L["V"][vi[:, 0]] * Wc[:, 0:1] + L["V"][vi[:, 1]] * Wc[:, 1:2] + L["V"][vi[:, 2]] * Wc[:, 2:3]
        Nl = interp(L["N"])
        Nl /= np.linalg.norm(Nl, axis=1, keepdims=True) + 1e-12
        Tl = interp(L["T"])
        Tl -= (Tl * Nl).sum(1)[:, None] * Nl
        Tl /= np.linalg.norm(Tl, axis=1, keepdims=True) + 1e-12
        sg = np.sign(L["S"][li[:, 0]])
        sg[sg == 0] = 1
        Bl = sg[:, None] * np.cross(Nl, Tl)
        # 2. ray to the smooth sculpt
        sv = np.zeros(len(P))
        ray_to_surface(np.ascontiguousarray(P), np.ascontiguousarray(Nl), sv, 0.06, *comp.args())
        X0 = P + Nl * sv[:, None]
        G = comp.gradient(X0, eps=5e-4)
        N0 = G / (np.linalg.norm(G, axis=1, keepdims=True) + 1e-12)
        t1 = Tl - (Tl * N0).sum(1)[:, None] * N0
        t1 /= np.linalg.norm(t1, axis=1, keepdims=True) + 1e-12
        t2 = np.cross(N0, t1)
        fw = texel_m[tri]
        # 3. relief with gradient; filter width = texel size (per chunk: use the chunk median, the
        # head charts have their own density so split by texel size class)
        ch = np.zeros((len(P), skin.NCH))
        gr = np.zeros((len(P), 2))
        info = None
        for lo_, hi_ in ((0, 0.0018), (0.0018, 0.0026), (0.0026, 1.0)):
            m = (fw >= lo_) & (fw < hi_)
            if not m.any():
                continue
            fwm = float(np.median(fw[m]))
            c_, inf_ = skin.evaluate(comp, doc, X0[m], N0[m], grad_tangents=(t1[m], t2[m]), eps=0.5 * fwm, log=lambda *a: None, filter_width=fwm)
            ch[m] = c_
            gr[m] = inf_["grad"]
            if info is None:
                info = {k: np.zeros((len(P),) + v.shape[1:], v.dtype) for k, v in inf_.items() if k in ("reg", "masks")}
            info["reg"][m] = inf_["reg"]
            info["masks"][m] = inf_["masks"]
        nh = N0 - gr[:, 0:1] * t1 - gr[:, 1:2] * t2
        nh /= np.linalg.norm(nh, axis=1, keepdims=True)
        tn = np.stack([(nh * Tl).sum(1), (nh * Bl).sum(1), (nh * Nl).sum(1)], 1)
        tn /= np.linalg.norm(tn, axis=1, keepdims=True)
        out_n[sl] = tn
        # 4. AO, noise fields, albedo
        ao = np.zeros(len(P))
        sdf_ao(np.ascontiguousarray(X0), np.ascontiguousarray(N0), ao, *comp.args())
        micro = (0.72 + 0.28 * ch[:, 1]) * (1 - 0.5 * ch[:, 3])
        out_ao[sl] = np.clip(ao, 0, 1) * micro
        ex = {}
        for nm, sc_, sd in (("n1", 0.45, 201), ("n2", 0.13, 211), ("n3", 0.2, 221)):
            a = np.zeros(len(P))
            noise_field(np.ascontiguousarray(X0), sc_, sd, 4, a)
            ex[nm] = a
        col, rough, sss, msk = palette_albedo(pal, X0, N0, info["reg"], info["masks"], ch, ex)
        out_col[sl] = col
        out_r[sl] = rough
        out_sss[sl] = sss
        out_m[sl] = msk
        out_h[sl] = ch[:, 0] + sv * 0.0
        out_cav[sl] = np.clip(1 - (0.55 * (1 - ch[:, 1]) + 0.6 * ch[:, 3]), 0, 1)
        ctx.log(f"  texels {sl.stop}/{n}: {time.time()-t0:.1f}s")
    # assemble images
    tex = ctx.path("textures")
    os.makedirs(tex, exist_ok=True)
    sp = ctx.species
    def img(vals, ch_):
        a = np.zeros((size, size, ch_) if ch_ > 1 else (size, size), np.float32)
        a[ys, xs] = vals
        a = fill_uncovered(a, cov)
        return a[::-1]                                 # UV v=0 at the bottom
    files = {}
    a = img(to_srgb(out_col), 3)
    Image.fromarray((a * 255 + 0.5).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_BaseColor.png"))
    nm = img(out_n * 0.5 + 0.5, 3)
    Image.fromarray((np.clip(nm, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_Normal.png"))
    nm[..., 1] = 1 - nm[..., 1]
    Image.fromarray((np.clip(nm, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_Normal_DX.png"))
    orm = np.stack([img(out_ao, 1), img(out_r, 1), np.zeros((size, size), np.float32)], 2)
    Image.fromarray((np.clip(orm, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_ORM.png"))
    hmin, hmax = float(out_h.min()), float(out_h.max())
    hh = img((out_h - hmin) / (hmax - hmin), 1)
    Image.fromarray((np.clip(hh, 0, 1) * 65535).astype(np.uint16)).save(os.path.join(tex, f"T_{sp}_Height.png"))
    Image.fromarray((np.clip(img(out_sss, 1), 0, 1) * 255).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_SSS.png"))
    Image.fromarray((np.clip(img(out_cav, 1), 0, 1) * 255).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_Cavity.png"))
    Image.fromarray((np.clip(img(out_m, 3), 0, 1) * 255).astype(np.uint8)).save(os.path.join(tex, f"T_{sp}_Masks.png"))
    eye_texture(pal).save(os.path.join(tex, f"T_{sp}_Eye_BaseColor.png"))
    st = dict(size=size, covered_pct=round(float(cov.mean() * 100), 1), height_range_m=[hmin, hmax],
              texel_mm_median=round(float(np.median(texel_m) * 1000), 2),
              maps=sorted(os.listdir(tex)), normal_convention="OpenGL (+Y) in _Normal.png, DirectX (-Y) in _Normal_DX.png")
    ctx.save_json("bake_stats.json", st)
    ctx.log(f"  bake: {st}")
    # small previews of the maps
    prev = []
    for nm_ in ("BaseColor", "Normal", "ORM"):
        im = Image.open(os.path.join(tex, f"T_{sp}_{nm_}.png")).convert("RGB").resize((1024, 1024))
        prev.append(im)
    sheet = Image.new("RGB", (3072, 1024))
    for i, im in enumerate(prev):
        sheet.paste(im, (1024 * i, 0))
    sheet.save(ctx.opath("stages", "bake_maps.png"))
