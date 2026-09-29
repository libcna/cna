# SPDX-License-Identifier: MS-PL
"""Catalog v2 glasses, hats and facial hair, fitted to head.py's face.

Hats are built outside the hat envelope (the head surface plus HAT_LIFT down to each hat's line),
which is also where hair.squash_under_hat() flattens a style, so every hat fits every hair.
Builders return (Mesh, tint, texture kind, base colour) parts like garments.py.
"""
import math

from . import rig, head, hair
from .mathutil import add, sub, mul, normalize, cross, dot, length, lerp, smoothstep
from .mesh import Mesh, ellipsoid, tube

I = rig.INDEX
PI = math.pi
HEADW = lambda *a: {I["Head"]: 1.0}
HAT_LIFT = 0.024


# ----- glasses ------------------------------------------------------------------------------------

def _rim(centre, shape, w, h, n=28):
    """Closed rim outline in the face plane (x right, y up) around `centre`."""
    pts = []
    for k in range(n):
        a = 2.0 * PI * k / n
        c, s = math.cos(a), math.sin(a)
        if shape == "round":
            x, y = w * c, h * s
        elif shape == "square":
            x = w * math.copysign(abs(c) ** 0.45, c)
            y = h * math.copysign(abs(s) ** 0.45, s)
        elif shape == "aviator":
            # A teardrop: wide at the top, drooping toward the outer bottom corner.
            x = w * c
            y = h * s * (1.0 if s > 0 else 1.25) - 0.004 * (1.0 - s) * (x / w)
        else:  # browline: a straight heavy top
            x = w * math.copysign(abs(c) ** 0.6, c)
            y = h * (0.85 if s > 0 else 1.0) * math.copysign(abs(s) ** (0.4 if s > 0 else 0.8), s)
        pts.append((centre[0] + x, centre[1] + y, centre[2]))
    return pts


def glasses(body_type, style):
    s = head.SCALE[body_type]
    a = head.anchors(body_type)
    c = head.centre(body_type)
    frame, lenses = Mesh(), Mesh()
    shape = {"glasses_round": "round", "glasses_square": "square", "glasses_aviator": "aviator",
             "glasses_browline": "browline"}[style]
    w, h = {"round": (0.034, 0.032), "square": (0.038, 0.027), "aviator": (0.036, 0.028), "browline": (0.037, 0.026)}[shape]
    w, h = w * s, h * s
    thick = {"browline": 0.0030}.get(shape, 0.0024) * s
    fronts = []
    for side, name in ((1.0, "Left"), (-1.0, "Right")):
        eye = a["eye" + name]
        centre = (eye[0] + side * 0.002 * s, eye[1] + 0.002 * s, eye[2] + 0.017 * s)
        # Rims tilt back slightly toward the temples, following the face.
        rim = _rim(centre, shape, w, h)
        rim = [(p[0], p[1], p[2] - 0.012 * s * max(0.0, side * (p[0] - centre[0]) / w) ** 2) for p in rim]
        radii = [thick * (1.6 if (shape == "browline" and p[1] > centre[1] + 0.2 * h) else 1.0) for p in rim]
        frame.append(tube(rim, radii, 6, HEADW, closed=True))
        if shape == "aviator":
            rows = [[centre] * len(rim), [lerp(centre, p, 0.55) for p in rim], [lerp(centre, p, 0.98) for p in rim]]
            lens = Mesh().grid(rows, HEADW)
            lenses.append(lens)
        fronts.append((centre, rim))
    (lc, lrim), (rc, rrim) = fronts
    bridge_y = lc[1] + 0.35 * h
    nose = a["noseRoot"]
    frame.append(tube([(lc[0] - w, bridge_y, lc[2] - 0.002 * s), (0.0, bridge_y + 0.004 * s, nose[2] + 0.013 * s),
                       (rc[0] + w, bridge_y, rc[2] - 0.002 * s)], [thick * 0.9] * 3, 6, HEADW))
    if shape == "aviator":
        frame.append(tube([(lc[0] - w * 0.7, lc[1] + h * 1.02, lc[2]), (0.0, lc[1] + h * 1.08, nose[2] + 0.01 * s),
                           (rc[0] + w * 0.7, rc[1] + h * 1.02, rc[2])], [thick * 0.7] * 3, 6, HEADW))
    for side, (centre, rim) in ((1.0, fronts[0]), (-1.0, fronts[1])):
        hinge = (centre[0] + side * w * 1.02, centre[1] + 0.25 * h, centre[2] - 0.012 * s)
        ear = a["ear" + ("Left" if side > 0 else "Right")]
        side_pt = head.side_point(body_type, side, hinge[1] - c[1], (hinge[2] + ear[2]) * 0.5 - c[2], 0.006 * s)[0]
        frame.append(tube([hinge, (side_pt[0], hinge[1], side_pt[2]), (ear[0] - side * 0.004 * s, ear[1] + 0.024 * s, ear[2] - 0.010 * s),
                           (ear[0] - side * 0.008 * s, ear[1] + 0.006 * s, ear[2] - 0.022 * s)], [thick * 0.85] * 4, 6, HEADW,
                          round_end=True))
    parts = [(frame, "accessory", "plain", (1.0, 1.0, 1.0))]
    if lenses.positions:
        parts.append((lenses, "none", "plain", (0.10, 0.12, 0.15)))
    return parts


