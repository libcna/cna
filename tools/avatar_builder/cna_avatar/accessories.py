# SPDX-License-Identifier: MS-PL
"""The catalog's glasses, hats and facial hair, fitted to head.py's face.

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
        elif shape == "cat":
            # Rounded below, sweeping up to points at the outer top corners (outer = +x side here,
            # mirrored by the caller's side).
            x = w * math.copysign(abs(c) ** 0.7, c)
            y = h * math.copysign(abs(s) ** 0.8, s)
            y += 0.85 * h * max(0.0, c) ** 2 * max(0.0, s)
            x += 0.20 * w * max(0.0, c) * max(0.0, s)
        elif shape == "shield":
            # A wide, shallow sport lens.
            x = w * math.copysign(abs(c) ** 0.5, c)
            y = h * math.copysign(abs(s) ** 0.6, s) * (1.0 - 0.15 * max(0.0, -s))
        else:  # browline: a straight heavy top
            x = w * math.copysign(abs(c) ** 0.6, c)
            y = h * (0.85 if s > 0 else 1.0) * math.copysign(abs(s) ** (0.4 if s > 0 else 0.8), s)
        pts.append((centre[0] + x, centre[1] + y, centre[2]))
    return pts


# (rim shape, half width, half height, rim thickness, lens colour or None, heavy brow bar).
GLASSES = {
    "glasses_round": ("round", 0.034, 0.032, 0.0024, None, False),
    "glasses_square": ("square", 0.038, 0.027, 0.0024, None, False),
    "glasses_aviator": ("aviator", 0.036, 0.028, 0.0024, (0.10, 0.12, 0.15), False),
    "glasses_browline": ("browline", 0.037, 0.026, 0.0030, None, True),
    "glasses_narrow": ("square", 0.036, 0.018, 0.0022, None, False),
    "glasses_wire": ("round", 0.030, 0.028, 0.0013, None, False),
    "glasses_thick": ("square", 0.039, 0.029, 0.0042, None, False),
    "glasses_cateye": ("cat", 0.037, 0.026, 0.0030, None, False),
    "glasses_oversized": ("round", 0.042, 0.039, 0.0030, None, False),
    "glasses_bigsquare": ("square", 0.043, 0.035, 0.0032, None, False),
    "sunglasses_sport": ("shield", 0.044, 0.024, 0.0034, (0.12, 0.14, 0.20), False),
    "sunglasses_square": ("square", 0.040, 0.030, 0.0034, (0.08, 0.08, 0.09), False),
    "sunglasses_round": ("round", 0.033, 0.031, 0.0022, (0.14, 0.10, 0.06), False),
}


def glasses(body_type, style):
    s = head.SCALE[body_type]
    a = head.anchors(body_type)
    c = head.centre(body_type)
    frame, lenses = Mesh(), Mesh()
    shape, w, h, thick, lens, heavy = GLASSES[style]
    w, h, thick = w * s, h * s, thick * s
    fronts = []
    for side, name in ((1.0, "Left"), (-1.0, "Right")):
        eye = a["eye" + name]
        centre = (eye[0] + side * 0.002 * s, eye[1] + 0.002 * s, eye[2] + 0.017 * s)
        # Rims tilt back slightly toward the temples, following the face; shapes with an outer side
        # are mirrored on the right.
        rim = _rim(centre, shape, w, h)
        if side < 0:
            rim = [(2.0 * centre[0] - p[0], p[1], p[2]) for p in reversed(rim)]
        rim = [(p[0], p[1], p[2] - 0.012 * s * max(0.0, side * (p[0] - centre[0]) / w) ** 2) for p in rim]
        radii = [thick * (1.6 if (heavy and p[1] > centre[1] + 0.2 * h) else 1.0) for p in rim]
        frame.append(tube(rim, radii, 6, HEADW, closed=True))
        if lens:
            rows = [[centre] * len(rim), [lerp(centre, p, 0.55) for p in rim], [lerp(centre, p, 0.98) for p in rim]]
            lenses.append(Mesh().grid(rows, HEADW))
        fronts.append((centre, rim))
    (lc, lrim), (rc, rrim) = fronts
    bridge_y = lc[1] + 0.35 * h
    nose = a["noseRoot"]
    frame.append(tube([(lc[0] - w, bridge_y, lc[2] - 0.002 * s), (0.0, bridge_y + 0.004 * s, nose[2] + 0.013 * s),
                       (rc[0] + w, bridge_y, rc[2] - 0.002 * s)], [max(thick * 0.9, 0.0016 * s)] * 3, 6, HEADW))
    if shape == "aviator":
        frame.append(tube([(lc[0] - w * 0.7, lc[1] + h * 1.02, lc[2]), (0.0, lc[1] + h * 1.08, nose[2] + 0.01 * s),
                           (rc[0] + w * 0.7, rc[1] + h * 1.02, rc[2])], [thick * 0.7] * 3, 6, HEADW))
    arm = max(thick * 0.85, 0.0018 * s)
    for side, (centre, rim) in ((1.0, fronts[0]), (-1.0, fronts[1])):
        hinge = (centre[0] + side * w * 1.02, centre[1] + 0.25 * h, centre[2] - 0.012 * s)
        ear = a["ear" + ("Left" if side > 0 else "Right")]
        side_pt = head.side_point(body_type, side, hinge[1] - c[1], (hinge[2] + ear[2]) * 0.5 - c[2], 0.006 * s)[0]
        frame.append(tube([hinge, (side_pt[0], hinge[1], side_pt[2]), (ear[0] - side * 0.004 * s, ear[1] + 0.024 * s, ear[2] - 0.010 * s),
                           (ear[0] - side * 0.008 * s, ear[1] + 0.006 * s, ear[2] - 0.022 * s)], [arm] * 4, 6, HEADW,
                          round_end=True))
    parts = [(frame, "accessory", "plain", (1.0, 1.0, 1.0))]
    if lenses.positions:
        parts.append((lenses, "none", "plain", lens))
    return parts


# ----- hats ---------------------------------------------------------------------------------------

def hat_line(front, side, back):
    return hair.hairline(front, (front + side) * 0.5, side, back, wrap=(0.55, 1.0))


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


def _angle_to(phi, centre):
    return (phi - centre + PI) % (2.0 * PI) - PI


def fedora_crown(body_type, line, lift, segments=48):
    """A fedora's crown: walls rising from the band line to above the head, pinched at the front
    sides and tapering a little, closed by a top with a crease along its length. Returns the crown
    and the ribbon band around its foot."""
    s = head.SCALE[body_type]
    c = head.centre(body_type)
    top_y = head.point(body_type, 0.0, 0.0, lift)[1] + 0.024 * s
    bases = [(PI + 2.0 * PI * k / segments, head.point(body_type, line(PI + 2.0 * PI * k / segments),
                                                       PI + 2.0 * PI * k / segments, lift)) for k in range(segments)]
    centre = (c[0], top_y, c[2] - 0.006 * s)

    def wall(phi, b, f, out=0.0):
        pinch = sum(math.exp(-(_angle_to(phi, a) / (0.13 * PI)) ** 2) for a in (0.22 * PI, -0.22 * PI))
        scale = 1.0 - 0.13 * f ** 1.3 - 0.13 * pinch * f ** 1.6
        rel = (b[0] - centre[0], b[2] - centre[2])
        norm = math.hypot(*rel) or 1.0
        # The crown dips toward the front, as a snap-brim fedora does.
        y_top = top_y - 0.012 * s * max(0.0, math.cos(phi))
        return (centre[0] + rel[0] * scale + rel[0] / norm * out, b[1] + (y_top - b[1]) * f,
                centre[2] + rel[1] * scale + rel[1] / norm * out)

    walls = [[wall(phi, b, f) for phi, b in bases] for f in (1.0, 0.8, 0.6, 0.4, 0.2, 0.0)]
    length_ = max(abs(edge[2] - centre[2]) for edge in walls[0])
    rows = []
    for u in (0.0, 0.25, 0.5, 0.72, 0.9):
        ring = []
        for edge in walls[0]:
            x, z = centre[0] + (edge[0] - centre[0]) * u, centre[2] + (edge[2] - centre[2]) * u
            # The crease: a trough along the crown's length, fading before its ends.
            crease = 0.024 * s * math.exp(-((x - centre[0]) / (0.030 * s)) ** 2) * \
                (1.0 - smoothstep(0.55, 0.95, abs(z - centre[2]) / length_))
            ring.append((x, edge[1] - crease + 0.004 * s * (1.0 - u), z))
        rows.append(ring)
    inner = [head.point(body_type, line(phi), phi, lift * 0.55) for phi, _ in bases]
    crown_mesh = Mesh().grid(rows + walls + [inner], HEADW, uv_seam=True)
    band = Mesh().grid([[wall(phi, b, 0.26, 0.003 * s) for phi, b in bases],
                        [wall(phi, b, 0.02, 0.003 * s) for phi, b in bases]], HEADW)
    return crown_mesh, band


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
    if style in REVISED_HATS:
        return revised_hat(body_type, style)
    s = head.SCALE[body_type]
    lift = HAT_LIFT * s
    parts = []
    main, trim = Mesh(), Mesh()
    if style == "hat_cap":
        line = hat_line(0.36 * PI, 0.44 * PI, 0.46 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.010 * s * math.sin(t) * (0.5 + 0.5 * math.cos(p))))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.085 * math.cos(p) ** 2, lambda p: 0.020 * math.cos(p) ** 2,
                         segments=24, phi_range=(-0.36 * PI, 0.36 * PI), curl=0.03))
        # The button sits on the crown's top (whose bulge is zero there).
        top = head.point(body_type, 0.0, 0.0, lift + 0.003 * s)
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
        crown_mesh, band = fedora_crown(body_type, line, lift)
        main.append(crown_mesh)
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.058 + 0.01 * math.cos(p),
                         lambda p: 0.004 - 0.018 * math.sin(p) ** 2 + 0.014 * max(0.0, math.cos(p))))
        trim.append(band)
        parts = [(main, "accessory", "fleece", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.16, 0.15, 0.15))]
    elif style == "hat_headband":
        line = hat_line(0.30 * PI, 0.44 * PI, 0.52 * PI)
        band = []
        for k in range(48):
            phi = PI + 2 * PI * k / 48
            band.append(head.point(body_type, line(phi) - 0.02 * PI, phi, 0.022 * s))
        main.append(tube(band, [0.013 * s] * 48, 8, HEADW, closed=True))
        parts = [(main, "accessory", "rib", (1.0, 1.0, 1.0))]
    else:
        raise ValueError(style)
    return parts


# The head region each covering hat flattens the hair in (the catalog's hairCover): its lowest
# angle from the crown as a fraction of pi at the face, temples, sides and nape -- each hat's own
# crown line, reaching a little lower so hair never shows through the crown's edge -- and how far
# above the head surface the flattened hair lies (head-local metres at male scale; inside every
# crown, which is HAT_LIFT out).
HAT_COVER = {
    "hat_cap": (0.40, 0.45, 0.47, 0.50), "hat_beanie": (0.42, 0.47, 0.50, 0.52),
    "hat_bucket": (0.42, 0.46, 0.48, 0.50), "hat_fedora": (0.41, 0.45, 0.47, 0.49),
}
UNDER_HAT_LIFT = 0.019
HAT_COVER.update({
    "hat_backwardcap": (0.40, 0.45, 0.47, 0.50), "hat_trucker": (0.40, 0.45, 0.47, 0.50), "hat_flatcap": (0.39, 0.44, 0.46, 0.48),
    "hat_sunhat": (0.41, 0.45, 0.47, 0.49), "hat_tophat": (0.40, 0.44, 0.46, 0.48), "hat_beret": (0.30, 0.33, 0.36, 0.40),
    "hat_helmet": (0.38, 0.44, 0.46, 0.50), "hat_slouchbeanie": (0.42, 0.47, 0.50, 0.54), "hat_western": (0.40, 0.44, 0.46, 0.48),
})


REVISED_HATS = ["hat_backwardcap", "hat_trucker", "hat_flatcap", "hat_sunhat", "hat_tophat", "hat_beret", "hat_helmet",
                "hat_slouchbeanie", "hat_visor", "hat_western"]


def revised_hat(body_type, style):
    s = head.SCALE[body_type]
    lift = HAT_LIFT * s
    main, trim = Mesh(), Mesh()
    c = head.centre(body_type)
    if style in ("hat_backwardcap", "hat_trucker"):
        line = hat_line(0.36 * PI, 0.44 * PI, 0.46 * PI)
        tall = 0.020 if style == "hat_trucker" else 0.010
        main.append(crown(body_type, line, lift, bulge=lambda t, p: tall * s * math.sin(t) * (0.5 + 0.5 * math.cos(p))))
        rng = (PI - 0.36 * PI, PI + 0.36 * PI) if style == "hat_backwardcap" else (-0.36 * PI, 0.36 * PI)
        bill = brim(body_type, line, lift * 0.9, lambda p: 0.085 * math.cos(p) ** 2, lambda p: 0.020 * math.cos(p) ** 2,
                    segments=24, phi_range=rng, curl=0.03 if style == "hat_trucker" else 0.0)
        main.append(bill)
        top = head.point(body_type, 0.0, 0.0, lift + 0.003 * s)
        trim.append(ellipsoid(top, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.009 * s, 0.006 * s, 0.009 * s), 8, 6, HEADW))
        if style == "hat_trucker":
            # A taller crown of mesh fabric.
            return [(main, "accessory", "jersey", (1.0, 1.0, 1.0)), (trim, "accessory", "plain", (0.8, 0.8, 0.8))]
        return [(main, "accessory", "canvas", (1.0, 1.0, 1.0)), (trim, "accessory", "plain", (0.8, 0.8, 0.8))]
    if style == "hat_flatcap":
        line = hat_line(0.37 * PI, 0.43 * PI, 0.45 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.016 * s * smoothstep(0.05 * PI, 0.25 * PI, t) *
                          max(0.0, math.cos(p)) - 0.008 * s * (1.0 - smoothstep(0.0, 0.2 * PI, t))))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.040 * math.cos(p) ** 2, lambda p: 0.012 * math.cos(p) ** 2,
                         segments=20, phi_range=(-0.30 * PI, 0.30 * PI)))
        return [(main, "accessory", "check", (1.0, 1.0, 1.0))]
    if style in ("hat_sunhat", "hat_western"):
        line = hat_line(0.40 * PI, 0.44 * PI, 0.46 * PI)
        if style == "hat_western":
            crown_mesh, band = fedora_crown(body_type, line, lift)
            main.append(crown_mesh)
            main.append(brim(body_type, line, lift * 0.9, lambda p: 0.085, lambda p: -0.020 * math.sin(p) ** 2 + 0.010 * math.cos(p) ** 2,
                             curl=0.0))
            trim.append(band)
            return [(main, "accessory", "leather", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.30, 0.20, 0.12))]
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.010 * s * smoothstep(0.1 * PI, 0.3 * PI, t)))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.105, lambda p: 0.026))
        band = []
        for k in range(48):
            phi = PI + 2 * PI * k / 48
            band.append(head.point(body_type, line(phi) - 0.025 * PI, phi, lift + 0.003 * s))
        trim.append(tube(band, [0.006 * s] * 48, 6, HEADW, closed=True))
        return [(main, "accessory", "canvas", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.20, 0.20, 0.22))]
    if style == "hat_tophat":
        line = hat_line(0.40 * PI, 0.44 * PI, 0.46 * PI)
        base = [head.point(body_type, line(PI + 2 * PI * k / 40), PI + 2 * PI * k / 40, lift) for k in range(40)]
        cx = sum(p[0] for p in base) / 40.0
        cz = sum(p[2] for p in base) / 40.0
        top_y = head.point(body_type, 0.0, 0.0, lift)[1] + 0.10 * s
        rows = [base]
        for f in (0.25, 0.6, 1.0):
            rows.append([(cx + (p[0] - cx) * (1.0 + 0.04 * f), p[1] + (top_y - p[1]) * f, cz + (p[2] - cz) * (1.0 + 0.04 * f)) for p in base])
        rows.append([(cx + (p[0] - cx) * 0.6, top_y, cz + (p[2] - cz) * 0.6) for p in base])
        rows.append([(cx, top_y, cz)] * 40)
        m = Mesh()
        m.patch_origin = (cx, (top_y + base[0][1]) * 0.5, cz)
        main.append(m.grid(rows, HEADW))
        main.append(brim(body_type, line, lift * 0.9, lambda p: 0.050, lambda p: -0.008 * math.sin(p) ** 2))
        band = [(cx + (p[0] - cx) * 1.012, p[1] + 0.012 * s, cz + (p[2] - cz) * 1.012) for p in base]
        trim.append(tube(band, [0.007 * s] * 40, 6, HEADW, closed=True))
        return [(main, "accessory", "fleece", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.12, 0.12, 0.13))]
    if style == "hat_beret":
        line = hat_line(0.30 * PI, 0.36 * PI, 0.40 * PI)
        main.append(crown(body_type, line, lift * 0.8, bulge=lambda t, p: 0.030 * s * math.sin(min(t / (0.30 * PI), 1.0) * PI) *
                          (0.7 + 0.3 * math.sin(p))))
        top = head.point(body_type, 0.02 * PI, 0.0, lift + 0.040 * s)
        trim.append(tube([top, add(top, (0.0, 0.010 * s, 0.0))], [0.004 * s] * 2, 6, HEADW, round_end=True))
        return [(main, "accessory", "fleece", (1.0, 1.0, 1.0)), (trim, "accessory", "plain", (0.8, 0.8, 0.8))]
    if style == "hat_helmet":
        line = hat_line(0.38 * PI, 0.44 * PI, 0.48 * PI)
        main.append(crown(body_type, line, lift + 0.008 * s, bulge=lambda t, p: 0.012 * s * smoothstep(0.0, 0.3 * PI, t) *
                          (1.0 - 0.5 * max(0.0, math.cos(p)))))
        rim = [head.point(body_type, line(PI + 2 * PI * k / 48), PI + 2 * PI * k / 48, lift + 0.008 * s) for k in range(48)]
        trim.append(tube(rim, [0.006 * s] * 48, 6, HEADW, closed=True))
        for side in (1.0, -1.0):
            start = head.side_point(body_type, side, -0.005 * s, 0.012 * s, 0.010 * s)[0]
            chin = add(head.anchors(body_type)["chin"], (side * 0.020 * s, -0.004 * s, -0.012 * s))
            trim.append(tube([start, lerp(start, chin, 0.5), chin], [0.0022 * s] * 3, 6, HEADW))
        return [(main, "accessory", "plain", (1.0, 1.0, 1.0)), (trim, "none", "plain", (0.18, 0.18, 0.20))]
    if style == "hat_slouchbeanie":
        line = hat_line(0.42 * PI, 0.47 * PI, 0.50 * PI)
        main.append(crown(body_type, line, lift, bulge=lambda t, p: 0.030 * s * (1.0 - smoothstep(0.0, 0.40 * PI, t)) *
                          (0.4 + 0.6 * max(0.0, -math.cos(p)))))
        ring_lo = [head.point(body_type, line(PI + 2 * PI * k / 48), PI + 2 * PI * k / 48, lift + 0.008 * s) for k in range(48)]
        main.append(tube(ring_lo, [0.007 * s] * 48, 6, HEADW, closed=True))
        return [(main, "accessory", "rib", (1.0, 1.0, 1.0))]
    if style == "hat_visor":
        line = hat_line(0.30 * PI, 0.40 * PI, 0.46 * PI)
        band = []
        for k in range(48):
            phi = PI + 2 * PI * k / 48
            band.append(head.point(body_type, line(phi) - 0.03 * PI, phi, 0.020 * s))
        main.append(tube(band, [0.010 * s] * 48, 8, HEADW, closed=True))
        main.append(brim(body_type, lambda p: line(p) - 0.03 * PI, 0.018 * s, lambda p: 0.075 * math.cos(p) ** 2,
                         lambda p: 0.016 * math.cos(p) ** 2, segments=24, phi_range=(-0.36 * PI, 0.36 * PI), curl=0.02))
        return [(main, "accessory", "jersey", (1.0, 1.0, 1.0))]
    raise ValueError(style)


# ----- facial hair ------------------------------------------------------------------------------

def _beard_region(my, full, chin_only=False, sideburns=False, mutton=False, chin_strap=False):
    """region(x, y, z) in head-local male-scale metres: 1 where a beard of this shape grows."""
    def region(x, y, z):
        ax = abs(x)
        # The cheek line runs from the sideburn in front of the ear (high) to beside the mouth (low);
        # nothing wraps behind the ears.
        cheek_y = -0.016 - 0.052 * smoothstep(0.000, 0.075, z)
        under = smoothstep(cheek_y + 0.010, cheek_y - 0.020, y) * smoothstep(-0.050, -0.018, z)
        if sideburns or mutton:
            # In front of the ear from under the hairline at the temple down past the lobe (the ear's
            # front edge is at z 0.002); mutton chops widen onto the cheeks and follow the jaw down.
            # Revision 3 cut them off at the full beard's cheek line, leaving a patch under the ear.
            # They thin toward the top, where they run on under the hairline.
            side = smoothstep(0.050, 0.080, ax) * smoothstep(0.085, 0.045, y)
            back = smoothstep(-0.008, 0.008, z)
            if mutton:
                front = 0.026 + 0.040 * smoothstep(-0.010, -0.075, y)
                return side * back * smoothstep(front + 0.016, front, z) * smoothstep(-0.116, -0.096, y)
            return side * back * smoothstep(0.038, 0.022, z) * smoothstep(-0.064, -0.046, y)
        if chin_only:
            return smoothstep(0.040, 0.026, ax) * smoothstep(my - 0.020, my - 0.034, y) * smoothstep(-0.02, 0.02, z)
        if full:
            # Clear of the mouth, the beard grows from just under the lower lip round to the jaw
            # (revision 3 left a bare patch under the lip, cut off square at 4 cm either side).
            clear = 1.0 - smoothstep(0.032, 0.048, ax)
            chin = 1.0 - clear * (1.0 - smoothstep(my - 0.016, my - 0.026, y))
            return under * chin
        if chin_strap:
            edge = smoothstep(-0.100, -0.122, y) + smoothstep(0.040, 0.010, z)
            return under * min(1.0, edge)
        return under
    return region


def _face_shell(body_type, region, thick, under=0.004, grooves=0.0, spacing=0.011, rows=40, columns=84, lift=0.0,
                theta=(0.46, 0.92), phi=0.66, drop=True, thin=None):
    """A shell over the lower face where region(x, y, z) (head-local male metres) is 1, `thick`
    above the skin, diving under it outside the region so its edge grows out of the skin.
    grooves: how deep the soft vertical channels between beard clumps are (0 smooth), `spacing`
    apart, bending outward toward the jaw; drop: leave out what lies wholly outside the region.
    u runs across the face and v down it over the shell's (theta, phi) range; thin(x, y, z) scales the
    thickness where the hair is thinner than its region (sideburns running on under the hairline)."""
    s = head.SCALE[body_type]
    c = head.centre(body_type)
    grid, inside_of, uvs, heights = [], [], [], []
    for r in range(rows):
        th = PI * (theta[0] + (theta[1] - theta[0]) * r / (rows - 1))
        row, ins, uv_row = [], [], []
        for k in range(columns):
            ph = -phi * PI + 2.0 * phi * PI * k / (columns - 1)
            local = head.local_point(body_type, th, ph)
            x, y, z = local[0] / s, local[1] / s, local[2] / s
            inside = region(x, y, z)
            h = thick * inside * (thin(x, y, z) if thin else 1.0)
            if grooves:
                # Clumps of hair growing down and a little outward along the jaw.
                lean = x + 0.35 * x * smoothstep(-0.02, -0.12, y)
                band = 0.5 + 0.5 * math.cos(2.0 * PI * lean / spacing + 0.8 * hair.jitter(int(lean / spacing * 4.0), 7))
                h *= 1.0 - grooves * band ** 2 * smoothstep(0.0, 0.6, inside)
            heights.append(lift * s + h - under * s * (1.0 - inside))
            row.append(head.point(body_type, th, ph, heights[-1]))
            ins.append(inside)
            uv_row.append((k / (columns - 1), r / (rows - 1)))
        grid.append(row)
        inside_of.append(ins)
        uvs.append(uv_row)
    shell = Mesh()
    shell.patch_origin = c
    shell.grid(grid, HEADW, wrap=False)
    shell.uvs = [uv for row in uvs for uv in row]
    if drop and thick > 0.0:
        # A grown beard ends where it meets the skin, cut along that line: dropping whole facets
        # and diving under the skin left its edge stepped along the facets.
        return shell.clipped(heights, 0.00025 * s)
    if drop:
        flat = [v for row in inside_of for v in row]
        kept = []
        for t in range(0, len(shell.indices), 3):
            tri = shell.indices[t:t + 3]
            if max(flat[i] for i in tri) > 0.02:
                kept += tri
        shell.indices = kept
        shell.compact()
    return shell


def _mustache_region(my, width=0.044, height=0.014, centre_gap=0.0, droop=0.0, taper=1.0):
    """1 over the upper lip: from under the nose down to the lip, following the lip's curve out
    past the mouth's corners to `width` each side (male metres), thinning toward its ends; `droop`
    carries the ends down beside the mouth."""
    nose_bottom = head.NOSE_TIP_Y - 0.011
    def region(x, y, z):
        ax = abs(x)
        u = min(ax / width, 1.0)
        # Full in the middle, thinning toward the ends (the shell shows where this is above ~0.3).
        # Rounded in section and toward its ends (a steep edge read as a slab in profile).
        end = (1.0 - 0.72 * u * u) * smoothstep(1.0, 1.0 - 0.30 * taper, u)
        top = nose_bottom - 0.002 - 0.010 * u * u - droop * smoothstep(0.55, 1.0, u)
        bottom = top - height * (1.0 - 0.45 * u * u) - droop * 1.4 * smoothstep(0.45, 1.0, u)
        soft = min(0.0052, 0.32 * height)
        band = smoothstep(bottom - 0.0015, bottom + soft, y) * smoothstep(top + 0.0015, top - soft, y)
        gap = smoothstep(centre_gap * 0.5, centre_gap * 0.5 + 0.003, ax) if centre_gap else 1.0
        return band * end * gap * smoothstep(0.02, 0.06, z)
    return region


def _union(*regions):
    return lambda x, y, z: max(r(x, y, z) for r in regions)


STUBBLE_THETA, STUBBLE_PHI = (0.46, 0.92), 0.66


def stubble_region(body_type, dilate=0.0):
    """Where stubble grows: the full beard's and the mustache's region, grown by `dilate` metres
    (the halo round a grown beard)."""
    s = head.SCALE[body_type]
    my = sub(head.anchors(body_type)["mouth"], head.centre(body_type))[1] / s
    base = _union(_beard_region(my, True), _mustache_region(my, width=0.044, height=0.020))
    if not dilate:
        return base
    offsets = [(0.0, 0.0)] + [(dilate * math.cos(a), dilate * math.sin(a)) for a in (k * PI / 4 for k in range(8))]
    line = head.MOUTH_LINE[0]
    nose_bottom = head.NOSE_TIP_Y - 0.011

    def grown(x, y, z):
        # Grown, but never onto the nose or the lips.
        nose = smoothstep(nose_bottom - 0.004, nose_bottom + 0.002, y) * smoothstep(0.024, 0.014, abs(x))
        lips = smoothstep(line - 0.013, line - 0.009, y) * smoothstep(line + 0.008, line + 0.004, y) * smoothstep(0.036, 0.028, abs(x))
        return max(base(x + dx, y + dy, z) for dx, dy in offsets) * (1.0 - max(nose, lips))
    return grown


def stubble_texture(body_type, density, width=384, height=192, dilate=0.0):
    """The stubble texture of one body: fine soft dots where the beard grows (grown by `dilate`),
    over the stubble shell's (u, v) -- u across the face, v from the cheeks down under the jaw."""
    from . import textures
    s = head.SCALE[body_type]
    region = stubble_region(body_type, dilate)
    rows = []
    for py in range(height):
        v = (py + 0.5) / height
        th = PI * (STUBBLE_THETA[0] + (STUBBLE_THETA[1] - STUBBLE_THETA[0]) * v)
        row = []
        for px in range(width):
            u = (px + 0.5) / width
            ph = -STUBBLE_PHI * PI + 2.0 * STUBBLE_PHI * PI * u
            local = head.local_point(body_type, th, ph)
            row.append(region(local[0] / s, local[1] / s, local[2] / s))
        rows.append(row)

    def fn(u, v):
        inside = rows[min(int(v * height), height - 1)][min(int(u * width), width - 1)]
        if inside <= 0.0:
            return (1.0, 1.0, 1.0, 0.0)
        cx, cy = u * 260.0, v * 130.0
        best = 1.0
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                ix, iy = math.floor(cx) + dx, math.floor(cy) + dy
                qx = ix + 0.5 + 0.8 * (textures._hash(ix, iy, 41) - 0.5)
                qy = iy + 0.5 + 0.8 * (textures._hash(ix, iy, 43) - 0.5)
                best = min(best, math.hypot(cx - qx, cy - qy))
        dots = smoothstep(0.45, 0.12, best)
        return (1.0, 1.0, 1.0, density * smoothstep(0.0, 0.6, inside) * (0.28 + 0.30 * dots))
    return textures.image_rgba(width, height, fn)


