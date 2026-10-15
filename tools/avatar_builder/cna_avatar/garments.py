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

def drape_sections(body_type, inflate, rows, fit=0.5, lower=0.0, flare=0.0, steps=2):
    """Torso garment sections between two section rows (inclusive): the body's, inflated, and
    below the chest hanging toward a straight fall from it (`fit` 0 follows the body, 1 hangs
    straight down from the bust and shoulder blades); `lower` drops the bottom row (a longer hem)
    and `flare` widens it. Each span between rows is split into `steps` (smooth interpolation) so
    folds have rows to live on."""
    full = body.torso_sections(body_type, inflate)
    chest = full[CHEST_ROW]
    sections = []
    for index, (c, rx, rz, n) in enumerate(full):
        if index < CHEST_ROW and fit:
            k = fit * smoothstep(chest[0][1] + 0.01, chest[0][1] - 0.10, c[1])
            # The front hangs from the bust, the back from the shoulder blades, the sides from the
            # chest; a little taper keeps it from reading as a box.
            rx = rx + max(0.0, chest[1] * 0.97 - rx) * k
            front = max(c[2] + rz, chest[0][2] + chest[2] * 0.985)
            back = min(c[2] - rz, chest[0][2] - chest[2] * 0.99)
            rz2 = 0.5 * (front - back)
            cz = 0.5 * (front + back)
            rz = rz + (rz2 - rz) * k
            c = (c[0], c[1], c[2] + (cz - c[2]) * k)
        sections.append((c, rx, rz, n))
    sections = sections[rows[0]:rows[1] + 1]
    if lower or flare:
        c, rx, rz, n = sections[0]
        sections[0] = (add(c, (0.0, -lower, 0.0)), rx + flare, rz + flare, n)
    out = []
    for a_, b_ in zip(sections, sections[1:]):
        for j in range(steps):
            t = j / steps
            s_ = t * t * (3.0 - 2.0 * t)
            out.append((lerp(a_[0], b_[0], t), a_[1] + (b_[1] - a_[1]) * s_, a_[2] + (b_[2] - a_[2]) * s_,
                        a_[3] + (b_[3] - a_[3]) * s_))
    out.append(sections[-1])
    return out


# The torso section row at the chest (the fullest), from which loose garments hang.
CHEST_ROW = 7


class Folds:
    """Low-frequency cloth folds as an outward offset of a garment's surface at (angle round it,
    height y): hanging folds that grow toward a hem, and gathers just above an elastic band."""

    def __init__(self, seed, hang=0.0, hang_from=0.0, hang_to=0.0, count=7, gather=0.0, gather_at=0.0, gather_span=0.05,
                 back=1.0):
        self.seed, self.hang, self.hang_from, self.hang_to, self.count = seed, hang, hang_from, hang_to, count
        self.gather, self.gather_at, self.gather_span, self.back = gather, gather_at, gather_span, back
        self.phases = [2.0 * PI * (0.5 + 0.5 * _jit(seed, k)) for k in range(4)]

    def __call__(self, angle, y):
        out = 0.0
        if self.hang:
            grow = smoothstep(self.hang_from, self.hang_to, y)
            if grow > 0.0:
                wave = 0.55 * math.cos(self.count * angle + self.phases[0]) + \
                    0.30 * math.cos((self.count + 3) * angle + self.phases[1]) + \
                    0.15 * math.cos((2 * self.count - 1) * angle + self.phases[2])
                side = 1.0 if math.cos(angle) > -0.2 else self.back
                out += self.hang * grow * side * max(0.0, 0.5 + 0.5 * wave) ** 1.5
        if self.gather:
            near = smoothstep(self.gather_at + self.gather_span, self.gather_at, y) * smoothstep(self.gather_at - 0.005, self.gather_at + 0.004, y)
            if near > 0.0:
                ripple = 0.5 + 0.5 * math.cos(2.0 * PI * (y - self.gather_at) / 0.018 + 0.6 * math.cos(3 * angle + self.phases[3]))
                out += self.gather * near * ripple * (0.6 + 0.4 * math.cos(5 * angle + self.phases[2]))
        return out


def _jit(*keys):
    h = 2166136261
    for k in keys:
        h = ((h ^ (int(k) & 0xffffffff)) * 16777619) & 0xffffffff
    h ^= h >> 15
    h = (h * 2246822519) & 0xffffffff
    h ^= h >> 13
    return (h & 0xffff) / 32767.5 - 1.0


def _lip_rows(ring, centre, thickness, depth, inward=True):
    """Rows continuing a garment's open edge back inside it: the fabric rolls over its edge and
    lines the opening for `depth`, `thickness` in from the outside, so an edge seen at an angle has
    the thickness of cloth. ring: the edge ring; centre: a point on the garment's axis there;
    depth: the direction and distance the lining runs back along the garment."""
    rows = []
    for k, (f_in, f_back) in enumerate(((0.45, 0.10), (0.9, 0.35), (1.0, 1.0))):
        row = []
        for p in ring:
            to_axis = sub(centre, p)
            to_axis = (to_axis[0], to_axis[1], to_axis[2])
            d = length(to_axis)
            inwards = mul(to_axis, thickness * f_in / d) if d > 1e-9 else (0.0, 0.0, 0.0)
            row.append(add(add(p, inwards), mul(depth, f_back)))
        rows.append(row)
    return rows


def garment_torso(body_type, inflate, rows, fit=0.5, lower=0.0, flare=0.0, folds=None, segments=48, lip=0.0035,
                  cap_bottom=None, weights=None, opening=None):
    """A torso garment (shirt body, trousers' seat) between two section rows: draped sections,
    folds, and the fabric's thickness turned in at the bottom edge (unless capped). With an
    `opening` (neck_opening's point function) a top reaching row 10 closes over the shoulders in a
    yoke up to its neck opening."""
    sections = drape_sections(body_type, inflate, rows, fit, lower, flare)
    if weights is None:
        weights = hem_weights(body_type) if rows[0] >= 1 else body.torso_weights(body_type)
    adjust = top_clearance(body_type, inflate) if rows[0] >= 1 else body.clear_of_hips(body_type, inflate + HIP_CLEARANCE)
    grid = []
    for c, rx, rz, n in sections:
        ring = body.superellipse_ring(c, rx, rz, n, segments)
        if folds is not None:
            out = []
            for k, p in enumerate(ring):
                angle = 2.0 * PI * k / segments
                off = folds(angle, c[1])
                if off:
                    d = normalize((p[0] - c[0], 0.0, p[2] - c[2]))
                    p = add(p, mul(d, off))
                out.append(p)
            ring = out
        grid.append([adjust(p) for p in ring])
    if cap_bottom is not None:
        c, rx, rz, n = sections[0]
        caps = []
        for i in range(4, 0, -1):
            a = PI * 0.5 * i / 4
            caps.append(body.superellipse_ring(add(c, (0.0, -cap_bottom * math.sin(a), 0.0)), rx * math.cos(a) + 1e-5,
                                               rz * math.cos(a) + 1e-5, n, segments))
        grid = caps + grid
    source = {}
    if cap_bottom is None and lip:
        c = sections[0][0]
        lining = _lip_rows(grid[0], c, lip, (0.0, 0.012, 0.0))
        # The lining moves with the hem it turns in from.
        for row in lining:
            for q, edge in zip(row, grid[0]):
                source[q] = edge
        grid = list(reversed(lining)) + grid
    if opening is not None and rows[1] == 10:
        grid += [[adjust(q) for q in ring] for ring in yoke_rows(body_type, inflate, segments, opening)]
    return Mesh().grid(grid, lambda p, v, u: weights(source.get(p, p)), uv_seam=True)


def shell(body_type, inflate, rows, segments=40, cap_bottom=None, lower=0.0, flare=0.0, open_front=None, fit=None, folds=None,
          neckline=(0.0, False), ease=0.010):
    """Torso loft between two section rows (inclusive), inflated; `lower` drops the bottom row
    (a longer hem) and `flare` widens it. Tops (from row 1 up) hang from the chest by `fit` and
    close over the shoulders to a neck opening (neckline: its front depth and whether it is a V;
    None leaves the top open at row 10)."""
    if fit is None:
        fit = 0.45 if rows[0] >= 1 else 0.0
    opening = None
    if neckline is not None and rows[1] == 10:
        opening = neck_opening(body_type, inflate, ease=ease, depth=neckline[0], v=neckline[1])[0]
    return garment_torso(body_type, inflate, rows, fit=fit, lower=lower, flare=flare, folds=folds,
                         segments=max(segments, 48), cap_bottom=cap_bottom, opening=opening)