# ----- hats ---------------------------------------------------------------------------------------

def hat_line(front, side, back):
    return hair.hairline(front, (front + side) * 0.5, side, back)


def crown(body_type, line, lift, bulge=lambda theta, phi: 0.0, segments=48, rings=12):
    """A hat crown: head surface + lift down to line(phi), ending in a turned rim."""
    rows = []
    for r in range(rings + 1):
        ring = []
        for k in range(segments):
            phi = PI + 2.0 * PI * k / segments
            theta = line(phi) * (0.02 + 0.98 * r / rings)
            ring.append(head.point(body_type, theta, phi, lift + bulge(theta, phi)))
        rows.append(ring)
    inner = [[head.point(body_type, line(PI + 2.0 * PI * k / segments), PI + 2.0 * PI * k / segments, lift * 0.55)
              for k in range(segments)]]
    rows.insert(0, [head.point(body_type, 0.0, 0.0, lift + bulge(0.0, 0.0))] * segments)
    return Mesh().grid(rows + inner, HEADW, uv_seam=True)


def brim(body_type, line, lift, width, droop, segments=48, phi_range=None, curl=0.0):
    """A brim leaving the crown's rim: width(phi) outward, droop(phi) down (metres at male
    scale). A full ring, or a bill between phi_range's two angles."""
    s = head.SCALE[body_type]
    c = head.centre(body_type)
    sections = []
    count = segments + 1 if phi_range else segments
    for k in range(count):
        phi = (phi_range[0] + (phi_range[1] - phi_range[0]) * k / segments) if phi_range else PI + 2.0 * PI * k / segments
        base = head.point(body_type, line(phi), phi, lift)
        out = normalize(sub(base, add(c, (0.0, -0.04 * s, 0.0))))
        out = normalize((out[0], 0.0, out[2]))
        wv, dv = width(phi) * s, droop(phi) * s
        tip = add(base, (out[0] * wv, -dv + curl * s * abs(math.sin(phi)), out[2] * wv))
        mid = lerp(base, tip, 0.55)
        t = 0.0035 * s
        # A thin closed cross-section: top from the crown to the edge, bottom back again.
        sections.append([add(base, (0.0, t, 0.0)), add(mid, (0.0, t - dv * 0.1, 0.0)), tip, add(mid, (0.0, -t - dv * 0.1, 0.0)),
                         add(base, (0.0, -t, 0.0))])
    if phi_range:
        ends = [[mul(add(add(add(add(q[0], q[1]), q[2]), q[3]), q[4]), 0.2)] * 5 for q in (sections[0], sections[-1])]
        sections = [ends[0]] + sections + [ends[1]]
    else:
        sections.append(sections[0])
    m = Mesh()
    m.patch_origin = c
    return m.grid(sections, HEADW)