def _hanging_beard(body_type, from_y, half_width, length_, seed, thick, count=7):
    """A beard grown long: strands from under the chin and along the jaw hanging down onto the
    chest and drawn together toward a rounded point (a hair mass), from head-local height from_y
    across half_width each side."""
    from .hairmass import Obstacles, Flow, shell
    s = head.SCALE[body_type]
    ob = Obstacles(body_type)
    roots = []
    for k in range(count):
        x = -half_width + 2.0 * half_width * k / (count - 1)
        p, _, _ = head.front_point(body_type, x * s, from_y * s, 0.002 * s)
        roots.append((p, normalize((x * 0.4, -1.0, 0.35)), "beard"))
    point = add(head.front_point(body_type, 0.0, from_y * s, 0.0)[0], (0.0, -length_ * s, 0.035 * s))
    flow = Flow(gravity=10.0, lift=0.003 * s, cling=0.0, body=True, attract=point, attract_gain=16.0)
    # Short beards end in soft rounded clumps, long ones taper to points.
    pointed = smoothstep(0.03, 0.09, length_)
    return shell(ob, roots, length_ * s, flow, lambda t: thick * s * (1.0 - 0.55 * t), seed, closed=False,
                 tip=0.15 + 0.75 * pointed, notch=(0.008 + 0.032 * pointed) * s, tip_round=0.85, groove=0.78, sub_columns=5)


