# SPDX-License-Identifier: MS-PL
"""The catalog's clothing: tops, bottoms and shoes, built as offset lofts over the body's own
sections and skin weights so they deform with it, with the details that make a garment read as
one: rolled necklines, collars, hoods, pockets, cuffs, hems, waistbands, soles and laces.

Every builder returns a list of (Mesh, tint, texture kind, base colour); tint "none" parts keep
their base colour (soles, laces, buttons), "accessory" parts take the description's accessory
colour (a jacket's inner shirt).
"""
import math

from . import rig, body, head
from .mathutil import add, sub, mul, normalize, cross, dot, length, lerp, smoothstep, clamp, frame_from_axis
from .mesh import Mesh, ellipsoid, tube, blend

I = rig.INDEX
PI = math.pi


# ----- shared pieces ------------------------------------------------------------------------------

def shell(body_type, inflate, rows, segments=40, cap_bottom=None, lower=0.0, flare=0.0, open_front=None):
    """Torso loft between two section rows (inclusive), inflated; `lower` drops the bottom row
    (a longer hem) and `flare` widens it."""
    sections = body.torso_sections(body_type, inflate)[rows[0]:rows[1] + 1]
    if lower or flare:
        c, rx, rz, n = sections[0]
        sections[0] = (add(c, (0.0, -lower, 0.0)), rx + flare, rz + flare, n)
    # Tops (from row 1 up) hang over the thighs; bottoms have legs of their own.
    weights = hem_weights(body_type) if rows[0] >= 1 else body.torso_weights(body_type)
    return body.section_loft(sections, segments, weights, cap_bottom=cap_bottom,
                             adjust=body.clear_of_hips(body_type, inflate + HIP_CLEARANCE))


def hem_weights(body_type):
    """Torso weights for a top: below the waist the cloth follows both thighs, shared smoothly
    across the front and back centre, so a bent hip lifts the hem instead of pushing the trousers
    through it, and the hem neither tears between the legs nor stays behind."""
    tw = body.torso_weights(body_type)
    hip_y = rig.PROPORTIONS[body_type]["hip"][1]
    left, right = I["HipLeft"], I["HipRight"]

    def fn(p):
        w = tw(p)
        # Only below the hip joints: cloth above them would swing inward as a thigh rises.
        share = 0.9 * smoothstep(hip_y + 0.01, hip_y - 0.05, p[1])
        if share <= 0.0:
            return w
        w = {k: v * (1.0 - share) for k, v in w.items()}
        side = smoothstep(-0.06, 0.06, p[0])
        w[left] = w.get(left, 0.0) + share * side
        w[right] = w.get(right, 0.0) + share * (1.0 - side)
        return w
    return fn


def ring_band(body_type, row, inflate, radius, segments=40, offset=(0.0, 0.0, 0.0), squash=1.0):
    """A soft rolled band (hem, waistband, neck rib) around a torso section row."""
    c, rx, rz, n = body.torso_sections(body_type, inflate)[row]
    c = add(c, offset)
    loop = body.superellipse_ring(c, rx, rz, n, segments)
    return tube(loop, [radius] * len(loop), 8, body.torso_weights(body_type), closed=True)


def _hem(body_type, inflate, flare, lower, radius):
    """The rolled bottom hem of a top, matching shell(..., lower, flare)."""
    c, rx, rz, n = body.torso_sections(body_type, inflate)[1]
    loop = body.superellipse_ring(add(c, (0.0, -lower, 0.0)), rx + flare, rz + flare, n, 40)
    return tube(loop, [radius] * len(loop), 8, hem_weights(body_type), closed=True)


def neck_rib(body_type, inflate, radius, drop=0.0, depth=0.0, width=1.0):
    """A crew or scoop neckline rib: a ring around the base of the neck; `depth` lowers the front."""
    sections = body.torso_sections(body_type, inflate)
    c, rx, rz, n = sections[10]
    loop = []
    for k in range(32):
        a = 2.0 * PI * k / 32
        front = max(0.0, math.cos(a)) ** 2
        loop.append((c[0] + rx * 0.92 * width * math.sin(a), c[1] - drop - depth * front,
                     c[2] + rz * math.cos(a) * (1.0 + 0.25 * front * (depth > 0))))
    return tube(loop, [radius] * len(loop), 8, body.torso_weights(body_type), closed=True)


def sleeve(body_type, side, inflate, end, cuff=0.0, flare=0.0, start=0.02):
    m = body.arm(body_type, side, inflate=inflate * 0.8, t_range=(start, end), flare=flare)
    if cuff:
        m.append(_limb_ring(body_type, side, "arm", end, inflate + flare, cuff))
    return m


def trouser_leg(body_type, side, inflate, end, cuff=0.0, flare=0.0):
    m = body.leg(body_type, side, inflate=inflate, t_range=(-0.02, end), flare=flare)
    if cuff:
        m.append(_limb_ring(body_type, side, "leg", end, inflate + flare, cuff))
    return m


def _limb_ring(body_type, side, kind, t, inflate, radius):
    """A cuff ring at position t along an arm or leg (matching body.limb's sections)."""
    pos = rig.joint_positions(body_type)
    if kind == "arm":
        a, b, c = (pos[I[n + side]] for n in ("Shoulder", "Elbow", "Wrist"))
        profile, weights = body.ARM[body_type], body.arm_weights(body_type, side)
    else:
        a, b, c = (pos[I[n + side]] for n in ("Hip", "Knee", "Ankle"))
        profile, weights = body.LEG[body_type], body.leg_weights(body_type, side)
    upper, lower = length(sub(b, a)), length(sub(c, b))
    knee = upper / (upper + lower)
    if t <= knee:
        centre, axis = add(a, mul(normalize(sub(b, a)), t * (upper + lower))), normalize(sub(b, a))
    else:
        centre, axis = add(b, mul(normalize(sub(c, b)), (t - knee) * (upper + lower))), normalize(sub(c, b))
    _, w, d = body._profile_at(profile, t)
    from .mathutil import frame_from_axis
    u, v, _ = frame_from_axis(axis, (0.0, 0.0, 1.0))
    loop = [add(centre, add(mul(u, (w + inflate) * math.cos(2 * PI * k / 24)), mul(v, (d + inflate) * math.sin(2 * PI * k / 24))))
            for k in range(24)]
    wt = weights(t)
    return tube(loop, [radius] * len(loop), 8, lambda p: wt, closed=True)


# How far a torso garment keeps beyond the tops of the legs, on top of its own inflation: enough
# for a top to cover any bottom's legs.
HIP_CLEARANCE = 0.004

def patch(body_type, centre, size, normal, weights, depth=0.004, segments=12):
    """A thin rounded slab (pocket, label) lying on a surface facing `normal`."""
    n = normalize(normal)
    right = normalize(cross((0.0, 1.0, 0.0), n)) if abs(n[1]) < 0.9 else (1.0, 0.0, 0.0)
    up = cross(n, right)
    return ellipsoid(add(centre, mul(n, depth * 0.5)), (right, up, n), (size[0], size[1], depth), segments, 6,
                     lambda p, v, u: weights(p))


def front_z(body_type, inflate, x, y):
    """Depth of the torso's front surface at (x, y), between the section rows around y."""
    sections = body.torso_sections(body_type, inflate)
    for low, high in zip(sections, sections[1:]):
        if low[0][1] <= y <= high[0][1]:
            t = (y - low[0][1]) / max(high[0][1] - low[0][1], 1e-6)
            z = []
            for c, rx, rz, n in (low, high):
                s = clamp(x / rx, -1.0, 1.0)
                z.append(c[2] + rz * (1.0 - abs(s) ** n) ** (1.0 / n))
            return z[0] + (z[1] - z[0]) * t
    c, rx, rz, n = sections[0] if y < sections[0][0][1] else sections[-1]
    return c[2] + rz * (1.0 - abs(clamp(x / rx, -1.0, 1.0)) ** n) ** (1.0 / n)


def kangaroo_pocket(body_type, inflate, bottom, height, bottom_half, top_half, weights, depth=0.005):
    """A hoodie's front pocket: a flat rounded trapezoid hugging the belly, wider at the bottom,
    with a rolled edge all round."""
    m = Mesh()
    m.patch_origin = (0.0, bottom + height * 0.5, 0.0)
    columns, rows_ = 14, 8
    grid = []
    for j in range(rows_ + 1):
        v = j / rows_
        half = bottom_half + (top_half - bottom_half) * v
        y = bottom + height * v
        grid.append([(half * (2.0 * i / columns - 1.0), y, 0.0) for i in range(columns + 1)])
    face = [[(x, y, front_z(body_type, inflate, x, y) + depth) for x, y, _ in row] for row in grid]
    m.append(Mesh().grid(face, lambda q, v, u: weights(q), wrap=False))
    m.patch_origin = (0.0, bottom + height * 0.5, 0.0)
    # The rolled edge: round the outline, just proud of the face.
    outline = [q for q in face[0]] + [row[-1] for row in face[1:]] + list(reversed(face[-1][:-1])) + \
        [row[0] for row in reversed(face[1:-1])]
    m.append(tube(outline, [depth * 0.9] * len(outline), 6, lambda q: weights(q), closed=True))
    return m


