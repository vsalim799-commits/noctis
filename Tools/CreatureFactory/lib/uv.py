"""Stage 'uv': anatomical seams + unwrap + packing of the low-poly body.

Charts follow the anatomy (dominant bone group of each face, from the SDF ownership weights):
head (+jaw), body (pelvis, spine, neck, tail), thigh L/R, lower leg L/R (shin, metatarsus, toes),
arm L/R. Chart borders are seams (rings at the joints). Inside each chart a cut tree joins its border
loops and its tips (tail tip, chin, toe and finger tips) along the least visible surface: ventral
midline for the body and the head, medial/posterior side for the limbs (Dijkstra on the edge graph
with a visibility cost). Unwrap = Blender angle-based, islands area-normalised, head islands scaled
x HEAD_BOOST (more texels on the face), packed with rotation.
"""
from __future__ import annotations

import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra, connected_components

from .sdf_stage import compile_body

HEAD_BOOST = 1.6       # linear texel-density multiplier for the head chart
GROUPS = {
    "head": ("head", "jaw", "eye_l", "eye_r"),
    "body": ("root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "neck_02", "neck_03") + tuple(f"tail_{i:02d}" for i in range(1, 11)),
    "thigh_l": ("thigh_l",), "thigh_r": ("thigh_r",),
    "leg_l": ("shin_l", "metatarsus_l", "toes_l"), "leg_r": ("shin_r", "metatarsus_r", "toes_r"),
    "arm_l": ("upperarm_l", "forearm_l", "hand_l"), "arm_r": ("upperarm_r", "forearm_r", "hand_r"),
    "tail_a": (), "tail_b": (),          # split from 'body' by position, unwrapped analytically
}
TAIL_SPLITS = (-1.9, -4.5)               # anatomy x of the two ring seams on the tail
ANALYTIC_TAIL = False                    # experimental cylindrical pelt for the tail charts (off: ABF)


def face_labels(comp, doc, V, polys, smooth_iters=4):
    """Chart of each face = bone group of the primitive that owns the surface there (nearest
    primitive surface), majority-smoothed; disconnected pieces merged into their surroundings."""
    names = [b["name"] for b in doc["bones"]]
    gnames = list(GROUPS)
    gid = {}
    for j, g in enumerate(gnames):
        for b in GROUPS[g]:
            gid[b] = j
    C = np.array([V[p].mean(0) for p in polys])
    _, bw, _, _, best = comp.attributes(C, return_best=True)
    sb = comp.bone[np.maximum(best, 0)]
    lab = np.array([gid.get(names[i], 1) for i in sb])
    body = gnames.index("body")
    lab[(lab == body) & (C[:, 0] < TAIL_SPLITS[0])] = gnames.index("tail_a")
    lab[(lab == gnames.index("tail_a")) & (C[:, 0] < TAIL_SPLITS[1])] = gnames.index("tail_b")
    # sweep primitives (body) own the trunk/neck/tail whatever their chain bone
    emap = {}
    adj = [[] for _ in polys]
    for fi, p in enumerate(polys):
        for k in range(len(p)):
            e = (min(p[k], p[(k + 1) % len(p)]), max(p[k], p[(k + 1) % len(p)]))
            if e in emap:
                o = emap[e]
                adj[fi].append(o)
                adj[o].append(fi)
            else:
                emap[e] = fi
    ng = len(gnames)
    for _ in range(smooth_iters):
        new = lab.copy()
        for fi in range(len(polys)):
            if adj[fi]:
                cnt = np.bincount(lab[adj[fi]], minlength=ng)
                cnt[lab[fi]] += 1
                new[fi] = int(np.argmax(cnt))
        lab = new
    # keep only the largest connected piece of each label; merge the other pieces into the
    # neighbouring label that surrounds them
    for _ in range(6):
        rows, cols = [], []
        for fi in range(len(polys)):
            for o in adj[fi]:
                if lab[o] == lab[fi]:
                    rows.append(fi)
                    cols.append(o)
        A = coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(len(polys), len(polys)))
        nc, cid = connected_components(A, directed=False)
        sizes = np.bincount(cid, minlength=nc)
        clab = np.zeros(nc, int)
        clab[cid] = lab
        keep = np.zeros(nc, bool)
        for L in np.unique(lab):
            cs = np.nonzero(clab == L)[0]
            keep[cs[np.argmax(sizes[cs])]] = True
        if keep.all():
            break
        for c in np.nonzero(~keep)[0]:
            fl = np.nonzero(cid == c)[0]
            nb = [lab[o] for f in fl for o in adj[f] if lab[o] != lab[f]]
            if nb:
                lab[fl] = np.bincount(nb).argmax()
    return lab, gnames, adj, emap


