# SPDX-License-Identifier: MS-PL
"""The canonical CNA avatar rig: exactly XNA's 71 bone slots and parent table.

Model space is meters, Y up, the avatar facing +Z, feet on y = 0; the avatar's left is +X.
Every bind rotation is identity, so a bind pose is only the joint translations, and a rotation
authored for one body type fits every body and height.
"""
import math
from .mathutil import add, mul, normalize, cross, lerp

# Parent of every slot, identical to the table read from the XNA reference assembly (and to
# kParentBoneIds in AvatarRenderer.cpp; test_catalog checks both agree).
PARENTS = [
    -1, 0, 0, 0, 0, 1, 2, 2, 3, 3, 1, 6, 5, 6, 5, 8, 5, 8, 5, 14, 12, 11, 16, 15, 14, 20, 20, 20, 22, 22, 22,
    25, 25, 25, 28, 28, 28, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 43, 44,
    45, 46, 47, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
]
BONE_COUNT = 71

# AvatarBone names; slots XNA leaves unnamed are helper joints here ("Slot04", ...), placed at a
# sensible point and never skinned or animated by CNA assets.
NAMED = {
    0: "Root", 1: "BackLower", 2: "HipLeft", 3: "HipRight", 5: "BackUpper", 6: "KneeLeft", 8: "KneeRight",
    11: "AnkleLeft", 12: "CollarLeft", 14: "Neck", 15: "AnkleRight", 16: "CollarRight", 19: "Head",
    20: "ShoulderLeft", 21: "ToeLeft", 22: "ShoulderRight", 23: "ToeRight", 25: "ElbowLeft", 28: "ElbowRight",
    33: "WristLeft", 36: "WristRight", 37: "FingerIndexLeft", 38: "FingerMiddleLeft", 39: "FingerRingLeft",
    40: "FingerSmallLeft", 41: "PropLeft", 42: "SpecialLeft", 43: "FingerThumbLeft", 44: "FingerIndexRight",
    45: "FingerMiddleRight", 46: "FingerRingRight", 47: "FingerSmallRight", 48: "PropRight",
    49: "SpecialRight", 50: "FingerThumbRight", 51: "FingerIndex2Left", 52: "FingerMiddle2Left",
    53: "FingerRing2Left", 54: "FingerSmall2Left", 55: "FingerThumb2Left", 56: "FingerIndex2Right",
    57: "FingerMiddle2Right", 58: "FingerRing2Right", 59: "FingerSmall2Right", 60: "FingerThumb2Right",
    61: "FingerIndex3Left", 62: "FingerMiddle3Left", 63: "FingerRing3Left", 64: "FingerSmall3Left",
    65: "FingerThumb3Left", 66: "FingerIndex3Right", 67: "FingerMiddle3Right", 68: "FingerRing3Right",
    69: "FingerSmall3Right", 70: "FingerThumb3Right",
}
NAMES = [NAMED.get(i, "Slot%02d" % i) for i in range(BONE_COUNT)]
INDEX = {name: i for i, name in enumerate(NAMES)}
HELPERS = [i for i in range(BONE_COUNT) if i not in NAMED] + [INDEX[n] for n in
           ("PropLeft", "PropRight", "SpecialLeft", "SpecialRight")]

FINGERS = ("Index", "Middle", "Ring", "Small")
SIDES = (("Left", 1.0), ("Right", -1.0))

BODY_TYPES = ("female", "male")

# Authored proportions (catalog v3: a head about a fifth larger on a shorter neck, the shoulders
# and chest lowered to meet it, shorter legs, sturdier limbs and larger hands -- a friendlier
# toy-like figure); the renderer scales uniformly to a description's height.
PROPORTIONS = {
    "male": dict(height=1.80, head_center=1.566, head_joint=1.369, neck=1.321, collar=(0.037, 1.290),
                 shoulder=(0.190, 1.266), back_upper=1.078, back_lower=0.890, hip=(0.100, 0.806),
                 knee=(0.101, 0.444), ankle=(0.100, 0.082), toe=(0.100, 0.024, 0.132), arm_angle=73.0,
                 upper_arm=0.246, forearm=0.222, hand=1.40),
    "female": dict(height=1.68, head_center=1.457, head_joint=1.270, neck=1.227, collar=(0.034, 1.198),
                   shoulder=(0.170, 1.178), back_upper=1.003, back_lower=0.834, hip=(0.102, 0.760),
                   knee=(0.096, 0.420), ankle=(0.092, 0.078), toe=(0.092, 0.021, 0.120), arm_angle=73.0,
                   upper_arm=0.226, forearm=0.206, hand=1.28),
}
FINGER_SEGMENTS = {"Index": (0.027, 0.019, 0.014), "Middle": (0.029, 0.021, 0.015),
                   "Ring": (0.027, 0.019, 0.014), "Small": (0.022, 0.016, 0.012), "Thumb": (0.026, 0.021, 0.015)}
FINGER_SPREAD = {"Index": 0.026, "Middle": 0.009, "Ring": -0.009, "Small": -0.025}