def hat(body_type, style):
    s = head.SCALE[body_type]
    lift = HAT_LIFT * s
    parts = []
    main, trim = Mesh(), Mesh()
    if style == "hat_cap":
        line = hat_line(0.36 * PI, 0.44 * PI, 0.46 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.010 * s * math.sin(t) * (0.5 + 0.5 * math.cos(p))))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.085 * math.cos(p) ** 2, lambda p: 0.020 * math.cos(p) ** 2,
                         segments=24, phi_range=(-0.36 * PI, 0.36 * PI), curl=0.03))
        top = head.point(body_type, 0.0, 0.0, lift + 0.012 * s)
        trim.append(ellipsoid(top, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.009 * s, 0.006 * s, 0.009 * s), 8, 6, HEADW))
        parts = [(main, "accessory", "canvas", (1.0, 1.0, 1.0)), (trim, "accessory", "plain", (0.8, 0.8, 0.8))]
    elif style == "hat_beanie":
        line = hat_line(0.40 * PI, 0.46 * PI, 0.50 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.018 * s * (1.0 - smoothstep(0.0, 0.35 * PI, t))))
        cuff = line
        ring_lo = [head.point(body_type, cuff(PI + 2 * PI * k / 48), PI + 2 * PI * k / 48, lift + 0.010 * s) for k in range(48)]
        ring_hi = [head.point(body_type, cuff(PI + 2 * PI * k / 48) - 0.09 * PI, PI + 2 * PI * k / 48, lift + 0.012 * s) for k in range(48)]
        bottom_in = [head.point(body_type, cuff(PI + 2 * PI * k / 48), PI + 2 * PI * k / 48, lift * 0.5) for k in range(48)]
        top_in = [head.point(body_type, cuff(PI + 2 * PI * k / 48) - 0.09 * PI, PI + 2 * PI * k / 48, lift * 0.8) for k in range(48)]
        main.append(Mesh().grid([top_in, ring_hi, ring_lo, bottom_in], HEADW))
        top = head.point(body_type, 0.0, 0.0, lift + 0.030 * s)
        trim.append(ellipsoid(top, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.028 * s, 0.025 * s, 0.028 * s), 14, 10, HEADW))
        parts = [(main, "accessory", "rib", (1.0, 1.0, 1.0)), (trim, "accessory", "fleece", (1.0, 1.0, 1.0))]
    elif style == "hat_bucket":
        line = hat_line(0.40 * PI, 0.45 * PI, 0.47 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.012 * s * smoothstep(0.1 * PI, 0.3 * PI, t)))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.060, lambda p: 0.030))
        parts = [(main, "accessory", "canvas", (1.0, 1.0, 1.0))]
    elif style == "hat_fedora":
        line = hat_line(0.38 * PI, 0.43 * PI, 0.45 * PI)
        main.append(crown(body_type, line, lift,
                          bulge=lambda t, p: 0.034 * s * math.sin(min(t * 2.4, PI * 0.5)) -
                          0.030 * s * math.exp(-((t / (0.10 * PI)) ** 2)) * (0.5 + 0.5 * math.cos(p) ** 2)))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.055 + 0.01 * math.cos(p),
                         lambda p: 0.004 - 0.016 * math.sin(p) ** 2 + 0.012 * max(0.0, math.cos(p))))
        band = [head.point(body_type, line(PI + 2 * PI * k / 48) - 0.03 * PI, PI + 2 * PI * k / 48, lift + 0.003 * s) for k in range(48)]
        trim.append(tube(band, [0.009 * s] * 48, 6, HEADW, closed=True))
        parts = [(main, "accessory", "fleece", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.16, 0.15, 0.15))]
    elif style == "hat_headband":
        line = hat_line(0.30 * PI, 0.44 * PI, 0.52 * PI)
        band = []
        for k in range(48):
            phi = PI + 2 * PI * k / 48
            band.append(head.point(body_type, line(phi) - 0.02 * PI, phi, 0.016 * s))
        main.append(tube(band, [0.013 * s] * 48, 8, HEADW, closed=True))
        parts = [(main, "accessory", "rib", (1.0, 1.0, 1.0))]
    else:
        raise ValueError(style)
    return parts


HAT_COVER = {  # the head region each hat covers, for squashing hair under it
    "hat_cap": (0.36 * PI, 0.44 * PI, 0.46 * PI), "hat_beanie": (0.40 * PI, 0.46 * PI, 0.50 * PI),
    "hat_bucket": (0.40 * PI, 0.45 * PI, 0.47 * PI), "hat_fedora": (0.38 * PI, 0.43 * PI, 0.45 * PI),
}


# ----- facial hair ------------------------------------------------------------------------------

