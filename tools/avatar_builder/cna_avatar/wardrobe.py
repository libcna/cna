# SPDX-License-Identifier: MS-PL
"""Original CNA wardrobe items, each skinned to the same 71-slot rig as the bodies."""
import math

from . import rig, body
from .mathutil import add, sub, mul, normalize, smoothstep, lerp, frame_from_axis
from .mesh import Mesh, ellipsoid, capsule, loft, tube, segment_weights, blend

I = rig.INDEX
HEAD = lambda p: {I["Head"]: 1.0}


# ----- tops -------------------------------------------------------------------------------------

def _torso_shell(body_type, inflate, bottom, top, neckline=None, cap_bottom=None):
    """Shirt body between two torso-table rows (inclusive), with an optional neckline ring."""
    sections = body.torso_sections(body_type, inflate)[bottom:top + 1]
    if neckline:
        c, rx, rz = sections[-1]
        sections.append((add(c, (0.0, neckline[0], 0.0)), neckline[1], neckline[2]))
    return loft(sections, 28, body.torso_weights(body_type), cap_bottom=cap_bottom, cap_top=None)


def _sleeve(body_type, side, sign, inflate, upper_end, fore_end=None, flare=0.006):
    pos = rig.joint_positions(body_type)
    L = body.LIMBS[body_type]
    sh, el, wr = pos[I["Shoulder" + side]], pos[I["Elbow" + side]], pos[I["Wrist" + side]]
    m = Mesh()
    upper = segment_weights(I["Collar" + side], I["Shoulder" + side], I["Elbow" + side])
    m.append(capsule(sh, el, L["upper_arm"][0] + inflate, L["upper_arm"][1] + inflate, 16, upper, body_rings=8,
                     cap_start=True, cap_end=False, end_t=upper_end, flare=0.0 if fore_end else flare))
    if fore_end:
        fore = segment_weights(I["Shoulder" + side], I["Elbow" + side], I["Wrist" + side])
        m.append(capsule(el, wr, L["forearm"][0] + inflate, L["forearm"][1] + inflate, 16, fore, body_rings=8,
                         cap_start=True, cap_end=False, end_t=fore_end, flare=flare))
    return m


def top(body_type, style):
    m = Mesh()
    a = body.anchors(body_type)
    if style == "top_tshirt":
        m.append(_torso_shell(body_type, 0.013, 1, 7, neckline=(0.02, 0.085, 0.072)))
        for side, sign in rig.SIDES:
            m.append(_sleeve(body_type, side, sign, 0.012, 0.5))
    elif style == "top_longsleeve":
        m.append(_torso_shell(body_type, 0.012, 1, 7, neckline=(0.03, 0.078, 0.066)))
        for side, sign in rig.SIDES:
            m.append(_sleeve(body_type, side, sign, 0.011, 1.0, fore_end=0.93))
    elif style == "top_hoodie":
        m.append(_torso_shell(body_type, 0.024, 0, 7, neckline=(0.02, 0.10, 0.085)))
        for side, sign in rig.SIDES:
            m.append(_sleeve(body_type, side, sign, 0.020, 1.0, fore_end=0.9, flare=0.004))
        # Hood lying on the upper back, and a front pocket.
        collar = rig.PROPORTIONS[body_type]["collar"][1]
        hood = lambda p: blend(I["BackUpper"], I["Neck"], 0.35)
        m.append(ellipsoid((0.0, collar + 0.03, -0.085), body.UP_AXES, (0.12, 0.075, 0.06), 20, 10,
                           lambda p, v, u: hood(p)))
        front = [s for s in body.torso_sections(body_type, 0.024)][3]
        z = front[0][2] + front[2] + 0.004
        m.append(ellipsoid((0.0, a["waist"] - 0.005, z - 0.01), body.UP_AXES, (0.095, 0.045, 0.014), 16, 8,
                           lambda p, v, u: {I["BackLower"]: 0.6, I["BackUpper"]: 0.4}))
    elif style == "top_tank":
        m.append(_torso_shell(body_type, 0.011, 1, 6))
        collar = rig.PROPORTIONS[body_type]["collar"][1]
        for side, sign in rig.SIDES:
            x = sign * 0.095
            sections = body.torso_sections(body_type, 0.011)
            front_z = sections[6][0][2] + sections[6][2] - 0.01
            path = [(x, collar - 0.05, front_z), (x, collar - 0.005, front_z * 0.7), (x, collar + 0.012, 0.0),
                    (x, collar - 0.005, -front_z * 0.7), (x, collar - 0.05, -front_z)]
            m.append(tube(path, 0.012, 8, body.torso_weights(body_type)))
    else:
        raise ValueError(style)
    return m