def front_point(body_type, row, inflate, x):
    """A point on the front of a torso row at horizontal offset x."""
    c, rx, rz, n = body.torso_sections(body_type, inflate)[row]
    s = clamp(x / rx, -1.0, 1.0)
    z = rz * (1.0 - abs(s) ** n) ** (1.0 / n)
    return (c[0] + x, c[1], c[2] + z)


def row_y(body_type, row):
    return body.torso_sections(body_type)[row][0][1]


# ----- tops ---------------------------------------------------------------------------------------

def top(body_type, style):
    if style in REVISED_TOPS:
        return revised_top(body_type, style)
    tw = hem_weights(body_type)
    parts = []
    main = Mesh()
    trim = Mesh()
    if style in ("top_tshirt", "top_polo"):
        main.append(shell(body_type, 0.017, (1, 10), lower=0.03, flare=0.030))
        main.append(_hem(body_type, 0.017, 0.030, 0.03, 0.006))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.016, 0.42, cuff=0.006, flare=0.006))
        if style == "top_tshirt":
            main.append(neck_rib(body_type, 0.012, 0.009, drop=0.004))
        else:
            main.append(_collar(body_type, 0.016, spread=0.55))
            for k in range(2):
                trim.append(_button(body_type, 9, 0.022, -0.028 - 0.03 * k, tw))
        parts.append((main, "top", "knit", (1.0, 1.0, 1.0)))
    elif style == "top_longsleeve":
        main.append(shell(body_type, 0.017, (1, 10), lower=0.03, flare=0.028))
        main.append(_hem(body_type, 0.017, 0.028, 0.03, 0.006))
        main.append(neck_rib(body_type, 0.011, 0.009, drop=0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.014, 0.95, cuff=0.007))
        parts.append((main, "top", "knit", (1.0, 1.0, 1.0)))
    elif style == "top_hoodie":
        main.append(shell(body_type, 0.028, (1, 10), lower=0.04, flare=0.010))
        main.append(_hem(body_type, 0.028, 0.010, 0.04, 0.012))
        main.append(neck_rib(body_type, 0.024, 0.011, drop=0.0))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.024, 0.93, cuff=0.011))
        main.append(_hood(body_type))
        pocket_y = row_y(body_type, 3) - 0.035
        main.append(kangaroo_pocket(body_type, 0.028, pocket_y, 0.10, 0.105, 0.078, tw))
        for sx in (0.024, -0.024):
            p = front_point(body_type, 10, 0.03, sx)
            trim.append(tube([add(p, (0.0, 0.0, 0.012)), add(p, (0.0, -0.05, 0.024)), add(p, (sx * 0.2, -0.10, 0.03))],
                             [0.0035] * 3, 6, tw, round_end=True))
        parts.append((main, "top", "fleece", (1.0, 1.0, 1.0)))
    elif style == "top_tank":
        # The body up to the chest, then a scooped neckline and armholes, with flat straps.
        sections = body.torso_sections(body_type, 0.017)
        c, rx, rz, n = sections[1]
        rows = [(add(c, (0.0, -0.03, 0.0)), rx + 0.028, rz + 0.028, n)] + sections[2:8]
        main.append(body.section_loft(rows, 40, tw, adjust=body.clear_of_hips(body_type, 0.017 + HIP_CLEARANCE)))
        main.append(_hem(body_type, 0.017, 0.028, 0.03, 0.005))
        c8, rx8, rz8, n8 = sections[8]
        c7 = sections[7][0]
        rim = []
        for k in range(40):
            a = 2.0 * PI * k / 40
            sa, ca = math.sin(a), math.cos(a)
            x = rx8 * 0.97 * math.copysign(abs(sa) ** (2.0 / n8), sa)
            z = c8[2] + rz8 * math.copysign(abs(ca) ** (2.0 / n8), ca)
            # Deeper at the front than at the back, highest where the straps meet it.
            scoop = 0.050 * max(ca, 0.0) ** 2 + 0.028 * max(-ca, 0.0) ** 2
            rim.append((x, c8[1] - scoop, z))
        top_row = [(p[0], p[1], p[2]) for p in rim]
        main.append(Mesh().grid([body.superellipse_ring(c7, sections[7][1], sections[7][2], sections[7][3], 40), top_row],
                                lambda p, v, u: tw(p), uv_seam=True))
        main.append(tube(rim, [0.005] * len(rim), 6, tw, closed=True))
        collar_y = rig.PROPORTIONS[body_type]["collar"][1]
        for side, sign in rig.SIDES:
            x = sign * rx8 * 0.50
            # The straps start on the neckline rim itself, front and back.
            front = min((p for p in rim if p[2] > c8[2]), key=lambda p: abs(p[0] - x))
            back = min((p for p in rim if p[2] < c8[2]), key=lambda p: abs(p[0] - x))
            front, back = add(front, (0.0, 0.004, -0.002)), add(back, (0.0, 0.004, 0.002))
            path = [front, (x * 0.96, collar_y + 0.014, rz8 * 0.55), (x * 0.94, collar_y + 0.030, c8[2]),
                    (x * 0.96, collar_y + 0.014, -rz8 * 0.55), back]
            strap = tube(path, [0.010] * 5, 8, tw)
            # Flatten the strap onto the shoulder.
            strap.positions = [(p[0], p[1] - 0.004 * max(0.0, p[1] - collar_y) / 0.03, p[2]) for p in strap.positions]
            main.append(strap)
        parts.append((main, "top", "knit", (1.0, 1.0, 1.0)))
    elif style == "top_shirt":
        main.append(shell(body_type, 0.019, (1, 10), lower=0.045, flare=0.032))
        main.append(_hem(body_type, 0.019, 0.032, 0.045, 0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.018, 0.44, cuff=0.007, flare=0.008))
        main.append(_collar(body_type, 0.018, spread=0.7))
        for k in range(5):
            trim.append(_button_at(body_type, 0.022, row_y(body_type, 9) - 0.045 - k * 0.078, tw))
        c = front_point(body_type, 7, 0.018, 0.06)
        main.append(patch(body_type, add(c, (0.0, -0.02, 0.0)), (0.028, 0.032), (0.1, 0.0, 1.0), tw, depth=0.004))
        parts.append((main, "top", "canvas", (1.0, 1.0, 1.0)))
    elif style == "top_sweater":
        main.append(shell(body_type, 0.024, (1, 10), lower=0.03, flare=0.026))
        main.append(_hem(body_type, 0.024, 0.026, 0.03, 0.012))
        main.append(neck_rib(body_type, 0.020, 0.013, drop=0.006))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.022, 0.93, cuff=0.012))
        parts.append((main, "top", "cable", (1.0, 1.0, 1.0)))
    elif style == "top_jacket":
        # An open zip jacket (top colour) over a crew shirt (accessory colour).
        inner = Mesh()
        inner.append(shell(body_type, 0.017, (1, 10), lower=0.02, flare=0.026))
        inner.append(_hem(body_type, 0.017, 0.026, 0.02, 0.005))
        inner.append(neck_rib(body_type, 0.009, 0.008, drop=0.004))
        parts.append((inner, "accessory", "knit", (1.0, 1.0, 1.0)))
        main.append(_open_shell(body_type, 0.030, (1, 10), gap=0.34, lower=0.04, flare=0.030))
        main.append(_collar_stand(body_type, 0.030))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.026, 0.94, cuff=0.011))
        parts.append((main, "top", "canvas", (1.0, 1.0, 1.0)))
    elif style == "top_turtleneck":
        main.append(shell(body_type, 0.017, (1, 10), lower=0.03, flare=0.028))
        main.append(_hem(body_type, 0.017, 0.028, 0.03, 0.007))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.015, 0.95, cuff=0.008))
        main.append(body.neck(body_type, inflate=0.016))
        parts.append((main, "top", "rib", (1.0, 1.0, 1.0)))
    else:
        raise ValueError(style)
    if trim.positions:
        parts.append((trim, "none", "plain", (0.92, 0.91, 0.88)))
    return parts


