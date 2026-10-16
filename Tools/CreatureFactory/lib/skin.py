"""Procedural skin relief (numba): the displacement field shared by the 'detail' stage (real
displacement of the dense high-poly mesh) and the 'bake' stage (texel-exact normal/height/cavity maps
from the same function), so the baked maps match the sculpt.

disp(p, n, region scores, masks, dominant bone) -> metres along the outward normal, plus channels used
by the albedo/roughness synthesis:
    0 disp          total displacement (m)
    1 top           0 in scale grooves .. 1 on scale tops
    2 rnd           per-scale random value (cell hash) in [0, 1)
    3 crease        0..1 depth of folds/wrinkles creases
    4 feature       0..1 feature-scale / rugose plate coverage
    5 scute         0..1 scute plate coverage

Layers (parameters in Data/Anatomy/<sp>.json 'skin'):
  basement scales  three Worley layers (fine/medium/large cells), weights per region, dorsal->coarser,
                   ventral->finer; dome profile from the F2-F1 edge distance (non-imbricate pebbles)
  micro scales     lids / lips (very small cells)
  feature scales   sparse larger round tubercles (design assumption)
  rugose plates    cornified irregular plates over the skull rugosities
  folds            neck (dorso-ventral folds across the neck axis), throat (longitudinal), joints (rings)
  wrinkles         ridged value-noise fBm on flanks/thighs
  scutes           transverse plates on the front of the metatarsus and the top of the toes, pads below
"""
from __future__ import annotations

import math
import numpy as np
from numba import njit, prange

from .sdf import REGIONS

MASKS = ["neck_folds", "throat", "joint_folds", "scutes_meta", "scutes_toe", "pads", "rugose", "lid", "lip", "brow",
         "nostril", "jowl", "arm"]
NCH = 6


# ----------------------------------------------------------------------------------------------
@njit(cache=True, fastmath=True)
def _hash(ix, iy, iz, s):
    h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791) ^ (s * 2654435761)
    h = h & 0xFFFFFFFF
    h = ((h >> 16) ^ h) * 0x45D9F3B
    h = h & 0xFFFFFFFF
    h = ((h >> 16) ^ h) * 0x45D9F3B
    h = h & 0xFFFFFFFF
    h = (h >> 16) ^ h
    return (h & 0xFFFFFF) / 16777216.0


@njit(cache=True, fastmath=True)
def worley(x, y, z, seed):
    """F1, F2 (cell units) and the hash of the nearest feature point (jittered grid, 27 cells)."""
    ix = math.floor(x)
    iy = math.floor(y)
    iz = math.floor(z)
    f1 = 1e9
    f2 = 1e9
    hid = 0.0
    for dx in range(-1, 2):
        for dy in range(-1, 2):
            for dz in range(-1, 2):
                cx = ix + dx
                cy = iy + dy
                cz = iz + dz
                px = cx + 0.08 + 0.84 * _hash(cx, cy, cz, seed)
                py = cy + 0.08 + 0.84 * _hash(cx, cy, cz, seed + 1)
                pz = cz + 0.08 + 0.84 * _hash(cx, cy, cz, seed + 2)
                d = math.sqrt((px - x) ** 2 + (py - y) ** 2 + (pz - z) ** 2)
                if d < f1:
                    f2 = f1
                    f1 = d
                    hid = _hash(cx, cy, cz, seed + 3)
                elif d < f2:
                    f2 = d
    return f1, f2, hid