# ----- bottoms ----------------------------------------------------------------------------------

def _pelvis(body_type, inflate):
    # Crotch row up to just above the lower back, closed underneath.
    return _torso_shell(body_type, inflate, 0, 3, cap_bottom=0.055)


def _trouser_leg(body_type, side, inflate, thigh_end, shin_end=None, flare=0.008):
    pos = rig.joint_positions(body_type)
    L = body.LIMBS[body_type]
    hip, knee, ankle = pos[I["Hip" + side]], pos[I["Knee" + side]], pos[I["Ankle" + side]]
    m = Mesh()
    m.append(capsule(hip, knee, L["thigh"][0] + inflate, L["thigh"][1] + inflate, 18,
                     segment_weights(I["BackLower"], I["Hip" + side], I["Knee" + side], 0.2, 0.25), body_rings=8,
                     cap_start=True, cap_end=False, end_t=thigh_end, flare=0.0 if shin_end else flare))
    if shin_end:
        m.append(capsule(knee, ankle, L["shin"][0] + inflate, L["shin"][1] + inflate + 0.004, 18,
                         segment_weights(I["Hip" + side], I["Knee" + side], I["Ankle" + side], 0.25, 0.25),
                         body_rings=8, cap_start=True, cap_end=False, end_t=shin_end, flare=flare))
    return m


def bottom(body_type, style):
    m = Mesh()
    if style == "bottom_jeans":
        m.append(_pelvis(body_type, 0.009))
        for side, _ in rig.SIDES:
            m.append(_trouser_leg(body_type, side, 0.011, 1.0, shin_end=0.9))
    elif style == "bottom_shorts":
        m.append(_pelvis(body_type, 0.009))
        for side, _ in rig.SIDES:
            m.append(_trouser_leg(body_type, side, 0.012, 0.55, flare=0.012))
    elif style == "bottom_skirt":
        m.append(_pelvis(body_type, 0.008))
        p = rig.PROPORTIONS[body_type]
        a = body.anchors(body_type)
        hip_y = p["hip"][1]
        waist = body.torso_sections(body_type, 0.008)[3]
        hips = body.torso_sections(body_type, 0.008)[1]
        bottom_y = p["knee"][1] + 0.08
        sections = []
        for i in range(7):
            t = i / 6.0
            y = lerp((0, bottom_y, 0), (0, a["lower"] + 0.02, 0), 1.0 - t)[1]
            flare = 0.05 * t * t
            rx = hips[1] + 0.012 + flare if y < hip_y else lerp((hips[1] + 0.012, 0, 0), (waist[1] + 0.004, 0, 0),
                                                                 smoothstep(hip_y, a["lower"] + 0.02, y))[0]
            rz = hips[2] + 0.012 + flare * 0.8 if y < hip_y else lerp((hips[2] + 0.012, 0, 0), (waist[2] + 0.004, 0, 0),
                                                                       smoothstep(hip_y, a["lower"] + 0.02, y))[0]
            sections.append(((0.0, y, 0.0), rx, rz))
        sections.reverse()

        def skirt(pt):
            k = 0.65 * smoothstep(hip_y, bottom_y, pt[1])
            s = smoothstep(-0.06, 0.06, pt[0])
            w = {I["BackLower"]: 1.0 - k}
            if k > 0:
                w[I["HipLeft"]] = k * s
                w[I["HipRight"]] = k * (1.0 - s)
            return w
        m.append(loft(sections, 32, skirt))
    else:
        raise ValueError(style)
    return m


# ----- shoes ------------------------------------------------------------------------------------

def shoes(body_type, style):
    """Returns (upper Mesh tinted by the shoe color, sole Mesh left untinted)."""
    pos = rig.joint_positions(body_type)
    L = body.LIMBS[body_type]
    upper, sole = Mesh(), Mesh()
    for side, sign in rig.SIDES:
        ankle, toe = pos[I["Ankle" + side]], pos[I["Toe" + side]]
        rx, ry, rz = L["foot"]
        weights = lambda p, side=side, toe_z=toe[2]: blend(I["Ankle" + side], I["Toe" + side],
                                                          smoothstep(toe_z - 0.06, toe_z, p[2]))
        center = (ankle[0], ry * 0.95 + 0.012, ankle[2] + 0.04)
        upper.append(ellipsoid(center, body.UP_AXES, (rx + 0.012, ry + 0.012, rz + 0.016), 20, 12,
                               lambda p, v, u, w=weights: w(p), v_range=(0.0, 0.62)))
        sole.append(ellipsoid((center[0], 0.014, center[2]), body.UP_AXES, (rx + 0.016, 0.016, rz + 0.02), 20, 8,
                              lambda p, v, u, w=weights: w(p)))
        if style == "shoes_boots":
            knee = pos[I["Knee" + side]]
            upper.append(capsule(knee, ankle, L["shin"][0] + 0.016, L["shin"][1] + 0.016, 18,
                                 segment_weights(I["Hip" + side], I["Knee" + side], I["Ankle" + side], 0.25, 0.25),
                                 body_rings=5, cap_start=False, cap_end=True, start_t=0.55, end_t=1.0))
        elif style != "shoes_sneakers":
            raise ValueError(style)
    return upper, sole


