# SPDX-License-Identifier: MS-PL
"""Catalog v3 revision 3 footwear: lasted shoes rather than domes over the foot.

An upper is lofted along the foot from the heel to the toe through rounded-rectangle sections --
flat over the instep, upright at the sides, a cup round the heel that holds the ankle, a toe box
rounding down to the sole -- and stands on a sole slab with a lip all round, toe spring and, for
dress shoes, a heel block. The details that make a shoe read are geometry: the welt seam where the
upper meets the sole, toe caps, a tongue with crossing laces, a loafer's apron and strap, a heel
tab, a padded collar round the opening, a rubber toe on canvas shoes, elastic gussets and shafts
for boots. Every low shoe has a sock, so no skin shows between it and a trouser hem.

Builders return (Mesh, tint, texture kind, colour) parts like the rest of the wardrobe.
"""
import math

from . import rig, body
from .mathutil import add, sub, mul, normalize, cross, dot, length, lerp, smoothstep, clamp
from .mesh import Mesh, tube, blend

I = rig.INDEX
PI = math.pi

# Every style: L length, sole/heel thickness, toe and collar heights, toe spring, the shaft (top t of
# the leg, inflation) of boots, sock colour (None: no sock), upper texture, sole colour, details.
STYLES = {
    "shoes_sneakers": dict(L=0.290, sole=0.022, heel_block=0.0, toe=0.046, collar=0.070, spring=0.012, shaft=None,
                           sock=(0.94, 0.94, 0.92), texture="canvas", sole_colour=(0.95, 0.95, 0.92), laces=5,
                           rubber_toe=True, tab=True, welt=False, padded=True),
    "shoes_boots": dict(L=0.295, sole=0.026, heel_block=0.006, toe=0.050, collar=0.080, spring=0.008, shaft=(0.80, 0.009),
                        sock=None, texture="leather", sole_colour=(0.18, 0.16, 0.15), laces=0, welt=True, toe_cap=True),
    "shoes_hightops": dict(L=0.295, sole=0.024, heel_block=0.0, toe=0.048, collar=0.080, spring=0.012, shaft=(0.86, 0.008),
                           sock=None, texture="canvas", sole_colour=(0.95, 0.95, 0.92), laces=6, rubber_toe=True, tab=True,
                           padded=True),
    "shoes_dress": dict(L=0.285, sole=0.011, heel_block=0.016, toe=0.038, collar=0.060, spring=0.010, shaft=None,
                        sock=(0.20, 0.20, 0.22), texture="leather", sole_colour=(0.16, 0.14, 0.13), laces=3, welt=True,
                        toe_cap=True, pointed=0.25),
    "shoes_flats": dict(L=0.255, sole=0.008, heel_block=0.004, toe=0.036, collar=0.040, spring=0.006, shaft=None,
                        sock=None, texture="leather", sole_colour=(0.30, 0.24, 0.20), laces=0, low_vamp=True, bow=True,
                        pointed=0.15),
    "shoes_running": dict(L=0.300, sole=0.030, heel_block=0.006, toe=0.046, collar=0.072, spring=0.016, shaft=None,
                          sock=(0.94, 0.94, 0.92), texture="jersey", sole_colour=(0.95, 0.95, 0.92), laces=5, tab=True,
                          padded=True, accent=True),
    "shoes_loafers": dict(L=0.285, sole=0.012, heel_block=0.010, toe=0.040, collar=0.052, spring=0.010, shaft=None,
                          sock=(0.20, 0.20, 0.22), texture="leather", sole_colour=(0.20, 0.16, 0.13), laces=0, welt=True,
                          apron=True, strap=True),
    "shoes_slipons": dict(L=0.285, sole=0.016, heel_block=0.0, toe=0.044, collar=0.058, spring=0.010, shaft=None,
                          sock=(0.94, 0.94, 0.92), texture="canvas", sole_colour=(0.95, 0.95, 0.92), laces=0,
                          rubber_toe=True, apron=True, gusset=True),
    "shoes_hiking": dict(L=0.300, sole=0.032, heel_block=0.006, toe=0.050, collar=0.084, spring=0.010, shaft=(0.84, 0.009),
                         sock=None, texture="leather", sole_colour=(0.22, 0.20, 0.18), laces=7, welt=True, toe_cap=True,
                         padded=True),
    "shoes_chelsea": dict(L=0.290, sole=0.018, heel_block=0.008, toe=0.046, collar=0.080, spring=0.008, shaft=(0.86, 0.008),
                          sock=None, texture="leather", sole_colour=(0.16, 0.15, 0.15), laces=0, welt=True, gusset=True,
                          tab=True),
    "shoes_rainboots": dict(L=0.300, sole=0.024, heel_block=0.004, toe=0.050, collar=0.082, spring=0.006, shaft=(0.62, 0.010),
                            sock=None, texture="plain", sole_colour=None, laces=0),
    "shoes_skate": dict(L=0.300, sole=0.024, heel_block=0.0, toe=0.050, collar=0.074, spring=0.012, shaft=None,
                        sock=(0.94, 0.94, 0.92), texture="canvas", sole_colour=(0.95, 0.95, 0.92), laces=5, rubber_toe=True,
                        tab=True, padded=True, accent=True, chunky=True),
    "shoes_maryjanes": dict(L=0.255, sole=0.012, heel_block=0.008, toe=0.040, collar=0.042, spring=0.006, shaft=None,
                            sock=(0.94, 0.94, 0.92), texture="leather", sole_colour=(0.18, 0.15, 0.14), laces=0,
                            low_vamp=True, bar=True),
}


