"""Stage 'anim': procedural Actions keyed from biomechanics (baked FK, root motion on 'root').

Gait model (walk): v = sim.locomotion.walk_speed_ms (preferred walking speed), hip height
h = sim.hip_height_m, Froude Fr = v^2 / (g h), relative stride length lambda/h = 2.3 Fr^0.3
(Alexander 1976), duty factor beta = sim.locomotion.duty_factor_walk. Cycle period T = lambda / v,
rounded to whole frames at 30 fps (v adjusted accordingly, logged in the metadata). During stance the
metatarsophalangeal joint (MTP, head of 'toes_*') is fixed in the world (no foot sliding: checked and
reported); at the end of stance the foot rolls over the toe tips (digitigrade roll-off); in swing the
MTP follows an eased arc. Each leg is solved analytically: metatarsus angle chosen from the stance
progress (folding in swing), then two-bone IK femur/tibia with the knee forward.
Trunk: pelvis bob (2 per stride, highest at mid-stance), lateral sway towards the stance leg, roll
and yaw; tail travelling wave (phase lag along the tail) and vertical counter-motion; neck/head
stabilise the gaze (counter-rotation of the trunk motion).

Clips (all 30 fps): idle (breathing, weight shifts, slow look), walk (loop, root motion),
walk_fast (loop, only if sim max_speed allows), turn_left_90 (stepping turn in place), look_around,
sniff (carnivore), bite (head strike, small gape: the mouth interior is not modelled), alert
(vigilance), vocal_closed_mouth (closed-mouth low-frequency vocal posture, Riede et al. 2016
hypothesis: throat/neck posture only), lie_down, rest_loop, get_up.
"""
from __future__ import annotations

import math
import numpy as np

FPS = 30
G = 9.81


# ----------------------------------------------------------------------------------------------
# rotations in Blender armature space (creature faces -Y, up +Z, animal right = -X)
def rot(axis, ang):
    a = np.asarray(axis, float)
    a = a / np.linalg.norm(a)
    x, y, z = a
    c, s = math.cos(ang), math.sin(ang)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


LAT = np.array([-1.0, 0, 0])   # animal right
FWD = np.array([0, -1.0, 0])
UP = np.array([0, 0, 1.0])


def pitch(a):    # nose up for a > 0
    return rot(LAT, a)


def yaw(a):      # turn left for a > 0
    return rot(UP, a)


def roll(a):     # right side down for a > 0
    return rot(FWD, a)


def frame_from(d, lat):
    d = d / np.linalg.norm(d)
    l = lat - (lat @ d) * d
    l /= np.linalg.norm(l)
    return np.stack([d, l, np.cross(d, l)], 1)