def visibility(V, N, chart, doc):
    """0 = hidden (good place for a seam) .. 1 = visible."""
    up = (N[:, 1] + 1) * 0.5
    if chart in ("body", "head", "tail_a", "tail_b"):
        mid = np.clip(np.abs(V[:, 2]) / 0.25, 0, 1)
        return 0.75 * up + 0.25 * mid
    side = 1.0 if chart.endswith("_r") else -1.0
    medial = (N[:, 2] * side + 1) * 0.5           # 0 when the normal faces the body midline
    post = (N[:, 0] + 1) * 0.5                     # 0 when facing backwards
    if chart.startswith("leg"):
        return 0.45 * medial + 0.35 * post + 0.2 * up
    return 0.6 * medial + 0.4 * post


def cut_tree(V, N, polys, faces, chart, doc, terminals_extra, log=print):
    """Edges (pairs) of a tree joining all border loops of the chart and the extra tip vertices."""
    fset = faces
    E = {}
    for fi in fset:
        p = polys[fi]
        for k in range(len(p)):
            a, b = int(p[k]), int(p[(k + 1) % len(p)])
            e = (min(a, b), max(a, b))
            E[e] = E.get(e, 0) + 1
    edges = np.array(list(E.keys()))
    border = np.array([E[tuple(e)] == 1 for e in edges])
    verts = np.unique(edges)
    vi = {v: i for i, v in enumerate(verts)}
    ei = np.vectorize(vi.get)(edges)
    vis = visibility(V[verts], N[verts], chart, doc)
    L = np.linalg.norm(V[edges[:, 0]] - V[edges[:, 1]], axis=1)
    w = L * (1.0 + 12.0 * (0.5 * (vis[ei[:, 0]] + vis[ei[:, 1]])) ** 2)
    n = len(verts)
    A = coo_matrix((np.r_[w, w], (np.r_[ei[:, 0], ei[:, 1]], np.r_[ei[:, 1], ei[:, 0]])), shape=(n, n)).tocsr()
    # border loops
    be = ei[border]
    terms = []
    if len(be):
        B = coo_matrix((np.ones(len(be)), (be[:, 0], be[:, 1])), shape=(n, n))
        nc, lab = connected_components(B, directed=False)
        bverts = np.unique(be)
        for c in np.unique(lab[bverts]):
            terms.append(set(bverts[lab[bverts] == c].tolist()))
    for t in terminals_extra:
        if t in vi:
            terms.append({vi[t]})
    if len(terms) <= 1 and not terminals_extra:
        return []
    tree = set(terms[0])
    remaining = terms[1:]
    cut = []
    while remaining:
        dist, pred, _src = dijkstra(A, indices=sorted(tree), min_only=True, return_predecessors=True)
        best, bt, bv = np.inf, None, None
        for ti, t in enumerate(remaining):
            tl = list(t)
            k = int(np.argmin(dist[tl]))
            if dist[tl[k]] < best:
                best, bt, bv = dist[tl[k]], ti, tl[k]
        if bt is None or not np.isfinite(best):
            log(f"    chart {chart}: unreachable terminal")
            break
        v = bv
        while v not in tree and v >= 0:
            u = pred[v]
            cut.append((verts[u], verts[v]))
            tree.add(v)
            v = u
        tree |= remaining[bt]
        remaining.pop(bt)
    return cut


