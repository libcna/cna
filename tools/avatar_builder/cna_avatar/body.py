# SPDX-License-Identifier: MS-PL
"""The catalog v2 bodies: soft, rounded toy-like figures skinned to the 71-slot rig.

Limbs are single lofts from the shoulder or hip to the wrist or ankle, so elbows and knees bend
one continuous surface instead of meeting capsules; hands are sculpted (palm pad, knuckles,
tapered jointed fingers, thumb pad); the head is head.py's.
"""
import math

from . import rig, head
from .mathutil import add, sub, mul, normalize, smoothstep, lerp, cross, dot, length, frame_from_axis
from .mesh import Mesh, ellipsoid, tube, blend

I = rig.INDEX
UP_AXES = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))

# Limb profiles: (t along the limb, half-width, half-depth), t = 0 at the shoulder or hip and 1 at
# the wrist or ankle; the elbow and knee sit where the rig puts them.
ARM = {
    "male": [(0.02, 0.046, 0.048), (0.07, 0.053, 0.055), (0.22, 0.051, 0.050), (0.40, 0.045, 0.043),
             (0.52, 0.039, 0.037), (0.60, 0.040, 0.037), (0.70, 0.041, 0.035), (0.86, 0.034, 0.029),
             (1.00, 0.029, 0.023)],
    "female": [(0.02, 0.040, 0.042), (0.07, 0.046, 0.048), (0.22, 0.043, 0.043), (0.40, 0.038, 0.037),
               (0.52, 0.033, 0.032), (0.60, 0.034, 0.032), (0.70, 0.034, 0.030), (0.86, 0.029, 0.025),
               (1.00, 0.025, 0.020)],
}
LEG = {
    "male": [(-0.08, 0.082, 0.084), (0.08, 0.084, 0.086), (0.30, 0.074, 0.076), (0.46, 0.060, 0.062),
             (0.52, 0.056, 0.058), (0.60, 0.057, 0.060), (0.70, 0.058, 0.062), (0.86, 0.046, 0.047),
             (1.00, 0.039, 0.040)],
    "female": [(-0.08, 0.084, 0.086), (0.08, 0.086, 0.088), (0.30, 0.073, 0.075), (0.46, 0.057, 0.059),
               (0.52, 0.052, 0.054), (0.60, 0.053, 0.056), (0.70, 0.054, 0.057), (0.86, 0.042, 0.043),
               (1.00, 0.035, 0.036)],
}
NECK = {"male": 0.054, "female": 0.047}
FOOT = {"male": (0.034, 0.032, 0.090), "female": (0.031, 0.030, 0.082)}
HAND = {"male": dict(palm=(0.047, 0.021, 0.080), finger=0.0126, thumb=0.0142),
        "female": dict(palm=(0.042, 0.019, 0.074), finger=0.0112, thumb=0.0128)}

# Torso sections from the crotch to the base of the neck: (anchor, offset, half-width, half-depth,
# centre z, squareness exponent). Anchors are rig heights.
TORSO = {
    "male": [("hip", -0.060, 0.110, 0.084, 0.000, 2.2), ("hip", -0.010, 0.158, 0.104, -0.004, 2.4),
             ("hip", 0.050, 0.160, 0.104, -0.004, 2.4), ("lower", 0.030, 0.150, 0.098, 0.000, 2.4),
             ("waist", 0.000, 0.144, 0.096, 0.004, 2.4), ("upper", -0.030, 0.150, 0.101, 0.008, 2.5),
             ("upper", 0.040, 0.164, 0.108, 0.012, 2.6), ("upper", 0.110, 0.174, 0.110, 0.012, 2.6),
             ("collar", -0.050, 0.182, 0.104, 0.004, 2.6), ("collar", -0.022, 0.208, 0.094, 0.000, 2.5),
             ("collar", 0.016, 0.130, 0.078, -0.006, 2.2), ("neck", -0.004, 0.064, 0.060, -0.004, 2.0)],
    "female": [("hip", -0.060, 0.112, 0.086, 0.000, 2.2), ("hip", -0.010, 0.166, 0.108, -0.006, 2.3),
               ("hip", 0.050, 0.162, 0.106, -0.006, 2.3), ("lower", 0.030, 0.140, 0.094, 0.000, 2.3),
               ("waist", 0.000, 0.124, 0.088, 0.004, 2.3), ("upper", -0.030, 0.130, 0.094, 0.010, 2.3),
               ("upper", 0.040, 0.140, 0.112, 0.020, 2.3), ("upper", 0.100, 0.148, 0.110, 0.016, 2.4),
               ("collar", -0.044, 0.160, 0.098, 0.006, 2.5), ("collar", -0.020, 0.184, 0.086, 0.000, 2.5),
               ("collar", 0.014, 0.116, 0.072, -0.006, 2.2), ("neck", -0.004, 0.057, 0.054, -0.004, 2.0)],
}


