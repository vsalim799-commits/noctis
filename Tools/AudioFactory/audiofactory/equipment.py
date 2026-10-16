"""Modern field equipment of the researcher (Data/Equipment/equipment.json; every value there is a game-design
assumption): 6x6 hybrid rover (electric drive, auxiliaries, range-extender generator, doors), observation
drone, camera shutter, autonomous audio recorder handling, radio link to the base camp.

Levels at 1 m mirror the core (Source/NoctisCore/Private/Research/FieldGear.cpp, ResearchSystem.cpp):
  * rover: electric drive 52 dB + 12 log10(1 + v/3) (noise_electric_db_1m), generator 80 dB (noise_generator_db_1m),
    power sum when both run — the generator is ~28 dB louder than the electric drive at rest;
  * drone: 78 dB at hover (noise_db_1m) + 10 log10(P / P_hover); blade-passing frequency 160 Hz at hover
    (blade_pass_hz). Rotor speed follows P ~ Omega^3, so BPF = 160 (P/P_hover)^(1/3);
    P = P_hover (1 + 0.25 (v/v_max)^2) + m g v_z / 0.6 (DroneSystem);
  * camera shutter: 55 dB, centroid ~3 kHz, 50 ms (ResearchSystem::takePhoto).
Engine loops are 20 s and exactly periodic (carriers rounded to whole cycles, periodic noise and modulation).
Distance variants of the generator and the drone use the ISO 9613-1 absorption of propagation.py applied
circularly (the loop stays seamless).
"""
from __future__ import annotations

import math
import re

import numpy as np

from . import ambience as A
from . import common as C
from . import data as D
from . import manifest as M
from . import propagation as P

LOOP_S = 20.0
N = C.ns(LOOP_S)
VAR = 4
ISO1 = "iso1993_9613_1"
DIST = [("d50", 50.0), ("d200", 200.0), ("d800", 800.0)]

# Drivetrain and engine assumptions (game design, documented in the manifest).
GEAR_RATIO = 10.0          # motor : wheel
POLE_PAIRS_DRIVE = 4
PINION_TEETH = 19
PWM_HZ = 10000.0           # inverter switching frequency (audible whine)
TREAD_PITCH_M = 0.12
GEN_RPM = 1500.0           # 4-stroke, 4 cylinders -> firing frequency 50 Hz
GEN_CYL = 4
GEN_FAN_BLADES = 8
DRONE_BLADES = 2
DRONE_POLE_PAIRS = 7


def hump_env(n: int, i0: int, m: int) -> np.ndarray:
    e = np.zeros(n)
    m = max(2, min(m, n - i0))
    e[i0:i0 + m] = np.sin(np.linspace(0.0, math.pi, m)) ** 2
    return e


def per_hz(f: float, loop_s: float = LOOP_S) -> float:
    return round(f * loop_s) / loop_s


def ctl(r, cycles, slope=0.5):
    return A.ctl(N, r, cycles, slope)


def tone(f, r, mod=None, depth=0.0, n=N):
    """Exactly periodic tone (f rounded to whole cycles per loop) with optional zero-mean FM."""
    f = per_hz(f, n / C.SR)
    inst = f * (1.0 + (depth * mod if mod is not None else 0.0))
    return np.sin(2.0 * math.pi * np.cumsum(inst) / C.SR + r.uniform(0, 2 * math.pi))


def pnoise(r, lo, hi, expo=0.0, n=N):
    return C.periodic_noise(n, r, lo, hi, expo)


def circ_distance(x, dist, T, RH, ref_hz):
    ref = float(P.excess_attenuation_db(ref_hz, dist, T, RH))

    def g(f):
        return C.db2lin(-(P.excess_attenuation_db(f, dist, T, RH) - ref))

    return C.circular_filter(x, g)


def stereoize(x, r, width=0.15):
    """Mono machine -> slightly wide stereo by a decorrelated copy of the noise part (fixed source, 2D/3D)."""
    d = np.roll(x, C.ns(0.0007))
    return np.stack([x, (1 - width) * x + width * d], axis=1)