def hang_folds(body_type, seed, depth, count=5):
    """Soft folds hanging from the chest to the hem of a loose top, deepest at the hem."""
    chest_y = body.torso_sections(body_type)[CHEST_ROW][0][1]
    hem_y = body.torso_sections(body_type)[1][0][1]
    return Folds(seed, hang=depth * 1.6, hang_from=chest_y - 0.02, hang_to=hem_y - 0.02, count=count, back=0.7)


def gather_folds(body_type, seed, lower, band, depth):
    """A top gathered by a ribbed band at its hem: the cloth blouses over it in soft ripples."""
    hem_y = body.torso_sections(body_type)[1][0][1] - lower
    chest_y = body.torso_sections(body_type)[CHEST_ROW][0][1]
    return Folds(seed, hang=depth * 0.8, hang_from=chest_y - 0.04, hang_to=hem_y + 0.05, count=6, gather=depth,
                 gather_at=hem_y + max(0.010, band * 3.2), gather_span=0.06)


def hem_weights(body_type):
    """Torso weights for a top: below the waist the cloth follows both thighs, shared smoothly
    across the front and back centre, so a bent hip lifts the hem instead of pushing the trousers
    through it, and the hem neither tears between the legs nor stays behind."""
    tw = body.torso_weights(body_type)
    hip_y = rig.PROPORTIONS[body_type]["hip"][1]
    left, right = I["HipLeft"], I["HipRight"]

    def fn(p):
        w = tw(p)
        # Only below the hip joints: cloth above them would swing inward as a thigh rises. The
        # front follows the thighs most (a raised knee lifts it); the back hangs from the seat.
        share = 0.9 * smoothstep(hip_y + 0.01, hip_y - 0.05, p[1]) * (0.45 + 0.55 * smoothstep(-0.08, 0.04, p[2]))
        if share <= 0.0:
            return w
        w = {k: v * (1.0 - share) for k, v in w.items()}
        side = smoothstep(-0.06, 0.06, p[0])
        w[left] = w.get(left, 0.0) + share * side
        w[right] = w.get(right, 0.0) + share * (1.0 - side)
        return w
    return fn


def waistband(body_type, inflate, height=0.032, proud=0.0015, loops=0, segments=48):
    """A trousers' or skirt's waistband: a flat band round the waist (section row 3) a little proud
    of the garment, stitched along its edges, with `loops` belt loops; it stays inside the
    thinnest top (it never stands further out than inflate + proud)."""
    tw = body.torso_weights(body_type)
    sections = body.torso_sections(body_type, inflate)
    c3, rx3, rz3, n3 = sections[3]
    c2, rx2, rz2, n2 = sections[2]
    rows = []
    for f, out in ((-0.04, 0.0), (0.0, proud * 0.7), (0.06, proud), (0.94, proud), (1.0, proud * 0.6), (1.04, 0.0)):
        y = c3[1] - height * 0.75 + height * f
        k = clamp((y - c2[1]) / max(c3[1] - c2[1], 1e-6), 0.0, 1.0)
        c = lerp(c2, c3, k)
        rx, rz = rx2 + (rx3 - rx2) * k, rz2 + (rz3 - rz2) * k
        rows.append(body.superellipse_ring((c[0], y, c[2]), rx + out, rz + out, n3, segments))
    m = Mesh().grid(rows, lambda p, v, u: tw(p), uv_seam=True)
    for k in range(loops):
        a = 2.0 * PI * (k + 0.5) / loops + 0.25
        ys = (c3[1] - height * 0.82, c3[1] + height * 0.30)
        path = []
        for y in (ys[0], 0.5 * (ys[0] + ys[1]), ys[1]):
            kk = clamp((y - c2[1]) / max(c3[1] - c2[1], 1e-6), 0.0, 1.0)
            c = lerp(c2, c3, kk)
            rx, rz = rx2 + (rx3 - rx2) * kk + proud + 0.0012, rz2 + (rz3 - rz2) * kk + proud + 0.0012
            sa, ca = math.sin(a), math.cos(a)
            path.append((c[0] + rx * math.copysign(abs(sa) ** (2.0 / n3), sa), y, c[2] + rz * math.copysign(abs(ca) ** (2.0 / n3), ca)))
        loop = tube(path, [0.0022] * 3, 6, tw)
        loop.positions = [(p[0], p[1], p[2]) for p in loop.positions]
        m.append(loop)
    return m


def ring_band(body_type, row, inflate, radius, segments=40, offset=(0.0, 0.0, 0.0), squash=1.0):
    """A soft rolled band (hem, waistband, neck rib) around a torso section row."""
    c, rx, rz, n = body.torso_sections(body_type, inflate)[row]
    c = add(c, offset)
    loop = body.superellipse_ring(c, rx, rz, n, segments)
    return tube(loop, [radius] * len(loop), 8, body.torso_weights(body_type), closed=True)


def _hem(body_type, inflate, flare, lower, radius, fit=0.45, segments=48):
    """The bottom hem of a top, on the draped body of shell(..., lower, flare, fit): a flat band
    a little proud of the cloth -- a stitched hem for thin `radius`, a ribbed band for thick."""
    sections = drape_sections(body_type, inflate, (1, 2), fit, lower, flare, steps=1)
    c, rx, rz, n = sections[0]
    height = max(0.010, radius * 3.2)
    proud = 0.0012 + 0.10 * radius
    weights = hem_weights(body_type)
    adjust = top_clearance(body_type, inflate)
    rows = []
    for y, out in ((-0.0010, 0.0), (0.0, proud * 0.8), (0.0015, proud), (height - 0.0015, proud), (height, proud * 0.6),
                   (height + 0.0015, 0.0)):
        cc = add(c, (0.0, y, 0.0))
        rows.append([adjust(p) for p in body.superellipse_ring(cc, rx + out, rz + out, n, segments)])
    return Mesh().grid(rows, lambda p, v, u: weights(p), uv_seam=True)


def neck_opening(body_type, inflate, ease=0.010, depth=0.0, v=False, width=1.0):
    """A top's neck opening at the base of the neck: point(a) on it at angle a (0 the front,
    toward the avatar's left), dipping `depth` at the front (a scoop, or a point for v)."""
    sec = body.torso_sections(body_type)
    c11, rx11, rz11, _ = sec[11]
    r = ease + inflate * 0.35
    cy = c11[1] + inflate * 0.7

    def point(a):
        sa, ca = math.sin(a), math.cos(a)
        wrapped = abs(((a + PI) % (2.0 * PI)) - PI)
        if v:
            dip = max(0.0, 1.0 - wrapped / 0.85)
        else:
            dip = max(0.0, ca) ** 2
        widen = 1.0 + 0.35 * dip * (depth > 0.0)
        return (c11[0] + (rx11 + r) * width * sa * widen, cy - depth * dip, c11[2] + (rz11 + r) * ca * (1.0 + 0.25 * dip * (depth > 0.0)))
    return point, (c11[0], cy, c11[2])


def yoke_rows(body_type, inflate, segments, opening):
    """Rows from a top's chest-and-shoulder ring (section row 10) in over the tops of the shoulders
    to its neck opening, clear of the skin below."""
    c10, rx10, rz10, n10 = body.torso_sections(body_type, inflate)[10]
    outer = body.superellipse_ring(c10, rx10, rz10, n10, segments)
    rows = []
    for f in (0.3, 0.62, 1.0):
        ring = []
        for k in range(segments):
            a = 2.0 * PI * k / segments
            p1 = opening(a)
            p0 = outer[k]
            s_ = f * f * (3.0 - 2.0 * f)
            ring.append((p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * s_ + 0.004 * math.sin(PI * f),
                         p0[2] + (p1[2] - p0[2]) * f))
        rows.append(ring)
    return rows


