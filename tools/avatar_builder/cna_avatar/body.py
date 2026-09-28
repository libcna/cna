# SPDX-License-Identifier: MS-PL
"""The avatar body: a friendly, toy-like figure skinned to the 71-slot rig."""
import math

from . import rig
from .mathutil import add, sub, mul, normalize, smoothstep, lerp
from .mesh import Mesh, ellipsoid, capsule, loft, tube, segment_weights, blend

I = rig.INDEX
UP_AXES = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))

# Limb radii per body type: (start, end).
LIMBS = {
    "male": dict(upper_arm=(0.058, 0.046), forearm=(0.046, 0.038), thigh=(0.085, 0.060), shin=(0.056, 0.041),
                 neck=0.052, palm=(0.050, 0.056, 0.028), finger=0.0135, foot=(0.047, 0.046, 0.112)),
    "female": dict(upper_arm=(0.050, 0.040), forearm=(0.040, 0.033), thigh=(0.084, 0.056), shin=(0.052, 0.037),
                   neck=0.046, palm=(0.046, 0.052, 0.026), finger=0.0125, foot=(0.042, 0.042, 0.102)),
}

# Torso cross-sections: (anchor, offset, rx, rz, z) from crotch to neck.
TORSO = {
    "male": [("hip", -0.065, 0.12, 0.090, 0.0), ("hip", 0.0, 0.165, 0.110, 0.0), ("lower", 0.02, 0.160, 0.106, 0.0),
             ("waist", 0.0, 0.148, 0.100, 0.005), ("upper", 0.0, 0.158, 0.108, 0.01), ("upper", 0.09, 0.172, 0.114, 0.015),
             ("collar", -0.045, 0.178, 0.106, 0.005), ("collar", -0.005, 0.150, 0.090, 0.0),
             ("neck", 0.0, 0.072, 0.062, -0.005)],
    "female": [("hip", -0.065, 0.125, 0.092, 0.0), ("hip", 0.0, 0.172, 0.114, 0.0), ("lower", 0.02, 0.160, 0.106, 0.0),
               ("waist", 0.0, 0.128, 0.092, 0.005), ("upper", 0.0, 0.140, 0.102, 0.01), ("upper", 0.08, 0.152, 0.116, 0.022),
               ("collar", -0.04, 0.156, 0.100, 0.008), ("collar", -0.005, 0.132, 0.084, 0.0),
               ("neck", 0.0, 0.064, 0.056, -0.005)],
}


def anchors(body):
    p = rig.PROPORTIONS[body]
    return {"hip": p["hip"][1], "lower": p["back_lower"], "waist": (p["back_lower"] + p["back_upper"]) / 2.0,
            "upper": p["back_upper"], "collar": p["collar"][1], "neck": p["neck"]}


def torso_sections(body, inflate=0.0, scale=1.0):
    a = anchors(body)
    return [((0.0, a[anchor] + offset, z), rx * scale + inflate, rz * scale + inflate)
            for anchor, offset, rx, rz, z in TORSO[body]]


def torso_weights(body):
    p = rig.PROPORTIONS[body]
    a = anchors(body)
    hip_y, collar_y, neck_y = p["hip"][1], p["collar"][1], p["neck"]
    shoulder_x = p["shoulder"][0]

    def fn(pt):
        x, y = pt[0], pt[1]
        side = "Left" if x >= 0 else "Right"
        ax = abs(x)
        w = blend(I["BackLower"], I["BackUpper"], smoothstep(a["waist"] - 0.05, a["upper"] + 0.06, y))
        extras = {}
        hip = 0.6 * (1.0 - smoothstep(hip_y - 0.07, hip_y + 0.05, y)) * smoothstep(0.0, 0.08, ax)
        if hip > 0:
            extras[I["Hip" + side]] = hip
        collar = 0.6 * smoothstep(collar_y - 0.10, collar_y, y) * smoothstep(0.05, 0.15, ax)
        if collar > 0:
            extras[I["Collar" + side]] = collar
        shoulder = 0.45 * smoothstep(shoulder_x - 0.06, shoulder_x, ax) * smoothstep(collar_y - 0.13, collar_y - 0.03, y)
        if shoulder > 0:
            extras[I["Shoulder" + side]] = shoulder
        neck = 0.5 * smoothstep(collar_y, neck_y, y) * (1.0 - smoothstep(0.05, 0.10, ax))
        if neck > 0:
            extras[I["Neck"]] = neck
        total = sum(extras.values())
        if total > 0.9:
            extras = {k: v * 0.9 / total for k, v in extras.items()}
            total = 0.9
        w = {k: v * (1.0 - total) for k, v in w.items()}
        for k, v in extras.items():
            w[k] = w.get(k, 0.0) + v
        return w
    return fn