def _collar(body_type, inflate, spread=0.6):
    """A turned-down shirt collar: two leaves around the neck, open at the front."""
    tw = body.torso_weights(body_type)
    c, rx, rz, n = body.torso_sections(body_type, inflate)[10]
    rows = []
    for level in range(4):
        h = level / 3.0
        ring = []
        for k in range(29):
            a = spread * 0.5 + (2.0 * PI - spread) * k / 28
            fall = 0.035 * h
            ring.append((c[0] + (rx * 0.95 + 0.012 * h) * math.sin(a), c[1] + 0.022 - fall * 1.2 + 0.018 * (1 - h),
                         c[2] + (rz * 0.95 + 0.014 * h) * math.cos(a) + 0.004 * h))
        rows.append(ring)
    thick = [[add(p, (0.0, -0.004, 0.0)) for p in ring] for ring in reversed(rows)]
    return Mesh().grid(rows + thick, lambda p, v, u: tw(p), wrap=False)


def _collar_stand(body_type, inflate):
    tw = body.torso_weights(body_type)
    c, rx, rz, n = body.torso_sections(body_type, inflate)[10]
    loop = []
    for k in range(25):
        a = 0.45 + (2.0 * PI - 0.9) * k / 24
        loop.append((c[0] + rx * math.sin(a), c[1] + 0.012, c[2] + rz * math.cos(a)))
    return tube(loop, [0.013] * len(loop), 8, tw)


def _hood(body_type):
    """The hood lying folded on the upper back."""
    tw = body.torso_weights(body_type)
    c, rx, rz, n = body.torso_sections(body_type, 0.03)[10]
    collar_y = rig.PROPORTIONS[body_type]["collar"][1]
    centre = (0.0, collar_y + 0.035, c[2] - rz - 0.035)
    hood = lambda p, v, u: {I["BackUpper"]: 0.55, I["Neck"]: 0.45}
    m = ellipsoid(centre, ((1, 0, 0), normalize((0, 1, 0.35)), normalize((0, -0.35, 1))), (0.135, 0.085, 0.045), 24, 12, hood)
    rim = [(0.13 * math.sin(a), collar_y + 0.035 + 0.075 * math.cos(a), c[2] - rz - 0.012 + 0.02 * math.cos(a))
           for a in (PI * 0.5 + PI * k / 12 for k in range(13))]
    m.append(tube(rim, [0.016] * len(rim), 8, tw))
    return m


def _scoop(body_type, row, inflate):
    tw = body.torso_weights(body_type)
    c, rx, rz, n = body.torso_sections(body_type, inflate)[row]
    loop = []
    for k in range(20):
        a = -0.5 * PI + PI * k / 19
        x = rx * 0.50 * math.sin(a)
        p = front_point(body_type, row, inflate, x)
        loop.append((p[0], p[1] + 0.012 - 0.04 * math.cos(a), p[2] + 0.002))
    return tube(loop, [0.006] * len(loop), 6, tw)


def _button(body_type, row, inflate, dy, weights):
    p = front_point(body_type, row, inflate, 0.0)
    return ellipsoid(add(p, (0.0, dy, 0.003)), ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.006, 0.006, 0.0025), 8, 5,
                     lambda q, v, u: weights(q))


def _button_at(body_type, inflate, y, weights):
    rows = body.torso_sections(body_type, inflate)
    for (c0, rx0, rz0, n0), (c1, rx1, rz1, n1) in zip(rows, rows[1:]):
        if c0[1] <= y <= c1[1]:
            t = (y - c0[1]) / (c1[1] - c0[1])
            z = c0[2] + rz0 + (c1[2] + rz1 - c0[2] - rz0) * t
            return ellipsoid((0.0, y, z + 0.003), ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.006, 0.006, 0.0025), 8, 5,
                             lambda q, v, u: weights(q))
    return Mesh()


def _open_shell(body_type, inflate, rows, gap, lower=0.0, flare=0.004):
    """A jacket body open down the front: the loft covers every angle except `gap` radians."""
    clear = body.clear_of_hips(body_type, inflate + HIP_CLEARANCE)
    sections = body.torso_sections(body_type, inflate)[rows[0]:rows[1] + 1]
    c, rx, rz, n = sections[0]
    sections[0] = (add(c, (0.0, -lower, 0.0)), rx + flare, rz + flare, n)
    tw = hem_weights(body_type)
    grid = []
    for c, rx, rz, n in sections:
        ring = []
        for k in range(37):
            a = gap * 0.5 + (2.0 * PI - gap) * k / 36
            s, co = math.sin(a), math.cos(a)
            ring.append(clear((c[0] + rx * math.copysign(abs(s) ** (2.0 / n), s), c[1],
                               c[2] + rz * math.copysign(abs(co) ** (2.0 / n), co))))
        grid.append(ring)
    outer = Mesh().grid(grid, lambda p, v, u: tw(p), wrap=False)
    # Lapel edges: a rolled border along both front edges.
    for index in (0, 36):
        edge = [ring[index] for ring in grid]
        outer.append(tube(edge, [0.008] * len(edge), 6, tw))
    return outer


# ----- bottoms ------------------------------------------------------------------------------------

def bottom(body_type, style):
    if style in REVISED_BOTTOMS:
        return revised_bottom(body_type, style)
    tw = body.torso_weights(body_type)
    main = Mesh()
    parts = []
    if style in ("bottom_jeans", "bottom_cargo", "bottom_joggers", "bottom_shorts"):
        inflate = 0.010 if style != "bottom_joggers" else 0.014
        main.append(shell(body_type, inflate, (0, 3), cap_bottom=0.05))
        main.append(ring_band(body_type, 3, inflate - 0.002, 0.0055 if style != "bottom_joggers" else 0.007))
        end = {"bottom_shorts": 0.40}.get(style, 0.97)
        for side, sign in rig.SIDES:
            cuff = {"bottom_jeans": 0.008, "bottom_joggers": 0.012, "bottom_shorts": 0.007, "bottom_cargo": 0.008}[style]
            flare = {"bottom_shorts": 0.014, "bottom_joggers": -0.004}.get(style, 0.004)
            main.append(trouser_leg(body_type, side, inflate + 0.002, end, cuff=cuff, flare=flare))
            if style == "bottom_cargo":
                pos = rig.joint_positions(body_type)
                hip, knee = pos[I["Hip" + side]], pos[I["Knee" + side]]
                c = lerp(hip, knee, 0.55)
                lw = body.leg_weights(body_type, side)
                main.append(patch(body_type, add(c, (sign * 0.072, 0.0, 0.004)), (0.036, 0.048), (sign, 0.0, 0.25),
                                  lambda p, lw=lw: lw(0.3), depth=0.014))
        texture = {"bottom_jeans": "denim", "bottom_cargo": "canvas", "bottom_joggers": "fleece",
                   "bottom_shorts": "canvas"}[style]
        parts.insert(0, (main, "bottom", texture, (1.0, 1.0, 1.0)))
        return parts
    if style in ("bottom_skirt", "bottom_longskirt"):
        main.append(shell(body_type, 0.008, (0, 3), cap_bottom=0.05))
        main.append(ring_band(body_type, 3, 0.007, 0.005))
        p = rig.PROPORTIONS[body_type]
        a = body.anchors(body_type)
        waist = body.torso_sections(body_type, 0.012)[3]
        hips = body.torso_sections(body_type, 0.012)[1]
        bottom_y = p["knee"][1] + (0.07 if style == "bottom_skirt" else -0.16)
        hip_y = p["hip"][1]
        flare = 0.07 if style == "bottom_skirt" else 0.09
        sections = []
        for i in range(10):
            t = i / 9.0
            y = a["lower"] + 0.02 + (bottom_y - a["lower"] - 0.02) * t
            k = smoothstep(a["lower"] + 0.02, hip_y - 0.02, y)
            # Within the hips' 1.2 cm: every top (1.7 cm out) must cover the skirt's waist.
            rx = waist[1] + (hips[1] - waist[1]) * k + flare * t * t
            rz = waist[2] + (hips[2] - waist[2]) * k + flare * 0.8 * t * t
            sections.append(((0.0, y, 0.0), rx, rz, 2.2))
        sections.reverse()

        def skirt(pt):
            # Below the hip joints the skirt rides on both thighs, so a raised knee lifts it.
            k = 0.95 * smoothstep(hip_y + 0.005, hip_y - 0.06, pt[1])
            s = smoothstep(-0.07, 0.07, pt[0])
            w = {I["BackLower"]: 1.0 - k}
            if k > 0:
                w[I["HipLeft"]] = k * s
                w[I["HipRight"]] = k * (1.0 - s)
            return w
        main.append(body.section_loft(sections, 48, skirt))
        hem = body.superellipse_ring(sections[0][0], sections[0][1], sections[0][2], 2.2, 48)
        main.append(tube(hem, [0.006] * len(hem), 6, skirt, closed=True))
        return [(main, "bottom", "pleat" if style == "bottom_skirt" else "knit", (1.0, 1.0, 1.0))]
    raise ValueError(style)