def anchors(body):
    p = rig.PROPORTIONS[body]
    return {"hip": p["hip"][1], "lower": p["back_lower"], "waist": (p["back_lower"] + p["back_upper"]) / 2.0,
            "upper": p["back_upper"], "collar": p["collar"][1], "neck": p["neck"]}


def torso_sections(body, inflate=0.0, scale=1.0):
    """(centre, half-width, half-depth, squareness) rows, crotch to neck."""
    a = anchors(body)
    return [((0.0, a[anchor] + offset, z), rx * scale + inflate, rz * scale + inflate, n)
            for anchor, offset, rx, rz, z, n in TORSO[body]]


def superellipse_ring(centre, rx, rz, n, segments, phase=0.0):
    """Horizontal ring, starting at the front (+z) and going toward the avatar's left (+x)."""
    ring = []
    for k in range(segments):
        a = phase + 2.0 * math.pi * k / segments
        s, c = math.sin(a), math.cos(a)
        x = rx * math.copysign(abs(s) ** (2.0 / n), s)
        z = rz * math.copysign(abs(c) ** (2.0 / n), c)
        ring.append((centre[0] + x, centre[1], centre[2] + z))
    return ring


def torso_weights(body):
    p = rig.PROPORTIONS[body]
    a = anchors(body)
    hip_y, collar_y, neck_y = p["hip"][1], p["collar"][1], p["neck"]
    shoulder_x = p["shoulder"][0]

    def fn(pt):
        x, y = pt[0], pt[1]
        side = "Left" if x >= 0 else "Right"
        ax = abs(x)
        w = blend(I["BackLower"], I["BackUpper"], smoothstep(a["waist"] - 0.06, a["upper"] + 0.05, y))
        extras = {}
        hip = 0.6 * (1.0 - smoothstep(hip_y - 0.07, hip_y + 0.06, y)) * smoothstep(0.0, 0.09, ax)
        if hip > 0:
            extras[I["Hip" + side]] = hip
        collar = 0.55 * smoothstep(collar_y - 0.11, collar_y - 0.01, y) * smoothstep(0.04, 0.14, ax)
        if collar > 0:
            extras[I["Collar" + side]] = collar
        shoulder = 0.5 * smoothstep(shoulder_x - 0.07, shoulder_x - 0.005, ax) * smoothstep(collar_y - 0.15, collar_y - 0.04, y)
        if shoulder > 0:
            extras[I["Shoulder" + side]] = shoulder
        neck = 0.55 * smoothstep(collar_y - 0.01, neck_y + 0.01, y) * (1.0 - smoothstep(0.05, 0.10, ax))
        if neck > 0:
            extras[I["Neck"]] = neck
        total = sum(extras.values())
        if total > 0.92:
            extras = {k: v * 0.92 / total for k, v in extras.items()}
            total = 0.92
        w = {k: v * (1.0 - total) for k, v in w.items()}
        for k, v in extras.items():
            w[k] = w.get(k, 0.0) + v
        return w
    return fn


def section_loft(sections, segments, weight_fn, cap_bottom=None, cap_top=None, uv_band=(0.0, 1.0)):
    """Loft through (centre, rx, rz, n) sections; caps are dome heights (None leaves an end open)."""
    rows = []
    if cap_bottom is not None:
        c, rx, rz, n = sections[0]
        for i in range(4, 0, -1):
            a = math.pi * 0.5 * i / 4
            rows.append(superellipse_ring(add(c, (0.0, -cap_bottom * math.sin(a), 0.0)), rx * math.cos(a) + 1e-5,
                                          rz * math.cos(a) + 1e-5, n, segments))
    for c, rx, rz, n in sections:
        rows.append(superellipse_ring(c, rx, rz, n, segments))
    if cap_top is not None:
        c, rx, rz, n = sections[-1]
        for i in range(1, 5):
            a = math.pi * 0.5 * i / 4
            rows.append(superellipse_ring(add(c, (0.0, cap_top * math.sin(a), 0.0)), rx * math.cos(a) + 1e-5,
                                          rz * math.cos(a) + 1e-5, n, segments))
    return Mesh().grid(rows, lambda p, v, u: weight_fn(p), uv_seam=True)