def _footprint(length_, width, heel_width, u):
    """Half-width of the sole outline at u along the foot (0 heel, 1 toe tip)."""
    taper_heel = math.sqrt(max(0.0, 1.0 - ((0.12 - min(u, 0.12)) / 0.12) ** 2))
    taper_toe = math.sqrt(max(0.0, 1.0 - (max(u - 0.82, 0.0) / 0.18) ** 2))
    widen = heel_width + (width - heel_width) * smoothstep(0.10, 0.68, u)
    return 0.5 * widen * taper_heel * taper_toe


class Last:
    """The shoe's shape for one foot: where its sections are and how big."""

    def __init__(self, body_type, side, spec):
        pos = rig.joint_positions(body_type)
        self.ankle, self.toe = pos[I["Ankle" + side]], pos[I["Toe" + side]]
        self.sign = 1.0 if side == "Left" else -1.0
        self.scale = 1.0 if body_type == "male" else 0.92
        sc = self.scale
        self.spec = spec
        self.L = spec["L"] * sc
        self.W = (0.112 if spec.get("chunky") else 0.106) * sc
        self.HW = 0.080 * sc
        self.heel_z = self.ankle[2] - 0.066 * sc
        self.cx = self.ankle[0] + self.sign * 0.004
        self.leg_r = body.LEG[body_type][-1][1]
        rx, ry, rz = body.FOOT[body_type]
        self.foot = ((self.ankle[0], ry + 0.013, self.ankle[2] + 0.048), (rx, ry, rz))
        self.side = side
        self.weights = lambda p: blend(I["Ankle" + side], I["Toe" + side], smoothstep(self.toe[2] - 0.07, self.toe[2] + 0.01, p[2]))

    def z(self, u):
        return self.heel_z + self.L * u

    def spring(self, u):
        """How far the sole curves up under the toe."""
        return self.spec["spring"] * self.scale * smoothstep(0.70, 1.0, u) ** 1.6

    def sole_top(self, u):
        s = self.spec
        heel = s.get("heel_block", 0.0) * self.scale * (1.0 - smoothstep(0.22, 0.34, u))
        return s["sole"] * self.scale + heel + self.spring(u)

    def half_width(self, u, y=0.0):
        """The upper's half-width at u: the sole outline, and round the heel wide enough to hold the
        ankle and a sock."""
        w = _footprint(self.L, self.W, self.HW, u) * 0.96
        (fx, fy, fz), (frx, fry, frz) = self.foot
        q = (self.z(u) - fz) / frz
        if abs(q) < 1.0:
            w = max(w, frx * math.sqrt(1.0 - q * q) + 0.004)
        dz = self.z(u) - self.ankle[2]
        wrap = self.leg_r + 0.007
        if abs(dz) < wrap:
            w = max(w, math.sqrt(wrap * wrap - dz * dz) * smoothstep(0.0, 0.06, u))
        return w

    def foot_top(self, u):
        """The top of the foot inside the shoe at u (0 where the foot is not)."""
        (cx, cy, cz), (rx, ry, rz) = self.foot
        q = (self.z(u) - cz) / rz
        return cy + ry * math.sqrt(1.0 - q * q) if abs(q) < 1.0 else 0.0

    def height(self, u):
        """The upper's top: the style's line, but always over the foot (an opening in a low shoe
        shows the foot instead, see opening())."""
        return max(self.styled_height(u), self.foot_top(u) + 0.004)

    def opening(self, u, y):
        """Whether a point of the upper at u, height y lies in a low shoe's opening (above the
        style's line, behind the vamp), where the foot shows."""
        if not self.spec.get("low_vamp"):
            return False
        return u < 0.60 and y > self.styled_height(u) + 0.0015

    def styled_height(self, u):
        """The style's line: the collar round the heel, the throat sloping down over the instep,
        the toe box, rounding down to the sole at the tip."""
        s, sc = self.spec, self.scale
        sole = s["sole"] * sc
        collar, toe = s["collar"] * sc - sole, s["toe"] * sc - sole
        instep = toe * 1.02 if s.get("low_vamp") else toe + (collar - toe) * 0.55
        h = collar + (instep - collar) * smoothstep(0.30, 0.48, u)
        h = h + (toe - h) * smoothstep(0.55, 0.80, u)
        tip = math.sqrt(max(0.0, 1.0 - (max(u - 0.80, 0.0) / 0.20) ** 2))
        return self.sole_top(u) + h * (0.30 + 0.70 * tip)

    def section(self, u, squash=1.0, grow=0.0, segments=20):
        """A rounded-rectangle section of the upper at u, from the sole edge on one side over the top
        to the other, bottom at the sole's top."""
        z = self.z(u)
        base = self.sole_top(u) - 0.003
        top = self.height(u) + grow
        w = self.half_width(u) + grow
        h = max(top - base, 0.004)
        pts = []
        n = 3.2 if not self.spec.get("pointed") else 3.2 - 1.0 * self.spec["pointed"] * smoothstep(0.75, 1.0, u)
        for k in range(segments + 1):
            a = PI * k / segments                    # 0: outer sole edge, pi/2: top, pi: inner sole edge
            ca, sa = math.cos(a), math.sin(a)
            x = w * math.copysign(abs(ca) ** (2.0 / n), ca)
            y = h * squash * abs(sa) ** (2.0 / n)
            pts.append((self.cx + x, base + y, z))
        return pts