def head_center(body):
    return (0.0, rig.PROPORTIONS[body]["head_center"], 0.005)


def head_mesh(body, segments=24, rings=16):
    return ellipsoid(head_center(body), UP_AXES, rig.HEAD_RADII, segments, rings, lambda p, v, u: {I["Head"]: 1.0},
                     uv_seam=True, phase=math.pi)


def head_details(body):
    c = head_center(body)
    head = lambda p, v, u: {I["Head"]: 1.0}
    m = Mesh()
    for s in (1.0, -1.0):
        m.append(ellipsoid(add(c, (s * 0.121, -0.008, -0.004)), UP_AXES, (0.020, 0.033, 0.017), 12, 8, head))
    m.append(ellipsoid(add(c, (0.0, -0.032, 0.13)), UP_AXES, (0.017, 0.016, 0.018), 12, 8, head))
    return m


def arm(body, side, sign, inflate=0.0, upper_range=(0.0, 1.0), fore_range=None, open_ends=False, flare=0.0,
        segments=12):
    """Upper arm and forearm; a clothing sleeve passes ranges and inflation."""
    pos = rig.joint_positions(body)
    L = LIMBS[body]
    sh, el, wr = pos[I["Shoulder" + side]], pos[I["Elbow" + side]], pos[I["Wrist" + side]]
    m = Mesh()
    ua0, ua1 = L["upper_arm"]
    fa0, fa1 = L["forearm"]
    upper = segment_weights(I["Collar" + side], I["Shoulder" + side], I["Elbow" + side])
    if upper_range:
        m.append(capsule(sh, el, ua0 + inflate, ua1 + inflate, segments, upper, body_rings=8,
                         cap_start=not open_ends or upper_range[0] == 0.0,
                         cap_end=not open_ends and upper_range[1] >= 1.0,
                         start_t=upper_range[0], end_t=upper_range[1], flare=flare))
    if fore_range:
        fore = segment_weights(I["Shoulder" + side], I["Elbow" + side], I["Wrist" + side])
        m.append(capsule(el, wr, fa0 + inflate, fa1 + inflate, segments, fore, body_rings=8,
                         cap_start=True, cap_end=not open_ends and fore_range[1] >= 1.0,
                         start_t=fore_range[0], end_t=fore_range[1], flare=flare))
    return m