def torso(body, inflate=0.0, rows=None, segments=32, caps=(0.05, None)):
    sections = torso_sections(body, inflate)
    if rows is not None:
        sections = sections[rows[0]:rows[1] + 1]
    return section_loft(sections, segments, torso_weights(body), cap_bottom=caps[0], cap_top=caps[1])


def _densify(profile, steps):
    """Profile rows resampled to `steps` evenly spaced t values (smooth cosine interpolation)."""
    out = []
    t0, t1 = profile[0][0], profile[-1][0]
    for k in range(steps + 1):
        t = t0 + (t1 - t0) * k / steps
        for (ta, wa, da), (tb, wb, db) in zip(profile, profile[1:]):
            if ta <= t <= tb:
                s = 0.5 - 0.5 * math.cos(math.pi * (t - ta) / (tb - ta))
                out.append((t, wa + (wb - wa) * s, da + (db - da) * s))
                break
    return out


def limb(start, middle, end, profile, weights, segments=16, steps=20, inflate=0.0, t_range=None, hint=(0.0, 0.0, 1.0),
         cap_start=True, cap_end=True, flare=0.0):
    """A loft through two straight segments (start-middle-end); profile t is 0 at `start`, the
    middle joint's share of the length at `middle`, 1 at `end` (and may extend past either end).
    weights(t) gives the skin weights along the limb."""
    upper, lower = length(sub(middle, start)), length(sub(end, middle))
    total = upper + lower
    knee_t = upper / total
    axis_a, axis_b = normalize(sub(middle, start)), normalize(sub(end, middle))
    rows_def = _densify(profile, steps)
    if t_range:
        rows_def = [r for r in rows_def if t_range[0] - 1e-9 <= r[0] <= t_range[1] + 1e-9]
    rows, ts = [], []
    for index, (t, w, d) in enumerate(rows_def):
        if t <= knee_t:
            centre = add(start, mul(axis_a, t * total))
            axis = axis_a
        else:
            centre = add(middle, mul(axis_b, (t - knee_t) * total))
            axis = axis_b
        # Around the joint the ring plane turns halfway between the two segments.
        k = smoothstep(knee_t - 0.06, knee_t + 0.06, t)
        axis = normalize(lerp(axis_a, axis_b, k))
        u, v, _ = frame_from_axis(axis, hint)
        if flare and t_range and index == len(rows_def) - 1:
            w, d = w + flare, d + flare
        ring = []
        for c in range(segments):
            a = 2.0 * math.pi * c / segments
            ring.append(add(centre, add(mul(u, (w + inflate) * math.cos(a)), mul(v, (d + inflate) * math.sin(a)))))
        rows.append(ring)
        ts.append(t)
    # Rounded caps.
    def cap(ring_index, direction, count=4):
        centre = mul(rows[ring_index][0], 0.0)
        for p in rows[ring_index]:
            centre = add(centre, p)
        centre = mul(centre, 1.0 / segments)
        radius = sum(length(sub(p, centre)) for p in rows[ring_index]) / segments
        out = []
        for i in range(1, count + 1):
            a = math.pi * 0.5 * i / count
            out.append(([add(add(centre, mul(sub(p, centre), math.cos(a) + 1e-4)), mul(direction, radius * 0.9 * math.sin(a)))
                         for p in rows[ring_index]], ts[ring_index]))
        return out
    if cap_start:
        extra = cap(0, mul(axis_a, -1.0))
        rows = [r for r, _ in reversed(extra)] + rows
        ts = [t for _, t in reversed(extra)] + ts
    if cap_end:
        extra = cap(len(rows) - 1, axis_b)
        rows = rows + [r for r, _ in extra]
        ts = ts + [t for _, t in extra]
    last = len(rows) - 1
    return Mesh().grid(rows, lambda p, vv, uu: weights(ts[int(round(vv * last))]), uv_seam=True)