def _split(mesh, inside):
    """(the mesh without, the mesh with) the triangles all of whose corners are `inside`."""
    a, b = Mesh(), Mesh()
    for target, want in ((a, False), (b, True)):
        tris = [mesh.indices[t:t + 3] for t in range(0, len(mesh.indices), 3)]
        target.positions, target.normals, target.uvs, target.skin = mesh.positions, mesh.normals, mesh.uvs, mesh.skin
        target.indices = [i for tri in tris if all(inside[j] for j in tri) == want for i in tri]
        target.positions, target.normals, target.uvs, target.skin = list(mesh.positions), list(mesh.normals), list(mesh.uvs), list(mesh.skin)
        target.compact()
    return a, b


def _upper(last, grow=0.0, rows=20, segments=16):
    """The upper, lofted from the heel to the toe, closed at both ends."""
    us = [0.02 + 0.96 * (0.5 - 0.5 * math.cos(PI * i / rows)) for i in range(rows + 1)]
    sections = [last.section(u, grow=grow, segments=segments) for u in us]
    grid = []
    for sec in sections:
        bottom = (sec[0][0] * 0.5 + sec[-1][0] * 0.5, sec[0][1], sec[0][2])
        # Each section is closed under the foot so the loft is a solid (its underside is in the sole).
        ring = sec + [lerp(sec[-1], bottom, 0.5), bottom, lerp(bottom, sec[0], 0.5)]
        grid.append(ring)
    # Rounded ends: the heel and the toe tip.
    def cap(ring, toward, depth):
        centre = mul(ring[0], 0.0)
        for p in ring:
            centre = add(centre, p)
        centre = mul(centre, 1.0 / len(ring))
        out = []
        for j in (1, 2, 3):
            a = PI * 0.5 * j / 3
            out.append([add(add(centre, mul(sub(p, centre), math.cos(a) + 1e-4)), (0.0, 0.0, toward * depth * math.sin(a))) for p in ring])
        return out
    grid = list(reversed(cap(grid[0], -1.0, 0.012 * last.scale))) + grid + cap(grid[-1], 1.0, 0.010 * last.scale)
    m = Mesh().grid(grid, lambda p, v, u: last.weights(p))
    if not last.spec.get("low_vamp"):
        return m, Mesh()
    inside = [last.opening((p[2] - last.heel_z) / last.L, p[1]) for p in m.positions]
    return _split(m, inside)