def neck_band(body_type, opening, centre, height=0.014, proud=0.002, segments=48, hug=0.94):
    """A ribbed neckline band standing on a neck opening: its top hugs the neck a little and turns
    in, so the edge shows the cloth's thickness."""
    weights = body.torso_weights(body_type)
    rows = []
    base = [opening(2.0 * PI * k / segments) for k in range(segments)]
    for f, out, up in ((0.0, 0.0, -0.002), (0.0, proud, 0.0), (0.85, proud * 0.8, 0.0), (1.0, proud * 0.4, 0.002)):
        ring = []
        for p in base:
            d = normalize((p[0] - centre[0], 0.0, p[2] - centre[2]))
            q = (centre[0] + (p[0] - centre[0]) * (1.0 - (1.0 - hug) * f), p[1] + height * f + up, centre[2] + (p[2] - centre[2]) * (1.0 - (1.0 - hug) * f))
            ring.append(add(q, mul(d, out)))
        rows.append(ring)
    top_centre = add(centre, (0.0, height, 0.0))
    rows += _lip_rows(rows[-1], top_centre, 0.003, (0.0, -0.012, 0.0))
    return Mesh().grid(rows, lambda p, v, u: weights(p), uv_seam=True)


def neck_rib(body_type, inflate, radius, drop=0.0, depth=0.0, width=1.0):
    """A crew or scoop neckline: a ribbed band round the neck opening; `depth` lowers the front."""
    opening, centre = neck_opening(body_type, inflate, depth=depth, width=width)
    return neck_band(body_type, opening, centre, height=max(0.010, radius * 1.4), proud=0.0015 + radius * 0.1)


def surface_path(body_type, inflate, a, opening, fit=0.45):
    """The path over a top's surface at angle a from its neck opening outward over the yoke and
    down the body: a list of points from the opening (as an arc-length lookup with outward
    normals: (points, lengths, normals))."""
    sections = drape_sections(body_type, inflate, (1, 10), fit, steps=2)
    pts = []
    p_open = opening(a)
    c10, rx10, rz10, n10 = sections[-1]
    s_, co = math.sin(a), math.cos(a)
    ring_pt = lambda c, rx, rz, n: (c[0] + rx * math.copysign(abs(s_) ** (2.0 / n), s_), c[1], c[2] + rz * math.copysign(abs(co) ** (2.0 / n), co))
    p10 = ring_pt(c10, rx10, rz10, n10)
    for f in (1.0, 0.62, 0.3):
        s2 = f * f * (3.0 - 2.0 * f)
        pts.append((p10[0] + (p_open[0] - p10[0]) * f, p10[1] + (p_open[1] - p10[1]) * s2 + 0.004 * math.sin(PI * f),
                    p10[2] + (p_open[2] - p10[2]) * f))
    for c, rx, rz, n in reversed(sections):
        pts.append(ring_pt(c, rx, rz, n))
    lengths = [0.0]
    for p0, p1 in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + length(sub(p1, p0)))
    normals = []
    for i, p in enumerate(pts):
        tangent = normalize(sub(pts[min(i + 1, len(pts) - 1)], pts[max(i - 1, 0)]))
        radial = normalize((p[0], 0.0, p[2] - c10[2]))
        n = normalize(sub(radial, mul(tangent, dot(radial, tangent))))
        normals.append(n)
    return pts, lengths, normals


def _along(path, distance):
    pts, lengths, normals = path
    distance = clamp(distance, 0.0, lengths[-1])
    for i in range(len(pts) - 1):
        if lengths[i + 1] >= distance:
            f = (distance - lengths[i]) / max(lengths[i + 1] - lengths[i], 1e-9)
            return lerp(pts[i], pts[i + 1], f), normalize(lerp(normals[i], normals[i + 1], f))
    return pts[-1], normals[-1]


def collar(body_type, inflate, gap=0.5, stand=0.024, fall=0.034, points=0.020, thick=0.003, flare=0.008, puff=0.0,
           back_fall=0.0, columns=36, ease=0.010, weights=None, garment=None, fit=0.45):
    """A collar on a neck opening: a stand round the neck and a fall turned down over it, lying
    on the garment (`garment`: its inflation; it covers the yoke and the shoulders), open at the
    front by `gap` radians; the fall's front corners reach down into points; `puff` gives the fall
    volume (a hood lying down), `back_fall` lengthens it at the back; `fit` is the garment's drape.
    The collar has the cloth's thickness all round, closed at its front edges, and its edge stands
    clear of the garment so it reads."""
    garment = inflate if garment is None else garment
    opening, centre = neck_opening(body_type, inflate, ease=ease)
    under = neck_opening(body_type, garment, ease=ease)[0]
    weights = weights or body.torso_weights(body_type)
    grid_cols = []
    angles = [gap * 0.5 + (2.0 * PI - gap) * k / (columns - 1) for k in range(columns)]
    for a in angles:
        base = opening(a)
        d = normalize((base[0] - centre[0], 0.0, base[2] - centre[2]))
        wrapped = abs(((a + PI) % (2.0 * PI)) - PI)
        front = smoothstep(0.75, gap * 0.5, wrapped)
        back = max(0.0, -math.cos(a)) ** 1.5
        L = fall + points * front + back_fall * back
        st = stand * (1.0 - 0.30 * front)
        top = add(add(base, (0.0, st, 0.0)), mul(d, -0.002))
        fold = add(add(top, (0.0, 0.004, 0.0)), mul(d, 0.006 + puff * 0.5))
        path = surface_path(body_type, garment, a, under, fit=fit)
        lift = thick + 0.0055 + puff
        p_mid, n_mid = _along(path, L * 0.40)
        p_end, n_end = _along(path, L)
        mid = add(add(p_mid, mul(n_mid, lift * 1.15 + 0.004)), (0.0, max(0.0, st - 0.010) * 0.5, 0.0))
        end = add(p_end, mul(n_end, lift * 0.85 + 0.002 * front))
        # The edge turns down onto the garment: a rim the light catches.
        inner_end = add(p_end, mul(n_end, 0.0015))
        inner_mid = add(mid, mul(n_mid, -thick - puff * 0.8))
        inner_fold = add(fold, mul(d, -thick * 0.9))
        # The cross-section is a closed loop: up the inside of the stand, over the fold, down the
        # top of the fall, round its edge, back up its underside and down the outside of the stand.
        grid_cols.append([add(base, mul(d, -0.001)), add(top, mul(d, -0.001)), fold, mid, end, inner_end, inner_mid,
                          inner_fold, add(top, mul(d, thick * 0.6)), add(base, mul(d, thick * 0.6))])
    rows = [[col[r] for col in grid_cols] for r in range(len(grid_cols[0]))]
    # Close the front edges: an extra column at each end collapsing the loop onto its middle.
    last = len(rows) - 1
    for side in (0, -1):
        col = [row[side] for row in rows]
        closed = [lerp(col[r], col[last - r], 0.5) for r in range(len(rows))]
        for r, row in enumerate(rows):
            if side == 0:
                row.insert(0, closed[r])
            else:
                row.append(closed[r])
    m = Mesh()
    m.patch_origin = centre
    m.grid(rows, lambda p, v, u: weights(p), wrap=False)
    # Make the top of the fall face away from the neck (the grid picks one side of the sheet).
    cols = columns + 2
    score = 0.0
    for t0 in range(0, len(m.indices), 3):
        idx = m.indices[t0:t0 + 3]
        if min(idx) // cols != 3:
            continue
        a_, b_, c_ = (m.positions[i] for i in idx)
        n = cross(sub(b_, a_), sub(c_, a_))
        score += dot(n, normalize((a_[0] - centre[0], 0.4, a_[2] - centre[2])))
    if score < 0.0:
        m.indices = [m.indices[i + o] for i in range(0, len(m.indices), 3) for o in (0, 2, 1)]
        m.normals = [mul(n, -1.0) for n in m.normals]
    return m