def analytic_body_uv(V, polys, faces, doc, nbins=240, nth=72):
    """Pelt of the trunk/neck/tail chart: u = arc length along the vertebral centre-line of the body
    sweep, v = arc length around the cross-section from the dorsal midline (polar profile r(theta)
    measured on the mesh per centre-line bin); the cut is the ventral midline (theta = +-pi).
    Returns {face: [(u, v) per corner]} and the cut edges."""
    from scipy.interpolate import PchipInterpolator
    from scipy.spatial import cKDTree
    from scipy.ndimage import uniform_filter1d
    sw = [s_ for s_ in doc["shapes"] if s_["kind"] == "sweep"][0]
    P = np.array([k["p"] for k in sw["points"]], float)
    seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    ss = np.linspace(0, seg[-1], 3000)
    Cc = np.stack([PchipInterpolator(seg, P[:, j])(ss) for j in range(3)], 1)
    T = np.gradient(Cc, axis=0)
    T /= np.linalg.norm(T, axis=1, keepdims=True)
    U = np.array([0, 1.0, 0])[None] - T[:, 1:2] * T
    U /= np.linalg.norm(U, axis=1, keepdims=True)
    S = np.cross(T, U)
    fv = np.unique(np.concatenate([polys[f] for f in faces]))
    _, idx = cKDTree(Cc).query(V[fv])
    d = V[fv] - Cc[idx]
    vv = (d * U[idx]).sum(1)
    ww = (d * S[idx]).sum(1)
    th = np.arctan2(ww, vv)
    r = np.hypot(vv, ww)
    sv = ss[idx]
    # polar profile per bin: r(theta), filled and smoothed, then arc length from theta = 0
    edges_s = np.linspace(ss[0], ss[-1], nbins + 1)
    bi = np.clip(np.searchsorted(edges_s, sv) - 1, 0, nbins - 1)
    tg = np.linspace(-np.pi, np.pi, nth + 1)
    tj = np.clip(np.searchsorted(tg, th) - 1, 0, nth - 1)
    R = np.full((nbins, nth), np.nan)
    sumr = np.zeros((nbins, nth))
    cnt = np.zeros((nbins, nth))
    np.add.at(sumr, (bi, tj), r)
    np.add.at(cnt, (bi, tj), 1)
    m = cnt > 0
    R[m] = sumr[m] / cnt[m]
    for b in range(nbins):                      # fill gaps around theta (periodic)
        row = R[b]
        ok = ~np.isnan(row)
        if ok.sum() == 0:
            continue
        x = np.nonzero(ok)[0]
        R[b] = np.interp(np.arange(nth), x, row[ok], period=nth)
    for j in range(nth):                        # fill empty bins along s
        col = R[:, j]
        ok = ~np.isnan(col)
        R[:, j] = np.interp(np.arange(nbins), np.nonzero(ok)[0], col[ok])
    R = uniform_filter1d(uniform_filter1d(R, 5, axis=0, mode="nearest"), 3, axis=1, mode="wrap")
    tc = 0.5 * (tg[:-1] + tg[1:])
    dth = tg[1] - tg[0]
    dR = np.gradient(R, dth, axis=1)
    ds = np.sqrt(R * R + dR * dR) * dth          # arc element per theta cell
    # cumulative arc from theta = 0 (dorsal) towards +pi and towards -pi
    A = np.zeros((nbins, nth + 1))
    A[:, 1:] = np.cumsum(ds, axis=1)
    i0 = np.searchsorted(tg, 0.0)
    A -= A[:, i0:i0 + 1]
    sc = 0.5 * (edges_s[:-1] + edges_s[1:])
    def arc(svals, tvals):
        b = np.clip(np.interp(svals, sc, np.arange(nbins)), 0, nbins - 1)
        b0 = np.floor(b).astype(int)
        b1 = np.minimum(b0 + 1, nbins - 1)
        f = b - b0
        a0 = np.array([np.interp(t, tg, A[k]) for t, k in zip(tvals, b0)])
        a1 = np.array([np.interp(t, tg, A[k]) for t, k in zip(tvals, b1)])
        return a0 * (1 - f) + a1 * f
    pos = {v: i for i, v in enumerate(fv)}
    out = {}
    cut = set()
    for f in faces:
        p = polys[f]
        k = [pos[v] for v in p]
        t = th[k].copy()
        if t.max() - t.min() > np.pi:
            t[t < 0] += 2 * np.pi
            for a in range(len(p)):
                b = (a + 1) % len(p)
                if (th[k[a]] < 0) != (th[k[b]] < 0) and abs(th[k[a]] - th[k[b]]) > np.pi:
                    cut.add((min(p[a], p[b]), max(p[a], p[b])))
        # arc for theta beyond pi: continue the profile periodically
        tt = np.where(t > np.pi, t - 2 * np.pi, t)
        a_ = arc(sv[k], tt)
        full = arc(sv[k], np.full(len(k), np.pi - 1e-9)) - arc(sv[k], np.full(len(k), -np.pi + 1e-9))
        a_ = np.where(t > np.pi, a_ + full, a_)
        out[f] = [(float(u), float(v)) for u, v in zip(sv[k], a_)]
    return out, sorted(cut)