def _sole(last, lip=0.003, segments=40):
    """The sole: the footprint a little larger than the upper, its sides straight, toe springing
    up, a heel block under dress shoes."""
    s = last.spec
    rows = []
    outline = []
    for k in range(segments):
        a = 2.0 * PI * k / segments
        # Round the outline: a from the heel's back centre round the outer side to the toe and back.
        u = 0.5 - 0.5 * math.cos(a)
        side = 1.0 if math.sin(a) >= 0.0 else -1.0
        outline.append((u, side))
    def ring(y_of, grow):
        pts = []
        for u, side in outline:
            w = _footprint(last.L, last.W, last.HW, u) + grow
            pts.append((last.cx + side * w, y_of(u), last.z(u) + (grow if u > 0.98 else -grow if u < 0.02 else 0.0)))
        return pts
    bottom = lambda u: last.spring(u) + (0.0 if s.get("heel_block", 0.0) == 0.0 else 0.0)
    top = lambda u: last.sole_top(u)
    rows.append(ring(lambda u: bottom(u) + 0.0005, lip - 0.003))
    rows.append(ring(lambda u: bottom(u) + 0.002, lip))
    rows.append(ring(lambda u: top(u) - 0.002, lip))
    rows.append(ring(lambda u: top(u), lip - 0.002))
    centre_bottom = (last.cx, 0.0005, last.z(0.5))
    centre_top = (last.cx, top(0.5) - 0.004, last.z(0.5))
    rows.insert(0, [centre_bottom] * segments)
    rows.append([centre_top] * segments)
    return Mesh().grid(rows, lambda p, v, u: last.weights(p))


def _curve_on_upper(last, points, lift=0.0012, radius=0.0014, grow=0.0):
    """A seam or a strap along the upper: points are (u, a) with a the angle round the section
    (0 outer sole edge, pi/2 the top, pi the inner edge); lifted a little off the surface."""
    path = []
    for u, a in points:
        sec = last.section(u, grow=grow + lift, segments=64)
        k = int(round(a / PI * 64))
        path.append(sec[max(0, min(64, k))])
    return tube(path, [radius] * len(path), 6, last.weights)


def _crossing(last, u, y, side):
    """Where the upper's section at u crosses height y on one side (x), or None if it never
    reaches it."""
    sec = last.section(u, segments=64)
    half = sec[:33] if side > 0 else list(reversed(sec[32:]))
    for p0, p1 in zip(half, half[1:]):
        if (p0[1] - y) * (p1[1] - y) <= 0.0 and p0[1] != p1[1]:
            f = (y - p0[1]) / (p1[1] - p0[1])
            return p0[0] + (p1[0] - p0[0]) * f
    return None