# ------------------------------------------------------------------------------------------------
# Rover
# ------------------------------------------------------------------------------------------------
def rover_drive(r, v):
    veh = {"wheel_radius_m": 0.6}
    wr = v / (2.0 * math.pi * veh["wheel_radius_m"])
    fm = wr * GEAR_RATIO
    fe = POLE_PAIRS_DRIVE * fm
    fmesh = PINION_TEETH * fm
    wobble = ctl(r, 6)
    y = np.zeros(N)
    info = {"speed_ms": v, "wheel_hz": round(wr, 3), "motor_hz": round(fm, 3), "electrical_hz": round(fe, 2), "gear_mesh_hz": round(fmesh, 2)}
    # Auxiliaries (900 W): cooling fans (7 blades, 3000 rpm -> 350 Hz) and a coolant pump hum.
    fan = sum(h ** -1.0 * tone(350.0 * h, r, wobble, 0.002) for h in range(1, 4)) + 0.6 * pnoise(r, 200.0, 4000.0, 1.0)
    pump = tone(50.0, r) + 0.5 * tone(100.0, r) + 0.2 * tone(150.0, r)
    aux = C.unit(fan) * 0.5 + 0.25 * C.unit(pump)
    y += aux
    if v > 0:
        load = min(1.0, 0.3 + v / 15.0)
        motor = (tone(fe, r, wobble, 0.004) + 0.5 * tone(6 * fe, r, wobble, 0.004) + 0.25 * tone(12 * fe, r, wobble, 0.004)
                 + 0.6 * tone(fmesh, r, wobble, 0.004) + 0.2 * tone(2 * fmesh, r, wobble, 0.004))
        pwm = sum(0.5 * tone(PWM_HZ + s * 2 * fe, r) for s in (-1, 1)) + 0.3 * tone(PWM_HZ, r)
        tyre_noise = pnoise(r, 60.0, 3000.0, 1.0) * (1.0 + 0.3 * ctl(r, 40))
        f_tread = v / TREAD_PITCH_M
        tread = sum((0.7 ** h) * tone(f_tread * h, r, wobble, 0.01) for h in range(1, 6))
        # Suspension / body: random low knocks from terrain irregularities (rate ~ v).
        nk = int(r.poisson(1.5 * v * LOOP_S / 3.0))
        kn = np.zeros(N + C.ns(1.0))
        if nk:
            x = C.impulses(kn.size, r.uniform(0, LOOP_S, nk), r.lognormal(0, 0.5, nk))
            kn = C.modal_bank(x, [45.0, 110.0, 260.0], [0.08, 0.04, 0.02], [1.0, 0.5, 0.25])
        knocks = C.wrap_circular(kn, N)
        y = (0.35 * C.unit(aux) + load * 0.8 * C.unit(motor) + 0.08 * C.unit(pwm)
             + min(1.5, v / 6.0) * C.unit(tyre_noise) + 0.3 * min(1.0, v / 8.0) * C.unit(tread) + 0.3 * C.unit(knocks))
        info.update({"tread_hz": round(f_tread, 2), "pwm_hz": PWM_HZ, "suspension_knocks": nk})
    return stereoize(y, r), info


def rover_level_db(v):
    return 52.0 + 12.0 * math.log10(1.0 + v / 3.0)


def generator_run(r):
    f_crank = per_hz(GEN_RPM / 60.0)
    f_fire = GEN_CYL * f_crank / 2.0
    hunt = ctl(r, 4)
    y = np.zeros(N)
    for h in range(1, 13):
        y += h ** -0.9 * tone(f_fire * h, r, hunt, 0.004)
    y += 0.35 * tone(f_crank, r, hunt, 0.004) + 0.2 * tone(1.5 * f_fire, r, hunt, 0.004)
    # Exhaust: broadband noise pulsed at the firing frequency.
    ph = 2.0 * math.pi * np.cumsum(f_fire * (1.0 + 0.004 * hunt)) / C.SR
    pulses = (0.5 + 0.5 * np.cos(ph)) ** 4
    exhaust = pnoise(r, 80.0, 3000.0, 1.0) * pulses
    # Mechanical clatter: valve / injector ticks, twice per firing cycle.
    tick_env = (0.5 + 0.5 * np.cos(2 * ph + 1.0)) ** 30
    clatter = pnoise(r, 1500.0, 9000.0) * tick_env
    fan = sum(h ** -1.0 * tone(f_crank * GEN_FAN_BLADES * h, r, hunt, 0.004) for h in range(1, 3)) + 0.5 * pnoise(r, 150.0, 2500.0, 1.0)
    y = C.unit(y) + 0.6 * C.unit(exhaust) + 0.2 * C.unit(clatter) + 0.3 * C.unit(fan)
    return y, {"rpm": GEN_RPM, "cylinders": GEN_CYL, "firing_hz": round(f_fire, 3), "crank_hz": round(f_crank, 3),
               "fan_bpf_hz": round(f_crank * GEN_FAN_BLADES, 2)}