# ----- shoes --------------------------------------------------------------------------------------

def _footprint(length_, width, heel_width, k):
    """Sole outline point k of n around a foot (z forward), as (x, z) relative to the heel."""
    a = 2.0 * PI * k
    s, c = math.sin(a), math.cos(a)
    z = 0.5 * length_ * (1.0 + c)
    widening = heel_width + (width - heel_width) * smoothstep(0.1, 0.7, z / length_)
    x = 0.5 * widening * math.copysign(abs(s) ** 0.75, s)
    return x, z


def shoe(body_type, side, style, socks):
    """(upper Mesh, trim Mesh) of one shoe; a sock, if the style has one, goes into `socks`."""
    pos = rig.joint_positions(body_type)
    ankle, toe = pos[I["Ankle" + side]], pos[I["Toe" + side]]
    sign = 1.0 if side == "Left" else -1.0
    scale = 1.0 if body_type == "male" else 0.92
    L = {"shoes_dress": 0.285, "shoes_flats": 0.255}.get(style, 0.29) * scale
    W, HW = 0.108 * scale, 0.080 * scale
    heel_z = ankle[2] - 0.065 * scale
    sole_t = {"shoes_boots": 0.030, "shoes_dress": 0.016, "shoes_flats": 0.010}.get(style, 0.024) * scale
    weights = lambda p: blend(I["Ankle" + side], I["Toe" + side], smoothstep(toe[2] - 0.07, toe[2] + 0.01, p[2]))
    cx = ankle[0] + sign * 0.004
    n = 36
    outline = [_footprint(L, W, HW, k / n) for k in range(n)]
    trim, upper = Mesh(), Mesh()
    # Sole: a rounded slab.
    rows = []
    rubber = 0.014 * scale if style in ("shoes_sneakers", "shoes_hightops") else 0.0
    for y, inset in ((0.0, 0.006), (0.003, 0.0), (sole_t - 0.003, 0.0), (sole_t, 0.004)):
        ring = []
        for x, z in outline:
            d = normalize((x, 0.0, (z - L * 0.5) * 0.35))
            toe_lift = rubber * smoothstep(0.80, 0.98, z / L) * (y / sole_t)
            ring.append((cx + x - d[0] * inset, y + toe_lift, heel_z + z - d[2] * inset))
        rows.append(ring)
    centre_bottom = (cx, 0.0, heel_z + L * 0.5)
    rows.insert(0, [centre_bottom] * n)
    rows.append([(cx, sole_t, heel_z + L * 0.5)] * n)
    sole = Mesh().grid(rows, lambda p, v, u: weights(p))
    (upper if style in ("shoes_dress", "shoes_flats") else trim).append(sole)
    # Upper: a dome over the footprint, low at the toe and full over the heel.
    cz = heel_z + L * 0.46
    toe_h = {"shoes_dress": 0.040, "shoes_flats": 0.030}.get(style, 0.046) * scale
    heel_h = {"shoes_flats": 0.048, "shoes_dress": 0.070}.get(style, 0.080) * scale

    def height(z):
        return toe_h + (heel_h - toe_h) * smoothstep(0.78, 0.32, (z - heel_z) / L)
    rows = []
    levels = 8
    for i in range(levels + 1):
        a = 0.5 * PI * i / levels
        r = math.cos(a)
        ring = []
        for x, z in outline:
            px, pz = cx + x * r, cz + (heel_z + z - cz) * r
            ring.append((px, sole_t - 0.004 + (height(pz) + 0.004) * math.sin(a) ** 0.8, pz))
        rows.append(ring)
    upper.append(Mesh().grid(rows, lambda p, v, u: weights(p)))
    collar_y = sole_t + heel_h - 0.004 * scale
    shin_r = body.LEG[body_type][-1][1]
    if style in ("shoes_boots", "shoes_hightops"):
        # The shaft follows the shin: boots are roomy and go over trousers, high-tops hug the
        # ankle and hide inside trouser legs.
        top_t = {"shoes_boots": 0.80, "shoes_hightops": 0.87}[style]
        inflate = {"shoes_boots": 0.021, "shoes_hightops": 0.008}[style]
        upper.append(body.leg(body_type, side, inflate=inflate, t_range=(top_t, 1.0)))
        collar_t = top_t
        if style == "shoes_boots":
            upper.append(_limb_ring(body_type, side, "leg", collar_t, inflate, 0.0065 * scale))
    elif style in ("shoes_sneakers", "shoes_dress"):
        # A sock covering the ankle, hidden inside trouser legs.
        sock = body.leg(body_type, side, inflate=0.005, t_range=(0.88, 1.0))
        socks.append(sock)
    if style == "shoes_sneakers":
        collar = [(ankle[0] + (shin_r + 0.009) * math.sin(2 * PI * k / 24), collar_y,
                   ankle[2] - 0.004 + (shin_r + 0.009) * 1.05 * math.cos(2 * PI * k / 24)) for k in range(24)]
        (trim if style == "shoes_sneakers" else upper).append(tube(collar, [0.0055 * scale] * 24, 8, weights, closed=True))
    if style in ("shoes_sneakers", "shoes_hightops", "shoes_boots"):
        for k in range(4 if style != "shoes_boots" else 5):
            z = heel_z + L * (0.66 - 0.075 * k)
            yy = sole_t + height(z) + 0.002
            trim.append(tube([(cx - 0.022, yy - 0.004, z), (cx, yy + 0.002, z + 0.004), (cx + 0.022, yy - 0.004, z)],
                             [0.0035] * 3, 6, weights, round_end=True))
    return upper, trim


def shoes(body_type, style):
    if style in REVISED_SHOES:
        return revised_shoes(body_type, style)
    parts = []
    upper, trim, socks = Mesh(), Mesh(), Mesh()
    for side, _ in rig.SIDES:
        u, t = shoe(body_type, side, style, socks)
        upper.append(u)
        trim.append(t)
    texture = {"shoes_boots": "leather", "shoes_dress": "leather", "shoes_flats": "leather"}.get(style, "canvas")
    parts.append((upper, "shoes", texture, (1.0, 1.0, 1.0)))
    if trim.positions:
        colour = (0.18, 0.17, 0.16) if style == "shoes_boots" else (0.95, 0.95, 0.92)
        parts.append((trim, "none", "plain", colour))
    if socks.positions:
        parts.append((socks, "none", "rib", (0.94, 0.94, 0.92) if style == "shoes_sneakers" else (0.20, 0.20, 0.22)))
    return parts


# ----- catalog v3 revision 2: more tops, bottoms and shoes ---------------------------------------

REVISED_TOPS = ["top_vneck", "top_raglan", "top_buttonup", "top_flannel", "top_sweatshirt", "top_ziphoodie", "top_quarterzip",
                "top_cardigan", "top_vest", "top_puffervest", "top_puffer", "top_blazer", "top_denimjacket", "top_jersey",
                "top_stripedtee", "top_blouse", "top_henley", "top_windbreaker", "top_layered"]
REVISED_BOTTOMS = ["bottom_chinos", "bottom_formal", "bottom_sportshorts", "bottom_cargoshorts", "bottom_capri",
                   "bottom_wideleg", "bottom_leggings", "bottom_denimskirt", "bottom_midiskirt", "bottom_trackpants",
                   "bottom_bermuda"]
REVISED_SHOES = ["shoes_running", "shoes_loafers", "shoes_slipons", "shoes_hiking", "shoes_chelsea", "shoes_sandals",
                 "shoes_rainboots", "shoes_skate", "shoes_maryjanes"]


def v_rib(body_type, inflate, radius, depth):
    """A V neckline: a rib around the base of the neck dipping to a point at the front."""
    c, rx, rz, n = body.torso_sections(body_type, inflate)[10]
    loop = []
    for k in range(40):
        a = 2.0 * PI * k / 40
        wrapped = abs(((a + PI) % (2.0 * PI)) - PI)
        v = max(0.0, 1.0 - wrapped / 0.85)
        loop.append((c[0] + rx * 0.92 * math.sin(a), c[1] - depth * v, c[2] + rz * math.cos(a) * (1.0 + 0.30 * v)))
    return tube(loop, [radius] * len(loop), 8, body.torso_weights(body_type), closed=True)