def run(ctx):
    import bpy
    import bmesh
    from . import bl
    doc = ctx.anatomy()
    comp = compile_body(doc)
    bpy.ops.wm.open_mainfile(filepath=ctx.path("lowpoly.blend"))
    ob = bpy.data.objects["body"]
    for m in list(ob.modifiers):
        ob.modifiers.remove(m)
    ob.data.materials.clear()
    me = ob.data
    Vb = np.array([v.co for v in me.vertices])
    V = bl.from_bl(Vb)
    polys = [np.array(p.vertices) for p in me.polygons]
    G = comp.gradient(V)
    N = G / (np.linalg.norm(G, axis=1, keepdims=True) + 1e-12)
    lab, gnames, adj, emap = face_labels(comp, doc, V, polys)
    for j, g in enumerate(gnames):
        ctx.log(f"  chart {g}: {(lab == j).sum()} faces")
    # seams: chart borders
    seams = set()
    edge_faces = {}
    for fi, p in enumerate(polys):
        for k in range(len(p)):
            e = (min(p[k], p[(k + 1) % len(p)]), max(p[k], p[(k + 1) % len(p)]))
            edge_faces.setdefault(e, []).append(fi)
    for e, fl in edge_faces.items():
        if len(fl) == 2 and lab[fl[0]] != lab[fl[1]]:
            seams.add(e)
    # tips
    bones = {b["name"]: b for b in doc["bones"]}
    from scipy.spatial import cKDTree
    kd = cKDTree(V)
    tips = {g: [] for g in gnames}
    tips["tail_b"].append(int(np.argmin(V[:, 0])))

    tips["head"].append(int(kd.query(bones["jaw"]["tail"])[1]))
    for c in doc["claws"]:
        b = c["bone"]
        side = b[-1]
        g = ("leg_" if b.startswith(("toes", "metatarsus")) else "arm_") + side
        tips[g].append(int(kd.query(c["points"][0])[1]))
    body_uv = {}
    for j, g in enumerate(gnames):
        faces = np.nonzero(lab == j)[0]
        if len(faces) == 0:
            continue
        if g.startswith("tail") and ANALYTIC_TAIL:
            tuv, cut = analytic_body_uv(V, polys, faces, doc)
            body_uv.update(tuv)
        else:
            cut = cut_tree(V, N, polys, faces, g, doc, tips[g], log=ctx.log)
        for a, b in cut:
            seams.add((min(a, b), max(a, b)))
        ctx.log(f"  chart {g}: cut {len(cut)} edges")
    for ed in me.edges:
        a, b = ed.vertices
        ed.use_seam = (min(a, b), max(a, b)) in seams
    # chart ids as a face attribute (used by the bake for the head texel boost check)
    bl.set_attr(me, "chart", lab.astype(np.float32), domain="FACE")
    bpy.context.view_layer.objects.active = ob
    for o in bpy.context.scene.objects:
        o.select_set(o == ob)
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method="ANGLE_BASED", fill_holes=True, correct_aspect=True, margin=0.002)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.minimize_stretch(fill_holes=True, blend=0.0, iterations=300)
    # tail tubes: analytic (arc length along the spine, arc length around the section)
    bpy.ops.object.mode_set(mode="OBJECT")
    uvd = me.uv_layers.active.data
    for fi, uvs_ in body_uv.items():
        p_ = me.polygons[fi]
        for k, li in enumerate(range(p_.loop_start, p_.loop_start + p_.loop_total)):
            uvd[li].uv = uvs_[k]
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.average_islands_scale()
    # head boost
    bm = bmesh.from_edit_mesh(me)
    uvl = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    head_j = gnames.index("head")
    # scale each head island about its own centre
    head_faces = [f for f in bm.faces if lab[f.index] == head_j]
    if head_faces:
        uv = np.array([l[uvl].uv[:] for f in head_faces for l in f.loops])
        c = uv.mean(0)
        for f in head_faces:
            for l in f.loops:
                l[uvl].uv = (c + (np.array(l[uvl].uv[:]) - c) * HEAD_BOOST).tolist()
    bmesh.update_edit_mesh(me)
    bpy.ops.uv.pack_islands(rotate=True, margin_method="FRACTION", margin=0.004, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")
    # stats: islands, texel density per chart, stretch
    uvs = np.zeros((len(me.loops), 2), np.float32)
    me.uv_layers.active.data.foreach_get("uv", uvs.ravel())
    lt = np.array([p.loop_start for p in me.polygons])
    tex = ctx.q["tex"]
    area3 = np.zeros(len(polys))
    areau = np.zeros(len(polys))
    for fi, p in enumerate(me.polygons):
        ls = list(range(p.loop_start, p.loop_start + p.loop_total))
        P3 = V[list(p.vertices)]
        U = uvs[ls]
        for k in range(1, len(ls) - 1):
            area3[fi] += 0.5 * np.linalg.norm(np.cross(P3[k] - P3[0], P3[k + 1] - P3[0]))
            a, b = U[k] - U[0], U[k + 1] - U[0]
            areau[fi] += 0.5 * abs(a[0] * b[1] - a[1] * b[0])
    dens = {}
    for j, g in enumerate(gnames):
        m = lab == j
        if m.any():
            dens[g] = round(float(np.sqrt(areau[m].sum() * tex * tex / area3[m].sum())), 1)
    ratio = (areau / np.maximum(area3, 1e-12))
    st = dict(uv_coverage=round(float(areau.sum()), 3), texel_density_px_per_m=dens,
              area_stretch_p5_p95=[round(float(x), 3) for x in np.percentile(ratio / np.median(ratio), [5, 95])],
              seams=len(seams), texture_px=tex)
    ctx.log(f"  uv: {st}")
    ctx.save_json("uv_stats.json", st)
    np.save(ctx.path("face_charts.npy"), lab)
    bpy.ops.wm.save_as_mainfile(filepath=ctx.path("uv.blend"), compress=True)
    # UV layout image
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (1024, 1024), (20, 20, 22))
    dr = ImageDraw.Draw(im)
    cols = [(220, 120, 90), (120, 200, 120), (110, 140, 230), (110, 140, 230), (210, 200, 90), (210, 200, 90), (200, 110, 200), (200, 110, 200), (90, 200, 200), (160, 220, 90)]
    for fi, p in enumerate(me.polygons):
        U = uvs[p.loop_start:p.loop_start + p.loop_total] * 1024
        dr.polygon([(u, 1024 - v) for u, v in U], outline=cols[lab[fi] % len(cols)])
    im.save(ctx.opath("stages", "uv_layout.png"))