def generator_transient(r, start: bool):
    dur = r.uniform(4.0, 5.5)
    n = C.ns(dur)
    t = C.tvec(n)
    f_run = GEN_RPM / 60.0
    if start:
        t_crank = r.uniform(0.7, 1.4)
        t_ramp = r.uniform(0.8, 1.5)
        crank_hz = r.uniform(3.5, 4.5)                    # ~240 rpm cranking
        f = np.where(t < t_crank, crank_hz, crank_hz + (f_run - crank_hz) * np.clip((t - t_crank) / t_ramp, 0, 1) ** 0.6)
        f = f * (1.0 + np.where(t > t_crank + t_ramp, 0.03 * np.exp(-(t - t_crank - t_ramp) / 0.5) * np.sin(2 * math.pi * 1.2 * t), 0.0))
        firing = (t > t_crank).astype(float)
        starter = np.where(t < t_crank + 0.1, 1.0, 0.0) * C.lowpass(np.ones(n), 30.0)
        info = {"cranking_s": round(t_crank, 3), "run_up_s": round(t_ramp, 3), "cranking_rpm": round(60 * crank_hz, 1)}
    else:
        t_down = r.uniform(1.5, 2.5)
        f = f_run * np.clip(1.0 - t / t_down, 0.0, 1.0) ** 0.7
        firing = (t < 0.05).astype(float)
        starter = np.zeros(n)
        info = {"run_down_s": round(t_down, 3)}
    ph = 2.0 * math.pi * np.cumsum(f) / C.SR
    comp = (0.5 + 0.5 * np.cos(GEN_CYL / 2.0 * ph)) ** 6                       # compression / firing pulses
    body = C.bandpass(r.standard_normal(n), 40.0, 1500.0) * comp
    tonal = sum(h ** -0.9 * np.sin(h * GEN_CYL / 2.0 * ph) for h in range(1, 8)) * (f / f_run)
    run = (0.6 * C.unit(tonal) + C.unit(body)) * np.maximum(firing, 0.25 if start else 0.0)
    if not start:
        run = (0.6 * C.unit(tonal) + C.unit(body)) * np.clip(f / f_run, 0, 1) ** 0.8
        # final shudder when the engine stops on compression
        j = C.ns(info["run_down_s"])
        run += 0.6 * C.unit(C.modal_bank(C.impulses(n, [info["run_down_s"]], [1.0]), [35.0, 90.0, 220.0], [0.12, 0.06, 0.03], [1, .5, .3])) \
            * (t > info["run_down_s"] - 0.01)
        del j
    starter_whine = (np.sin(2 * math.pi * 900.0 * t) * 0.5 + C.bandpass(r.standard_normal(n), 2000.0, 6000.0) * 0.4) * starter
    y = C.unit(run) + 0.4 * C.unit(starter_whine) if start else C.unit(run)
    y *= C.ramp_env(n, C.ns(0.01), C.ns(0.3))
    return y, info


def rover_door(r, close: bool):
    n = C.ns(1.6)
    t0 = 0.05
    if close:
        x = C.impulses(n, [t0, t0 + r.uniform(0.008, 0.02)], [1.0, 0.6])
        panel = C.modal_bank(x, r.uniform(0.9, 1.1) * np.array([85.0, 160.0, 310.0, 620.0]), [0.12, 0.08, 0.05, 0.02], [1.0, 0.7, 0.4, 0.2])
        latch = C.modal_bank(C.impulses(n, [t0 + 0.003], [1.0]), r.uniform(0.9, 1.1) * np.array([2300.0, 4100.0, 6900.0]),
                             [0.01, 0.006, 0.004], [1, 0.6, 0.4])
        seal = C.noise_burst(n, r, 100.0, 1200.0, 0.002, 0.05, C.ns(t0))       # air pushed out by the gasket
        y = C.unit(panel) + 0.6 * C.unit(latch) + 0.4 * C.unit(seal)
        info = {"action": "close", "panel_modes_hz": [85, 160, 310, 620]}
    else:
        unl = C.modal_bank(C.impulses(n, [t0], [1.0]), r.uniform(0.9, 1.1) * np.array([1800.0, 3300.0, 5200.0]), [0.008, 0.005, 0.003], [1, .6, .4])
        pop = C.noise_burst(n, r, 80.0, 900.0, 0.01, 0.08, C.ns(t0 + 0.08))      # seal release
        hinge = C.bandpass(r.standard_normal(n), 400.0, 2500.0) * hump_env(n, C.ns(t0 + 0.12), C.ns(r.uniform(0.5, 0.8)))
        y = C.unit(unl) + 0.6 * C.unit(pop) + 0.25 * C.unit(hinge)
        info = {"action": "open"}
    return y, info


# ------------------------------------------------------------------------------------------------
# Drone
# ------------------------------------------------------------------------------------------------
def drone_power_ratio(ds, v=0.0, vz=0.0):
    d = ds.equipment["drone"]
    m, rotors, dia = d["mass_kg"], d["rotors"], d["rotor_diameter_m"]
    thrust = m * C.G
    area = rotors * math.pi * (0.5 * dia) ** 2
    ph = thrust ** 1.5 / math.sqrt(2.0 * 1.225 * area) / 0.55
    p = ph * (1.0 + 0.25 * (v / d["max_speed_ms"]) ** 2) + max(0.0, m * C.G * vz / 0.6)
    return p / ph, ph


def drone_loop(ds, r, ratio):
    d = ds.equipment["drone"]
    bpf0 = float(d["blade_pass_hz"]) * ratio ** (1.0 / 3.0)
    y = np.zeros(N)
    rot = []
    for k in range(d["rotors"]):
        # attitude control: each rotor's speed wanders by ~1-2 % (beating between rotors)
        mod = ctl(r, 25, 0.3)
        bpf = bpf0 * (1.0 + r.uniform(-0.012, 0.012))
        shaft = bpf / DRONE_BLADES
        s = sum(h ** -0.8 * tone(bpf * h, r, mod, 0.012) for h in range(1, 11))
        s += 0.25 * tone(shaft, r, mod, 0.012)                                   # blade imbalance (once per revolution)
        s += 0.12 * (tone(DRONE_POLE_PAIRS * shaft, r, mod, 0.012) + 0.5 * tone(2 * DRONE_POLE_PAIRS * shaft, r, mod, 0.012))
        y += s
        rot.append({"bpf_hz": round(bpf, 2), "shaft_hz": round(shaft, 2)})
    ph = 2.0 * math.pi * np.cumsum(np.full(N, per_hz(bpf0))) / C.SR
    broad = pnoise(r, 300.0, 7000.0, 0.8) * (1.0 + 0.35 * np.cos(ph))          # blade-vortex / turbulence noise modulated at BPF
    y = C.unit(y) + 0.45 * C.unit(broad)
    return y, {"bpf_mean_hz": round(bpf0, 2), "rotors": rot, "power_ratio": round(ratio, 4), "blades_per_rotor": DRONE_BLADES,
               "motor_electrical_hz": round(DRONE_POLE_PAIRS * bpf0 / DRONE_BLADES, 1)}