def _collar_rim(last, radius=0.005):
    """The padded collar round the shoe's opening: round the heel cup and to the throat; for a low
    shoe, round the opening where the foot shows and over the vamp in front of it."""
    s = last.spec
    low = s.get("low_vamp")
    throat_u = 0.40 if not low else 0.60
    pts = []
    if low:
        us = [0.03 + (throat_u - 0.03) * (0.5 - 0.5 * math.cos(PI * k / 14)) for k in range(15)]
        left, right = [], []
        for u in us:
            y = last.styled_height(u)
            for side, out in ((1.0, left), (-1.0, right)):
                top = last.height(u)
                if top - y < 0.003:
                    # No opening here (round the heel): the cup's own top edge.
                    x = last.cx + side * last.half_width(u) * 0.55
                    y = top - 0.002
                else:
                    x = _crossing(last, u, y, side)
                    x = last.cx + side * last.half_width(u) * 0.8 if x is None else x
                out.append((x, y + 0.001, last.z(u)))
        # Over the vamp at the throat, following the section's top.
        sec = last.section(throat_u, segments=32)
        y_t = last.styled_height(throat_u)
        over = [p for p in sec if p[1] >= y_t]
        front = [(p[0], p[1] + 0.0015, p[2] + 0.002) for p in over]
        if front and front[0][0] < front[-1][0]:
            front.reverse()
        pts = left + front[1:-1] + list(reversed(right))
    else:
        for k in range(32):
            a = 2.0 * PI * k / 32
            u = 0.03 + (throat_u - 0.03) * (0.5 - 0.5 * math.cos(a))
            w = last.half_width(u) + 0.001
            y = last.height(u) + 0.001
            pts.append((last.cx + math.sin(a) * w * (1.0 - 0.15 * smoothstep(0.25, throat_u, u)), y, last.z(u)))
    return tube(pts, [radius * last.scale] * len(pts), 8, last.weights, closed=True)


def _laces(last, count, lace_colour_mesh):
    """A tongue up the throat with laces crossing it between two rows of eyelets."""
    s = last.spec
    tongue = Mesh()
    u0, u1 = 0.36, 0.66
    path = []
    for k in range(8):
        u = u1 + (u0 - u1) * k / 7
        path.append((last.cx, last.height(u) + 0.004 + 0.006 * (k / 7) ** 2, last.z(u)))
    tongue.append(tube(path, [0.010 * last.scale] * len(path), 8, last.weights, round_end=True))
    tongue.positions = [(p[0], p[1] - 0.004 * max(0.0, 1.0 - abs(p[0] - last.cx) / (0.012 * last.scale)), p[2])
                        if p[1] < last.height(0.5) else p for p in tongue.positions]
    laces = Mesh()
    for k in range(count):
        u = u1 - (u1 - u0) * (k + 0.5) / count
        y = last.height(u) + 0.004
        z = last.z(u)
        half = 0.020 * last.scale
        a, b = (last.cx - half, y - 0.002, z), (last.cx + half, y - 0.002, z + 0.006)
        laces.append(tube([a, (last.cx, y + 0.0015, z + 0.003), b], [0.0026 * last.scale] * 3, 6, last.weights, round_end=True))
    if count:
        # The bow on top.
        u = u0 + 0.02
        top = (last.cx, last.height(u) + 0.008, last.z(u))
        for side in (1.0, -1.0):
            laces.append(tube([top, add(top, (side * 0.012, 0.004, -0.006)), add(top, (side * 0.018, -0.002, -0.002))],
                              [0.0025 * last.scale] * 3, 6, last.weights, round_end=True))
    return tongue, laces