def placket(body_type, inflate, top_row, bottom_y, radius, weights):
    """A line down the centre front (a zip or a button placket) from a torso row to bottom_y."""
    top = front_point(body_type, top_row, inflate + 0.002, 0.0)
    path = []
    for k in range(9):
        y = top[1] + (bottom_y - top[1]) * k / 8
        path.append((0.0, y, front_z(body_type, inflate + 0.002, 0.0, y) + 0.001))
    return tube(path, [radius] * len(path), 6, weights)


def buttons_down(body_type, inflate, count, top_y, spacing, weights):
    m = Mesh()
    for k in range(count):
        m.append(_button_at(body_type, inflate, top_y - k * spacing, weights))
    return m


def revised_top(body_type, style):
    tw = hem_weights(body_type)
    parts, main, trim, accent = [], Mesh(), Mesh(), Mesh()
    texture = "knit"
    trim_colour = (0.92, 0.91, 0.88)
    collar_y = row_y(body_type, 9)

    def tee(inflate=0.017, sleeves=0.42, hem=0.006):
        main.append(shell(body_type, inflate, (1, 10), lower=0.03, flare=0.030))
        main.append(_hem(body_type, inflate, 0.030, 0.03, hem))
        for side, _ in rig.SIDES:
            (accent if style == "top_raglan" else main).append(sleeve(body_type, side, inflate - 0.001, sleeves, cuff=0.006,
                                                                     flare=0.006 if sleeves < 0.6 else 0.0))

    if style in ("top_vneck", "top_jersey", "top_stripedtee", "top_raglan"):
        tee()
        if style == "top_stripedtee":
            main.append(neck_rib(body_type, 0.012, 0.009, drop=0.004))
            texture = "stripes"
        else:
            main.append(v_rib(body_type, 0.013, 0.008, 0.055))
            texture = "jersey" if style == "top_jersey" else "knit"
    elif style == "top_layered":
        tee()
        main.append(neck_rib(body_type, 0.012, 0.009, drop=0.004))
        for side, _ in rig.SIDES:
            accent.append(sleeve(body_type, side, 0.011, 0.95, cuff=0.007, start=0.36))
    elif style in ("top_buttonup", "top_flannel", "top_henley"):
        henley = style == "top_henley"
        inflate = 0.017 if henley else 0.019
        main.append(shell(body_type, inflate, (1, 10), lower=0.045 if not henley else 0.03, flare=0.030))
        main.append(_hem(body_type, inflate, 0.030, 0.045 if not henley else 0.03, 0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, inflate - 0.001, 0.95, cuff=0.008))
        if henley:
            main.append(neck_rib(body_type, 0.012, 0.009, drop=0.004))
            trim.append(buttons_down(body_type, 0.022, 3, collar_y - 0.035, 0.028, tw))
        else:
            main.append(_collar(body_type, 0.018, spread=0.7))
            trim.append(buttons_down(body_type, 0.022, 6, collar_y - 0.045, 0.070, tw))
            c = front_point(body_type, 7, inflate, 0.065)
            main.append(patch(body_type, add(c, (0.0, -0.02, 0.0)), (0.028, 0.032), (0.1, 0.0, 1.0), tw, depth=0.004))
        texture = "check" if style == "top_flannel" else "canvas" if not henley else "knit"
    elif style == "top_sweatshirt":
        main.append(shell(body_type, 0.026, (1, 10), lower=0.035, flare=0.012))
        main.append(_hem(body_type, 0.026, 0.012, 0.035, 0.013))
        main.append(neck_rib(body_type, 0.022, 0.013, drop=0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.024, 0.93, cuff=0.012))
        texture = "fleece"
    elif style == "top_ziphoodie":
        main.append(shell(body_type, 0.028, (1, 10), lower=0.04, flare=0.010))
        main.append(_hem(body_type, 0.028, 0.010, 0.04, 0.012))
        main.append(neck_rib(body_type, 0.024, 0.011, drop=0.0))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.024, 0.93, cuff=0.011))
        main.append(_hood(body_type))
        trim.append(placket(body_type, 0.028, 10, row_y(body_type, 1) - 0.035, 0.0035, tw))
        for sx in (0.07, -0.07):
            c = front_point(body_type, 3, 0.028, sx)
            main.append(patch(body_type, add(c, (0.0, -0.02, 0.0)), (0.040, 0.050), (sx * 3.0, 0.0, 1.0), tw, depth=0.005))
        trim_colour = (0.55, 0.56, 0.58)
        texture = "fleece"
    elif style in ("top_quarterzip", "top_windbreaker", "top_puffer"):
        inflate = {"top_quarterzip": 0.018, "top_windbreaker": 0.030, "top_puffer": 0.044}[style]
        main.append(shell(body_type, inflate, (1, 10), lower=0.035 if style != "top_puffer" else 0.05, flare=0.014))
        main.append(_hem(body_type, inflate, 0.014, 0.035 if style != "top_puffer" else 0.05, 0.008 if style == "top_quarterzip" else 0.012))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, inflate - 0.002, 0.93, cuff=0.010))
        main.append(_collar_stand(body_type, inflate))
        bottom_y = collar_y - 0.12 if style == "top_quarterzip" else row_y(body_type, 1) - 0.04
        trim.append(placket(body_type, inflate, 10, bottom_y, 0.003, tw))
        trim_colour = (0.55, 0.56, 0.58)
        texture = {"top_quarterzip": "jersey", "top_windbreaker": "canvas", "top_puffer": "quilt"}[style]
    elif style in ("top_cardigan", "top_blazer", "top_denimjacket", "top_vest", "top_puffervest"):
        inner = Mesh()
        inner.append(shell(body_type, 0.017, (1, 10), lower=0.02, flare=0.026))
        inner.append(_hem(body_type, 0.017, 0.026, 0.02, 0.005))
        if style in ("top_blazer", "top_vest"):
            inner.append(_collar(body_type, 0.016, spread=0.8))
            for side, _ in rig.SIDES:
                inner.append(sleeve(body_type, side, 0.015, 0.95, cuff=0.007) if style == "top_vest" else Mesh())
        else:
            inner.append(neck_rib(body_type, 0.009, 0.008, drop=0.004))
            if style == "top_puffervest":
                for side, _ in rig.SIDES:
                    inner.append(sleeve(body_type, side, 0.015, 0.95, cuff=0.008))
        inner_texture = "canvas" if style in ("top_blazer", "top_vest") else "knit"
        inner_parts = [(inner, "accessory", inner_texture, (1.0, 1.0, 1.0))]
        if style == "top_cardigan":
            main.append(_open_shell(body_type, 0.026, (1, 10), gap=0.30, lower=0.05, flare=0.026))
            for side, _ in rig.SIDES:
                main.append(sleeve(body_type, side, 0.024, 0.94, cuff=0.011))
            for k in range(4):
                y = collar_y - 0.10 - k * 0.075
                p = front_point(body_type, 7, 0.030, 0.0)
                trim.append(ellipsoid((0.045, y, front_z(body_type, 0.030, 0.045, y) + 0.003), ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
                                      (0.006, 0.006, 0.0025), 8, 5, lambda q, v, u: tw(q)))
            texture = "cable"
        elif style == "top_blazer":
            main.append(_open_shell(body_type, 0.030, (1, 10), gap=0.62, lower=0.085, flare=0.034))
            main.append(_collar_stand(body_type, 0.030))
            for side, _ in rig.SIDES:
                main.append(sleeve(body_type, side, 0.027, 0.95, cuff=0.006))
            for sx in (0.085, -0.085):
                c = front_point(body_type, 2, 0.032, sx)
                main.append(patch(body_type, c, (0.036, 0.010), (sx * 2.5, 0.0, 1.0), tw, depth=0.004))
            for k in range(2):
                y = row_y(body_type, 3) + 0.02 - k * 0.07
                trim.append(ellipsoid((0.040, y, front_z(body_type, 0.034, 0.040, y) + 0.003), ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
                                      (0.007, 0.007, 0.003), 8, 5, lambda q, v, u: tw(q)))
            trim_colour = (0.20, 0.20, 0.22)
            texture = "plain"
        elif style == "top_denimjacket":
            main.append(_open_shell(body_type, 0.030, (1, 10), gap=0.24, lower=0.04, flare=0.022))
            main.append(_collar(body_type, 0.030, spread=0.5))
            for side, _ in rig.SIDES:
                main.append(sleeve(body_type, side, 0.027, 0.94, cuff=0.010))
            for sx in (0.075, -0.075):
                c = front_point(body_type, 7, 0.032, sx)
                main.append(patch(body_type, add(c, (0.0, -0.01, 0.0)), (0.032, 0.026), (sx * 2.0, 0.0, 1.0), tw, depth=0.005))
            trim_colour = (0.78, 0.62, 0.30)
            texture = "denim"
        else:
            # Waistcoat or puffer vest: sleeveless over the inner shirt.
            puffy = style == "top_puffervest"
            inflate = 0.040 if puffy else 0.024
            main.append(_open_shell(body_type, inflate, (1, 9), gap=0.10, lower=0.03 if puffy else 0.05, flare=0.010))
            main.append(_hem(body_type, inflate, 0.010, 0.03 if puffy else 0.05, 0.006))
            if puffy:
                main.append(_collar_stand(body_type, inflate))
                trim.append(placket(body_type, inflate, 9, row_y(body_type, 1) - 0.03, 0.003, tw))
                trim_colour = (0.55, 0.56, 0.58)
            else:
                trim.append(buttons_down(body_type, inflate + 0.006, 4, collar_y - 0.11, 0.06, tw))
                trim_colour = (0.20, 0.20, 0.22)
            texture = "quilt" if puffy else "plain"
        parts += inner_parts
    elif style == "top_blouse":
        main.append(shell(body_type, 0.018, (1, 10), lower=0.05, flare=0.060))
        main.append(_hem(body_type, 0.018, 0.060, 0.05, 0.005))
        main.append(neck_rib(body_type, 0.012, 0.007, drop=0.006, depth=0.035))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.020, 0.34, cuff=0.006, flare=0.016))
        texture = "plain"
    else:
        raise ValueError(style)
    parts.append((main, "top", texture, (1.0, 1.0, 1.0)))
    if accent.positions:
        parts.append((accent, "accessory", "knit", (1.0, 1.0, 1.0)))
    if trim.positions:
        parts.append((trim, "none", "plain", trim_colour))
    return parts