def drone_transient(ds, r, takeoff: bool):
    d = ds.equipment["drone"]
    bpf_h = float(d["blade_pass_hz"])
    dur = r.uniform(4.0, 5.0)
    n = C.ns(dur)
    t = C.tvec(n)
    t_ramp = r.uniform(0.8, 1.4)
    if takeoff:
        prof = np.clip(t / t_ramp, 0, 1) ** 0.7
        climb = np.clip((t - t_ramp - 0.3) / 0.4, 0, 1) * np.clip(1 - (t - t_ramp - 2.5) / 1.0, 0, 1)
        f = bpf_h * (prof * (0.85 + 0.15 * prof) + 0.13 * climb)
        amp = prof ** 1.5 * (1.0 + 0.2 * climb)
        info = {"spin_up_s": round(t_ramp, 3), "climb_bpf_hz": round(bpf_h * 1.13, 1)}
    else:
        t_touch = r.uniform(1.0, 1.8)
        f = bpf_h * np.where(t < t_touch, 0.97, 0.97 * np.clip(1 - (t - t_touch) / t_ramp, 0, 1) ** 0.6)
        amp = np.where(t < t_touch, 1.0, np.clip(1 - (t - t_touch) / t_ramp, 0, 1) ** 1.2)
        info = {"touchdown_s": round(t_touch, 3), "spin_down_s": round(t_ramp, 3)}
    y = np.zeros(n)
    for k in range(d["rotors"]):
        fk = f * (1.0 + r.uniform(-0.01, 0.01))
        ph = 2.0 * math.pi * np.cumsum(fk) / C.SR + r.uniform(0, 6.28)
        y += sum(h ** -0.8 * np.sin(h * ph) for h in range(1, 9))
        y += 0.12 * np.sin(DRONE_POLE_PAIRS / DRONE_BLADES * ph)
    y = C.unit(y) * amp
    broad = C.bandpass(r.standard_normal(n), 300.0, 7000.0) * amp * (f / bpf_h) ** 2
    y = y + 0.45 * C.unit(broad) * np.max(np.abs(y))
    if not takeoff:
        y += 0.4 * C.unit(C.modal_bank(C.impulses(n, [info["touchdown_s"]], [1.0]), [180.0, 520.0, 1400.0], [0.03, 0.015, 0.008], [1, .5, .3]))
    y *= C.ramp_env(n, C.ns(0.02), C.ns(0.2))
    return y, info


# ------------------------------------------------------------------------------------------------
# Handheld
# ------------------------------------------------------------------------------------------------
def shutter(r, burst: bool):
    frames = 1 if not burst else int(r.integers(10, 14))
    fps = 10.0
    exp_s = float(r.choice([1 / 1000, 1 / 500, 1 / 250, 1 / 125]))
    dur = (frames - 1) / fps + 0.25
    n = C.ns(dur)
    y = np.zeros(n)
    modes = r.uniform(0.92, 1.08) * np.array([1800.0, 2900.0, 4300.0, 6800.0, 9500.0])
    taus = np.array([0.006, 0.004, 0.003, 0.002, 0.0015])
    for k in range(frames):
        t0 = 0.02 + k / fps * r.uniform(0.99, 1.01)
        x = C.impulses(n, [t0, t0 + 0.004, t0 + 0.004 + exp_s + 0.002], [0.6, 1.0, 0.9])     # 1st curtain close/open, 2nd curtain
        y += C.modal_bank(x, modes, taus, [1.0, 0.8, 0.6, 0.4, 0.25])
        # re-cocking motor (40-60 ms)
        i0 = C.ns(t0 + 0.012 + exp_s)
        m = C.ns(r.uniform(0.04, 0.06))
        if i0 + m < n:
            tt = C.tvec(m)
            whirr = (np.sin(2 * math.pi * r.uniform(140, 180) * 12 * tt) * 0.5 + C.bandpass(r.standard_normal(m), 1500.0, 6000.0)) \
                * np.sin(np.linspace(0, math.pi, m))
            y[i0:i0 + m] += 0.25 * whirr
    return y, {"frames": frames, "frame_rate_fps": fps if burst else None, "exposure_s": exp_s, "body_modes_hz": [round(m, 1) for m in modes]}