def build(body_type, style):
    """The parts of a pair of shoes in a style."""
    if style == "shoes_sandals":
        return sandals(body_type)
    spec = STYLES[style]
    upper, sole, trim, accent, socks, upper_trim, foot = Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    for side, _ in rig.SIDES:
        last = Last(body_type, side, spec)
        shoe, bare = _upper(last)
        upper.append(shoe)
        foot.append(bare)
        if spec["sole_colour"] is not None:
            sole.append(_sole(last))
        else:
            upper.append(_sole(last))
        if spec.get("welt"):
            upper_trim.append(_curve_on_upper(last, [(u, 0.10) for u in (0.04, 0.2, 0.4, 0.6, 0.8, 0.92)] +
                                              [(0.97, 0.5 * PI)] + [(u, PI - 0.10) for u in (0.92, 0.8, 0.6, 0.4, 0.2, 0.04)],
                                              radius=0.0016))
        if spec.get("toe_cap"):
            upper_trim.append(_curve_on_upper(last, [(0.80, 0.12), (0.78, 0.30 * PI), (0.77, 0.5 * PI), (0.78, 0.70 * PI),
                                                     (0.80, PI - 0.12)], radius=0.0013))
        if spec.get("rubber_toe"):
            cap = Mesh()
            us = [0.84 + 0.15 * i / 6 for i in range(7)]
            grid = []
            for u in us:
                sec = last.section(u, grow=0.0015, segments=20)
                h_cut = last.sole_top(u) + 0.022 * last.scale
                grid.append([(p[0], min(p[1], h_cut), p[2]) for p in sec])
            cap.patch_origin = (last.cx, last.sole_top(0.9) + 0.012 * last.scale, last.z(0.86))
            cap.grid(grid, lambda p, v, uu: last.weights(p), wrap=False)
            trim.append(cap)
        if spec.get("apron"):
            upper_trim.append(_curve_on_upper(last, [(0.50, 0.30 * PI), (0.62, 0.32 * PI), (0.74, 0.38 * PI), (0.80, 0.5 * PI),
                                                     (0.74, 0.62 * PI), (0.62, 0.68 * PI), (0.50, 0.70 * PI)], radius=0.0016))
        if spec.get("strap"):
            upper_trim.append(_curve_on_upper(last, [(0.47, 0.15 * PI), (0.46, 0.35 * PI), (0.46, 0.5 * PI), (0.46, 0.65 * PI),
                                                     (0.47, 0.85 * PI)], radius=0.0035, lift=0.002))
        if spec.get("bar"):
            trim.append(_curve_on_upper(last, [(0.40, 0.08 * PI), (0.36, 0.30 * PI), (0.34, 0.5 * PI), (0.36, 0.70 * PI),
                                               (0.40, 0.92 * PI)], radius=0.0040, lift=0.004))
        if spec.get("bow"):
            u = 0.60
            top = (last.cx, last.height(u) + 0.003, last.z(u))
            for sgn in (1.0, -1.0):
                upper_trim.append(tube([top, add(top, (sgn * 0.010, 0.002, 0.002)), add(top, (sgn * 0.014, -0.001, -0.003))],
                                       [0.0035 * last.scale, 0.004 * last.scale, 0.002 * last.scale], 6, last.weights,
                                       round_end=True))
        if spec.get("padded") or spec.get("low_vamp") or not spec["shaft"]:
            upper_trim.append(_collar_rim(last, 0.0045 if spec.get("padded") else 0.0028))
        if spec.get("tab"):
            u = 0.02
            p0 = (last.cx, last.height(0.05) - 0.002, last.z(u) - 0.004)
            trim.append(tube([add(p0, (0.0, -0.012, 0.0)), add(p0, (0.0, 0.006, -0.004)), add(p0, (0.0, 0.012, 0.0))],
                             [0.0045 * last.scale] * 3, 6, last.weights, round_end=True))
        if spec.get("accent"):
            for flank in (0.10, PI - 0.10):
                pts = [(u, flank + (0.25 * PI if flank < 1 else -0.25 * PI) * smoothstep(0.15, 0.7, u)) for u in
                       (0.15, 0.30, 0.45, 0.60, 0.72)]
                accent.append(_curve_on_upper(last, pts, radius=0.0042, lift=0.0015))
        if spec["laces"]:
            tongue, laces = _laces(last, spec["laces"], trim)
            upper.append(tongue)
            trim.append(laces)
        if spec["shaft"]:
            top_t, inflate = spec["shaft"]
            shaft = body.leg(body_type, side, inflate=inflate, t_range=(top_t, 1.0))
            upper.append(shaft)
            from .garments import _limb_ring
            upper_trim.append(_limb_ring(body_type, side, "leg", top_t, inflate, 0.006 * last.scale))
            if spec.get("gusset"):
                for flank in (1.0, -1.0):
                    pts = []
                    for k in range(5):
                        y = last.spec["collar"] * last.scale * (0.55 + 0.6 * k / 4)
                        pts.append((last.ankle[0] + flank * (last.leg_r + inflate + 0.0012), y, last.ankle[2] - 0.002))
                    accent.append(tube(pts, [0.008 * last.scale] * 5, 6, last.weights))
        elif spec.get("gusset"):
            for flank in (1.0, -1.0):
                u = 0.33
                c = (last.cx + flank * (last.half_width(u) + 0.0012), last.height(u) - 0.012, last.z(u))
                accent.append(tube([add(c, (0.0, -0.010, -0.008)), add(c, (0.0, 0.004, 0.0)), add(c, (0.0, -0.010, 0.008))],
                                   [0.006 * last.scale] * 3, 6, last.weights))
        if spec["sock"] is not None:
            socks.append(body.leg(body_type, side, inflate=0.005, t_range=(0.86, 1.0)))
    parts = [(upper, "shoes", spec["texture"], (1.0, 1.0, 1.0))]
    if upper_trim.positions:
        parts.append((upper_trim, "shoes", spec["texture"], (0.80, 0.79, 0.78)))
    if sole.positions:
        parts.append((sole, "none", "leather" if spec["sole_colour"][0] < 0.5 else "plain", spec["sole_colour"]))
    if trim.positions:
        lace_colour = spec["sole_colour"] if spec["sole_colour"] is not None else (0.95, 0.95, 0.92)
        parts.append((trim, "none", "plain", lace_colour))
    if accent.positions:
        parts.append((accent, "accessory", "plain", (1.0, 1.0, 1.0)))
    if socks.positions:
        parts.append((socks, "none", "rib", spec["sock"]))
    if foot.positions:
        # The top of the foot in a low shoe's opening.
        parts.append((foot, "skin", "plain", (1.0, 1.0, 1.0)))
    return parts