def arm_direction(sign, p):
    a = math.radians(p["arm_angle"])
    return (sign * math.cos(a), -math.sin(a), 0.0)


def hand_frame(sign, p):
    """(along, palm normal toward the body, spread toward the front) for one hand."""
    d = arm_direction(sign, p)
    n = mul(normalize(cross(d, (0.0, 0.0, 1.0))), sign)
    return d, n, (0.0, 0.0, 1.0)


def joint_positions(body):
    """World-space bind position of each of the 71 joints."""
    p = PROPORTIONS[body]
    pos = [None] * BONE_COUNT
    j = lambda name, value: pos.__setitem__(INDEX[name], value)
    j("Root", (0.0, 0.0, 0.0))
    j("BackLower", (0.0, p["back_lower"], -0.01))
    j("BackUpper", (0.0, p["back_upper"], -0.015))
    j("Neck", (0.0, p["neck"], -0.01))
    j("Head", (0.0, p["head_joint"], -0.005))
    pos[4] = (0.0, p["hip"][1], 0.0)
    pos[10] = (0.0, p["back_lower"] + 0.07, 0.06)
    pos[18] = (0.0, p["back_upper"] + 0.11, 0.06)
    pos[24] = (0.0, p["head_center"] - 0.03, 0.10)
    for side, s in SIDES:
        j("Hip" + side, (s * p["hip"][0], p["hip"][1], 0.0))
        j("Knee" + side, (s * p["knee"][0], p["knee"][1], 0.012))
        j("Ankle" + side, (s * p["ankle"][0], p["ankle"][1], -0.015))
        j("Toe" + side, (s * p["toe"][0], p["toe"][1], p["toe"][2]))
        j("Collar" + side, (s * p["collar"][0], p["collar"][1], -0.005))
        shoulder = (s * p["shoulder"][0], p["shoulder"][1], -0.01)
        j("Shoulder" + side, shoulder)
        d = arm_direction(s, p)
        elbow = add(shoulder, mul(d, p["upper_arm"]))
        wrist = add(elbow, mul(d, p["forearm"]))
        j("Elbow" + side, elbow)
        j("Wrist" + side, wrist)
        along, normal, spread = hand_frame(s, p)
        k = p["hand"]
        for finger in FINGERS:
            back = -0.005 if finger == "Small" else 0.0
            base = add(add(wrist, mul(along, (0.085 + back) * k)), mul(spread, FINGER_SPREAD[finger] * k))
            lengths = FINGER_SEGMENTS[finger]
            j("Finger%s%s" % (finger, side), base)
            j("Finger%s2%s" % (finger, side), add(base, mul(along, lengths[0] * k)))
            j("Finger%s3%s" % (finger, side), add(base, mul(along, (lengths[0] + lengths[1]) * k)))
        thumb_dir = thumb_direction(s, p)
        thumb = add(add(add(wrist, mul(along, 0.025 * k)), mul(spread, 0.028 * k)), mul(normal, 0.012 * k))
        lengths = FINGER_SEGMENTS["Thumb"]
        j("FingerThumb" + side, thumb)
        j("FingerThumb2" + side, add(thumb, mul(thumb_dir, lengths[0] * k)))
        j("FingerThumb3" + side, add(thumb, mul(thumb_dir, (lengths[0] + lengths[1]) * k)))
        j("Prop" + side, add(add(wrist, mul(along, 0.055 * k)), mul(normal, 0.02 * k)))
        j("Special" + side, add(add(wrist, mul(along, 0.05 * k)), mul(normal, -0.02 * k)))
    # Helper slots: thigh/calf twist points and two upper-arm and forearm twist points per side.
    for side, s, thigh, calf, arm, fore in (("Left", 1, 7, 13, (26, 27), (31, 32)),
                                            ("Right", -1, 9, 17, (29, 30), (34, 35))):
        pos[thigh] = lerp(pos[INDEX["Hip" + side]], pos[INDEX["Knee" + side]], 0.5)
        pos[calf] = lerp(pos[INDEX["Knee" + side]], pos[INDEX["Ankle" + side]], 0.5)
        for n, slot in enumerate(arm):
            pos[slot] = lerp(pos[INDEX["Shoulder" + side]], pos[INDEX["Elbow" + side]], (n + 1) / 3.0)
        for n, slot in enumerate(fore):
            pos[slot] = lerp(pos[INDEX["Elbow" + side]], pos[INDEX["Wrist" + side]], (n + 1) / 3.0)
    assert all(v is not None for v in pos)
    return pos


def thumb_direction(sign, p):
    along, normal, spread = hand_frame(sign, p)
    return normalize(add(add(mul(along, 0.8), mul(spread, 0.42)), mul(normal, 0.36)))


def local_translations(body):
    pos = joint_positions(body)
    return [pos[i] if PARENTS[i] < 0 else tuple(pos[i][k] - pos[PARENTS[i]][k] for k in range(3))
            for i in range(BONE_COUNT)]


def validate():
    assert len(PARENTS) == BONE_COUNT
    for i, parent in enumerate(PARENTS):
        assert parent < i, "parents precede children"
    assert len(set(NAMES)) == BONE_COUNT