def recorder_button(r):
    n = C.ns(0.4)
    t0 = 0.02
    t1 = t0 + r.uniform(0.08, 0.2)
    modes = r.uniform(0.9, 1.1) * np.array([3200.0, 5400.0, 8100.0])
    y = C.modal_bank(C.impulses(n, [t0, t1], [1.0, 0.6]), modes, [0.0012, 0.0008, 0.0005], [1, .6, .4])
    y += 0.3 * C.unit(C.modal_bank(C.impulses(n, [t0], [1.0]), [180.0, 420.0], [0.01, 0.006], [1, .5]))   # thump into the housing
    return y, {"press_release_s": round(t1 - t0, 3)}


def recorder_handling(r):
    dur = r.uniform(1.5, 3.0)
    n = C.ns(dur + 0.3)
    e = hump_env(n, C.ns(0.05), C.ns(dur))
    rub = C.bandpass(r.standard_normal(n), 200.0, 5000.0) * e * (1 + 0.8 * C.smooth_noise(n, r, 9.0)).clip(0)
    k = int(r.integers(2, 6))
    thumps = C.modal_bank(C.impulses(n, 0.05 + np.sort(r.uniform(0, dur, k)), r.lognormal(0, 0.4, k)), [60.0, 140.0, 330.0],
                          [0.03, 0.015, 0.008], [1.0, 0.6, 0.3])
    y = 0.6 * C.unit(rub) + C.unit(thumps)
    return y, {"handling_s": round(dur, 3), "thumps": k, "note": "bruit solidien transmis à la capsule (basses fréquences)"}


def radio_element(r, kind):
    if kind == "ptt":
        n = C.ns(0.35)
        x = C.impulses(n, [0.02], [1.0])
        y = C.modal_bank(x, r.uniform(0.9, 1.1) * np.array([2600.0, 4800.0]), [0.0015, 0.001], [1, .5])
        # carrier key-up "kerchunk": a short burst of band-limited noise before the audio path settles
        y = C.unit(y) + 0.5 * C.unit(C.noise_burst(n, r, 300.0, 3000.0, 0.002, 0.03, C.ns(0.03)))
        return y, {"element": "push_to_talk"}
    if kind == "squelch":
        n = C.ns(0.5)
        L = r.uniform(0.12, 0.25)
        y = C.bandpass(r.standard_normal(n), 300.0, 3400.0, order=4) * (C.tvec(n) > 0.02) * (C.tvec(n) < 0.02 + L)
        y *= C.ramp_env(n, C.ns(0.002), C.ns(0.002))
        return y, {"element": "squelch_tail", "tail_s": round(L, 3)}
    # roger beep: two short tones in the voice band
    n = C.ns(0.4)
    t = C.tvec(n)
    f1, f2 = r.choice([1000.0, 1200.0]), r.choice([1500.0, 1800.0])
    y = np.where(t < 0.12, np.sin(2 * math.pi * f1 * t), np.sin(2 * math.pi * f2 * t)) * ((t > 0.02) & (t < 0.24))
    y = C.bandpass(y, 300.0, 3400.0) * C.ramp_env(n, C.ns(0.003), C.ns(0.15))
    return y, {"element": "roger_beep", "tones_hz": [f1, f2]}


def radio_static(r, storm: bool):
    y = pnoise(r, 300.0, 3400.0, 0.0, n=N)
    y *= 1.0 + 0.15 * ctl(r, 30)
    info = {"band_hz": [300, 3400]}
    if storm:
        # Atmospherics ("sferics") from distant lightning: impulsive crackles, clustered.
        nc = int(r.poisson(1.2 * LOOP_S))
        tc = r.uniform(0, LOOP_S, nc)
        k = r.poisson(6, nc) + 1
        tt = np.repeat(tc, k) + r.exponential(0.01, int(k.sum()))
        x = C.impulses(N + C.ns(0.5), tt, r.lognormal(0, 0.8, tt.size) * r.choice([-1, 1], tt.size))
        cr = C.wrap_circular(C.bandpass(x, 300.0, 3400.0), N)
        y = C.unit(y) + 1.5 * C.unit(cr)
        info.update({"sferic_clusters": nc, "crackles": int(tt.size)})
    return y, info


# ------------------------------------------------------------------------------------------------
# Jobs and records
# ------------------------------------------------------------------------------------------------
def jobs(ds: D.Dataset) -> list[tuple]:
    return [("equipment", "job_rover", {}), ("equipment", "job_drone", {}), ("equipment", "job_handheld", {})]


def _snd(ds, sid, sub, group, label_fr, cap_fr, cap_en, y, params, lvl, ue, qa=None, loop=False, equipment=None, srcs=None,
         basis=None, normalize=("peak", -1.0), level_extra=None):
    srcs = [s for s in (srcs or []) if ds.has_source(s)]
    q = {"peak_dbfs_max": -0.5}
    if loop:
        q.update({"loop_seam": True})
    if qa:
        q.update(qa)
    lv = {"source_spl_db_1m": round(lvl, 2), "reference": "dB SPL re 20 µPa à 1 m (FieldGear.cpp / equipment.json ; hypothèse de conception)"}
    if level_extra:
        lv.update(level_extra)
    if not loop and re.search(r"_\d{2}$", sid):
        ue = dict(ue, random_group=re.sub(r"_\d{2}$", "", sid))
    return M.Sound(id=sid, category="equipment", subcategory=sub, group=group, label_fr=label_fr, audio=y,
                   confidence="speculative", basis_fr=basis or "Matériel moderne fictif mais plausible (equipment.json) ; synthèse physique "
                   "(raies tonales des machines tournantes, bruit large bande, modes de structure).",
                   sources=srcs, params=params, ue5=ue, qa=q, loop=loop, equipment=equipment, level=lv, normalize=normalize,
                   fade=not loop, caption={"fr": cap_fr, "en": cap_en}, subject="modern_equipment", loop_xfade=loop,
                   variant=params.get("variant_info", {}))