@njit(cache=True, fastmath=True)
def _smooth(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


@njit(cache=True, fastmath=True)
def vnoise(x, y, z, seed):
    """Smooth value noise in [-1, 1]."""
    ix = math.floor(x)
    iy = math.floor(y)
    iz = math.floor(z)
    fx = x - ix
    fy = y - iy
    fz = z - iz
    ux = fx * fx * (3 - 2 * fx)
    uy = fy * fy * (3 - 2 * fy)
    uz = fz * fz * (3 - 2 * fz)
    a = 0.0
    for c in range(8):
        dx = c & 1
        dy = (c >> 1) & 1
        dz = (c >> 2) & 1
        w = (ux if dx else 1 - ux) * (uy if dy else 1 - uy) * (uz if dz else 1 - uz)
        a += w * _hash(ix + dx, iy + dy, iz + dz, seed)
    return 2 * a - 1


@njit(cache=True, fastmath=True)
def fbm(x, y, z, seed, octaves):
    s = 0.0
    amp = 0.5
    f = 1.0
    for o in range(octaves):
        s += amp * vnoise(x * f, y * f, z * f, seed + 7 * o)
        f *= 2.03
        amp *= 0.5
    return s


@njit(cache=True, fastmath=True)
def scale_layer(x, y, z, cell, groove, seed, fw):
    """Dome-shaped non-imbricate scale: returns (height 0..1, top 0..1, cell hash). fw = filter
    width (m, texel or voxel size): grooves are never narrower than the filter, and scales smaller
    than ~3 filter widths fade out (no aliasing in the bake / on the mesh)."""
    f1, f2, hid = worley(x / cell, y / cell, z / cell, seed)
    e = 0.5 * (f2 - f1)                      # distance to the cell border (cell units)
    g = max(groove, 0.6 * fw / cell)
    top = _smooth(0.0, g, e)
    dome = top * (0.82 + 0.18 * (1.0 - min(1.0, f1 / 0.75)))
    fade = _smooth(1.6, 3.5, cell / max(fw, 1e-6))
    return dome * (0.75 + 0.5 * hid) * fade, 1.0 - fade * (1.0 - top), hid


# ----------------------------------------------------------------------------------------------
@njit(cache=True, fastmath=True)
def skin_disp(x, y, z, nx, ny, nz, reg, mk, bone, P, LW, BH, BA, BL, out):
    """Evaluate the relief at one point. P = scalar params (see pack_params), LW = 10x3 layer
    weights per region, BH/BA/BL = bone heads, unit axes, lengths. Writes the NCH channels to out."""
    # --- layer weights from the regions ---------------------------------------------------------
    w0 = 0.0
    w1 = 0.0
    w2 = 0.0
    rs = 0.0
    for r in range(10):
        s = reg[r]
        w0 += s * LW[r, 0]
        w1 += s * LW[r, 1]
        w2 += s * LW[r, 2]
        rs += s
    if rs < 1e-6:
        w1 = 1.0
        rs = 1.0
    w0 /= rs
    w1 /= rs
    w2 /= rs
    dors = max(0.0, ny)
    vent = max(0.0, -ny)
    shift = P[4] * dors
    w2 += shift * (w0 + w1) * 0.5
    w0 -= shift * w0 * 0.5
    w1 -= shift * w1 * 0.5
    shift = P[5] * vent
    w0 += shift * (w1 + w2) * 0.5
    w1 -= shift * w1 * 0.5
    w2 -= shift * w2 * 0.5
    m_lid = mk[7]
    m_lip = mk[8]
    m_rug = mk[6]
    m_pad = mk[5]
    m_sm = mk[3]
    m_st = mk[4]
    m_micro = max(m_lid, m_lip)
    plate = max(m_sm, max(m_st, m_pad))
    keep = 1.0 - max(m_micro, max(plate, 0.85 * m_rug))
    disp = 0.0
    top = 0.0
    rnd = 0.0
    hr = P[2]
    gr = P[3]
    cells0 = P[0]
    cells1 = P[1]
    cells2 = P[22]
    if w0 * keep > 0.02:
        h, t, hid = scale_layer(x, y, z, cells0, gr, 11, P[50])
        disp += w0 * keep * h * hr * cells0
        top += w0 * keep * t
        rnd += w0 * keep * hid
    if w1 * keep > 0.02:
        h, t, hid = scale_layer(x, y, z, cells1, gr, 23, P[50])
        disp += w1 * keep * h * hr * cells1
        top += w1 * keep * t
        rnd += w1 * keep * hid
    if w2 * keep > 0.02:
        h, t, hid = scale_layer(x, y, z, cells2, gr * 0.9, 37, P[50])
        disp += w2 * keep * h * hr * cells2
        top += w2 * keep * t
        rnd += w2 * keep * hid
    # --- micro scales on lids and lips -------------------------------------------------------------
    if m_micro > 0.02:
        c = P[6] if m_lip >= m_lid else P[7]
        h, t, hid = scale_layer(x, y, z, c, 0.22, 51, P[50])
        disp += m_micro * h * 0.2 * c
        top += m_micro * t
        rnd += m_micro * hid
    # --- rugose cornified plates -----------------------------------------------------------------
    feat = 0.0
    if m_rug > 0.02:
        c = P[8]
        f1, f2, hid = worley(x / c, y / c, z / c, 61)
        e = 0.5 * (f2 - f1)
        t = _smooth(0.0, max(0.09, 0.6 * P[50] / c), e)
        bump = t * (0.6 + 0.4 * hid) + 0.25 * fbm(x * 40.0, y * 40.0, z * 40.0, 67, 2)
        disp += m_rug * bump * P[9]
        top += m_rug * t
        rnd += m_rug * hid
        feat = max(feat, m_rug * t)
    # --- feature scales (sparse tubercles) ----------------------------------------------------------
    fw = 0.0
    for r in range(10):
        fw += reg[r] * P[30 + r]
    fw /= rs
    fw *= (0.4 + 0.6 * dors) * (1.0 - m_micro) * (1.0 - plate)
    if fw > 0.02:
        c = P[10]
        f1, f2, hid = worley(x / c, y / c, z / c, 71)
        if hid < P[11]:
            rr = P[12]
            t = 1.0 - min(1.0, f1 / rr)
            b = math.sqrt(max(0.0, 1.0 - (1.0 - t) ** 2)) if t > 0 else 0.0
            disp += fw * b * P[13]
            feat = max(feat, fw * _smooth(0.0, 0.25, t))
    # --- plates: scutes on metatarsus (front) and toes (top), pads below ---------------------------
    sc = 0.0
    if plate > 0.02 and bone >= 0:
        hx = x - BH[bone, 0]
        hy = y - BH[bone, 1]
        hz = z - BH[bone, 2]
        ax = BA[bone, 0]
        ay = BA[bone, 1]
        az = BA[bone, 2]
        u = hx * ax + hy * ay + hz * az
        # 'front' of the limb: +x made perpendicular to the bone
        fx = 1.0 - ax * ax
        fy = -ax * ay
        fz = -ax * az
        fl = math.sqrt(fx * fx + fy * fy + fz * fz) + 1e-9
        fx /= fl
        fy /= fl
        fz /= fl
        front = nx * fx + ny * fy + nz * fz
        upn = ny
        L = P[14] if m_sm >= m_st else P[15]
        warp = 0.18 * vnoise(x * 9.0, y * 9.0, z * 9.0, 81)
        uu = u / L + warp
        k = math.floor(uu)
        fr = uu - k
        band = _smooth(0.0, 0.12, fr) * _smooth(1.0, 0.86, fr)
        # lateral subdivision of each band (brick pattern) away from the front midline
        sx = hy * az - hz * ay
        sy = hz * ax - hx * az
        sz = hx * ay - hy * ax
        lat = (sx * fx + sy * fy + sz * fz)
        cols = lat / (L * 1.25) + 0.5 * (k % 2)
        cf = cols - math.floor(cols)
        colg = _smooth(0.0, 0.1, cf) * _smooth(1.0, 0.9, cf)
        fronty = max(0.0, front) if m_sm >= m_st else max(0.0, 0.6 * upn + 0.6 * front)
        plate_t = band * (colg + (1.0 - colg) * _smooth(0.55, 0.9, fronty))
        rid = _hash(int(k), int(math.floor(cols)), bone, 91)
        scw = max(m_sm, m_st) * _smooth(0.05, 0.45, fronty)
        if scw > 0.01:
            disp += scw * plate_t * P[16] * (0.85 + 0.3 * rid)
            sc = scw * plate_t
            rnd = rnd * (1 - scw) + scw * rid
            top = top * (1 - scw) + scw * plate_t
        # remaining plate area (sides/back of the foot, toe undersides): pads / small scales
        rest = plate - scw
        if m_pad > 0.02 or rest > 0.02:
            pw = max(m_pad * _smooth(0.2, -0.4, upn), rest * 0.8)
            c = P[17]
            h, t, hid = scale_layer(x, y, z, c, 0.2, 97, P[50])
            disp += pw * h * P[18]
            top += pw * t
            rnd += pw * hid
    # --- folds ----------------------------------------------------------------------------------
    crease = 0.0
    m_neck = mk[0]
    if m_neck > 0.02:
        # coordinate along the neck axis (P[40:46] = origin, axis), slanted, warped
        u = (x - P[40]) * P[43] + (y - P[41]) * P[44] + (z - P[42]) * P[45]
        u += 0.22 * (y - P[41]) * 0.0 + 0.035 * fbm(x * 3.0, y * 3.0, z * 3.0, 101, 2) / 0.5
        lam = P[19]
        s = math.sin(2 * math.pi * u / lam)
        side = min(1.0, abs(nz) * 1.6) * (1.0 - 0.6 * dors)
        mod = 0.55 + 0.45 * vnoise(x * 4.0, y * 4.0, z * 4.0, 103)
        a = m_neck * side * mod
        disp += a * P[20] * (math.sqrt(abs(s)) - 0.62)
        crease = max(crease, a * (1.0 - _smooth(0.0, 0.45, abs(s))))
    m_thr = mk[1]
    if m_thr > 0.02:
        lam = P[24]
        s = math.sin(2 * math.pi * (z + 0.03 * fbm(x * 5.0, y * 5.0, z * 5.0, 107, 2) / 0.5) / lam)
        a = m_thr * vent
        disp += a * P[25] * (math.sqrt(abs(s)) - 0.62)
        crease = max(crease, a * (1.0 - _smooth(0.0, 0.45, abs(s))))
    m_j = max(mk[2], mk[11])
    if m_j > 0.02 and bone >= 0:
        u = (x - BH[bone, 0]) * BA[bone, 0] + (y - BH[bone, 1]) * BA[bone, 1] + (z - BH[bone, 2]) * BA[bone, 2]
        u += 0.02 * fbm(x * 8.0, y * 8.0, z * 8.0, 109, 2) / 0.5
        lam = P[26]
        s = math.sin(2 * math.pi * u / lam)
        a = m_j * (0.5 + 0.5 * vnoise(x * 6.0, y * 6.0, z * 6.0, 113))
        disp += a * P[27] * (math.sqrt(abs(s)) - 0.62)
        crease = max(crease, a * (1.0 - _smooth(0.0, 0.45, abs(s))))
    # --- wrinkles: ridged fBm on flanks / thighs / neck / tail --------------------------------------
    ww = (reg[3] + reg[6] + reg[2] + 0.6 * reg[5] + 0.5 * reg[4]) / rs
    if ww > 0.02:
        f = 1.0 / P[29]
        rn = 1.0 - abs(fbm(x * f, y * f * 1.7, z * f, 117, 3) * 2.0)
        rn = rn * rn
        disp += ww * P[28] * (rn - 0.5) * (1.0 - plate)
        crease = max(crease, ww * 0.5 * max(0.0, rn - 0.6) / 0.4)
    out[0] = disp
    out[1] = min(1.0, top)
    out[2] = rnd - math.floor(rnd)
    out[3] = min(1.0, crease)
    out[4] = min(1.0, feat)
    out[5] = min(1.0, sc)


def pack_params(skin, doc, filter_width=0.0):
    """Flatten the 'skin' dictionary of the anatomy spec into the kernel arrays."""
    P = np.zeros(64)
    L = skin["scale_layers_m"]
    P[0], P[1], P[22] = L[0], L[1], L[2]
    P[2] = skin["scale_height_ratio"]
    P[3] = skin["groove_ratio"] / 0.5        # groove in units of the half cell
    P[4] = skin.get("dorsal_coarsen", 0.3)
    P[5] = skin.get("ventral_refine", 0.3)
    P[6] = skin.get("lip_scales_m", 0.008)
    P[7] = skin.get("lid_scales_m", 0.006)
    P[8] = skin["rugose"]["cell_m"]
    P[9] = skin["rugose"]["height_m"]
    fs = skin["feature_scales"]
    P[10] = fs["cell_m"]
    P[11] = fs["density"]
    P[12] = fs["radius_ratio"]
    P[13] = fs["height_m"]
    P[14] = skin["scutes"]["meta_length_m"]
    P[15] = skin["scutes"]["toe_length_m"]
    P[16] = skin["scutes"]["height_m"]
    P[17] = skin["pads"]["cell_m"]
    P[18] = skin["pads"]["height_m"]
    P[19] = skin["neck_folds"]["wavelength_m"]
    P[20] = skin["neck_folds"]["amplitude_m"]
    P[24] = skin["throat_folds"]["wavelength_m"]
    P[25] = skin["throat_folds"]["amplitude_m"]
    P[26] = skin["joint_folds"]["wavelength_m"]
    P[27] = skin["joint_folds"]["amplitude_m"]
    P[28] = skin["flank_wrinkles"]["amplitude_m"]
    P[29] = skin["flank_wrinkles"]["scale_m"]
    P[50] = filter_width
    for r, name in enumerate(REGIONS):
        P[30 + r] = fs["regions"].get(name, 0.0)
    bones = {b["name"]: b for b in doc["bones"]}
    o = np.array(bones["neck_01"]["head"], float)
    e = np.array(bones["head"]["head"], float)
    ax = (e - o) / np.linalg.norm(e - o)
    P[40:43] = o
    P[43:46] = ax
    LW = np.zeros((10, 3))
    for r, name in enumerate(REGIONS):
        LW[r] = skin["region_layers"].get(name, [0.3, 0.5, 0.2])
    BH = np.array([b["head"] for b in doc["bones"]], float)
    BT = np.array([b["tail"] for b in doc["bones"]], float)
    BA = BT - BH
    BL = np.linalg.norm(BA, axis=1)
    BA = BA / np.maximum(BL, 1e-9)[:, None]
    return P, LW, BH, BA, BL


def mask_matrix(masks, mask_names):
    """Reorder the Compiled mask columns into the MASKS order used by the kernel."""
    M = np.zeros((len(masks), len(MASKS)), np.float64)
    for j, m in enumerate(MASKS):
        if m in mask_names:
            M[:, j] = masks[:, mask_names.index(m)]
    return M


@njit(parallel=True, cache=True, fastmath=True)
def skin_points(pts, nrm, reg, mk, bone, P, LW, BH, BA, BL, out):
    for j in prange(pts.shape[0]):
        o = np.empty(NCH)
        skin_disp(pts[j, 0], pts[j, 1], pts[j, 2], nrm[j, 0], nrm[j, 1], nrm[j, 2], reg[j], mk[j], bone[j], P, LW, BH, BA, BL, o)
        for c in range(NCH):
            out[j, c] = o[c]


@njit(parallel=True, cache=True, fastmath=True)
def skin_points_grad(pts, nrm, t1, t2, eps, reg, mk, bone, P, LW, BH, BA, BL, out, grad):
    """Channels at the points + tangential gradient of the displacement (central differences along
    two tangent directions t1, t2)."""
    for j in prange(pts.shape[0]):
        o = np.empty(NCH)
        q = np.empty(NCH)
        x = pts[j, 0]
        y = pts[j, 1]
        z = pts[j, 2]
        nx = nrm[j, 0]
        ny = nrm[j, 1]
        nz = nrm[j, 2]
        skin_disp(x, y, z, nx, ny, nz, reg[j], mk[j], bone[j], P, LW, BH, BA, BL, o)
        for c in range(NCH):
            out[j, c] = o[c]
        skin_disp(x + eps * t1[j, 0], y + eps * t1[j, 1], z + eps * t1[j, 2], nx, ny, nz, reg[j], mk[j], bone[j], P, LW, BH, BA, BL, q)
        a = q[0]
        skin_disp(x - eps * t1[j, 0], y - eps * t1[j, 1], z - eps * t1[j, 2], nx, ny, nz, reg[j], mk[j], bone[j], P, LW, BH, BA, BL, q)
        grad[j, 0] = (a - q[0]) / (2 * eps)
        skin_disp(x + eps * t2[j, 0], y + eps * t2[j, 1], z + eps * t2[j, 2], nx, ny, nz, reg[j], mk[j], bone[j], P, LW, BH, BA, BL, q)
        a = q[0]
        skin_disp(x - eps * t2[j, 0], y - eps * t2[j, 1], z - eps * t2[j, 2], nx, ny, nz, reg[j], mk[j], bone[j], P, LW, BH, BA, BL, q)
        grad[j, 1] = (a - q[0]) / (2 * eps)


def evaluate(comp, doc, pts, nrm, grad_tangents=None, eps=6e-4, log=print, filter_width=0.0):
    """Attributes (regions, masks, dominant bone) at the points, then the relief channels."""
    import time
    t0 = time.time()
    reg, bw, feat, masks = comp.attributes(pts)
    M = mask_matrix(masks.astype(np.float64), comp.mask_names)
    bone = np.argmax(bw, axis=1).astype(np.int64)
    P, LW, BH, BA, BL = pack_params(doc["skin"], doc, filter_width)
    reg = np.ascontiguousarray(reg, dtype=np.float64)
    out = np.zeros((len(pts), NCH))
    log(f"  attributes for {len(pts)} points: {time.time() - t0:.1f}s")
    if grad_tangents is None:
        skin_points(np.ascontiguousarray(pts), np.ascontiguousarray(nrm), reg, M, bone, P, LW, BH, BA, BL, out)
        log(f"  skin relief: {time.time() - t0:.1f}s")
        return out, dict(reg=reg, masks=M, bone=bone, bw=bw, feat=feat)
    t1, t2 = grad_tangents
    g = np.zeros((len(pts), 2))
    skin_points_grad(np.ascontiguousarray(pts), np.ascontiguousarray(nrm), np.ascontiguousarray(t1), np.ascontiguousarray(t2), eps,
                     reg, M, bone, P, LW, BH, BA, BL, out, g)
    log(f"  skin relief + gradient: {time.time() - t0:.1f}s")
    return out, dict(reg=reg, masks=M, bone=bone, bw=bw, feat=feat, grad=g)