# ----- hair -------------------------------------------------------------------------------------

def head_shell(body_type, inflate, theta_max, bulge=lambda theta, phi: 0.0, segments=32, rings=12):
    """A shell over the head down to theta_max(phi) (phi 0 = face, pi = back), with the rim
    tucked slightly inward so it never shows its open edge."""
    c = body.head_center(body_type)
    rx, ry, rz = rig.HEAD_RADII
    rows = []
    for r in range(rings + 2):
        ring = []
        for k in range(segments):
            phi = 2.0 * math.pi * k / segments
            if r <= rings:
                theta = theta_max(phi) * r / rings
                extra = inflate + bulge(theta, phi)
            else:
                theta = theta_max(phi)
                extra = inflate * 0.2
            ring.append(add(c, ((rx + extra) * math.sin(theta) * math.sin(phi), (ry + extra) * math.cos(theta),
                                (rz + extra) * math.sin(theta) * math.cos(phi))))
        rows.append(ring)
    return Mesh().grid(rows, lambda p, v, u: HEAD(p))


def _hairline(front, side, back):
    """theta_max(phi) blending front (phi=0), sides (pi/2) and back (pi)."""
    def fn(phi):
        a = abs(((phi + math.pi) % (2 * math.pi)) - math.pi)
        if a < math.pi / 2:
            return lerp((front, 0, 0), (side, 0, 0), smoothstep(0.0, math.pi / 2, a))[0]
        return lerp((side, 0, 0), (back, 0, 0), smoothstep(math.pi / 2, math.pi, a))[0]
    return fn


def hair(body_type, style):
    c = body.head_center(body_type)
    m = Mesh()
    if style == "hair_short":
        m.append(head_shell(body_type, 0.014, _hairline(0.33 * math.pi, 0.47 * math.pi, 0.66 * math.pi),
                            bulge=lambda t, p: 0.012 * math.sin(t) * (0.5 + 0.5 * math.cos(p))))
    elif style == "hair_buzz":
        m.append(head_shell(body_type, 0.005, _hairline(0.36 * math.pi, 0.50 * math.pi, 0.62 * math.pi)))
    elif style == "hair_spiky":
        m.append(head_shell(body_type, 0.012, _hairline(0.34 * math.pi, 0.47 * math.pi, 0.64 * math.pi)))
        rx, ry, rz = rig.HEAD_RADII
        for ring, (theta, count, length_) in enumerate(((0.06 * math.pi, 1, 0.09), (0.18 * math.pi, 6, 0.08),
                                                      (0.30 * math.pi, 9, 0.065))):
            for k in range(count):
                phi = 2 * math.pi * k / count + ring * 0.4
                d = normalize((math.sin(theta) * math.sin(phi), math.cos(theta), math.sin(theta) * math.cos(phi) * 0.8 - 0.25))
                base = add(c, ((rx + 0.005) * math.sin(theta) * math.sin(phi), (ry + 0.005) * math.cos(theta),
                               (rz + 0.005) * math.sin(theta) * math.cos(phi)))
                m.append(capsule(base, add(base, mul(d, length_)), 0.024, 0.004, 7, lambda p, t: HEAD(p),
                                 body_rings=3, cap_rings=2))
    elif style == "hair_bob":
        m.append(head_shell(body_type, 0.014, _hairline(0.30 * math.pi, 0.74 * math.pi, 0.72 * math.pi),
                            bulge=lambda t, p: 0.03 * smoothstep(0.35 * math.pi, 0.7 * math.pi, t)))
    elif style == "hair_ponytail":
        m.append(head_shell(body_type, 0.013, _hairline(0.32 * math.pi, 0.5 * math.pi, 0.64 * math.pi)))
        top = add(c, (0.0, 0.035, -0.16))
        m.append(ellipsoid(top, body.UP_AXES, (0.03, 0.03, 0.022), 12, 8, lambda p, v, u: HEAD(p)))
        m.append(tube([add(top, (0, -0.005, -0.012)), add(c, (0.0, -0.03, -0.20)), add(c, (0.0, -0.10, -0.205)),
                       add(c, (0.0, -0.17, -0.185))], [0.034, 0.036, 0.028, 0.012], 12, HEAD))
    else:
        raise ValueError(style)
    return m