def _face_shell(body_type, region, lift, rows=10, cols=28):
    """A shell over the face where region(x, y) (head-local, male scale) > 0, following the head."""
    s = head.SCALE[body_type]
    grid = []
    for r in range(rows + 1):
        theta = PI * (0.50 + 0.36 * r / rows)
        row = []
        for k in range(cols + 1):
            phi = -0.62 * PI + 1.24 * PI * k / cols
            local = head.local_point(body_type, theta, phi)
            inside = region(local[0] / s, local[1] / s)
            row.append(head.point(body_type, theta, phi, lift * inside))
        grid.append(row)
    return grid


def facial_hair(body_type, style):
    s = head.SCALE[body_type]
    a = head.anchors(body_type)
    c = head.centre(body_type)
    locks = Mesh()
    mouth = sub(a["mouth"], c)

    def mustache():
        m = Mesh()
        for side in (1.0, -1.0):
            for k in range(3):
                x0 = side * (0.004 + 0.010 * k) * s
                start = head.front_point(body_type, x0, mouth[1] + 0.027 * s, 0.004 * s)[0]
                end = head.front_point(body_type, x0 + side * 0.022 * s, mouth[1] + 0.006 * s - 0.004 * k * s, 0.004 * s)[0]
                mid = add(lerp(start, end, 0.5), (0.0, 0.003 * s, 0.006 * s))
                m.append(hair.lock(body_type, hair._catmull([start, mid, end], 4), hair.taper(0.011 * s, 0.2),
                                   hair.taper(0.006 * s, 0.3)))
        return m

    if style == "facial_mustache":
        locks.append(mustache())
    elif style == "facial_goatee":
        locks.append(mustache())
        for k in range(5):
            x = (k - 2) * 0.010 * s
            start = head.front_point(body_type, x, mouth[1] - 0.030 * s, 0.003 * s)[0]
            end = head.front_point(body_type, x * 0.7, mouth[1] - 0.065 * s, 0.008 * s)[0]
            end = add(end, (0.0, -0.012 * s, 0.006 * s))
            locks.append(hair.lock(body_type, hair._catmull([start, lerp(start, end, 0.5), end], 4), hair.taper(0.012 * s, 0.25),
                                   hair.taper(0.007 * s, 0.3)))
    elif style in ("facial_beard", "facial_chinstrap"):
        full = style == "facial_beard"
        my = mouth[1] / s

        def region(x, y, z):
            ax = abs(x)
            # The cheek line runs from the sideburn in front of the ear (high) to beside the
            # mouth (low); nothing wraps behind the ears.
            cheek_y = 0.004 - 0.066 * smoothstep(0.010, 0.080, z)
            under = smoothstep(cheek_y + 0.004, cheek_y - 0.010, y) * smoothstep(-0.040, -0.020, z)
            if full:
                chin = smoothstep(my - 0.030, my - 0.042, y) if ax < 0.038 else 1.0
                return under * chin
            edge = smoothstep(-0.105, -0.118, y) + smoothstep(0.035, 0.015, z)
            return under * min(1.0, edge)
        thick = (0.012 if full else 0.006) * s
        grid = []
        for r in range(15):
            theta = PI * (0.48 + 0.40 * r / 14)
            row = []
            for k in range(41):
                phi = -0.64 * PI + 1.28 * PI * k / 40
                local = head.local_point(body_type, theta, phi)
                inside = region(local[0] / s, local[1] / s, local[2] / s)
                # Outside the region the shell dives under the skin, so its edge grows out of it.
                row.append(head.point(body_type, theta, phi, thick * inside - 0.003 * s * (1.0 - inside)))
            grid.append(row)
        shell = Mesh()
        shell.patch_origin = c
        locks.append(shell.grid(grid, HEADW, wrap=False))
        if full:
            locks.append(mustache())
            for k in range(7):
                x = (k - 3) * 0.013 * s
                start = head.front_point(body_type, x, mouth[1] - 0.040 * s, 0.010 * s)[0]
                end = add(head.front_point(body_type, x * 0.8, mouth[1] - 0.080 * s, 0.012 * s)[0], (0.0, -0.014 * s, 0.004 * s))
                locks.append(hair.lock(body_type, hair._catmull([start, lerp(start, end, 0.5), end], 4), hair.taper(0.016 * s, 0.25),
                                       hair.taper(0.009 * s, 0.3)))
    else:
        raise ValueError(style)
    return [(locks, "hair", "strands", (1.0, 1.0, 1.0))]