def arm_weights(body, side):
    p = rig.PROPORTIONS[body]
    elbow_t = p["upper_arm"] / (p["upper_arm"] + p["forearm"])
    collar, shoulder, elbow, wrist = (I[n + side] for n in ("Collar", "Shoulder", "Elbow", "Wrist"))

    def fn(t):
        w = {}
        k = smoothstep(elbow_t - 0.07, elbow_t + 0.07, t)
        w[shoulder], w[elbow] = 1.0 - k, k
        c = 0.35 * (1.0 - smoothstep(-0.10, 0.10, t))
        if c > 0:
            w = {j: v * (1.0 - c) for j, v in w.items()}
            w[collar] = w.get(collar, 0.0) + c
        r = 0.5 * smoothstep(0.90, 1.02, t)
        if r > 0:
            w = {j: v * (1.0 - r) for j, v in w.items()}
            w[wrist] = w.get(wrist, 0.0) + r
        return w
    return fn


def leg_weights(body, side):
    p = rig.PROPORTIONS[body]
    hip, knee, ankle = (I[n + side] for n in ("Hip", "Knee", "Ankle"))
    top = rig.joint_positions(body)
    knee_t = length(sub(top[knee], top[hip])) / (length(sub(top[knee], top[hip])) + length(sub(top[ankle], top[knee])))

    def fn(t):
        k = smoothstep(knee_t - 0.07, knee_t + 0.07, t)
        w = {hip: 1.0 - k, knee: k}
        b = 0.3 * (1.0 - smoothstep(-0.08, 0.06, t))
        if b > 0:
            w = {j: v * (1.0 - b) for j, v in w.items()}
            w[I["BackLower"]] = b
        a = 0.5 * smoothstep(0.90, 1.02, t)
        if a > 0:
            w = {j: v * (1.0 - a) for j, v in w.items()}
            w[ankle] = w.get(ankle, 0.0) + a
        return w
    return fn


def arm(body, side, inflate=0.0, t_range=None, flare=0.0, segments=16):
    pos = rig.joint_positions(body)
    sh, el, wr = pos[I["Shoulder" + side]], pos[I["Elbow" + side]], pos[I["Wrist" + side]]
    return limb(sh, el, wr, ARM[body], arm_weights(body, side), segments=segments, inflate=inflate, t_range=t_range,
                cap_start=t_range is None or t_range[0] <= 0.03, cap_end=t_range is None, flare=flare,
                hint=(0.0, 0.0, 1.0))


def leg(body, side, inflate=0.0, t_range=None, flare=0.0, segments=16):
    pos = rig.joint_positions(body)
    hip, knee, ankle = pos[I["Hip" + side]], pos[I["Knee" + side]], pos[I["Ankle" + side]]
    return limb(hip, knee, ankle, LEG[body], leg_weights(body, side), segments=segments, inflate=inflate,
                t_range=t_range, cap_start=t_range is None or t_range[0] <= -0.07, cap_end=t_range is None, flare=flare,
                hint=(0.0, 0.0, 1.0))