def mat_to_quat(m):
    t = np.trace(m)
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w = 0.25 * s
        x = (m[2, 1] - m[1, 2]) / s
        y = (m[0, 2] - m[2, 0]) / s
        z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w = (m[2, 1] - m[1, 2]) / s
        x = 0.25 * s
        y = (m[0, 1] + m[1, 0]) / s
        z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w = (m[0, 2] - m[2, 0]) / s
        x = (m[0, 1] + m[1, 0]) / s
        y = 0.25 * s
        z = (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w = (m[1, 0] - m[0, 1]) / s
        x = (m[0, 2] + m[2, 0]) / s
        y = (m[1, 2] + m[2, 1]) / s
        z = 0.25 * s
    q = np.array([w, x, y, z])
    return q / np.linalg.norm(q)


def ss(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ----------------------------------------------------------------------------------------------
class Skeleton:
    """Rest data of the armature (Blender armature space) and the pose solver."""

    def __init__(self, arm):
        self.names = [b.name for b in arm.bones]
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in arm.bones}
        self.M = {b.name: np.array(b.matrix_local.to_3x3()) for b in arm.bones}
        self.head = {b.name: np.array(b.head_local) for b in arm.bones}
        self.tail = {b.name: np.array(b.tail_local) for b in arm.bones}
        self.length = {n: float(np.linalg.norm(self.tail[n] - self.head[n])) for n in self.names}
        self.order = []
        seen = set()
        def visit(n):
            if n in seen:
                return
            p = self.parent[n]
            if p:
                visit(p)
            seen.add(n)
            self.order.append(n)
        for n in self.names:
            visit(n)

    def solve(self, pose):
        """pose: dict with
             root_pos (3), root_yaw (rad)
             pelvis_off (3, armature space), pelvis_rot (3x3 relative to the root)
             rel: {bone: 3x3 rotation relative to the parent (armature-space axes)} for trunk/arms/jaw/eyes
             legs: {'l'|'r': dict(Q=mtp position, toe_dir=unit vector, meta_dir=unit vector, pole=unit fwd)}
           returns {bone: (quat wxyz, loc or None)} local pose channels, plus world heads (for checks)."""
        D = {}
        H = {}
        out = {}
        R0 = yaw(pose.get("root_yaw", 0.0))
        D["root"] = R0
        H["root"] = np.asarray(pose.get("root_pos", np.zeros(3)), float)
        legs = pose.get("legs", {})
        rel = pose.get("rel", {})
        for n in self.order:
            p = self.parent[n]
            if n == "root":
                continue
            # head position from the parent's FK
            hp = H[p] + D[p] @ (self.head[n] - self.head[p])
            if n == "pelvis":
                Dn = R0 @ pose.get("pelvis_rot", np.eye(3))
                h = hp + np.asarray(pose.get("pelvis_off", np.zeros(3)))
            elif n.startswith(("thigh_", "shin_", "metatarsus_", "toes_")) and n[-1] in legs:
                s = n[-1]
                if "sol" not in legs[s]:
                    legs[s]["sol"] = self.leg_ik(s, H, D, legs[s])
                Dn = legs[s]["sol"][n]
                h = hp
            else:
                Dn = D[p] @ rel.get(n, np.eye(3))
                h = hp
            D[n] = Dn
            H[n] = h
        for n in self.order:
            p = self.parent[n]
            Dp = D[p] if p else np.eye(3)
            L = self.M[n].T @ Dp.T @ D[n] @ self.M[n]
            loc = None
            if n == "root":
                loc = self.M[n].T @ H[n]
            elif n == "pelvis":
                exp = H[p] + D[p] @ (self.head[n] - self.head[p])
                loc = self.M[n].T @ Dp.T @ (H[n] - exp)
            out[n] = (mat_to_quat(L), loc)
        return out, H, D

    def leg_ik(self, s, H, D, leg):
        """Thigh/shin/metatarsus/toes world delta rotations for the MTP target Q."""
        th, sh, mt, to = f"thigh_{s}", f"shin_{s}", f"metatarsus_{s}", f"toes_{s}"
        hip = H["pelvis"] + D["pelvis"] @ (self.head[th] - self.head["pelvis"])
        L1, L2, L3 = self.length[th], self.length[sh], self.length[mt]
        Q = np.asarray(leg["Q"], float)
        u = np.asarray(leg["meta_dir"], float)
        u /= np.linalg.norm(u)
        A = Q - u * L3
        e = A - hip
        d = np.linalg.norm(e)
        dmax = 0.997 * (L1 + L2)
        leg["stretch"] = max(0.0, d - dmax)
        if d > dmax:
            A = hip + e / d * dmax
            d = dmax
            e = A - hip
        e /= d
        pole = np.asarray(leg.get("pole", FWD), float)
        f = pole - (pole @ e) * e
        f /= np.linalg.norm(f)
        ca = (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)
        ca = min(1.0, max(-1.0, ca))
        K = hip + L1 * (ca * e + math.sqrt(1 - ca * ca) * f)
        lat = np.cross(f, e)                      # bend axis
        # rest frames
        out = {}
        for n, a, b in ((th, hip, K), (sh, K, A), (mt, A, A + u * L3)):
            r = self.tail[n] - self.head[n]
            Fr = frame_from(r, self.rest_lat(s))
            Fn = frame_from(b - a, lat if (lat @ self.rest_lat(s)) > 0 else -lat)
            out[n] = Fn @ Fr.T
        r = self.tail[to] - self.head[to]
        td = np.asarray(leg["toe_dir"], float)
        out[to] = frame_from(td, lat if (lat @ self.rest_lat(s)) > 0 else -lat) @ frame_from(r, self.rest_lat(s)).T
        leg["A"], leg["K"], leg["hip"] = A, K, hip
        return out

    def rest_lat(self, s):
        th, sh = f"thigh_{s}", f"shin_{s}"
        a = self.tail[th] - self.head[th]
        b = self.tail[sh] - self.head[sh]
        n = np.cross(a, b)
        n /= np.linalg.norm(n)
        return n


# ----------------------------------------------------------------------------------------------
# clips
class Gait:
    def __init__(self, sk, sim, speed, duty, lift=0.32, name="walk", stance_c=-0.2, drop=0.09):
        self.sk = sk
        loc = sim["locomotion"]
        self.h = float(sim["hip_height_m"])
        v = float(speed)
        Fr = v * v / (G * self.h)
        lam = 2.3 * Fr ** 0.3 * self.h
        T = lam / v
        nf = int(round(T * FPS))
        self.T = nf / FPS
        self.nf = nf
        self.stride = lam
        self.v = lam / self.T
        self.Fr = Fr
        self.duty = duty
        self.lift = lift
        self.name = name
        self.stance_c = stance_c        # stance excursion centre relative to the rest MTP (m, + forward)
        self.drop = drop                # mean pelvis lowering during the gait (m)

    def info(self):
        return dict(speed_ms=round(self.v, 3), froude=round(self.Fr, 4), stride_m=round(self.stride, 3),
                    stride_over_hip=round(self.stride / self.h, 3), period_s=round(self.T, 3), duty_factor=self.duty,
                    frames=self.nf, formula="stride/h = 2.3 Fr^0.3 (Alexander 1976), Fr = v^2/(g h)")


def rest_mtp(sk, s):
    return sk.head[f"toes_{s}"].copy()


def rest_dir(sk, n):
    d = sk.tail[n] - sk.head[n]
    return d / np.linalg.norm(d)


def walk_pose(sk, g, t, cycle_offset=0.0, ampl=1.0):
    """Pose at time t (s) of the walk loop; root moves forward at g.v (root motion)."""
    T, beta, lam = g.T, g.duty, g.stride
    x_root = g.v * t
    pose = {"root_pos": FWD * x_root, "root_yaw": 0.0, "legs": {}, "rel": {}}
    stance_c = g.stance_c
    stances = {}
    for s, ph0 in (("r", 0.0), ("l", 0.5)):
        ph = ((t / T) + ph0) % 1.0                   # 0 = touch-down
        Q0 = rest_mtp(sk, s)
        if ph < beta:
            sp = ph / beta
            # touch-down at x_root(t_td) + lam*beta/2 + c ... planted: world position fixed
            t_td = t - ph * T
            xq = g.v * t_td + 0.5 * beta * lam + stance_c
            Q = Q0 + FWD * xq
            # digitigrade roll-off over the toe tips in the last part of stance
            ro = ss(0.72, 1.0, sp)
            toe = rest_dir(sk, f"toes_{s}")
            L_t = sk.length[f"toes_{s}"]
            tip = Q + toe * L_t
            ang = ro * math.radians(38)
            # rotate Q about the toe tip, around the lateral axis (heel lifts, MTP rises and goes forward)
            Rr = pitch(-ang)
            Q = tip + Rr @ (Q - tip)
            toe_dir = Rr @ toe
            meta_ang = math.radians(24) - math.radians(34) * ss(0.3, 1.0, sp)
            stances[s] = sp
        else:
            sw = (ph - beta) / (1 - beta)
            t_lo = t - (ph - beta) * T
            x_lo = g.v * t_lo - 0.5 * beta * lam + stance_c   # lift-off MTP x (before roll-off)
            x_td = x_lo + lam
            e = 0.5 - 0.5 * math.cos(math.pi * ss(0.0, 1.0, sw))
            xq = x_lo + (x_td - x_lo) * e
            toe = rest_dir(sk, f"toes_{s}")
            L_t = sk.length[f"toes_{s}"]
            # start from the rolled-off position, blend to flat
            ang0 = math.radians(38) * (1 - ss(0.0, 0.35, sw))
            lift = g.lift * math.sin(math.pi * min(1.0, sw * 1.08)) ** 1.2
            Q = Q0 + FWD * xq
            tip = Q + toe * L_t
            Rr = pitch(-ang0)
            Q = tip + Rr @ (Q - tip)
            Q = Q + UP * lift
            curl = math.radians(30) * math.sin(math.pi * sw)
            toe_dir = pitch(-ang0 - curl) @ toe
            meta_ang = math.radians(-10) + math.radians(34) * ss(0.25, 1.0, sw) - math.radians(10) * math.sin(math.pi * sw)
            stances[s] = None
        meta = np.array([0.0, -math.sin(meta_ang), -math.cos(meta_ang)])   # forward-down
        pose["legs"][s] = dict(Q=Q, toe_dir=toe_dir, meta_dir=meta, pole=FWD)
    # trunk
    w = 2 * math.pi / T
    bob = -0.035 * math.cos(2 * w * t) * ampl - g.drop               # lowest at double support
    sway = 0.05 * math.sin(w * t + 0.4) * ampl                      # towards the stance leg (right first)
    pose["pelvis_off"] = UP * bob + LAT * sway
    pr = pitch(math.radians(1.0) * math.sin(2 * w * t - 0.6) * ampl) @ roll(math.radians(2.0) * math.sin(w * t) * ampl) @ yaw(math.radians(3.0) * math.sin(w * t + 1.2) * ampl)
    pose["pelvis_rot"] = pr
    rel = pose["rel"]
    # spine counter-rotates part of the pelvis yaw/roll; neck/head stabilise
    rel["spine_01"] = yaw(-math.radians(1.2) * math.sin(w * t + 1.2) * ampl) @ roll(-math.radians(0.8) * math.sin(w * t) * ampl)
    rel["spine_02"] = yaw(-math.radians(0.8) * math.sin(w * t + 1.2) * ampl)
    rel["spine_03"] = yaw(-math.radians(0.6) * math.sin(w * t + 1.2) * ampl)
    rel["neck_01"] = pitch(math.radians(1.5) * math.cos(2 * w * t) * ampl) @ yaw(-math.radians(0.8) * math.sin(w * t + 1.4) * ampl)
    rel["neck_02"] = pitch(math.radians(1.0) * math.cos(2 * w * t - 0.4) * ampl)
    rel["neck_03"] = pitch(-math.radians(0.8) * math.cos(2 * w * t - 0.8) * ampl)
    rel["head"] = pitch(-math.radians(1.6) * math.cos(2 * w * t - 1.0) * ampl) @ roll(-math.radians(1.5) * math.sin(w * t) * ampl)
    for i in range(1, 11):
        lag = 0.45 * i
        a = (0.6 + 0.12 * i) * ampl
        rel[f"tail_{i:02d}"] = yaw(math.radians(a) * math.sin(w * t + 1.2 - lag)) @ pitch(math.radians(0.5 + 0.08 * i) * math.cos(2 * w * t - 0.5 - 0.3 * i) * ampl)
    for s, sg in (("l", 1), ("r", -1)):
        rel[f"upperarm_{s}"] = pitch(math.radians(4) * math.sin(w * t + (0 if s == "l" else math.pi)) * ampl)
        rel[f"forearm_{s}"] = pitch(math.radians(3) * math.sin(w * t + 0.5 + (0 if s == "l" else math.pi)) * ampl)
    return pose, stances


def idle_pose(sk, t, dur):
    w = 2 * math.pi / (dur / 2)          # 2 breaths per clip
    pose = {"root_pos": np.zeros(3), "legs": {}, "rel": {}}
    br = math.sin(w * t)
    shift = math.sin(2 * math.pi * t / dur)
    pose["pelvis_off"] = UP * (-0.035 + 0.006 * br) + LAT * 0.035 * shift
    pose["pelvis_rot"] = roll(math.radians(0.8) * shift) @ pitch(math.radians(0.3) * br)
    for s in "lr":
        pose["legs"][s] = dict(Q=rest_mtp(sk, s), toe_dir=rest_dir(sk, f"toes_{s}"),
                               meta_dir=rest_dir(sk, f"metatarsus_{s}"), pole=FWD)
    rel = pose["rel"]
    rel["spine_02"] = pitch(math.radians(0.6) * br)
    rel["spine_03"] = pitch(math.radians(0.5) * br) @ roll(-math.radians(0.5) * shift)
    look = math.sin(2 * math.pi * t / dur + 0.8)
    rel["neck_01"] = pitch(math.radians(1.0) * br) @ yaw(math.radians(2.5) * look)
    rel["neck_02"] = yaw(math.radians(2.0) * look)
    rel["neck_03"] = yaw(math.radians(2.0) * look) @ pitch(-math.radians(0.6) * br)
    rel["head"] = yaw(math.radians(3.0) * look) @ roll(math.radians(1.0) * look) @ pitch(-math.radians(0.6) * br)
    rel["jaw"] = pitch(-math.radians(0.6) * max(0.0, -br))
    for i in range(1, 11):
        rel[f"tail_{i:02d}"] = yaw(math.radians(0.35 + 0.05 * i) * math.sin(2 * math.pi * t / dur - 0.35 * i)) @ pitch(math.radians(0.25) * br)
    for s in "lr":
        rel[f"upperarm_{s}"] = pitch(math.radians(1.5) * br)
    return pose


def head_clip_pose(sk, t, dur, kind):
    """Standing clips driven by neck/head/jaw curves (feet planted)."""
    pose = idle_pose(sk, t, dur)
    rel = pose["rel"]
    u = t / dur
    env = ss(0.0, 0.15, u) * (1 - ss(0.85, 1.0, u))
    if kind == "look_around":
        a = math.sin(2 * math.pi * u) * env
        b = math.sin(4 * math.pi * u + 0.5) * env
        for n, k in (("neck_01", 0.25), ("neck_02", 0.3), ("neck_03", 0.3), ("head", 0.35)):
            rel[n] = rel[n] @ yaw(math.radians(70) * k * a) @ pitch(math.radians(10) * k * b)
        rel["spine_03"] = rel["spine_03"] @ yaw(math.radians(8) * a)
    elif kind == "sniff":
        low = env
        sn = math.sin(2 * math.pi * 3.0 * u) * env
        rel["neck_01"] = rel["neck_01"] @ pitch(-math.radians(14) * low)
        rel["neck_02"] = rel["neck_02"] @ pitch(-math.radians(6) * low)
        rel["neck_03"] = rel["neck_03"] @ pitch(math.radians(4) * low)
        rel["head"] = rel["head"] @ pitch(-math.radians(6) * low + math.radians(2.0) * sn) @ yaw(math.radians(6) * math.sin(2 * math.pi * u) * env)
        pose["pelvis_rot"] = pose["pelvis_rot"] @ pitch(-math.radians(4) * low)
    elif kind == "bite":
        wind = ss(0.1, 0.35, u) * (1 - ss(0.35, 0.45, u))
        strike = ss(0.35, 0.45, u) * (1 - ss(0.6, 0.9, u))
        gape = ss(0.15, 0.35, u) * (1 - ss(0.42, 0.47, u))
        rel["neck_01"] = rel["neck_01"] @ pitch(math.radians(8) * wind - math.radians(14) * strike)
        rel["neck_02"] = rel["neck_02"] @ pitch(math.radians(6) * wind - math.radians(8) * strike)
        rel["head"] = rel["head"] @ pitch(math.radians(6) * wind - math.radians(6) * strike)
        rel["jaw"] = pitch(-math.radians(9) * gape)
        pose["pelvis_off"] = pose["pelvis_off"] + FWD * 0.25 * strike - FWD * 0.1 * wind
        pose["pelvis_rot"] = pose["pelvis_rot"] @ pitch(-math.radians(3) * strike)
    elif kind == "alert":
        up_ = env
        rel["neck_01"] = rel["neck_01"] @ pitch(math.radians(9) * up_)
        rel["neck_02"] = rel["neck_02"] @ pitch(math.radians(5) * up_)
        rel["head"] = rel["head"] @ pitch(-math.radians(5) * up_) @ yaw(math.radians(18) * math.sin(2 * math.pi * u * 1.5) * up_ * ss(0.3, 0.5, u))
        pose["pelvis_off"] = pose["pelvis_off"] + UP * 0.05 * up_
        pose["pelvis_rot"] = pose["pelvis_rot"] @ pitch(math.radians(3) * up_)
        for i in range(1, 11):
            rel[f"tail_{i:02d}"] = rel[f"tail_{i:02d}"] @ pitch(math.radians(0.8) * up_)
    elif kind == "vocal_closed_mouth":
        # neck lowered and arched, throat expanded (posture only), body pumping slowly
        low = env
        pump = 0.5 - 0.5 * math.cos(2 * math.pi * 2 * u)
        rel["neck_01"] = rel["neck_01"] @ pitch(-math.radians(10) * low)
        rel["neck_02"] = rel["neck_02"] @ pitch(math.radians(4) * low)
        rel["neck_03"] = rel["neck_03"] @ pitch(math.radians(8) * low)
        rel["head"] = rel["head"] @ pitch(-math.radians(10) * low + math.radians(2) * pump * low)
        rel["spine_02"] = rel["spine_02"] @ pitch(math.radians(1.5) * pump * low)
        pose["pelvis_off"] = pose["pelvis_off"] - UP * 0.04 * pump * low
    return pose


def lie_pose(sk, t, dur, mode):
    """Lie down (0->1), rest loop (1), get up (1->0). Resting on the pubic boot with the legs folded
    under the body, metatarsi flat on the ground (bird-like resting posture; design reconstruction)."""
    if mode == "lie_down":
        k = ss(0.05, 0.95, t / dur)
    elif mode == "get_up":
        k = 1 - ss(0.05, 0.95, t / dur)
    else:
        k = 1.0
    br = math.sin(2 * math.pi * t / 5.0)
    pose = {"root_pos": np.zeros(3), "legs": {}, "rel": {}}
    drop = 1.72 * k
    pose["pelvis_off"] = -UP * drop + FWD * 0.15 * k + UP * 0.006 * br * k
    pose["pelvis_rot"] = pitch(math.radians(6) * k)
    for s in "lr":
        Q0 = rest_mtp(sk, s)
        Q = Q0 + FWD * 0.75 * k + UP * 0.0
        # the metatarsus rotates down to lie on the ground (pointing forward)
        a = math.radians(24) + (math.radians(88) - math.radians(24)) * k
        meta = np.array([0.0, -math.sin(a), -math.cos(a)])
        pose["legs"][s] = dict(Q=Q, toe_dir=rest_dir(sk, f"toes_{s}"), meta_dir=meta, pole=FWD)
    rel = pose["rel"]
    rel["spine_01"] = pitch(-math.radians(3) * k)
    rel["spine_02"] = pitch(-math.radians(3) * k + math.radians(0.6) * br * k)
    rel["neck_01"] = pitch(-math.radians(10) * k)
    rel["neck_02"] = pitch(-math.radians(4) * k)
    rel["head"] = pitch(math.radians(6) * k - math.radians(0.6) * br * k)
    for i in range(1, 11):
        rel[f"tail_{i:02d}"] = pitch(-math.radians(1.6 if i < 4 else 0.6) * k) @ yaw(math.radians(1.5) * k * (1 if i > 5 else 0))
    for s in "lr":
        rel[f"upperarm_{s}"] = pitch(math.radians(10) * k)
    return pose


def turn_pose(sk, t, dur, total=math.radians(90), steps=4):
    """Stepping turn in place (left): the root yaws in 'steps' increments, each foot steps to its rest
    position relative to the rotated root, alternately."""
    u = t / dur
    pose = {"root_pos": np.zeros(3), "legs": {}, "rel": {}}
    yaw_t = total * ss(0.05, 0.95, u)
    pose["root_yaw"] = yaw_t
    seg = 1.0 / steps
    for k_, s in enumerate("rl"):
        # foot s steps during its own sub-intervals
        done = 0.0
        moving = None
        for j in range(steps):
            if (j % 2) != k_:
                continue
            a = 0.05 + 0.9 * j * seg
            b = a + 0.9 * seg
            if u >= b:
                done = 0.9 * (j + 1) * seg + 0.05
            elif u >= a:
                moving = (a, b, j)
        def foot_at(yw):
            Rr = yaw(yw)
            return Rr @ rest_mtp(sk, s), Rr @ rest_dir(sk, f"toes_{s}")
        prev_yaw = total * ss(0.05, 0.95, done) if done > 0 else 0.0
        Qp, Tp = foot_at(prev_yaw)
        if moving:
            a, b, j = moving
            target_yaw = total * ss(0.05, 0.95, min(1.0, b + 0.02))
            Qn, Tn = foot_at(target_yaw)
            e = ss(a, b, u)
            Q = Qp * (1 - e) + Qn * e + UP * 0.22 * math.sin(math.pi * e)
            td = Tp * (1 - e) + Tn * e
            meta_ang = math.radians(24) - math.radians(18) * math.sin(math.pi * e)
        else:
            Q, td = Qp, Tp
            meta_ang = math.radians(24)
        fwd = td - (td @ UP) * UP
        fwd /= np.linalg.norm(fwd)
        meta = math.sin(meta_ang) * fwd - math.cos(meta_ang) * UP
        pose["legs"][s] = dict(Q=Q, toe_dir=td / np.linalg.norm(td), meta_dir=meta, pole=fwd)
    w = 2 * math.pi * steps / 2 / dur
    pose["pelvis_off"] = UP * (-0.05 - 0.02 * abs(math.sin(w * t))) + LAT * 0.03 * math.sin(w * t)
    pose["pelvis_rot"] = yaw(math.radians(6) * math.sin(math.pi * u))
    rel = pose["rel"]
    for n, k in (("neck_01", 0.3), ("neck_02", 0.3), ("head", 0.4)):
        rel[n] = yaw(math.radians(25) * k * math.sin(math.pi * min(1.0, u * 1.3)))
    for i in range(1, 11):
        rel[f"tail_{i:02d}"] = yaw(-math.radians(1.2) * math.sin(math.pi * u - 0.2 * i))
    return pose


# ----------------------------------------------------------------------------------------------
def key_action(bpy, rig, sk, name, poses, loop=False):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    nfr = len(poses)
    chans = {}
    prevq = {}
    for fi, sol in enumerate(poses):
        for n, (q, loc) in sol.items():
            if n in prevq and np.dot(prevq[n], q) < 0:
                q = -q
            prevq[n] = q
            chans.setdefault(n, {"q": [], "l": []})
            chans[n]["q"].append(q)
            if loc is not None:
                chans[n]["l"].append(loc)
    for n, c in chans.items():
        Qs = np.array(c["q"])
        for i in range(4):
            fc = act.fcurves.new(f'pose.bones["{n}"].rotation_quaternion', index=i, action_group=n)
            fc.keyframe_points.add(nfr)
            co = np.stack([np.arange(nfr, dtype=np.float32) + 1, Qs[:, i]], 1).astype(np.float32)
            fc.keyframe_points.foreach_set("co", co.ravel())
            fc.keyframe_points.foreach_set("interpolation", np.full(nfr, 1, np.int32))   # LINEAR
            fc.update()
        if c["l"]:
            Ls = np.array(c["l"])
            for i in range(3):
                fc = act.fcurves.new(f'pose.bones["{n}"].location', index=i, action_group=n)
                fc.keyframe_points.add(nfr)
                co = np.stack([np.arange(nfr, dtype=np.float32) + 1, Ls[:, i]], 1).astype(np.float32)
                fc.keyframe_points.foreach_set("co", co.ravel())
                fc.keyframe_points.foreach_set("interpolation", np.full(nfr, 1, np.int32))
                fc.update()
    act.use_frame_range = True
    act.frame_start = 1
    act.frame_end = nfr
    act.use_cyclic = bool(loop)
    act["loop"] = bool(loop)
    return act


def run(ctx):
    import bpy
    from . import bl
    sim = ctx.species_data()["sim"]
    bpy.ops.wm.open_mainfile(filepath=ctx.path("rig.blend"))
    rig = bpy.data.objects["Armature"]
    sk = Skeleton(rig.data)
    loc = sim["locomotion"]
    clips = []
    rep = {"clips": {}}
    def add(name, poses, loop, extra=None):
        sols = [sk.solve(p)[0] for p in poses]
        act = key_action(bpy, rig, sk, name, sols, loop)
        info = dict(frames=len(poses), duration_s=round(len(poses) / FPS, 3), fps=FPS, loop=loop)
        if extra:
            info.update(extra)
        rep["clips"][name] = info
        clips.append(act)
        ctx.log(f"  clip {name}: {info}")
    # --- walk ---------------------------------------------------------------------------------
    def gait_clip(name, speed, duty, lift, ampl, stance_c=-0.2, drop=0.09):
        g = Gait(sk, sim, speed, duty, lift=lift, name=name, stance_c=stance_c, drop=drop)
        poses, slide, stretch = [], 0.0, 0.0
        prevQ = {}
        for f in range(g.nf + 1):
            t = f / FPS
            p, st = walk_pose(sk, g, t, ampl=ampl)
            sol, H, D = sk.solve(p)
            for s in "lr":
                stretch = max(stretch, p["legs"][s].get("stretch", 0.0))
                if st[s] is not None and st[s] < 0.7 and s in prevQ and prevQ[s][1] is not None and prevQ[s][1] < 0.7:
                    toes = H[f"toes_{s}"]
                    slide = max(slide, float(np.linalg.norm(toes - prevQ[s][0])))
                prevQ[s] = (H[f"toes_{s}"].copy(), st[s])
            poses.append(p)
        poses = poses[:-1]       # loop: last frame == first frame + stride
        info = g.info()
        info.update(root_motion=f"root bone translates {g.stride:.3f} m forward per cycle (-Y Blender, +X UE after import conversion)",
                    max_mtp_slide_per_frame_m=round(slide, 5), max_leg_overstretch_m=round(stretch, 4))
        add(name, poses, True, info)
        return g
    g = gait_clip("walk", loc["walk_speed_ms"], loc.get("duty_factor_walk", 0.6), 0.32, 1.0)
    ctx.walk_gait = g
    vmax = float(loc.get("max_speed_ms", 0))
    if vmax >= 2.5:
        v_fast = min(vmax * 0.5, 2.6)
        gait_clip("walk_fast", v_fast, max(0.5, loc.get("duty_factor_walk", 0.6) - 0.08), 0.38, 1.35, stance_c=-0.3, drop=0.24)
    # --- idle & standing behaviours ---------------------------------------------------------------
    dur = 8.0
    add("idle", [idle_pose(sk, f / FPS, dur) for f in range(int(dur * FPS))], True)
    for kind, d in (("look_around", 6.0), ("sniff", 4.0), ("bite", 2.4), ("alert", 4.0), ("vocal_closed_mouth", 5.0)):
        add(kind, [head_clip_pose(sk, f / FPS, d, kind) for f in range(int(d * FPS) + 1)], False)
    d = 4.0
    add("turn_left_90", [turn_pose(sk, f / FPS, d) for f in range(int(d * FPS) + 1)], False,
        dict(root_motion="root yaw +90 deg (turn left), no translation"))
    for mode, d, lp in (("lie_down", 5.0, False), ("rest_loop", 10.0, True), ("get_up", 4.0, False)):
        add(mode, [lie_pose(sk, f / FPS, d, mode) for f in range(int(d * FPS) + (0 if lp else 1))], lp)
    # NLA: one muted track per action so every action is kept/exported; walk active by default
    rig.animation_data_create()
    for act in clips:
        tr = rig.animation_data.nla_tracks.new()
        tr.name = act.name
        st_ = tr.strips.new(act.name, 1, act)
        tr.mute = True
    rig.animation_data.action = bpy.data.actions["walk"]
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = rep["clips"]["walk"]["frames"]
    ctx.save_json("anim_clips.json", rep)
    bpy.ops.wm.save_as_mainfile(filepath=ctx.path("anim.blend"), compress=True)