def _trousers(body_type, inflate, end, cuff, flare, texture, pockets=False, stripes=False, crease=False):
    tw = body.torso_weights(body_type)
    main, accent = Mesh(), Mesh()
    main.append(shell(body_type, inflate, (0, 3), cap_bottom=0.05))
    main.append(ring_band(body_type, 3, inflate - 0.002, 0.0055))
    pos = rig.joint_positions(body_type)
    for side, sign in rig.SIDES:
        main.append(trouser_leg(body_type, side, inflate + 0.002, end, cuff=cuff, flare=flare))
        hip, knee, ankle = pos[I["Hip" + side]], pos[I["Knee" + side]], pos[I["Ankle" + side]]
        lw = body.leg_weights(body_type, side)
        if pockets:
            c = lerp(hip, knee, 0.55)
            main.append(patch(body_type, add(c, (sign * 0.074, 0.0, 0.004)), (0.036, 0.046), (sign, 0.0, 0.25),
                              lambda p, lw=lw: lw(0.3), depth=0.014))
        if stripes or crease:
            # A side stripe (sportswear) or a pressed crease down the front of each leg.
            path, weights = [], []
            total = length(sub(knee, hip)) + length(sub(ankle, knee))
            for k in range(12):
                t = 0.02 + (end - 0.04) * k / 11
                if t * total <= length(sub(knee, hip)):
                    centre = add(hip, mul(normalize(sub(knee, hip)), t * total))
                else:
                    centre = add(knee, mul(normalize(sub(ankle, knee)), t * total - length(sub(knee, hip))))
                _, w, d = body._profile_at(body.LEG[body_type], t)
                offset = (sign * (w + inflate + 0.004), 0.0, 0.0) if stripes else (0.0, 0.0, d + inflate + 0.004)
                path.append(add(centre, offset))
                weights.append(lw(t))
            (accent if stripes else main).append(tube(path, [0.006 if stripes else 0.0018] * len(path), 6, None, row_weights=weights))
    parts = [(main, "bottom", texture, (1.0, 1.0, 1.0))]
    if accent.positions:
        parts.append((accent, "accessory", "plain", (1.0, 1.0, 1.0)))
    return parts


def _skirt(body_type, length_offset, flare, texture):
    main = Mesh()
    main.append(shell(body_type, 0.008, (0, 3), cap_bottom=0.05))
    main.append(ring_band(body_type, 3, 0.007, 0.005))
    p = rig.PROPORTIONS[body_type]
    a = body.anchors(body_type)
    waist = body.torso_sections(body_type, 0.012)[3]
    hips = body.torso_sections(body_type, 0.012)[1]
    bottom_y = p["knee"][1] + length_offset
    hip_y = p["hip"][1]
    sections = []
    for i in range(10):
        t = i / 9.0
        y = a["lower"] + 0.02 + (bottom_y - a["lower"] - 0.02) * t
        k = smoothstep(a["lower"] + 0.02, hip_y - 0.02, y)
        rx = waist[1] + (hips[1] - waist[1]) * k + flare * t * t
        rz = waist[2] + (hips[2] - waist[2]) * k + flare * 0.8 * t * t
        sections.append(((0.0, y, 0.0), rx, rz, 2.2))
    sections.reverse()

    def skirt(pt):
        k = 0.95 * smoothstep(hip_y + 0.005, hip_y - 0.06, pt[1])
        s = smoothstep(-0.07, 0.07, pt[0])
        w = {I["BackLower"]: 1.0 - k}
        if k > 0:
            w[I["HipLeft"]] = k * s
            w[I["HipRight"]] = k * (1.0 - s)
        return w
    main.append(body.section_loft(sections, 48, skirt))
    hem = body.superellipse_ring(sections[0][0], sections[0][1], sections[0][2], 2.2, 48)
    main.append(tube(hem, [0.006] * len(hem), 6, skirt, closed=True))
    return [(main, "bottom", texture, (1.0, 1.0, 1.0))]


def revised_bottom(body_type, style):
    if style == "bottom_chinos":
        return _trousers(body_type, 0.009, 0.97, 0.007, 0.0, "canvas")
    if style == "bottom_formal":
        return _trousers(body_type, 0.011, 0.98, 0.0, 0.010, "plain", crease=True)
    if style == "bottom_sportshorts":
        return _trousers(body_type, 0.012, 0.34, 0.006, 0.016, "jersey", stripes=True)
    if style == "bottom_cargoshorts":
        return _trousers(body_type, 0.011, 0.44, 0.007, 0.012, "canvas", pockets=True)
    if style == "bottom_capri":
        return _trousers(body_type, 0.009, 0.78, 0.007, 0.002, "canvas")
    if style == "bottom_wideleg":
        return _trousers(body_type, 0.014, 0.98, 0.0, 0.040, "plain")
    if style == "bottom_leggings":
        return _trousers(body_type, 0.004, 0.96, 0.0, -0.004, "knit")
    if style == "bottom_trackpants":
        return _trousers(body_type, 0.014, 0.97, 0.012, -0.004, "jersey", stripes=True)
    if style == "bottom_bermuda":
        return _trousers(body_type, 0.011, 0.52, 0.007, 0.010, "canvas")
    if style == "bottom_denimskirt":
        return _skirt(body_type, 0.11, 0.05, "denim")
    if style == "bottom_midiskirt":
        return _skirt(body_type, -0.07, 0.08, "pleat")
    raise ValueError(style)


# Footwear: (length, sole thickness, toe and heel height, toe rubber, laces, a sneaker collar,
# a shaft (top t of the leg, inflation), a sock colour, the upper's texture, the trim colour).
SHOE_SPECS = {
    "shoes_running": dict(L=0.30, sole=0.030, toe=0.046, heel=0.078, rubber=0.016, laces=4, collar=True, shaft=None,
                          sock=(0.94, 0.94, 0.92), texture="jersey", trim=(0.95, 0.95, 0.92), accent=True),
    "shoes_loafers": dict(L=0.285, sole=0.014, toe=0.044, heel=0.066, rubber=0.0, laces=0, collar=False, shaft=None,
                          sock=(0.20, 0.20, 0.22), texture="leather", trim=None),
    "shoes_slipons": dict(L=0.285, sole=0.016, toe=0.044, heel=0.064, rubber=0.008, laces=0, collar=False, shaft=None,
                          sock=None, texture="canvas", trim=(0.95, 0.95, 0.92)),
    "shoes_hiking": dict(L=0.30, sole=0.034, toe=0.050, heel=0.084, rubber=0.010, laces=5, collar=False, shaft=(0.84, 0.016),
                         sock=None, texture="leather", trim=(0.22, 0.20, 0.18)),
    "shoes_chelsea": dict(L=0.29, sole=0.020, toe=0.046, heel=0.076, rubber=0.0, laces=0, collar=False, shaft=(0.86, 0.014),
                          sock=None, texture="leather", trim=(0.16, 0.15, 0.15)),
    "shoes_rainboots": dict(L=0.30, sole=0.026, toe=0.048, heel=0.082, rubber=0.0, laces=0, collar=False, shaft=(0.62, 0.024),
                            sock=None, texture="plain", trim=None),
    "shoes_skate": dict(L=0.30, sole=0.026, toe=0.050, heel=0.080, rubber=0.016, laces=4, collar=True, shaft=None,
                        sock=(0.94, 0.94, 0.92), texture="canvas", trim=(0.95, 0.95, 0.92)),
    "shoes_maryjanes": dict(L=0.255, sole=0.012, toe=0.040, heel=0.058, rubber=0.0, laces=0, collar=False, shaft=None,
                            sock=(0.94, 0.94, 0.92), texture="leather", trim=None, strap=True),
}