def lapels(body_type, inflate, gap, bottom_y, width=0.050, notch=0.30, weights=None, thick=0.003):
    """A jacket's lapels: the fronts folded back from the collar down to the top button at
    bottom_y, widest below the collar (where a notch cuts them), lying on the jacket."""
    weights = weights or hem_weights(body_type)
    m = Mesh()
    opening = neck_opening(body_type, inflate, ease=0.016)[0]
    for side in (1.0, -1.0):
        a_edge = side * gap * 0.5
        path = surface_path(body_type, inflate, a_edge, opening, fit=0.7)
        pts, lengths, normals = path
        # Down the front edge from the opening to bottom_y.
        rows_outer, rows_inner = [], []
        steps = 14
        total = None
        for i in range(len(pts)):
            if pts[i][1] <= bottom_y:
                total = lengths[i]
                break
        total = total or lengths[-1]
        for j in range(steps + 1):
            f = j / steps
            p, n = _along(path, total * f)
            # Width: grows from the bottom point up to the notch, then narrows to the collar.
            up = 1.0 - f
            w = width * (smoothstep(0.0, 0.75, up) * (1.0 - 0.45 * smoothstep(1.0 - notch, 1.0, up)))
            # Outward from the opening edge toward the side.
            side_dir = normalize(cross(n, (0.0, 1.0, 0.0)))
            if dot(side_dir, (side, 0.0, 0.0)) < 0.0:
                side_dir = mul(side_dir, -1.0)
            edge = add(p, mul(n, 0.0015))
            outer = add(add(p, mul(side_dir, w)), mul(n, thick + 0.0015))
            inner_edge = add(edge, mul(n, thick + 0.0015))
            rows_outer.append([edge, inner_edge, outer, add(outer, mul(n, -thick))])
        grid = [[r[k] for r in rows_outer] for k in range(4)]
        grid.append(grid[0])
        lap = Mesh()
        lap.patch_origin = add(pts[0], (side * 0.05, -0.1, 0.0))
        lap.grid(grid, lambda q, v, u: weights(q), wrap=False)
        m.append(lap)
    return m


def _envelope(profile):
    """A limb profile made to only narrow toward its end: cloth hangs over a limb's hollows."""
    out = []
    for i, (t, w, d) in enumerate(profile):
        out.append((t, max(r[1] for r in profile[i:]), max(r[2] for r in profile[i:])))
    return out


def garment_limb(body_type, side, kind, inflate, t_range, ease=0.0, flare=0.0, folds=None, lip=0.0030, segments=20,
                 steps=18, cap=True, snug_cap=0.6):
    """A sleeve or trouser leg: a loft along the arm or leg over the limb's envelope (it does not
    follow the elbow's or knee's hollow), `inflate` round it plus `ease` growing toward its end,
    widening by `flare` at the end. folds(angle, t) adds an outward offset; the end turns in to show
    the cloth's thickness. A sleeve starts in a shallow cap that sits snug over the shoulder
    (`snug_cap` of the inflation taken back over its top)."""
    pos = rig.joint_positions(body_type)
    if kind == "arm":
        a_, b_, c_ = (pos[I[n + side]] for n in ("Shoulder", "Elbow", "Wrist"))
        profile, weights = _envelope(body.ARM[body_type]), body.arm_weights(body_type, side)
    else:
        a_, b_, c_ = (pos[I[n + side]] for n in ("Hip", "Knee", "Ankle"))
        profile, weights = _envelope(body.LEG[body_type]), body.leg_weights(body_type, side)
    upper, lower = length(sub(b_, a_)), length(sub(c_, b_))
    total = upper + lower
    knee_t = upper / total
    axis_a, axis_b = normalize(sub(b_, a_)), normalize(sub(c_, b_))
    t0, t1 = t_range
    rows, ts, centres, axes = [], [], [], []
    for i in range(steps + 1):
        t = t0 + (t1 - t0) * i / steps
        q = body._profile_at(profile, min(max(t, profile[0][0]), profile[-1][0]))
        _, w, d = q
        u_ = (t - t0) / max(t1 - t0, 1e-6)
        grow = ease * smoothstep(0.0, 1.0, u_) + flare * smoothstep(0.6, 1.0, u_)
        if t <= knee_t:
            centre = add(a_, mul(axis_a, t * total))
        else:
            centre = add(b_, mul(axis_b, (t - knee_t) * total))
        k = smoothstep(knee_t - 0.06, knee_t + 0.06, t)
        axis = normalize(lerp(axis_a, axis_b, k))
        u, v, _ = frame_from_axis(axis, (0.0, 0.0, 1.0))
        ring = []
        for c in range(segments):
            ang = 2.0 * PI * c / segments
            ca, sa = math.cos(ang), math.sin(ang)
            r_w, r_d = w + inflate + grow, d + inflate + grow
            if kind == "arm" and t < 0.20:
                # Over the top of the shoulder the cap sits close (u points outward-up at angle 0)
                # and narrows toward the body, so the shoulder slopes into the sleeve instead of
                # standing up in a puff.
                top = max(0.0, ca) ** 2 * smoothstep(0.20, 0.02, t)
                r_w -= inflate * snug_cap * top
                r_d -= inflate * snug_cap * top
            off = folds(ang, t) if folds else 0.0
            ring.append(add(centre, add(mul(u, (r_w + off) * ca), mul(v, (r_d + off) * sa))))
        rows.append(ring)
        ts.append(t)
        centres.append(centre)
        axes.append(axis)
    if cap and kind == "arm":
        # A low rounded cap closing the top, under the shoulder line (the yoke covers above it).
        c0, r0 = centres[0], rows[0]
        extra = []
        for j in (1, 2, 3, 4):
            a = PI * 0.5 * j / 4
            extra.append([add(add(c0, mul(sub(p, c0), math.cos(a) + 1e-4)), mul(axes[0], -0.45 * length(sub(p, c0)) * math.sin(a)))
                          for p in r0])
        rows = list(reversed(extra)) + rows
        ts = [ts[0]] * 4 + ts
    if lip:
        lining = _lip_rows(rows[-1], centres[-1], lip, mul(axes[-1], -0.015))
        rows += lining
        ts += [ts[-1]] * len(lining)
    last = len(rows) - 1
    return Mesh().grid(rows, lambda p, vv, uu: weights(ts[int(round(vv * last))]), uv_seam=True)


def sleeve_folds(seed, length_, elbow_t, bunch=0.0, elbow=0.0, hang=0.0):
    """Folds of a sleeve: rings bunched toward a long sleeve's cuff, creases in the crook of the
    elbow, soft hanging folds on a short sleeve."""
    phase = 2.0 * PI * (0.5 + 0.5 * _jit(seed, 1))

    def fn(angle, t):
        out = 0.0
        if bunch:
            near = smoothstep(length_ - 0.30, length_ - 0.04, t)
            if near:
                out += bunch * near * (0.5 + 0.5 * math.cos(2.0 * PI * t / 0.055 + 0.8 * math.cos(angle + phase)))
        if elbow:
            crook = math.exp(-((t - elbow_t) / 0.06) ** 2) * max(0.0, -math.sin(angle)) ** 2
            out += elbow * crook * (0.5 + 0.5 * math.cos(2.0 * PI * t / 0.035 + 2.0 * math.cos(angle)))
        if hang:
            out += hang * smoothstep(0.05, length_, t) * max(0.0, math.cos(3 * angle + phase)) ** 2
        return out
    return fn


def armhole_seam(body_type, side, inflate, t=0.10, radius=0.0016):
    """The seam where a set-in sleeve meets the body: a fine ridge round the top of the arm."""
    pos = rig.joint_positions(body_type)
    a_, b_ = pos[I["Shoulder" + side]], pos[I["Elbow" + side]]
    total = length(sub(b_, a_)) + length(sub(pos[I["Wrist" + side]], b_))
    axis = normalize(sub(b_, a_))
    profile = _envelope(body.ARM[body_type])
    _, w, d = body._profile_at(profile, t)
    u, v, _ = frame_from_axis(axis, (0.0, 0.0, 1.0))
    centre = add(a_, mul(axis, t * total))
    loop = []
    for k in range(28):
        ang = 2.0 * PI * k / 28
        top = max(0.0, math.cos(ang)) ** 2 * smoothstep(0.16, 0.02, t)
        r = inflate * (1.0 - 0.6 * top) + 0.0005
        # The seam rises toward the top of the shoulder, as an armhole does.
        lift = mul(axis, -0.022 * max(0.0, math.cos(ang)))
        loop.append(add(add(centre, add(mul(u, (w + r) * math.cos(ang)), mul(v, (d + r) * math.sin(ang)))), lift))
    wt = body.arm_weights(body_type, side)(t)
    return tube(loop, [radius] * len(loop), 6, lambda p: wt, closed=True)