def facial_hair(body_type, style):
    s = head.SCALE[body_type]
    a = head.anchors(body_type)
    c = head.centre(body_type)
    my = sub(a["mouth"], c)[1] / s
    beard_tex = "hair_cap"
    parts = []

    def add_part(mesh, texture=beard_tex, colour=(1.0, 1.0, 1.0)):
        if not (mesh.positions and mesh.indices):
            return
        # Pieces of one material are one part (one draw).
        for existing in parts:
            if existing[2] == texture and existing[3] == colour:
                existing[0].append(mesh)
                return
        parts.append((mesh, "hair", texture, colour))

    if style in ("facial_stubble", "facial_heavystubble"):
        # Stubble is a translucent shading over the skin, not a shell: a texture over the lower
        # face holding both the grain and where the beard grows, so it fades out at its edge.
        shell = _face_shell(body_type, stubble_region(body_type), 0.0, under=0.0, lift=0.0007, theta=STUBBLE_THETA,
                            phi=STUBBLE_PHI)
        texture = ("stubble" if style == "facial_stubble" else "stubble_heavy") + "_" + body_type
        return [(shell, "hair", texture, (1.0, 1.0, 1.0), {"cnaBlend": "alpha"})]
    mustaches = {
        "facial_mustache": dict(width=0.046, height=0.015),
        "facial_pencil": dict(width=0.040, height=0.0055, centre_gap=0.005),
        "facial_handlebar": dict(width=0.044, height=0.013),
        "facial_horseshoe": dict(width=0.046, height=0.014, droop=0.010),
        "facial_goatee": dict(width=0.040, height=0.013),
        "facial_vandyke": dict(width=0.042, height=0.011, centre_gap=0.004),
        "facial_beard": dict(width=0.048, height=0.016, droop=0.012),
        "facial_shortbeard": dict(width=0.046, height=0.014, droop=0.012),
        "facial_longbeard": dict(width=0.048, height=0.016, droop=0.012),
        "facial_muttonchops": dict(width=0.046, height=0.015),
    }
    region = None
    thick = 0.0
    if style in ("facial_beard", "facial_chinstrap", "facial_shortbeard", "facial_longbeard"):
        region = _beard_region(my, style != "facial_chinstrap", chin_strap=style == "facial_chinstrap")
        thick = {"facial_beard": 0.011, "facial_chinstrap": 0.006, "facial_shortbeard": 0.006, "facial_longbeard": 0.012}[style]
    elif style in ("facial_sideburns", "facial_muttonchops"):
        region = _beard_region(my, False, sideburns=style == "facial_sideburns", mutton=style == "facial_muttonchops")
        thick = 0.007
    elif style in ("facial_goatee", "facial_vandyke", "facial_chinbeard"):
        region = _beard_region(my, False, chin_only=True)
        thick = 0.010 if style != "facial_vandyke" else 0.009
    elif style == "facial_soulpatch":
        def region(x, y, z):
            return smoothstep(0.010, 0.006, abs(x)) * smoothstep(my - 0.012, my - 0.016, y) * \
                smoothstep(my - 0.036, my - 0.030, y) * smoothstep(0.02, 0.06, z)
        thick = 0.006
    elif style == "facial_horseshoe":
        def region(x, y, z):
            ax = abs(x)
            return smoothstep(0.044, 0.040, ax) * smoothstep(0.026, 0.030, ax) * smoothstep(my + 0.006, my - 0.002, y) * \
                smoothstep(my - 0.058, my - 0.050, y) * smoothstep(0.02, 0.06, z)
        thick = 0.008
    if region is not None:
        if style in ("facial_sideburns", "facial_muttonchops"):
            # From the temple, on finer facets (they are narrow and cross the side of the head).
            add_part(_face_shell(body_type, region, thick * s, under=0.0015, grooves=0.45, spacing=0.012, theta=(0.30, 0.92),
                                 rows=64, columns=128, thin=lambda x, y, z: 1.0 - 0.78 * smoothstep(-0.005, 0.040, y)))
        else:
            add_part(_face_shell(body_type, region, thick * s, grooves=0.45, spacing=0.012))
    if style in mustaches:
        m = mustaches[style]
        add_part(_face_shell(body_type, _mustache_region(my, **m), (0.0075 if style != "facial_pencil" else 0.0040) * s,
                             under=0.003, grooves=0.30, spacing=0.007, theta=(0.52, 0.66), phi=0.26, rows=30, columns=64))
    if style == "facial_handlebar":
        # The waxed ends curl up off each side.
        curls = Mesh()
        for side in (1.0, -1.0):
            start = head.front_point(body_type, side * 0.034 * s, (head.NOSE_TIP_Y - 0.026) * s, 0.004 * s)[0]
            mid = add(head.front_point(body_type, side * 0.046 * s, (head.NOSE_TIP_Y - 0.030) * s, 0.006 * s)[0], (0.0, 0.0, 0.004 * s))
            end = add(mid, (side * 0.006 * s, 0.010 * s, 0.002 * s))
            curls.append(hair.lock(body_type, hair._catmull([start, mid, end], 4), hair.taper(0.007 * s, 0.2),
                                   hair.taper(0.005 * s, 0.3)))
        add_part(curls, "hair_clump")
    if style == "facial_longbeard":
        add_part(_hanging_beard(body_type, my - 0.055, 0.050, 0.11, 491, 0.012), "hair_clump")
    elif style in ("facial_goatee", "facial_vandyke"):
        add_part(_hanging_beard(body_type, my - 0.052, 0.018, 0.022 if style == "facial_goatee" else 0.040, 492, 0.008),
                 "hair_clump")
    elif style == "facial_chinbeard":
        add_part(_hanging_beard(body_type, my - 0.052, 0.026, 0.030, 493, 0.010), "hair_clump")
    if not parts:
        raise ValueError(style)
    if style in ("facial_beard", "facial_shortbeard", "facial_longbeard", "facial_muttonchops", "facial_chinstrap"):
        # Under a grown beard the face is stubbled, so the beard's edge fades into the skin; the
        # halo reaches as far as its texture does (grown by the same 12 mm; cut at the ungrown region,
        # its facets ended in steps where the texture was still dark).
        halo = _face_shell(body_type, stubble_region(body_type, 0.012), 0.0, under=0.0, lift=0.0007, theta=STUBBLE_THETA,
                           phi=STUBBLE_PHI)
        parts.insert(0, (halo, "hair", "stubble_halo_" + body_type, (1.0, 1.0, 1.0), {"cnaBlend": "alpha"}))
    return parts