def hand(body, side, sign):
    """Palm pad from the wrist to the knuckles, knuckle row, four jointed fingers and a thumb with
    its pad. Fingers keep the rig's straight bind line; their shape does the rounding."""
    pos = rig.joint_positions(body)
    p = rig.PROPORTIONS[body]
    H = HAND[body]
    k = p["hand"]
    along, normal, spread = rig.hand_frame(sign, p)
    wr = I["Wrist" + side]
    wrist = pos[wr]
    m = Mesh()
    pw, pt, pl = H["palm"]
    pw, pt, pl = pw * k, pt * k, pl * k
    # Palm: superellipse sections from the wrist to the knuckles, thickest at the heel of the hand.
    rows = []
    steps = 9
    for i in range(steps + 1):
        t = i / steps
        c = add(wrist, mul(along, (0.004 + t * (pl - 0.008))))
        width = pw * (0.66 + 0.34 * smoothstep(0.0, 0.45, t)) * (1.0 - 0.06 * smoothstep(0.8, 1.0, t))
        thick = pt * (1.0 + 0.18 * math.sin(math.pi * min(t * 1.4, 1.0))) * (1.0 - 0.25 * smoothstep(0.75, 1.0, t))
        ring = []
        for s in range(16):
            a = 2.0 * math.pi * s / 16
            ca, sa = math.cos(a), math.sin(a)
            x = width * math.copysign(abs(ca) ** 0.8, ca)
            y = thick * math.copysign(abs(sa) ** 0.9, sa)
            ring.append(add(c, add(mul(spread, x), mul(normal, y))))
        rows.append(ring)
    # Close both ends with shallow domes.
    def dome(ring, direction, depth):
        centre = mul(ring[0], 0.0)
        for q in ring:
            centre = add(centre, q)
        centre = mul(centre, 1.0 / len(ring))
        return [[add(add(centre, mul(sub(q, centre), math.cos(math.pi * 0.5 * j / 3) + 1e-4)),
                     mul(direction, depth * math.sin(math.pi * 0.5 * j / 3))) for q in ring] for j in (1, 2, 3)]
    rows = list(reversed(dome(rows[0], mul(along, -1.0), 0.010 * k))) + rows + dome(rows[-1], along, 0.012 * k)
    palm = Mesh().grid(rows, lambda q, v, u: {wr: 1.0})
    m.append(palm)
    # Thumb pad (thenar), toward the thumb side of the palm.
    thumb_root = pos[I["FingerThumb" + side]]
    pad = add(lerp(wrist, thumb_root, 0.55), mul(normal, 0.006 * k))
    m.append(ellipsoid(pad, (spread, along, normal), (0.016 * k, 0.026 * k, 0.014 * k), 12, 8,
                       lambda q, v, u: {wr: 0.7, I["FingerThumb" + side]: 0.3}))
    for finger in rig.FINGERS + ("Thumb",):
        joints = [I["Finger%s%s" % (finger, side)], I["Finger%s2%s" % (finger, side)], I["Finger%s3%s" % (finger, side)]]
        points = [pos[j] for j in joints]
        direction = rig.thumb_direction(sign, p) if finger == "Thumb" else along
        tip = add(points[2], mul(direction, rig.FINGER_SEGMENTS[finger][2] * k * 0.9))
        points.append(tip)
        base = H["thumb" if finger == "Thumb" else "finger"] * k * {"Index": 1.0, "Middle": 1.04, "Ring": 0.98,
                                                                      "Small": 0.86, "Thumb": 1.0}[finger]
        start = sub(points[0], mul(direction, 0.012 * k))
        path, weights, radii = [start], [{wr: 0.6, joints[0]: 0.4}], [base * 1.05]
        for s in range(3):
            parent = wr if s == 0 else joints[s - 1]
            a, b = points[s], points[s + 1]
            # Knuckle bulge at each joint, slimmer between.
            path += [a, lerp(a, b, 0.5)]
            weights += [{parent: 0.5, joints[s]: 0.5}, {joints[s]: 1.0}]
            radii += [base * (1.06 - 0.09 * s), base * (0.94 - 0.09 * s)]
        path.append(points[3])
        weights.append({joints[2]: 1.0})
        radii.append(base * 0.74)
        m.append(tube(path, radii, 8, None, row_weights=weights, round_end=True))
    return m


def foot(body, side, inflate=0.0, segments=16):
    pos = rig.joint_positions(body)
    ankle = pos[I["Ankle" + side]]
    rx, ry, rz = FOOT[body]
    centre = (ankle[0], ry + 0.020, ankle[2] + 0.048)
    toe_z = pos[I["Toe" + side]][2]
    return ellipsoid(centre, UP_AXES, (rx + inflate, ry + inflate, rz + inflate), segments, 12,
                     lambda p, v, u: blend(I["Ankle" + side], I["Toe" + side], smoothstep(toe_z - 0.06, toe_z, p[2])))


def neck(body, inflate=0.0):
    pos = rig.joint_positions(body)
    base = add(pos[I["Neck"]], (0.0, -0.05, -0.004))
    top = add(pos[I["Head"]], (0.0, 0.035, 0.010))
    r = NECK[body] + inflate

    def weights(t):
        w = blend(I["BackUpper"], I["Neck"], smoothstep(0.0, 0.35, t))
        if t > 0.55:
            k = smoothstep(0.55, 1.0, t)
            w = {j: v * (1.0 - k) for j, v in w.items()}
            w[I["Head"]] = w.get(I["Head"], 0.0) + k
        return w
    profile = [(0.0, r * 1.25, r * 1.1), (0.25, r * 1.02, r * 0.98), (0.6, r, r * 0.96), (1.0, r * 0.94, r * 0.92)]
    return limb(base, lerp(base, top, 0.5), top, profile, weights, segments=18, steps=10)


def build(body):
    """(skin Mesh without the head, head Mesh, nose/ears Mesh)."""
    skin = Mesh()
    skin.append(torso(body)).append(neck(body))
    for side, sign in rig.SIDES:
        skin.append(arm(body, side))
        skin.append(hand(body, side, sign))
        skin.append(leg(body, side))
        skin.append(foot(body, side))
    details = Mesh().append(head.nose_mesh(body)).append(head.ear_meshes(body))
    return skin, head.head_mesh(body), details