def sleeve(body_type, side, inflate, end, cuff=0.0, flare=0.0, start=0.02, ease=None, folds=None, seam=True):
    """A sleeve from the shoulder to `end` along the arm: long sleeves bunch at the cuff and
    crease at the elbow, short ones hang open; `cuff` is a band at the end (a knit cuff when thick)."""
    p = rig.PROPORTIONS[body_type]
    elbow_t = p["upper_arm"] / (p["upper_arm"] + p["forearm"])
    long_sleeve = end > 0.7
    if ease is None:
        ease = 0.004 if long_sleeve else 0.010
    if folds is None:
        folds = sleeve_folds(int(end * 100) + (1 if side == "Left" else 2), end, elbow_t,
                             bunch=0.0035 if long_sleeve else 0.0, elbow=0.004 if long_sleeve else 0.0,
                             hang=0.0 if long_sleeve else 0.0025)
    m = garment_limb(body_type, side, "arm", inflate * 0.8, (start, end), ease=ease, flare=flare, folds=folds,
                     lip=0.0 if cuff > 0.009 else 0.0028)
    if seam and start < 0.05:
        m.append(armhole_seam(body_type, side, inflate * 0.8))
    if cuff:
        m.append(_cuff_band(body_type, side, "arm", end, inflate * 0.8 + ease + flare, cuff))
    return m


def _cuff_band(body_type, side, kind, t, inflate, radius):
    """A band at the end of a sleeve or trouser leg: a narrow stitched hem for a thin `radius`, a
    ribbed knit cuff gathered a little tighter for a thick one."""
    knit = radius > 0.009
    height = 0.040 if knit else max(0.008, radius * 2.0)
    pos = rig.joint_positions(body_type)
    if kind == "arm":
        a_, b_, c_ = (pos[I[n + side]] for n in ("Shoulder", "Elbow", "Wrist"))
        profile, weights = _envelope(body.ARM[body_type]), body.arm_weights(body_type, side)
    else:
        a_, b_, c_ = (pos[I[n + side]] for n in ("Hip", "Knee", "Ankle"))
        profile, weights = _envelope(body.LEG[body_type]), body.leg_weights(body_type, side)
    upper, lower = length(sub(b_, a_)), length(sub(c_, b_))
    total = upper + lower
    knee_t = upper / total
    axis_a, axis_b = normalize(sub(b_, a_)), normalize(sub(c_, b_))
    rows, ts = [], []
    span = height / total
    for f, out in ((-0.02, 0.0), (0.0, 0.7), (0.12, 1.0), (0.88, 1.0), (1.0, 0.7), (1.02, 0.0)):
        tt = t - span * (1.0 - f)
        _, w, d = body._profile_at(profile, min(max(tt, profile[0][0]), profile[-1][0]))
        if tt <= knee_t:
            centre = add(a_, mul(axis_a, tt * total))
        else:
            centre = add(b_, mul(axis_b, (tt - knee_t) * total))
        axis = normalize(lerp(axis_a, axis_b, smoothstep(knee_t - 0.06, knee_t + 0.06, tt)))
        u, v, _ = frame_from_axis(axis, (0.0, 0.0, 1.0))
        r = inflate + (0.0015 if not knit else -0.002) + out * (0.0012 if not knit else 0.004)
        rows.append([add(centre, add(mul(u, (w + r) * math.cos(2 * PI * k / 24)), mul(v, (d + r) * math.sin(2 * PI * k / 24))))
                     for k in range(24)])
        ts.append(tt)
    last = len(rows) - 1
    return Mesh().grid(rows, lambda p, vv, uu: weights(ts[int(round(vv * last))]), uv_seam=True)


def trouser_leg(body_type, side, inflate, end, cuff=0.0, flare=0.0, ease=None, folds=None):
    """A trouser leg from the seat to `end` along the leg: full-length legs stack in soft folds
    above the shoe and crease behind the knee; shorts hang open."""
    p = rig.PROPORTIONS[body_type]
    pos = rig.joint_positions(body_type)
    hip, knee, ankle = (pos[I[n + side]] for n in ("Hip", "Knee", "Ankle"))
    knee_t = length(sub(knee, hip)) / (length(sub(knee, hip)) + length(sub(ankle, knee)))
    full = end > 0.85
    if ease is None:
        ease = 0.004 if full else 0.010
    if folds is None:
        seed = int(end * 100) + (3 if side == "Left" else 4)
        phase = 2.0 * PI * (0.5 + 0.5 * _jit(seed, 2))

        def folds(angle, t):
            out = 0.0
            if full:
                # Stacking above the shoe and a few creases behind the knee.
                out += 0.004 * smoothstep(end - 0.16, end - 0.03, t) * \
                    (0.5 + 0.5 * math.cos(2.0 * PI * t / 0.05 + 1.2 * math.cos(angle + phase)))
                out += 0.0035 * math.exp(-((t - knee_t) / 0.05) ** 2) * max(0.0, -math.sin(angle)) ** 2 * \
                    (0.5 + 0.5 * math.cos(2.0 * PI * t / 0.03))
            # The front of the thigh drapes in a couple of long soft folds from the seat.
            out += 0.0025 * smoothstep(0.02, 0.25, t) * (1.0 - smoothstep(0.4, 0.6, t)) * max(0.0, math.cos(2 * angle + phase)) ** 3
            return out
    m = garment_limb(body_type, side, "leg", inflate, (-0.02, end), ease=ease, flare=flare, folds=folds, cap=False,
                     lip=0.0 if cuff > 0.009 else 0.0028)
    if cuff:
        m.append(_cuff_band(body_type, side, "leg", end, inflate + ease + flare, cuff))
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


def clear_of_shoulders(body_type, clearance):
    """A point adjustment moving a top's points out of the rounded tops of the arms (the shoulders'
    domes, which stand above the torso's sections), to `clearance` beyond them: the yoke wraps over
    the shoulder and the sleeve hangs from it."""
    pos = rig.joint_positions(body_type)
    caps = []
    for side, _ in rig.SIDES:
        sh, el, wr = pos[I["Shoulder" + side]], pos[I["Elbow" + side]], pos[I["Wrist" + side]]
        axis = normalize(sub(el, sh))
        total = length(sub(el, sh)) + length(sub(wr, el))
        _, w, d = body._profile_at(body.ARM[body_type], 0.02)
        r = max(w, d)
        caps.append((add(sh, mul(axis, 0.02 * total)), axis, r))

    def adjust(p):
        for centre, axis, r in caps:
            rel = sub(p, centre)
            along = dot(rel, axis)
            # The dome above the cap's ring (along -axis) is 0.9 r high; below it the arm is a tube.
            height = 0.9 * r if along < 0.0 else r * 4.0
            radial = sub(rel, mul(axis, along))
            q = math.sqrt(dot(radial, radial) / (r + clearance) ** 2 + (along / (height + clearance)) ** 2)
            if 1e-6 < q < 1.0 and along < 0.25 * r:
                p = add(centre, mul(rel, 1.0 / q))
        return p
    return adjust


def top_clearance(body_type, inflate):
    """A top's point adjustment: clear of the hips' outline above the hip joints and of the thighs
    below them, far enough that it covers every bottom's waist and legs (and a skirt's top)."""
    hips = body.clear_of_hips(body_type, inflate + HIP_CLEARANCE)
    thighs = clear_of_thighs(body_type, max(inflate + HIP_CLEARANCE + 0.004, 0.022))
    shoulders = clear_of_shoulders(body_type, inflate * 0.8)
    return lambda p: shoulders(thighs(hips(p)))

def patch(body_type, centre, size, normal, weights, depth=0.004, segments=12):
    """A thin rounded slab (pocket, label) lying on a surface facing `normal`."""
    n = normalize(normal)
    right = normalize(cross((0.0, 1.0, 0.0), n)) if abs(n[1]) < 0.9 else (1.0, 0.0, 0.0)
    up = cross(n, right)
    return ellipsoid(add(centre, mul(n, depth * 0.5)), (right, up, n), (size[0], size[1], depth), segments, 6,
                     lambda p, v, u: weights(p))