def job_rover(ds):
    veh = ds.equipment["vehicle"]
    out = []
    for v in (0.0, 3.0, 8.0, 15.0):
        sid = f"eq_rover_electric_{'idle' if v == 0 else f'{int(v)}ms'}"
        r = C.rng(sid)
        y, info = rover_drive(r, v)
        lvl = rover_level_db(v)
        params = {**info, "gear_ratio": GEAR_RATIO, "pole_pairs": POLE_PAIRS_DRIVE, "pinion_teeth": PINION_TEETH,
                  "tread_pitch_m": TREAD_PITCH_M, "wheel_radius_m": veh["wheel_radius_m"], "loop_s": LOOP_S,
                  "level_law": "52 + 12 log10(1 + v/3) dB à 1 m (VehicleSystem::updateAuxiliary)",
                  "drivetrain_note_fr": "Rapport de réduction, paires de pôles, denture et pas de sculpture : hypothèses de conception.",
                  "variant_info": {"speed_ms": v}}
        lbl = "au ralenti (auxiliaires)" if v == 0 else f"{v:g} m/s"
        out.append(_snd(ds, sid, "rover_drive", "equipment/rover", f"Véhicule 6×6 — traction électrique {lbl}",
                        "Véhicule (moteur électrique)", "Rover (electric drive)", y, params, lvl,
                        M.ue5("SC_Equipment_Vehicle", "ATT_Vehicle", True, "CON_Equipment_Unique", 0.8,
                              extra={"crossfade_parameter": "speed_ms", "crossfade_points_ms": [0, 3, 8, 15]}),
                        loop=True, equipment="vehicle", normalize=("rms", -20.0, -1.0)))
    # Generator.
    T, RH = A._cond(ds, "hell_creek", "day")
    r = C.rng("eq_rover_generator_run")
    g, ginfo = generator_run(r)
    base = {**ginfo, "loop_s": LOOP_S, "level_law": "80 dB à 1 m (noise_generator_db_1m)",
            "comparison_fr": f"Groupe électrogène ~{80 - 52} dB plus fort que la traction électrique à l'arrêt : perturbation acoustique simulée de la faune."}
    out.append(_snd(ds, "eq_rover_generator_run", "rover_generator", "equipment/rover", "Véhicule 6×6 — groupe électrogène en charge (1 m)",
                    "Groupe électrogène", "Generator running", stereoize(g, r), dict(base, variant_info={"distance_m": 1}),
                    veh["noise_generator_db_1m"], M.ue5("SC_Equipment_Vehicle", "ATT_Vehicle", True, "CON_Equipment_Unique", 0.9),
                    qa={"spectral_peak_hz": [0.9 * ginfo["firing_hz"], 1.1 * ginfo["firing_hz"]], "spectral_peak_search_hz": [15.0, 600.0]},
                    loop=True, equipment="vehicle", normalize=("rms", -18.0, -1.0)))
    for dn, dist in DIST:
        rec = P.received_level_db(veh["noise_generator_db_1m"], ginfo["firing_hz"], dist, T, RH)
        y = circ_distance(g, dist, T, RH, ginfo["firing_hz"])
        p = dict(base, variant_info={"distance_m": dist},
                 propagation={"distance_m": dist, "temperature_c": T, "relative_humidity": RH, "reference_hz": ginfo["firing_hz"],
                              "received_spl_db_at_firing_hz": round(rec, 2),
                              "model": "divergence sphérique + ISO 9613-1, filtrage circulaire (boucle continue)",
                              "file_normalisation": "réponse normalisée à 0 dB à la fréquence d'allumage"})
        out.append(_snd(ds, f"eq_rover_generator_run_{dn}", "rover_generator", "equipment/rover",
                        f"Véhicule 6×6 — groupe électrogène entendu à {dist:g} m", "Groupe électrogène au loin", "Distant generator",
                        stereoize(y, r), p, veh["noise_generator_db_1m"],
                        M.ue5("SC_Equipment_Vehicle", "ATT_Generator_DistanceBaked", True, "CON_Equipment_Unique", 0.7,
                              extra={"distance_variant_m": dist}),
                        qa={"transfer": {"base_id": "eq_rover_generator_run", "distance_m": dist, "temperature_c": T, "relative_humidity": RH,
                                         "foliage_m": 0.0, "ref_hz": ginfo["firing_hz"],
                                         "check_hz": [round(ginfo["firing_hz"] * h, 3) for h in (4, 8, 12)]}},
                        loop=True, equipment="vehicle", srcs=[ISO1], normalize=("rms", -18.0, -1.0),
                        level_extra={"received_spl_db_at_firing_hz": round(rec, 2)}))
    for start in (True, False):
        for k in range(VAR):
            sid = f"eq_rover_generator_{'start' if start else 'stop'}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = generator_transient(r, start)
            out.append(_snd(ds, sid, "rover_generator", "equipment/rover",
                            f"Véhicule 6×6 — {'démarrage' if start else 'arrêt'} du groupe électrogène — variation {k + 1}",
                            "Démarrage du groupe" if start else "Arrêt du groupe", "Generator start" if start else "Generator stop",
                            y, {**info, "variation": k + 1, "variant_info": {"action": "start" if start else "stop", "variation": k + 1}},
                            veh["noise_generator_db_1m"], M.ue5("SC_Equipment_Vehicle", "ATT_Vehicle", False, "CON_Equipment_Unique", 0.9),
                            equipment="vehicle"))
    for close in (False, True):
        for k in range(VAR):
            sid = f"eq_rover_door_{'close' if close else 'open'}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = rover_door(r, close)
            out.append(_snd(ds, sid, "rover_door", "equipment/rover",
                            f"Véhicule 6×6 — {'fermeture' if close else 'ouverture'} de la porte — variation {k + 1}",
                            "Porte du véhicule", "Rover door", y, {**info, "variation": k + 1, "variant_info": {"action": info["action"], "variation": k + 1}},
                            70.0 if close else 55.0, M.ue5("SC_Equipment_Vehicle", "ATT_Foley_Medium", False, "CON_Equipment_Unique", 0.7),
                            equipment="vehicle"))
    return out