def hand(body, side, sign):
    pos = rig.joint_positions(body)
    p = rig.PROPORTIONS[body]
    L = LIMBS[body]
    along, normal, spread = rig.hand_frame(sign, p)
    wrist = pos[I["Wrist" + side]]
    k = p["hand"]
    m = Mesh()
    palm_center = add(add(wrist, mul(along, 0.047 * k)), mul(normal, -0.002))
    wr = I["Wrist" + side]
    m.append(ellipsoid(palm_center, (spread, along, normal), L["palm"], 12, 8,
                       lambda pt, v, u: {wr: 1.0}))
    radius = L["finger"]
    for finger in rig.FINGERS + ("Thumb",):
        joints = [I["Finger%s%s" % (finger, side)], I["Finger%s2%s" % (finger, side)], I["Finger%s3%s" % (finger, side)]]
        points = [pos[j] for j in joints]
        direction = rig.thumb_direction(sign, p) if finger == "Thumb" else along
        points.append(add(points[2], mul(direction, rig.FINGER_SEGMENTS[finger][2] * k)))
        base_radius = radius * (1.15 if finger == "Thumb" else 0.9 if finger == "Small" else 1.0)
        # Each segment's midpoint belongs to its bone; the joints between are shared half and half.
        path, weights, radii = [], [], []
        for segment in range(3):
            parent = wr if segment == 0 else joints[segment - 1]
            path += [points[segment], lerp(points[segment], points[segment + 1], 0.5)]
            weights += [{parent: 0.5, joints[segment]: 0.5}, {joints[segment]: 1.0}]
            radii += [base_radius * (1.0 - 0.07 * segment), base_radius * (1.0 - 0.07 * segment - 0.035)]
        path.append(points[3])
        weights.append({joints[2]: 1.0})
        radii.append(base_radius * 0.8)
        m.append(tube(path, radii, 8, None, row_weights=weights, round_end=True))
    return m


def leg(body, side, sign, inflate=0.0, thigh_range=(0.0, 1.0), shin_range=(0.0, 1.0), open_ends=False, flare=0.0,
        segments=14):
    pos = rig.joint_positions(body)
    L = LIMBS[body]
    hip, knee, ankle = pos[I["Hip" + side]], pos[I["Knee" + side]], pos[I["Ankle" + side]]
    m = Mesh()
    if thigh_range:
        m.append(capsule(hip, knee, L["thigh"][0] + inflate, L["thigh"][1] + inflate, segments,
                         segment_weights(I["BackLower"], I["Hip" + side], I["Knee" + side], 0.2, 0.25), body_rings=8,
                         cap_start=True, cap_end=not open_ends and thigh_range[1] >= 1.0,
                         start_t=thigh_range[0], end_t=thigh_range[1], flare=flare))
    if shin_range:
        m.append(capsule(knee, ankle, L["shin"][0] + inflate, L["shin"][1] + inflate, segments,
                         segment_weights(I["Hip" + side], I["Knee" + side], I["Ankle" + side], 0.25, 0.25), body_rings=8,
                         cap_start=True, cap_end=not open_ends and shin_range[1] >= 1.0,
                         start_t=shin_range[0], end_t=shin_range[1], flare=flare))
    return m


def foot(body, side, sign, inflate=0.0, segments=14):
    pos = rig.joint_positions(body)
    L = LIMBS[body]
    ankle = pos[I["Ankle" + side]]
    rx, ry, rz = L["foot"]
    center = (ankle[0], ry * 0.95, ankle[2] + 0.035)
    toe_z = pos[I["Toe" + side]][2]
    return ellipsoid(center, UP_AXES, (rx + inflate, ry + inflate, rz + inflate), segments, 12,
                     lambda p, v, u: blend(I["Ankle" + side], I["Toe" + side], smoothstep(toe_z - 0.06, toe_z, p[2])))


def neck(body, inflate=0.0):
    pos = rig.joint_positions(body)
    top = add(pos[I["Head"]], (0.0, 0.05, 0.0))
    r = LIMBS[body]["neck"] + inflate
    return capsule(pos[I["Neck"]], top, r, r * 0.95, 16,
                   segment_weights(I["BackUpper"], I["Neck"], I["Head"], 0.3, 0.3), body_rings=4)


def torso(body):
    return loft(torso_sections(body), 24, torso_weights(body), cap_bottom=0.05, cap_top=0.02)


def build(body):
    """Returns (skin Mesh without head, head Mesh)."""
    skin = Mesh()
    skin.append(torso(body)).append(neck(body)).append(head_details(body))
    for side, sign in rig.SIDES:
        skin.append(arm(body, side, sign, fore_range=(0.0, 1.0)))
        skin.append(hand(body, side, sign))
        skin.append(leg(body, side, sign))
        skin.append(foot(body, side, sign))
    return skin, head_mesh(body)