def surface_patch(body_type, inflate, centre, half, weights, back=False, depth=0.0025, columns=10, rows=10, fit=0.0):
    """A pocket or a patch lying on a torso garment's front (or back): a rounded rectangle raised
    `depth` off the surface it follows, sinking into it at its edges (no rim stands off a curve)."""
    sections = body.torso_sections(body_type, inflate)
    cx, cy = centre
    hw, hh = half

    def surface_z(x, y):
        z = front_z(body_type, inflate, x, y, fit)
        return -z + 2.0 * _centre_z(sections, y) if back else z
    grid = []
    for j in range(rows + 1):
        v = j / rows * 2.0 - 1.0
        row = []
        for i in range(columns + 1):
            u = i / columns * 2.0 - 1.0
            edge = (abs(u) ** 4 + abs(v) ** 4) ** 0.25
            h = depth * smoothstep(1.0, 0.72, edge) - 0.0006 * smoothstep(0.85, 1.0, edge)
            x, y = cx + hw * u * (1.0 - 0.12 * max(0.0, -v) * (not back)), cy + hh * v
            z = surface_z(x, y)
            row.append((x, y, z + (-h if back else h)))
        grid.append(row)
    m = Mesh()
    m.patch_origin = (cx, cy, (surface_z(cx, cy) + (0.05 if back else -0.05)))
    return m.grid(grid, lambda q, vv, uu: weights(q), wrap=False)


def front_z(body_type, inflate, x, y, fit=0.0):
    """Depth of the torso's front surface at (x, y), between the section rows around y (of a top
    draped by `fit`, when given)."""
    sections = body.torso_sections(body_type, inflate) if not fit else drape_sections(body_type, inflate, (0, 11), fit, steps=1)
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


def kangaroo_pocket(body_type, inflate, bottom, height, bottom_half, top_half, weights, depth=0.005, fit=0.65):
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
    face = [[(x, y, front_z(body_type, inflate, x, y, fit) + depth) for x, y, _ in row] for row in grid]
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
        main.append(shell(body_type, 0.017, (1, 10), lower=0.03, flare=0.030, folds=hang_folds(body_type, 21, 0.006)))
        main.append(_hem(body_type, 0.017, 0.030, 0.03, 0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.016, 0.42, cuff=0.006, flare=0.006))
        if style == "top_tshirt":
            main.append(neck_rib(body_type, 0.012, 0.009, drop=0.004))
        else:
            main.append(_collar(body_type, 0.016, spread=0.55, garment=0.017, fit=0.45))
            for k in range(2):
                trim.append(_button(body_type, 9, 0.022, -0.028 - 0.03 * k, tw))
        parts.append((main, "top", "knit", (1.0, 1.0, 1.0)))
    elif style == "top_longsleeve":
        main.append(shell(body_type, 0.017, (1, 10), lower=0.03, flare=0.028, folds=hang_folds(body_type, 22, 0.006)))
        main.append(_hem(body_type, 0.017, 0.028, 0.03, 0.004))
        main.append(neck_rib(body_type, 0.011, 0.009, drop=0.004))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.014, 0.95, cuff=0.007))
        parts.append((main, "top", "knit", (1.0, 1.0, 1.0)))
    elif style == "top_hoodie":
        main.append(shell(body_type, 0.028, (1, 10), lower=0.04, flare=0.010, fit=0.65,
                          folds=gather_folds(body_type, 23, 0.04, 0.012, 0.004)))
        main.append(_hem(body_type, 0.028, 0.010, 0.04, 0.012, fit=0.65))
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
        main.append(body.section_loft(rows, 40, tw, adjust=top_clearance(body_type, 0.017)))
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
        main.append(shell(body_type, 0.019, (1, 10), lower=0.045, flare=0.032, fit=0.55, folds=hang_folds(body_type, 24, 0.007)))
        main.append(_hem(body_type, 0.019, 0.032, 0.045, 0.003, fit=0.55))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.018, 0.44, cuff=0.007, flare=0.008))
        main.append(_collar(body_type, 0.018, spread=0.7, garment=0.019, fit=0.55))
        for k in range(5):
            trim.append(_button_at(body_type, 0.019, row_y(body_type, 9) - 0.045 - k * 0.078, tw, fit=0.55))
        main.append(placket(body_type, 0.019, 9, row_y(body_type, 1) - 0.04, 0.0022, tw, fit=0.55))
        main.append(surface_patch(body_type, 0.019, (0.062, row_y(body_type, 7) - 0.02), (0.026, 0.030), tw, fit=0.55))
        parts.append((main, "top", "canvas", (1.0, 1.0, 1.0)))
    elif style == "top_sweater":
        main.append(shell(body_type, 0.024, (1, 10), lower=0.03, flare=0.026, fit=0.6,
                          folds=gather_folds(body_type, 25, 0.03, 0.012, 0.003)))
        main.append(_hem(body_type, 0.024, 0.026, 0.03, 0.012, fit=0.6))
        main.append(neck_rib(body_type, 0.020, 0.013, drop=0.006))
        for side, _ in rig.SIDES:
            main.append(sleeve(body_type, side, 0.022, 0.93, cuff=0.012))
        parts.append((main, "top", "cable", (1.0, 1.0, 1.0)))
    elif style == "top_jacket":
        # An open zip jacket (top colour) over a crew shirt (accessory colour).
        inner = Mesh()
        inner.append(shell(body_type, 0.017, (1, 10), lower=0.02, flare=0.004, fit=0.3))
        inner.append(_hem(body_type, 0.017, 0.004, 0.02, 0.003, fit=0.3))
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


def _collar(body_type, inflate, spread=0.6, garment=None, fit=0.45):
    """A turned-down shirt collar, open at the front by `spread`, with pointed front corners."""
    return collar(body_type, inflate, gap=spread, stand=0.022, fall=0.036, points=0.024, thick=0.0035, flare=0.004 + inflate * 0.3,
                  garment=garment, fit=fit)


def _collar_stand(body_type, inflate):
    """A jacket's collar standing round the neck (a zip jacket's, a quarter zip's)."""
    return collar(body_type, inflate, gap=0.45, stand=0.030, fall=0.010, points=0.0, thick=0.004, flare=0.0,
                  ease=0.012 + inflate * 0.15)


def _hood(body_type):
    """The hood folded down on the upper back: a full, soft collar of cloth lying over the
    shoulders and down the back."""
    hood = lambda p: {I["BackUpper"]: 0.55, I["Neck"]: 0.45} if p[1] > rig.PROPORTIONS[body_type]["collar"][1] - 0.02 else \
        body.torso_weights(body_type)(p)
    return collar(body_type, 0.030, gap=0.9, stand=0.034, fall=0.040, points=0.0, thick=0.008, flare=0.014, puff=0.024,
                  back_fall=0.12, columns=48, ease=0.016, weights=hood, garment=0.028, fit=0.65)


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


def _button_at(body_type, inflate, y, weights, fit=0.45, x=0.0):
    """A button on the front of a top (draped by `fit`) at height y."""
    z = front_z(body_type, inflate, x, y, fit)
    return ellipsoid((x, y, z + 0.0028), ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.006, 0.006, 0.0025), 8, 5,
                     lambda q, v, u: weights(q))