def job_drone(ds):
    d = ds.equipment["drone"]
    T, RH = A._cond(ds, "hell_creek", "day")
    out = []
    states = [("hover", 0.0, 0.0), ("cruise", 12.0, 0.0), ("climb", 0.0, 3.0)]
    hover = None
    for name, v, vz in states:
        ratio, ph = drone_power_ratio(ds, v, vz)
        sid = f"eq_drone_{name}"
        r = C.rng(sid)
        y, info = drone_loop(ds, r, ratio)
        lvl = d["noise_db_1m"] + 10.0 * math.log10(ratio)
        if name == "hover":
            hover = (y, info)
        params = {**info, "state": name, "speed_ms": v, "climb_ms": vz, "hover_power_w": round(ph, 1), "loop_s": LOOP_S,
                  "bpf_law": "BPF = blade_pass_hz × (P/P_vol stationnaire)^(1/3)", "level_law": "noise_db_1m + 10 log10(P/P_vol stationnaire) (DroneSystem)",
                  "variant_info": {"state": name}}
        out.append(_snd(ds, sid, "drone", "equipment/drone", f"Drone quadrirotor — {name}", "Drone", "Drone", stereoize(y, r), params, lvl,
                        M.ue5("SC_Equipment_Drone", "ATT_Drone", True, "CON_Equipment_Unique", 0.8),
                        qa={"spectral_peak_hz": [0.93 * info["bpf_mean_hz"], 1.07 * info["bpf_mean_hz"]], "spectral_peak_search_hz": [40.0, 2000.0]},
                        loop=True, equipment="drone_quad", normalize=("rms", -20.0, -1.0)))
    y0, info0 = hover
    for dn, dist in DIST:
        rec = P.received_level_db(d["noise_db_1m"], info0["bpf_mean_hz"], dist, T, RH)
        r = C.rng(f"eq_drone_hover_{dn}")
        y = circ_distance(y0, dist, T, RH, info0["bpf_mean_hz"])
        params = {**info0, "state": "hover", "variant_info": {"state": "hover", "distance_m": dist},
                  "propagation": {"distance_m": dist, "temperature_c": T, "relative_humidity": RH, "reference_hz": info0["bpf_mean_hz"],
                                  "received_spl_db_at_bpf": round(rec, 2), "model": "divergence sphérique + ISO 9613-1, filtrage circulaire"}}
        out.append(_snd(ds, f"eq_drone_hover_{dn}", "drone", "equipment/drone", f"Drone en vol stationnaire entendu à {dist:g} m",
                        "Drone au loin", "Distant drone", stereoize(y, r), params, d["noise_db_1m"],
                        M.ue5("SC_Equipment_Drone", "ATT_Drone_DistanceBaked", True, "CON_Equipment_Unique", 0.6, extra={"distance_variant_m": dist}),
                        qa={"transfer": {"base_id": "eq_drone_hover", "distance_m": dist, "temperature_c": T, "relative_humidity": RH, "foliage_m": 0.0,
                                         "ref_hz": info0["bpf_mean_hz"], "check_hz": [round(info0["bpf_mean_hz"] * h, 3) for h in (3, 6, 10)]}},
                        loop=True, equipment="drone_quad", srcs=[ISO1], normalize=("rms", -20.0, -1.0),
                        level_extra={"received_spl_db_at_bpf": round(rec, 2)}))
    for takeoff in (True, False):
        for k in range(VAR):
            sid = f"eq_drone_{'takeoff' if takeoff else 'landing'}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = drone_transient(ds, r, takeoff)
            out.append(_snd(ds, sid, "drone", "equipment/drone", f"Drone — {'décollage' if takeoff else 'atterrissage'} — variation {k + 1}",
                            "Décollage du drone" if takeoff else "Atterrissage du drone", "Drone take-off" if takeoff else "Drone landing",
                            y, {**info, "variation": k + 1, "variant_info": {"action": "takeoff" if takeoff else "landing", "variation": k + 1}},
                            d["noise_db_1m"], M.ue5("SC_Equipment_Drone", "ATT_Drone", False, "CON_Equipment_Unique", 0.8), equipment="drone_quad"))
    return out