def sandals(body_type):
    """Sandals: a contoured footbed on a sole and straps over the instep, round the toes and behind
    the heel; the foot shows."""
    spec = dict(L=0.272, sole=0.016, heel_block=0.0, toe=0.030, collar=0.040, spring=0.004)
    soles, straps = Mesh(), Mesh()
    for side, _ in rig.SIDES:
        last = Last(body_type, side, spec)
        last.W, last.HW = 0.100 * last.scale, 0.076 * last.scale
        soles.append(_sole(last, lip=0.004))
        foot_r = body.FOOT[body_type]
        for u, lift in ((0.70, 0.026), (0.42, 0.040)):
            z = last.z(u)
            w = foot_r[0] + 0.004
            straps.append(tube([(last.cx + w * math.cos(PI * k / 10), last.sole_top(u) + lift * last.scale * math.sin(PI * k / 10), z)
                                for k in range(11)], [0.0065 * last.scale] * 11, 6, last.weights))
        back = [(last.cx + (foot_r[0] + 0.006) * math.cos(PI * k / 12), 0.052 * last.scale,
                 last.ankle[2] - 0.036 * last.scale * math.sin(PI * k / 12)) for k in range(13)]
        straps.append(tube(back, [0.0055 * last.scale] * 13, 6, last.weights))
    return [(straps, "shoes", "leather", (1.0, 1.0, 1.0)), (soles, "none", "leather", (0.42, 0.30, 0.20))]