def _open_shell(body_type, inflate, rows, gap, lower=0.0, flare=0.004, fit=0.7, folds=None):
    """A jacket body open down the front: draped like a top over every angle except `gap` radians,
    closing over the shoulders to its collar, its hem turned in and its front edges rolled."""
    clear = top_clearance(body_type, inflate)
    sections = drape_sections(body_type, inflate, rows, fit, lower, flare)
    tw = hem_weights(body_type)
    columns = 41
    angles = [gap * 0.5 + (2.0 * PI - gap) * k / (columns - 1) for k in range(columns)]
    grid = []
    for c, rx, rz, n in sections:
        ring = []
        for a in angles:
            s_, co = math.sin(a), math.cos(a)
            p = (c[0] + rx * math.copysign(abs(s_) ** (2.0 / n), s_), c[1], c[2] + rz * math.copysign(abs(co) ** (2.0 / n), co))
            if folds is not None:
                off = folds(a, c[1])
                if off:
                    p = add(p, mul(normalize((p[0] - c[0], 0.0, p[2] - c[2])), off))
            ring.append(clear(p))
        grid.append(ring)
    source = {}
    lining = _lip_rows(grid[0], sections[0][0], 0.004, (0.0, 0.012, 0.0))
    for row in lining:
        for q, edge in zip(row, grid[0]):
            source[q] = edge
    grid = list(reversed(lining)) + grid
    if rows[1] == 10:
        opening = neck_opening(body_type, inflate, ease=0.016)[0]
        outer_ring = grid[-1]
        for f in (0.3, 0.62, 1.0):
            ring = []
            for a, p0 in zip(angles, outer_ring):
                p1 = opening(a)
                s2 = f * f * (3.0 - 2.0 * f)
                ring.append(clear((p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * s2 + 0.004 * math.sin(PI * f),
                                   p0[2] + (p1[2] - p0[2]) * f)))
            grid.append(ring)
    outer = Mesh().grid(grid, lambda p, v, u: tw(source.get(p, p)), wrap=False)
    # Front edges: a rolled border along both edges of the opening.
    for index in (0, columns - 1):
        edge = [ring[index] for ring in grid[3:]]
        outer.append(tube(edge, [0.005] * len(edge), 6, tw))
    return outer


def _seam_on(body_type, inflate, points_xy, front=True, radius=0.0014, lift=0.0006):
    """A stitched seam: a fine ridge along (x, y) points on the front (or back) of the garment."""
    tw = body.torso_weights(body_type)
    path = []
    for x, y in points_xy:
        z = front_z(body_type, inflate, x, y)
        if not front:
            sections = body.torso_sections(body_type, inflate)
            z = -front_z(body_type, inflate, x, y) + 2.0 * _centre_z(sections, y)
        path.append((x, y, z + (lift if front else -lift)))
    return tube(path, [radius] * len(path), 5, tw)


def _centre_z(sections, y):
    for low, high in zip(sections, sections[1:]):
        if low[0][1] <= y <= high[0][1]:
            t = (y - low[0][1]) / max(high[0][1] - low[0][1], 1e-6)
            return low[0][2] + (high[0][2] - low[0][2]) * t
    return sections[0][0][2]


def trouser_details(body_type, inflate, jeans=True, back_pockets=True):
    """What makes trousers read as made: the fly's stitched J, curved front pocket openings and,
    for jeans and casual trousers, patch pockets on the seat."""
    sections = body.torso_sections(body_type, inflate)
    waist_y = sections[3][0][1] - 0.032 * 0.75
    hip_y = sections[1][0][1]
    m = Mesh()
    # The fly: down the centre front, curving to one side at the bottom.
    fly = [(0.024, waist_y - 0.002), (0.024, waist_y - 0.06), (0.020, waist_y - 0.085), (0.006, waist_y - 0.098)]
    m.append(_seam_on(body_type, inflate, fly))
    for side in (1.0, -1.0):
        rx = sections[2][1]
        # Front pocket opening: a curve from the waistband down to the side seam.
        pocket = [(side * rx * (0.45 + 0.50 * f ** 1.6), waist_y - 0.002 - 0.075 * math.sin(0.5 * PI * f)) for f in
                  (0.0, 0.25, 0.5, 0.75, 1.0)]
        m.append(_seam_on(body_type, inflate, [(x * 0.92, y) for x, y in pocket]))
    tw = body.torso_weights(body_type)
    if back_pockets:
        for side in (1.0, -1.0):
            m.append(surface_patch(body_type, inflate, (side * 0.058, waist_y - 0.068), (0.038, 0.044), tw, back=True))
    return m


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
        main.append(waistband(body_type, inflate, height=0.032 if style != "bottom_joggers" else 0.040,
                              loops=5 if style in ("bottom_jeans", "bottom_cargo") else 0))
        if style in ("bottom_jeans", "bottom_cargo", "bottom_shorts"):
            main.append(trouser_details(body_type, inflate, jeans=style == "bottom_jeans"))
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
    if style == "bottom_skirt":
        return _skirt(body_type, 0.07, 0.07, "pleat", pleats=28)
    if style == "bottom_longskirt":
        return _skirt(body_type, -0.16, 0.09, "knit", seed=32)
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
    # Every toe box stands clear of the toes (the flats' too: they show the instep, not the toes).
    toe_h = {"shoes_dress": 0.040, "shoes_flats": 0.058}.get(style, 0.046) * scale
    heel_h = {"shoes_flats": 0.058, "shoes_dress": 0.070}.get(style, 0.080) * scale

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
    from . import shoes as footwear
    return footwear.build(body_type, style)


def _shoes_revision2(body_type, style):
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
    """A V neckline: a ribbed band round the neck opening dipping to a point at the front."""
    opening, centre = neck_opening(body_type, inflate, depth=depth, v=True)
    return neck_band(body_type, opening, centre, height=max(0.010, radius * 1.4), proud=0.0015 + radius * 0.1, hug=0.97)


def placket(body_type, inflate, top_row, bottom_y, radius, weights, fit=0.45):
    """A line down the centre front (a zip or a button placket) from a torso row to bottom_y, on a
    top draped by `fit`."""
    top_y = body.torso_sections(body_type)[top_row][0][1]
    path = []
    for k in range(13):
        y = top_y + (bottom_y - top_y) * k / 12
        path.append((0.0, y, front_z(body_type, inflate, 0.0, y, fit) + 0.0012))
    return tube(path, [radius] * len(path), 6, weights)


def buttons_down(body_type, inflate, count, top_y, spacing, weights, fit=0.45):
    m = Mesh()
    for k in range(count):
        m.append(_button_at(body_type, inflate, top_y - k * spacing, weights, fit))
    return m


def revised_top(body_type, style):
    tw = hem_weights(body_type)
    parts, main, trim, accent = [], Mesh(), Mesh(), Mesh()
    texture = "knit"
    trim_colour = (0.92, 0.91, 0.88)
    collar_y = row_y(body_type, 9)

    v_neck = style in ("top_vneck", "top_jersey", "top_raglan")

    def tee(inflate=0.017, sleeves=0.42, hem=0.004):
        main.append(shell(body_type, inflate, (1, 10), lower=0.03, flare=0.030, folds=hang_folds(body_type, 26, 0.006),
                          neckline=(0.055, True) if v_neck else (0.0, False)))
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
            trim.append(buttons_down(body_type, inflate, 3, collar_y - 0.035, 0.028, tw))
        else:
            main.append(_collar(body_type, 0.018, spread=0.7, garment=inflate))
            trim.append(buttons_down(body_type, inflate, 6, collar_y - 0.045, 0.070, tw))
            main.append(placket(body_type, inflate, 9, row_y(body_type, 1) - 0.04, 0.0022, tw))
            main.append(surface_patch(body_type, inflate, (0.065, row_y(body_type, 7) - 0.02), (0.026, 0.030), tw))
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
            main.append(surface_patch(body_type, 0.028, (sx, row_y(body_type, 3) - 0.02), (0.036, 0.046), tw, depth=0.003))
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
        inner.append(shell(body_type, 0.017, (1, 10), lower=0.02, flare=0.004, fit=0.3))
        inner.append(_hem(body_type, 0.017, 0.004, 0.02, 0.003, fit=0.3))
        if style in ("top_blazer", "top_vest"):
            inner.append(_collar(body_type, 0.016, spread=0.8, garment=0.017))
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
                trim.append(ellipsoid((0.045, y, front_z(body_type, 0.026, 0.045, y, 0.7) + 0.003), ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
                                      (0.006, 0.006, 0.0025), 8, 5, lambda q, v, u: tw(q)))
            texture = "cable"
        elif style == "top_blazer":
            main.append(_open_shell(body_type, 0.030, (1, 10), gap=0.62, lower=0.085, flare=0.034))
            main.append(collar(body_type, 0.030, gap=1.1, stand=0.020, fall=0.030, points=0.0, flare=0.006, ease=0.016,
                               garment=0.030, fit=0.7))
            main.append(lapels(body_type, 0.030, 0.62, row_y(body_type, 3) + 0.03))
            for side, _ in rig.SIDES:
                main.append(sleeve(body_type, side, 0.027, 0.95, cuff=0.006))
            for sx in (0.090, -0.090):
                main.append(surface_patch(body_type, 0.030, (sx, row_y(body_type, 2) + 0.01), (0.034, 0.009), tw, fit=0.7,
                                          depth=0.0035))
            for k in range(2):
                y = row_y(body_type, 3) + 0.02 - k * 0.07
                trim.append(ellipsoid((0.040, y, front_z(body_type, 0.030, 0.040, y, 0.7) + 0.004), ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
                                      (0.007, 0.007, 0.003), 8, 5, lambda q, v, u: tw(q)))
            trim_colour = (0.20, 0.20, 0.22)
            texture = "plain"
        elif style == "top_denimjacket":
            main.append(_open_shell(body_type, 0.030, (1, 10), gap=0.24, lower=0.04, flare=0.022))
            main.append(_collar(body_type, 0.030, spread=0.5, garment=0.030, fit=0.7))
            for side, _ in rig.SIDES:
                main.append(sleeve(body_type, side, 0.027, 0.94, cuff=0.010))
            for sx in (0.075, -0.075):
                main.append(surface_patch(body_type, 0.030, (sx, row_y(body_type, 7) - 0.01), (0.030, 0.026), tw, fit=0.7,
                                          depth=0.003))
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
                trim.append(placket(body_type, inflate, 9, row_y(body_type, 1) - 0.03, 0.003, tw, fit=0.7))
                trim_colour = (0.55, 0.56, 0.58)
            else:
                trim.append(buttons_down(body_type, inflate, 4, collar_y - 0.11, 0.06, tw, fit=0.7))
                trim_colour = (0.20, 0.20, 0.22)
            texture = "quilt" if puffy else "plain"
        parts += inner_parts
    elif style == "top_blouse":
        main.append(shell(body_type, 0.018, (1, 10), lower=0.05, flare=0.060, fit=0.6, folds=hang_folds(body_type, 27, 0.008),
                          neckline=(0.035, False)))
        main.append(_hem(body_type, 0.018, 0.060, 0.05, 0.003, fit=0.6))
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