def job_handheld(ds):
    out = []
    for burst in (False, True):
        for k in range(VAR):
            sid = f"eq_camera_shutter_{'burst' if burst else 'single'}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = shutter(r, burst)
            out.append(_snd(ds, sid, "camera_shutter", "equipment/handheld",
                            f"Boîtier photo — obturateur mécanique ({'rafale 10 i/s' if burst else 'vue unique'}) — variation {k + 1}",
                            "Déclenchement photo", "Camera shutter", y, {**info, "variation": k + 1, "variant_info": {"burst": burst, "variation": k + 1},
                                                                       "core_emission": "SoundKind::Mechanical, 55 dB, 3 kHz, 50 ms (ResearchSystem)"},
                            55.0, M.ue5("SC_Equipment_Handheld", "ATT_Handheld_FirstPerson", False, "CON_Equipment_Handheld", 0.7,
                                        extra={"world_emitter_note_fr": "Le cœur émet aussi ce son dans le SoundField (la faune peut l'entendre)."}),
                            qa={"centroid_hz": [1500.0, 9000.0]}, equipment="camera_body_ff"))
    for kind, fn in (("button", recorder_button), ("handling", recorder_handling)):
        for k in range(VAR):
            sid = f"eq_recorder_{kind}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = fn(r)
            out.append(_snd(ds, sid, "audio_recorder", "equipment/handheld",
                            f"Enregistreur autonome — {'bouton' if kind == 'button' else 'manipulation'} — variation {k + 1}",
                            "Enregistreur (bouton)" if kind == "button" else "Manipulation de l'enregistreur",
                            "Recorder button" if kind == "button" else "Recorder handling", y,
                            {**info, "variation": k + 1, "variant_info": {"element": kind, "variation": k + 1}}, 30.0 if kind == "button" else 35.0,
                            M.ue5("SC_Equipment_Handheld", "ATT_Handheld_FirstPerson", False, "CON_Equipment_Handheld", 0.5), equipment="audio_recorder"))
    radio_basis = ("Radio portative de liaison avec le camp de base : absente d'equipment.json, hypothèse de conception (radio FM "
                   "bande étroite, bande audio 300–3 400 Hz). Parasites d'orage = « sphériques » impulsifs des éclairs lointains.")
    for kind in ("ptt", "squelch", "roger"):
        for k in range(VAR):
            sid = f"eq_radio_{kind}_{k + 1:02d}"
            r = C.rng(sid)
            y, info = radio_element(r, kind)
            fr = {"ptt": "alternat (appui)", "squelch": "queue de squelch", "roger": "bip de fin d'émission"}[kind]
            en = {"ptt": "push-to-talk", "squelch": "squelch tail", "roger": "roger beep"}[kind]
            out.append(_snd(ds, sid, "radio", "equipment/radio", f"Radio du camp de base — {fr} — variation {k + 1}", f"Radio : {fr}", f"Radio {en}",
                            y, {**info, "variation": k + 1, "variant_info": {"element": kind, "variation": k + 1}}, 60.0,
                            M.ue5("SC_Equipment_Radio", "ATT_Handheld_FirstPerson", False, "CON_Equipment_Handheld", 0.9),
                            qa={"band_energy_ratio": {"lo_hz": 250.0, "hi_hz": 6000.0, "min": 0.6}} if kind != "ptt" else None,
                            equipment="radio_basecamp", basis=radio_basis))
    for storm in (False, True):
        sid = f"eq_radio_static_{'storm' if storm else 'idle'}"
        r = C.rng(sid)
        y, info = radio_static(r, storm)
        out.append(_snd(ds, sid, "radio", "equipment/radio", f"Radio — souffle de réception{' avec parasites d’orage' if storm else ''} (boucle)",
                        "Parasites radio" if storm else "Souffle radio", "Radio static (storm)" if storm else "Radio hiss",
                        y, {**info, "loop_s": LOOP_S, "variant_info": {"storm": storm}}, 50.0,
                        M.ue5("SC_Equipment_Radio", "ATT_Handheld_FirstPerson", True, "CON_Equipment_Unique", 0.6),
                        qa={"band_energy_ratio": {"lo_hz": 250.0, "hi_hz": 4000.0, "min": 0.8}}, loop=True,
                        equipment="radio_basecamp", basis=radio_basis, normalize=("rms", -24.0, -1.0)))
    return out