# ----- earrings (catalog v3 revision 2, format 3) ---------------------------------------------------

EARRINGS = ["earrings_stud", "earrings_bigstud", "earrings_hoop", "earrings_bighoop", "earrings_drop", "earrings_huggie"]


def earrings(body_type, style):
    """A pair, each side its own part (cnaSide) hanging from the lobe of the default ear."""
    s = head.SCALE[body_type]
    out = []
    for side, name in ((1.0, "left"), (-1.0, "right")):
        lobe, outward, up = head.ear_lobe(body_type, side)
        front = normalize(cross(up, outward)) if side > 0 else mul(normalize(cross(up, outward)), -1.0)
        m, gem = Mesh(), Mesh()
        if style in ("earrings_stud", "earrings_bigstud"):
            r = (0.0046 if style == "earrings_stud" else 0.0068) * s
            m.append(ellipsoid(add(lobe, mul(outward, 0.0025 * s)), (front, up, outward), (r, r, r * 0.8), 10, 8, HEADW))
        elif style in ("earrings_hoop", "earrings_bighoop", "earrings_huggie"):
            rad = {"earrings_hoop": 0.0090, "earrings_bighoop": 0.0160, "earrings_huggie": 0.0052}[style] * s
            thick = {"earrings_hoop": 0.0015, "earrings_bighoop": 0.0017, "earrings_huggie": 0.0020}[style] * s
            centre = add(add(lobe, mul(up, -rad * 0.85)), mul(outward, 0.0015 * s))
            loop = [add(centre, add(mul(up, rad * math.cos(2 * PI * k / 24)), mul(front, rad * math.sin(2 * PI * k / 24))))
                    for k in range(24)]
            m.append(tube(loop, [thick] * 24, 6, HEADW, closed=True))
        else:  # drop: a short chain and a teardrop
            top = add(lobe, mul(outward, 0.0015 * s))
            bottom = add(top, mul(up, -0.012 * s))
            m.append(tube([top, lerp(top, bottom, 0.5), bottom], [0.0008 * s] * 3, 5, HEADW))
            gem.append(ellipsoid(add(bottom, mul(up, -0.005 * s)), (front, up, outward), (0.0036 * s, 0.0055 * s, 0.0030 * s), 10, 8,
                                 HEADW))
        out.append((m, "jewelry", "plain", (1.0, 1.0, 1.0), {"cnaSide": name}))
        if gem.positions:
            out.append((gem, "accessory", "plain", (1.0, 1.0, 1.0), {"cnaSide": name}))
    return out