# ----- accessories ------------------------------------------------------------------------------

def _face_point(body_type, x, y_offset, forward=0.0):
    """Point on the front of the head at horizontal offset x and height offset y from its center."""
    c = body.head_center(body_type)
    rx, ry, rz = rig.HEAD_RADII
    k = 1.0 - (x / rx) ** 2 - (y_offset / ry) ** 2
    return (x, c[1] + y_offset, c[2] + rz * math.sqrt(max(k, 0.0)) + forward)


def glasses(body_type, style):
    m = Mesh()
    rims = []
    for sign in (1.0, -1.0):
        center = _face_point(body_type, sign * 0.045, 0.004, forward=0.014)
        if style == "glasses_round":
            loop = [add(center, (0.026 * math.cos(a), 0.024 * math.sin(a), 0.0))
                    for a in (2 * math.pi * k / 20 for k in range(20))]
        elif style == "glasses_square":
            loop = []
            w, h, r = 0.028, 0.019, 0.007
            for k in range(24):
                a = 2 * math.pi * k / 24
                x, y = math.cos(a), math.sin(a)
                sx = math.copysign(min(abs(x) * 1.6, 1.0), x)
                sy = math.copysign(min(abs(y) * 1.6, 1.0), y)
                loop.append(add(center, ((w - r) * sx + r * x, (h - r) * sy + r * y, 0.0)))
        else:
            raise ValueError(style)
        m.append(tube(loop, 0.0035, 6, HEAD, closed=True))
        rims.append(center)
    bridge_y = rims[0][1] + 0.006
    m.append(tube([(rims[0][0] - 0.024, bridge_y, rims[0][2] + 0.002), (0.0, bridge_y + 0.004, rims[0][2] + 0.006),
                   (rims[1][0] + 0.024, bridge_y, rims[1][2] + 0.002)], 0.003, 6, HEAD))
    c = body.head_center(body_type)
    rx = rig.HEAD_RADII[0]
    for sign in (1.0, -1.0):
        front = add(rims[0 if sign > 0 else 1], (sign * 0.027, 0.004, -0.004))
        m.append(tube([front, (sign * (rx + 0.006), front[1], c[2] + 0.04), (sign * (rx + 0.004), front[1] - 0.01, c[2] - 0.03)],
                      0.0028, 6, HEAD))
    return m


def hat(body_type, style):
    c = body.head_center(body_type)
    m = Mesh()
    if style == "hat_cap":
        m.append(head_shell(body_type, 0.03, _hairline(0.38 * math.pi, 0.42 * math.pi, 0.44 * math.pi)))
        front = _face_point(body_type, 0.0, 0.095, forward=0.0)
        m.append(ellipsoid(add(front, (0.0, 0.0, 0.05)), ((1, 0, 0), normalize((0, 1, -0.25)), normalize((0, 0.25, 1))),
                           (0.095, 0.007, 0.075), 20, 6, lambda p, v, u: HEAD(p)))
        m.append(ellipsoid(add(c, (0.0, rig.HEAD_RADII[1] + 0.028, 0.0)), body.UP_AXES, (0.012, 0.008, 0.012), 8, 6,
                           lambda p, v, u: HEAD(p)))
    elif style == "hat_beanie":
        m.append(head_shell(body_type, 0.028, _hairline(0.40 * math.pi, 0.44 * math.pi, 0.48 * math.pi),
                            bulge=lambda t, p: 0.02 * (1.0 - smoothstep(0.0, 0.3 * math.pi, t))))
        rx, ry, rz = rig.HEAD_RADII
        theta = 0.38 * math.pi
        cuff = [((0.0, c[1] + (ry + 0.03) * math.cos(theta) - 0.012, c[2]), (rx + 0.036) * math.sin(theta),
                 (rz + 0.036) * math.sin(theta)),
                ((0.0, c[1] + (ry + 0.03) * math.cos(theta) + 0.03, c[2]), (rx + 0.034) * math.sin(theta) * 0.97,
                 (rz + 0.034) * math.sin(theta) * 0.97)]
        m.append(loft(cuff, 32, HEAD, cap_bottom=None, cap_top=None))
        m.append(ellipsoid(add(c, (0.0, ry + 0.06, 0.0)), body.UP_AXES, (0.032, 0.03, 0.032), 14, 10,
                           lambda p, v, u: HEAD(p)))
    else:
        raise ValueError(style)
    return m