def _spec_shoe(body_type, side, spec, socks, accent):
    pos = rig.joint_positions(body_type)
    ankle, toe = pos[I["Ankle" + side]], pos[I["Toe" + side]]
    sign = 1.0 if side == "Left" else -1.0
    scale = 1.0 if body_type == "male" else 0.92
    L, W, HW = spec["L"] * scale, 0.108 * scale, 0.080 * scale
    heel_z = ankle[2] - 0.065 * scale
    sole_t = spec["sole"] * scale
    weights = lambda p: blend(I["Ankle" + side], I["Toe" + side], smoothstep(toe[2] - 0.07, toe[2] + 0.01, p[2]))
    cx = ankle[0] + sign * 0.004
    n = 36
    outline = [_footprint(L, W, HW, k / n) for k in range(n)]
    trim, upper = Mesh(), Mesh()
    rows = []
    rubber = spec["rubber"] * scale
    for y, inset in ((0.0, 0.006), (0.003, 0.0), (sole_t - 0.003, 0.0), (sole_t, 0.004)):
        ring = []
        for x, z in outline:
            d = normalize((x, 0.0, (z - L * 0.5) * 0.35))
            toe_lift = rubber * smoothstep(0.80, 0.98, z / L) * (y / sole_t)
            ring.append((cx + x - d[0] * inset, y + toe_lift, heel_z + z - d[2] * inset))
        rows.append(ring)
    rows.insert(0, [(cx, 0.0, heel_z + L * 0.5)] * n)
    rows.append([(cx, sole_t, heel_z + L * 0.5)] * n)
    sole = Mesh().grid(rows, lambda p, v, u: weights(p))
    (trim if spec["trim"] else upper).append(sole)
    cz = heel_z + L * 0.46
    toe_h, heel_h = spec["toe"] * scale, spec["heel"] * scale

    def height(z):
        return toe_h + (heel_h - toe_h) * smoothstep(0.78, 0.32, (z - heel_z) / L)
    rows = []
    for i in range(9):
        a = 0.5 * PI * i / 8
        r = math.cos(a)
        ring = []
        for x, z in outline:
            px, pz = cx + x * r, cz + (heel_z + z - cz) * r
            ring.append((px, sole_t - 0.004 + (height(pz) + 0.004) * math.sin(a) ** 0.8, pz))
        rows.append(ring)
    upper.append(Mesh().grid(rows, lambda p, v, u: weights(p)))
    collar_y = sole_t + heel_h - 0.004 * scale
    shin_r = body.LEG[body_type][-1][1]
    if spec["shaft"]:
        top_t, inflate = spec["shaft"]
        upper.append(body.leg(body_type, side, inflate=inflate, t_range=(top_t, 1.0)))
        upper.append(_limb_ring(body_type, side, "leg", top_t, inflate, 0.006 * scale))
    elif spec["sock"]:
        socks.append(body.leg(body_type, side, inflate=0.005, t_range=(0.88, 1.0)))
    if spec["collar"]:
        collar = [(ankle[0] + (shin_r + 0.009) * math.sin(2 * PI * k / 24), collar_y,
                   ankle[2] - 0.004 + (shin_r + 0.009) * 1.05 * math.cos(2 * PI * k / 24)) for k in range(24)]
        trim.append(tube(collar, [0.0055 * scale] * 24, 8, weights, closed=True))
    for k in range(spec["laces"]):
        z = heel_z + L * (0.66 - 0.075 * k)
        yy = sole_t + height(z) + 0.002
        trim.append(tube([(cx - 0.022, yy - 0.004, z), (cx, yy + 0.002, z + 0.004), (cx + 0.022, yy - 0.004, z)],
                         [0.0035] * 3, 6, weights, round_end=True))
    if spec.get("accent"):
        # A swoosh-free accent band along the side of the upper.
        for flank in (1.0, -1.0):
            path = []
            for k in range(8):
                z = heel_z + L * (0.18 + 0.55 * k / 7)
                x, _ = _footprint(L, W, HW, 0.25 if flank > 0 else 0.75)
                path.append((cx + flank * (abs(x) * 0.96 + 0.002), sole_t + height(z) * (0.35 + 0.25 * k / 7), z))
            accent.append(tube(path, [0.006 * scale] * len(path), 6, weights, round_end=True))
    if spec.get("strap"):
        z = heel_z + L * 0.42
        strap = [(cx + (0.040 * scale) * math.cos(PI * k / 10), sole_t + height(z) * 0.95 * math.sin(PI * k / 10) + 0.004, z)
                 for k in range(11)]
        upper.append(tube(strap, [0.004 * scale] * 11, 6, weights))
    return upper, trim


def _sandal(body_type, side):
    pos = rig.joint_positions(body_type)
    ankle, toe = pos[I["Ankle" + side]], pos[I["Toe" + side]]
    sign = 1.0 if side == "Left" else -1.0
    scale = 1.0 if body_type == "male" else 0.92
    L, W, HW = 0.27 * scale, 0.100 * scale, 0.076 * scale
    heel_z = ankle[2] - 0.060 * scale
    sole_t = 0.020 * scale
    weights = lambda p: blend(I["Ankle" + side], I["Toe" + side], smoothstep(toe[2] - 0.07, toe[2] + 0.01, p[2]))
    cx = ankle[0] + sign * 0.004
    n = 32
    outline = [_footprint(L, W, HW, k / n) for k in range(n)]
    rows = [[(cx, 0.0, heel_z + L * 0.5)] * n]
    for y, inset in ((0.0, 0.004), (sole_t, 0.0)):
        rows.append([(cx + x, y, heel_z + z) for x, z in outline])
    rows.append([(cx, sole_t, heel_z + L * 0.5)] * n)
    sole = Mesh().grid(rows, lambda p, v, u: weights(p))
    straps = Mesh()
    foot_r = body.FOOT[body_type]
    for frac, lift in ((0.70, 0.026), (0.40, 0.040)):
        z = heel_z + L * frac
        straps.append(tube([(cx + (foot_r[0] + 0.004) * math.cos(PI * k / 10), sole_t + lift * scale * math.sin(PI * k / 10), z)
                            for k in range(11)], [0.006 * scale] * 11, 6, weights))
    back = [(cx + (foot_r[0] + 0.004) * math.cos(PI * k / 12), 0.050 * scale, ankle[2] - 0.035 * scale * math.sin(PI * k / 12))
            for k in range(13)]
    straps.append(tube(back, [0.005 * scale] * 13, 6, weights))
    return sole, straps


def revised_shoes(body_type, style):
    if style == "shoes_sandals":
        soles, straps = Mesh(), Mesh()
        for side, _ in rig.SIDES:
            sole, strap = _sandal(body_type, side)
            soles.append(sole)
            straps.append(strap)
        return [(straps, "shoes", "leather", (1.0, 1.0, 1.0)), (soles, "none", "leather", (0.42, 0.30, 0.20))]
    spec = SHOE_SPECS[style]
    upper, trim, socks, accent = Mesh(), Mesh(), Mesh(), Mesh()
    for side, _ in rig.SIDES:
        u, t = _spec_shoe(body_type, side, spec, socks, accent)
        upper.append(u)
        trim.append(t)
    parts = [(upper, "shoes", spec["texture"], (1.0, 1.0, 1.0))]
    if trim.positions:
        parts.append((trim, "none", "plain", spec["trim"] or (0.18, 0.17, 0.16)))
    if accent.positions:
        parts.append((accent, "accessory", "plain", (1.0, 1.0, 1.0)))
    if socks.positions:
        parts.append((socks, "none", "rib", spec["sock"]))
    return parts