def _trousers(body_type, inflate, end, cuff, flare, texture, pockets=False, stripes=False, crease=False, tight=False):
    tw = body.torso_weights(body_type)
    main, accent = Mesh(), Mesh()
    main.append(shell(body_type, inflate, (0, 3), cap_bottom=0.05))
    main.append(waistband(body_type, inflate, loops=5 if texture in ("canvas", "denim", "plain") else 0))
    if texture in ("canvas", "denim", "plain"):
        main.append(trouser_details(body_type, inflate, jeans=False, back_pockets=texture != "plain"))
    pos = rig.joint_positions(body_type)
    for side, sign in rig.SIDES:
        if tight:
            main.append(trouser_leg(body_type, side, inflate + 0.002, end, cuff=cuff, flare=flare, ease=0.0,
                                    folds=lambda angle, t: 0.0))
        else:
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


def clear_of_thighs(body_type, clearance):
    """A point adjustment moving a skirt's points out of the thighs below the hips (along each
    leg's radial direction) to `clearance` beyond the leg's widest section below that height."""
    pos = rig.joint_positions(body_type)
    legs = []
    for side, _ in rig.SIDES:
        hip, knee = pos[I["Hip" + side]], pos[I["Knee" + side]]
        legs.append((hip, normalize(sub(knee, hip)), length(sub(knee, hip)) + length(sub(pos[I["Ankle" + side]], knee))))
    profile = _envelope(body.LEG[body_type])

    def adjust(p):
        for hip, axis, total in legs:
            rel = sub(p, hip)
            t = dot(rel, axis) / total
            if t < 0.0 or t > 0.6:
                continue
            _, w, d = body._profile_at(profile, max(t, profile[0][0]))
            r = max(w, d) + clearance
            radial = sub(rel, mul(axis, dot(rel, axis)))
            dist = length(radial)
            if 1e-6 < dist < r:
                p = add(p, mul(radial, r / dist - 1.0))
        return p
    return adjust


def _skirt(body_type, length_offset, flare, texture, pleats=0, seed=0):
    """A skirt from the waist to `length_offset` from the knee, flaring by `flare`: it clears the
    hips' outline, falls in soft folds (or `pleats` pressed pleats), and its hem shows the cloth's
    thickness."""
    main = Mesh()
    main.append(shell(body_type, 0.008, (0, 3), cap_bottom=0.05))
    main.append(waistband(body_type, 0.010, height=0.030))
    p = rig.PROPORTIONS[body_type]
    a = body.anchors(body_type)
    sections = body.torso_sections(body_type, 0.014)
    hips = sections[1]
    bottom_y = p["knee"][1] + length_offset
    hip_y = p["hip"][1]
    hips_clear = body.clear_of_hips(body_type, 0.016)
    thighs_clear = clear_of_thighs(body_type, 0.016)
    clear = lambda q: thighs_clear(hips_clear(q))
    segments = 72

    def body_at(y):
        """The body's section at height y (no narrower than the hips below them)."""
        if y <= hips[0][1]:
            return hips[1], hips[2]
        for low, high in zip(sections, sections[1:]):
            if low[0][1] <= y <= high[0][1]:
                f = (y - low[0][1]) / max(high[0][1] - low[0][1], 1e-6)
                return low[1] + (high[1] - low[1]) * f, low[2] + (high[2] - low[2]) * f
        return sections[3][1], sections[3][2]
    phases = [2.0 * PI * (0.5 + 0.5 * _jit(seed, k)) for k in range(3)]
    rows, ys = [], []
    for i in range(16):
        t = i / 15.0
        y = a["lower"] + 0.02 + (bottom_y - a["lower"] - 0.02) * t
        bx, bz = body_at(y)
        rx = bx + flare * t * t
        rz = bz + flare * 0.8 * t * t
        c = (0.0, y, 0.0)
        ring = []
        for j, q in enumerate(body.superellipse_ring(c, rx, rz, 2.2, segments)):
            ang = 2.0 * PI * j / segments
            d = normalize((q[0], 0.0, q[2]))
            if pleats:
                # Knife pleats: a sharp zigzag that opens toward the hem.
                saw = (ang * pleats / (2.0 * PI)) % 1.0
                off = 0.006 * smoothstep(0.05, 0.5, t) * (saw - 0.5)
            else:
                wave = 0.6 * math.cos(6 * ang + phases[0]) + 0.4 * math.cos(9 * ang + phases[1])
                off = (0.004 + flare * 0.12) * smoothstep(0.25, 1.0, t) * max(0.0, 0.5 + 0.5 * wave) ** 1.5
            ring.append(clear(add(q, mul(d, off))))
        rows.append(ring)
        ys.append(y)
    rows.reverse()
    hem_centre = (0.0, ys[-1], 0.0)
    lining = _lip_rows(rows[0], hem_centre, 0.003, (0.0, 0.012, 0.0))
    source = {q: edge for row in lining for q, edge in zip(row, rows[0])}
    rows = list(reversed(lining)) + rows

    def skirt(pt):
        k = 0.95 * smoothstep(hip_y + 0.005, hip_y - 0.06, pt[1])
        s = smoothstep(-0.07, 0.07, pt[0])
        w = {I["BackLower"]: 1.0 - k}
        if k > 0:
            w[I["HipLeft"]] = k * s
            w[I["HipRight"]] = k * (1.0 - s)
        return w
    main.append(Mesh().grid(rows, lambda q, v, u: skirt(source.get(q, q)), uv_seam=True))
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
        return _trousers(body_type, 0.004, 0.96, 0.0, -0.004, "knit", tight=True)
    if style == "bottom_trackpants":
        return _trousers(body_type, 0.014, 0.97, 0.012, -0.004, "jersey", stripes=True)
    if style == "bottom_bermuda":
        return _trousers(body_type, 0.011, 0.52, 0.007, 0.010, "canvas")
    if style == "bottom_denimskirt":
        return _skirt(body_type, 0.11, 0.05, "denim", seed=31)
    if style == "bottom_midiskirt":
        return _skirt(body_type, -0.07, 0.08, "pleat", pleats=24)
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
    "shoes_maryjanes": dict(L=0.255, sole=0.014, toe=0.056, heel=0.058, rubber=0.0, laces=0, collar=False, shaft=None,
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
        # In the trim colour, so the strap reads against the shoe.
        trim.append(tube(strap, [0.0045 * scale] * 11, 6, weights))
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