# ----- wristwear, gloves and rings (catalog v3 revision 2, format 3) -----------------------------

WRISTWEAR = ["wrist_watch", "wrist_sportwatch", "wrist_bangle", "wrist_beads", "wrist_band", "wrist_cuff"]
GLOVES = ["gloves_knit", "gloves_fingerless", "gloves_work", "gloves_formal"]
RINGS = ["ring_band", "ring_signet", "ring_gem"]
SIDE_NAMES = {"Left": "left", "Right": "right"}


def _wrist_frame(body_type, side, t):
    """(centre, along the forearm, outward (back of the wrist), half width, half depth, weights) at t
    along the arm."""
    pos = rig.joint_positions(body_type)
    sh, el, wr = (pos[I[n + side]] for n in ("Shoulder", "Elbow", "Wrist"))
    p = rig.PROPORTIONS[body_type]
    upper, lower = length(sub(el, sh)), length(sub(wr, el))
    axis = normalize(sub(wr, el))
    centre = add(el, mul(axis, (t - upper / (upper + lower)) * (upper + lower)))
    _, w, d = body._profile_at(body.ARM[body_type], t)
    along, normal, spread = rig.hand_frame(1.0 if side == "Left" else -1.0, p)
    return centre, axis, mul(normal, -1.0), w, d, body.arm_weights(body_type, side)(t)


def wristwear(body_type, style):
    parts = {}

    def add_part(key, mesh, tint, kind, colour, side):
        entry = parts.setdefault((key, side), (Mesh(), tint, kind, colour, {"cnaSide": SIDE_NAMES[side]}))
        entry[0].append(mesh)
    for side, _ in rig.SIDES:
        # Just past every sleeve's cuff (long sleeves end by t = 0.95), so long sleeves never hide it.
        t = 0.978
        centre, axis, back, w, d, wt = _wrist_frame(body_type, side, t)
        u, v, _ = frame_from_axis(axis, back)
        weights = lambda q, wt=wt: wt

        def band(inflate, radius, segments=28, wobble=0.0):
            loop = [add(centre, add(mul(u, (w + inflate) * math.cos(2 * PI * k / segments)),
                                    mul(v, (d + inflate) * math.sin(2 * PI * k / segments)))) for k in range(segments)]
            return tube(loop, [radius] * segments, 8, weights, closed=True)
        if style in ("wrist_watch", "wrist_sportwatch"):
            sport = style == "wrist_sportwatch"
            add_part("strap", band(0.004, 0.0055 if sport else 0.0042), "accessory" if sport else "none", "leather",
                     (1.0, 1.0, 1.0) if sport else (0.24, 0.16, 0.10), side)
            face_centre = add(centre, mul(back, d + 0.008))
            r = 0.0115 if sport else 0.0095
            add_part("case", ellipsoid(face_centre, (u, axis, back) if abs(dot(u, back)) < 0.5 else (v, axis, back),
                                       (r, r, 0.0045), 16, 8, lambda q, v_, u_, wt=wt: wt), "jewelry", "plain", (1.0, 1.0, 1.0), side)
            dial = add(face_centre, mul(back, 0.0038))
            add_part("dial", ellipsoid(dial, (v if abs(dot(u, back)) > 0.5 else u, axis, back), (r * 0.78, r * 0.78, 0.0012), 16, 6,
                                       lambda q, v_, u_, wt=wt: wt), "none", "plain", (0.10, 0.10, 0.12) if sport else (0.95, 0.94, 0.90), side)
        elif style == "wrist_bangle":
            add_part("metal", band(0.006, 0.0032), "jewelry", "plain", (1.0, 1.0, 1.0), side)
            add_part("metal", band(0.009, 0.0026), "jewelry", "plain", (1.0, 1.0, 1.0), side)
        elif style == "wrist_beads":
            beads = Mesh()
            for k in range(16):
                a = 2 * PI * k / 16
                q = add(centre, add(mul(u, (w + 0.006) * math.cos(a)), mul(v, (d + 0.006) * math.sin(a))))
                beads.append(ellipsoid(q, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.0042, 0.0042, 0.0042), 8, 6,
                                       lambda q_, v_, u_, wt=wt: wt))
            add_part("beads", beads, "accessory", "plain", (1.0, 1.0, 1.0), side)
        elif style == "wrist_band":
            add_part("band", body.arm(body_type, side, inflate=0.006, t_range=(0.955, 0.995)), "accessory", "rib", (1.0, 1.0, 1.0), side)
            add_part("band", _limb_ring(body_type, side, "arm", 0.955, 0.006, 0.004), "accessory", "rib", (1.0, 1.0, 1.0), side)
            add_part("band", _limb_ring(body_type, side, "arm", 0.995, 0.006, 0.004), "accessory", "rib", (1.0, 1.0, 1.0), side)
        elif style == "wrist_cuff":
            add_part("metal", body.arm(body_type, side, inflate=0.005, t_range=(0.955, 0.995)), "jewelry", "plain", (1.0, 1.0, 1.0), side)
            add_part("metal", _limb_ring(body_type, side, "arm", 0.955, 0.005, 0.0022), "jewelry", "plain", (1.0, 1.0, 1.0), side)
            add_part("metal", _limb_ring(body_type, side, "arm", 0.995, 0.005, 0.0022), "jewelry", "plain", (1.0, 1.0, 1.0), side)
        else:
            raise ValueError(style)
    return list(parts.values())


def gloves(body_type, style):
    main, trim = Mesh(), Mesh()
    texture = {"gloves_knit": "rib", "gloves_fingerless": "knit", "gloves_work": "leather", "gloves_formal": "plain"}[style]
    for side, sign in rig.SIDES:
        inflate = {"gloves_knit": 0.0030, "gloves_fingerless": 0.0024, "gloves_work": 0.0034, "gloves_formal": 0.0016}[style]
        segments = 1 if style == "gloves_fingerless" else 3
        main.append(body.hand(body_type, side, sign, inflate=inflate, finger_segments=segments, wrist_start=0.030))
        cuff_t = 0.90 if style == "gloves_formal" else 0.955
        main.append(body.arm(body_type, side, inflate=inflate + 0.001, t_range=(cuff_t, 1.0)))
        trim.append(_limb_ring(body_type, side, "arm", cuff_t, inflate + 0.002, 0.004 if style != "gloves_work" else 0.006))
    parts = [(main, "gloves", texture, (1.0, 1.0, 1.0))]
    parts.append((trim, "gloves" if style != "gloves_work" else "none", "rib" if style != "gloves_work" else "plain",
                  (1.0, 1.0, 1.0) if style != "gloves_work" else (0.22, 0.20, 0.18)))
    return parts


def rings(body_type, style):
    parts = {}
    pos = rig.joint_positions(body_type)
    p = rig.PROPORTIONS[body_type]
    k = p["hand"]
    for side, sign in rig.SIDES:
        base, nxt = pos[I["FingerRing" + side]], pos[I["FingerRing2" + side]]
        axis = normalize(sub(nxt, base))
        centre = lerp(base, nxt, 0.45)
        u, v, _ = frame_from_axis(axis, (0.0, 0.0, 1.0))
        radius = body.HAND[body_type]["finger"] * k * 0.98 * 0.98 + 0.0010
        joint = I["FingerRing" + side]
        weights = lambda q, joint=joint: {joint: 1.0}
        loop = [add(centre, add(mul(u, radius * math.cos(2 * PI * j / 20)), mul(v, radius * math.sin(2 * PI * j / 20)))) for j in range(20)]
        m = tube(loop, [0.0016] * 20, 6, weights, closed=True)
        _, normal, spread = rig.hand_frame(sign, p)
        back = mul(normal, -1.0)
        if style == "ring_signet":
            m.append(ellipsoid(add(centre, mul(back, radius + 0.0014)), (spread, axis, back), (0.0042, 0.0042, 0.0016), 10, 6,
                               lambda q, v_, u_, joint=joint: {joint: 1.0}))
        gem = None
        if style == "ring_gem":
            gem = ellipsoid(add(centre, mul(back, radius + 0.0026)), (spread, axis, back), (0.0030, 0.0030, 0.0026), 10, 6,
                            lambda q, v_, u_, joint=joint: {joint: 1.0})
        name = SIDE_NAMES[side]
        parts.setdefault(("band", side), (Mesh(), "jewelry", "plain", (1.0, 1.0, 1.0), {"cnaSide": name}))[0].append(m)
        if gem:
            parts.setdefault(("gem", side), (Mesh(), "accessory", "plain", (1.0, 1.0, 1.0), {"cnaSide": name}))[0].append(gem)
    return list(parts.values())
