# SPDX-License-Identifier: MS-PL
"""Catalog v3 revision 3 hairstyles, built as sculpted masses (hairmass.py) over a scalp cap.

Every style says where its strands start (round a crown, along a part, along the hairline), how
they flow (gravity, a tie they are drawn to, lift for volume) and how long they are; the shell
builder turns that into clumped locks with grooves, thickness and pointed tips. Tails, buns and
braids are masses of their own. Head angles: theta from the crown (0) to under the chin (pi), phi
from the face (0) toward the avatar's left.
"""
import math

from . import head
from .hair import cap, band_cap, hairline, weights_for, jitter
from .hairmass import (Obstacles, Flow, shell, crown_roots, part_roots, hairline_roots, surface_dir, march, resample,
                       curl_bumps, strand_paths, ringlets)
from .mathutil import add, sub, mul, normalize, smoothstep, lerp, length, cross, dot, clamp
from .mesh import Mesh, ellipsoid, tube

PI = math.pi


def line(front, temple, side, back):
    return hairline(front * PI, temple * PI, side * PI, back * PI)


STANDARD = (0.30, 0.39, 0.50, 0.74)
SHORT_LINE = (0.30, 0.39, 0.50, 0.70)
LONG_LINE = (0.28, 0.39, 0.51, 0.74)


def taper_thickness(root, tip, power=1.0):
    return lambda t: root + (tip - root) * min(t, 1.0) ** power


def build(body, style):
    """(locks Mesh, cap Mesh) for a style built as a mass, or None for one that is not."""
    fn = STYLES.get(style)
    if fn is None:
        return None
    s = head.SCALE[body]
    built = fn(body, Obstacles(body), s)
    return built if len(built) == 3 else (built[0], built[1], Mesh())


def tie_band(body, tie, direction, radius):
    """A hair tie: a small rounded band round the gathered hair at a tie."""
    w = weights_for(body)
    s = head.SCALE[body]
    d = normalize(direction)
    u = normalize(cross(d, (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)))
    v = cross(d, u)
    centre = add(tie, mul(d, 0.004 * s))
    rows = []
    for i in range(9):
        a = 2.0 * PI * i / 8
        ring = []
        for k in range(20):
            b = 2.0 * PI * k / 20
            out = add(mul(u, math.cos(b)), mul(v, math.sin(b)))
            r = radius + 0.0035 * s * math.cos(a)
            ring.append(add(add(centre, mul(out, r)), mul(d, 0.0045 * s * math.sin(a))))
        rows.append(ring)
    return Mesh().grid(rows, lambda p, vv, uu: w(p))


# ----- masses other than shells -------------------------------------------------------------------

def tail_mass(ob, tie, direction, length_, radius, seed, count=12, gravity=9.0, spread=0.6, wave=None, bumps=None,
              thickness=None, converge=0.55):
    """A gathered tail from a tie: strands from a ring round the tie, fanning a little, falling
    under gravity onto the back and converging toward the end."""
    d = normalize(direction)
    u = normalize(cross(d, (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)))
    v = cross(d, u)
    core = Flow(gravity=gravity, lift=0.0, cling=0.0, body=True)
    spine = resample(march(ob, tie, d, length_, core), 24)

    def axis(p):
        best, point = 1e9, spine[0]
        for a, b in zip(spine, spine[1:]):
            ab = sub(b, a)
            t = clamp(dot(sub(p, a), ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0)
            q = add(a, mul(ab, t))
            dist = length(sub(p, q))
            if dist < best:
                best, point = dist, q
        return point
    roots = []
    for k in range(count):
        a = 2.0 * PI * (k + 0.2 * jitter(seed, k, 5)) / count
        off = add(mul(u, math.cos(a)), mul(v, math.sin(a)))
        roots.append((add(tie, mul(off, radius)), normalize(add(d, mul(off, spread))), "tail"))
    end = spine[-1]
    flow = Flow(gravity=gravity, lift=0.0, cling=0.0, body=True, attract=end, attract_gain=converge * 6.0)
    return shell(ob, roots, length_, flow, thickness or (lambda t: radius * (0.8 - 0.35 * t)), seed, axis=axis,
                 tip=0.55, tip_round=0.7, groove=0.55, wave=wave, bumps=bumps, rows=14)


def bun_mass(body, centre, radius, seed, squash=0.82, grooves=7, wind=2.2):
    """A wound bun: a rounded mass with spiral grooves where the hair wraps round."""
    w = weights_for(body)
    rows = []
    for r in range(17):
        theta = PI * r / 16
        ring = []
        for k in range(28):
            phi = 2.0 * PI * k / 28
            g = 1.0 + 0.09 * math.sin(grooves * (phi + wind * theta) + seed) * math.sin(theta) ** 0.5
            rr = radius * g
            ring.append(add(centre, (rr * math.sin(theta) * math.cos(phi), rr * math.cos(theta) * squash,
                                     rr * math.sin(theta) * math.sin(phi))))
        rows.append(ring)
    return Mesh().grid(rows, lambda p, v, u: w(p), uv_seam=True)


def braid_mass(body, start, end, width, links, seed):
    """A plait: lobes crossing from side to side down a tapering path, ending in a small tuft."""
    s = head.SCALE[body]
    w = weights_for(body)
    m = Mesh()
    d = normalize(sub(end, start))
    side = normalize(cross(d, (0.0, 0.0, 1.0)))
    front = normalize(cross(side, d))
    for k in range(links):
        t = (k + 0.5) / links
        swing = 1.0 if k % 2 else -1.0
        r = width * s * (1.0 - 0.30 * t)
        centre = add(lerp(start, end, t), mul(side, 0.28 * r * swing))
        tilt = normalize(add(d, mul(side, -0.55 * swing)))
        axes = (normalize(cross(tilt, front)), tilt, front)
        m.append(ellipsoid(centre, axes, (r * 0.62, r * 1.05, r * 0.62), 12, 10, lambda p, v, u: w(p)))
    tip = add(end, mul(d, 0.05 * s))
    m.append(tube([end, lerp(end, tip, 0.5), tip], [width * 0.45 * s, width * 0.42 * s, width * 0.08 * s], 10,
                  lambda p: w(p), round_end=True))
    return m


def cloud_mass(body, ob, line_, lift, seed, size, strength):
    """Voluminous textured hair (an afro): a rounded mass over the scalp, fullest over the crown and
    sides, tucked in at the hairline, covered in round curl clusters."""
    bumps = curl_bumps(seed, size, strength)
    w = weights_for(body)
    rows = []
    for r in range(1, 30):
        ring = []
        for k in range(64):
            phi = PI + 2.0 * PI * k / 64
            theta = line_(phi) * (r / 29) ** 0.9
            base = ob.surface(surface_dir(theta, phi), 0.0)
            dvec = normalize(sub(base, ob.c))
            fall = 1.0 - 0.88 * smoothstep(0.78, 1.0, r / 29)
            p = add(base, mul(dvec, lift * (0.12 + 0.88 * fall)))
            ring.append(add(p, mul(dvec, bumps(p) * fall)))
        rows.append(ring)
    top = ob.surface((0.0, 1.0, 0.0), lift)
    rows.insert(0, [add(top, (0.0, bumps(top), 0.0))] * 64)
    return Mesh().grid(rows, lambda p, v, u: w(p), uv_seam=True)


def fringe_roots(ob, theta, half_width, count, seed=0):
    """Roots on an arc across the top of the head at theta, from the avatar's right to its left,
    heading forward: bangs."""
    roots = []
    for k in range(count):
        phi = -half_width + 2.0 * half_width * k / (count - 1)
        p = ob.surface(surface_dir(theta, phi), 0.0)
        ahead = sub(ob.surface(surface_dir(theta + 0.05, phi * 1.05), 0.0), p)
        roots.append((p, normalize(ahead), "fringe"))
    return roots


def spikes(ob, roots_at, length_, width, seed, lean=0.55):
    """Spiky locks: each a flat, tapering lock standing out of the hair at a root and leaning back
    with the flow (a little shell of three strands side by side); roots_at is a list of head
    angles (theta, phi)."""
    out = Mesh()
    for k, (theta, phi) in enumerate(roots_at):
        p = ob.surface(surface_dir(theta, phi), 0.0)
        normal = normalize(sub(p, ob.c))
        # Back over the head: toward the crown behind, and a little up.
        back = normalize(sub(ob.surface(surface_dir(theta + 0.12, phi + (PI - abs(phi)) * 0.0 * 0.0), 0.0) if False else
                             ob.surface(surface_dir(max(theta - 0.25, 0.0), PI if abs(phi) > 0.5 * PI else 0.0), 0.0), p))
        flow_dir = normalize(add(mul(normal, 1.0), mul(normalize(add(back, (0.0, 0.25, -0.6))), lean)))
        side = normalize(cross(flow_dir, normal))
        l = length_ * (0.75 + 0.35 * (0.5 + 0.5 * jitter(seed, k, 2)))
        wk = width * (0.8 + 0.3 * (0.5 + 0.5 * jitter(seed, k, 3)))
        roots = [(add(p, mul(side, wk * (j - 1) * 0.5)), flow_dir, "spike") for j in range(3)]
        tipp = add(add(p, mul(flow_dir, l)), mul(normalize(add(back, (0.0, -0.3, 0.0))), 0.25 * l))
        flow = Flow(gravity=0.0, lift=0.0, cling=0.0, body=False, attract=tipp, attract_gain=10.0)
        out.append(shell(ob, roots, l, flow, lambda t, wk=wk: wk * 0.40 * (1.0 - 0.5 * t), seed + k, sub_columns=3, rows=8,
                         tip=1.0, notch=l * 0.9, tip_round=0.9, groove=0.9, length_jitter=0.0, closed=False,
                         axis=lambda q, p=p, n=normal: sub(p, mul(n, 0.02))))
    return out


# ----- helpers shared by styles ---------------------------------------------------------------------

def _per_root(roots, choose):
    """A per-root function from a dict keyed by where each root is ("side", "crown", "front", ...)."""
    return lambda k: choose.get(roots[k][2], choose["*"])


def _tied(body, ob, s, theta, phi, line_, seed, count=30, thick=0.0075, lift=0.022, side=None):
    """Hair drawn smoothly from the hairline all round to a tie at (theta, phi): (locks, tie point).
    `side` keeps only the hairline on one side of the head (+1 left, -1 right) as an open shell."""
    tie = head.point(body, theta, phi, lift * s)
    roots = hairline_roots(ob, line_, count, inset=0.01, seed=seed)
    closed = side is None
    if side is not None:
        # One side's hairline, in order from the face round to the back (an open shell).
        roots = [r for r in roots if side * sub(r[0], ob.c)[0] > -0.004]
    flow = Flow(gravity=0.0, attract=tie, attract_gain=40.0, lift=0.0012 * s, body=False,
                until=lambda p: length(sub(p, tie)) < 0.012 * s)
    reach = lambda k: length(sub(tie, roots[k][0])) * 1.8
    locks = shell(ob, roots, reach, flow, lambda t: thick * s * (1.0 - 0.3 * t), seed, tip=0.1, tip_round=0.3,
                  length_jitter=0.02, groove=0.7, closed=closed)
    return locks, tie


def _away(body, tie, down=1.4):
    c = head.centre(body)
    return normalize(add(normalize(sub(tie, c)), (0.0, -down, 0.0)))


def _parted(body, ob, s, start, end, count, seed, hem, thickness, line_, gravity=16.0, front_length=0.10, front_hem=0.0, **kw):
    """A parted style: strands from both sides of a part flowing down, round the face, to a hem at
    head-local height `hem` (male metres below the head centre; `front_hem` more at the face); the
    fan at the front of the part sweeps aside at the hairline."""
    roots = part_roots(ob, start, end, count, seed=seed, front_fan=0.08)
    level = lambda p: ob.c[1] + (hem - front_hem * max(0.0, normalize(sub(p, ob.c))[2])) * s
    main = Flow(gravity=gravity, lift=0.0015 * s, cling_below=0.0, avoid_face=line_, behind=90.0,
                until=lambda p: p[1] < level(p))
    front = Flow(gravity=4.0, lift=0.0015 * s, stop=lambda phi: line_(phi) * 1.02, body=False, avoid_face=line_)
    # The mass hangs from both sides of the part and round the crown; the fan at the front of the
    # part, which ends at the hairline, is a little shell of its own (in one loop with the long
    # strands, the clumps between a short and a long strand would be slivers, and gape).
    hang = [r for r in roots if r[2] != "front"]
    locks = shell(ob, hang, 1.2 * s, main, thickness, seed, closed=False, **kw)
    fan = [r for r in roots if r[2] == "front"]
    if len(fan) >= 2 and front_length > 0.03:
        fan_kw = {k: v for k, v in kw.items() if k not in ("wave", "bumps", "curl", "flick")}
        locks.append(shell(ob, fan, front_length * s, front, thickness, seed + 1, closed=False, **fan_kw))
    return locks


def _crown(body, ob, s, theta, phi, count, seed, line_, thickness, scale=1.0, gravity=4.0, **kw):
    """Short hair from a crown swirl lying on the head and ending at the hairline (scaled by `scale`)."""
    roots = crown_roots(ob, theta, phi, count, radius=0.03 * s, seed=seed)
    flow = Flow(gravity=gravity, lift=0.0012 * s, stop=lambda p_: line_(p_) * scale, body=False, cling_below=-1.0)
    # The hairline ends the strands: clumps end together there, with small notches only.
    kw.setdefault("notch", 0.012 * s)
    kw.setdefault("length_jitter", 0.02)
    return shell(ob, roots, 0.40, flow, thickness, seed, **kw)


def _fringe(body, ob, s, theta, half_width, count, end_line, seed, thickness, **kw):
    """Bangs over the forehead to end_line (theta of phi)."""
    roots = fringe_roots(ob, theta, half_width, count, seed)
    flow = Flow(gravity=3.0, lift=0.0030 * s, stop=end_line, body=False)
    return shell(ob, roots, 0.25, flow, thickness, seed, closed=False, **kw)


# ----- the styles ---------------------------------------------------------------------------------

def _short(body, ob, s):
    locks = _crown(body, ob, s, 0.16 * PI, PI, 22, 101, line(*SHORT_LINE), taper_thickness(0.012 * s, 0.002 * s, 1.4),
                   tip=0.45, tip_round=0.7)
    return locks, cap(body, line(*STANDARD), 0.0025 * s)


def _crew(body, ob, s):
    locks = _crown(body, ob, s, 0.16 * PI, PI, 20, 131, line(*SHORT_LINE), taper_thickness(0.0065 * s, 0.0015 * s, 1.2),
                   tip=0.3, tip_round=0.8, groove=0.75)
    return locks, cap(body, line(*STANDARD), 0.0022 * s)


def _buzz(body, ob, s):
    return Mesh(), cap(body, line(0.30, 0.39, 0.50, 0.72), 0.0035 * s)


def _shaved(body, ob, s):
    return Mesh(), cap(body, line(0.31, 0.39, 0.50, 0.72), 0.0022 * s, segments=72, rings=20)


def _fade(body, ob, s):
    top = line(0.30, 0.33, 0.30, 0.34)
    locks = _crown(body, ob, s, 0.10 * PI, PI, 18, 132, top, taper_thickness(0.012 * s, 0.004 * s), scale=1.06,
                   tip=0.35, tip_round=0.7)
    return locks, cap(body, line(*STANDARD), 0.0026 * s)


def _spiky(body, ob, s):
    base = _crown(body, ob, s, 0.16 * PI, PI, 20, 102, line(*SHORT_LINE), taper_thickness(0.010 * s, 0.002 * s), tip=0.5)
    # Rows of locks over the top, the front row leaning forward-up, the rest back with the flow.
    at = []
    for row, (theta, count) in enumerate(((0.30 * PI, 9), (0.22 * PI, 8), (0.13 * PI, 6), (0.05 * PI, 3))):
        for k in range(count):
            phi = -0.62 * PI + 1.24 * PI * (k + 0.5 * (row % 2)) / max(count - 1 + 0.5 * (row % 2), 1)
            at.append((theta + 0.02 * jitter(102, row, k), phi * (1.0 - 0.15 * row)))
    for k in range(8):
        at.append((0.36 * PI + 0.06 * jitter(103, k), PI * (0.62 + 0.38 * k / 7) * (1.0 if k % 2 else -1.0)))
    base.append(spikes(ob, at, 0.060 * s, 0.030 * s, 102))
    return base, cap(body, line(*STANDARD), 0.0025 * s)


def _sidepart(body, ob, s):
    std = line(*SHORT_LINE)
    roots = part_roots(ob, (0.26 * PI, 0.22 * PI), (0.12 * PI, 0.85 * PI), 26, seed=108, front_fan=0.10)
    flow = Flow(gravity=3.0, lift=0.0014 * s, stop=lambda phi: std(phi) * 1.01, body=False, cling_below=-1.0)
    locks = shell(ob, roots, 0.40, flow, taper_thickness(0.014 * s, 0.003 * s, 1.3), 108, tip=0.4, tip_round=0.7,
                  notch=0.012 * s, length_jitter=0.02)
    return locks, cap(body, line(*STANDARD), 0.0025 * s)


def _slick(body, ob, s):
    std = line(*SHORT_LINE)
    back = head.point(body, 0.62 * PI, PI, 0.0)
    roots = hairline_roots(ob, line(0.30, 0.39, 0.50, 0.99), 26, inset=0.015, seed=133)
    # The front and sides of the hairline, in order from the right temple round to the left.
    roots = [r for r in roots if abs(math.atan2(sub(r[0], ob.c)[0], sub(r[0], ob.c)[2])) < 0.80 * PI]
    roots.sort(key=lambda r: math.atan2(sub(r[0], ob.c)[0], sub(r[0], ob.c)[2]))
    flow = Flow(gravity=0.0, attract=back, attract_gain=18.0, lift=0.0012 * s, body=False,
                until=lambda p: length(sub(p, back)) < 0.03 * s)
    locks = shell(ob, roots, 0.36, flow, taper_thickness(0.009 * s, 0.004 * s), 133, closed=False, tip=0.3, groove=0.72)
    locks.append(_crown(body, ob, s, 0.34 * PI, PI, 12, 134, std, taper_thickness(0.007 * s, 0.002 * s), tip=0.4))
    return locks, cap(body, line(*STANDARD), 0.0026 * s)


def _quiff(body, ob, s):
    sides = _crown(body, ob, s, 0.22 * PI, PI, 20, 135, line(*SHORT_LINE), taper_thickness(0.007 * s, 0.002 * s), tip=0.4)
    # The front lifted off the forehead and swept back over the top: full at the front, thinning
    # into the top of the head.
    roots = fringe_roots(ob, 0.31 * PI, 0.30 * PI, 11, 135)
    back = head.point(body, 0.18 * PI, PI, 0.0)
    roots = [(p, normalize(add(d, (0.0, 2.2, -0.6))), w) for p, d, w in roots]
    flow = Flow(gravity=0.0, lift=0.002 * s, cling=0.5, cling_below=-1.0, body=False, attract=back, attract_gain=16.0)
    volume = lambda t: (0.006 + 0.030 * math.exp(-((t - 0.22) / 0.20) ** 2)) * s
    sides.append(shell(ob, roots, 0.17 * s, flow, volume, 136, closed=False, tip=0.6, notch=0.03 * s, groove=0.72,
                       tip_round=0.6))
    return sides, cap(body, line(*STANDARD), 0.0026 * s)


def _crop(body, ob, s):
    fringe_line = line(0.355, 0.39, 0.50, 0.70)
    locks = _crown(body, ob, s, 0.18 * PI, PI, 22, 137, fringe_line, taper_thickness(0.011 * s, 0.004 * s), tip=0.55,
                   tip_round=0.6, flick=0.003 * s)
    return locks, cap(body, line(*STANDARD), 0.0026 * s)


def _curlyshort(body, ob, s):
    locks = _crown(body, ob, s, 0.16 * PI, PI, 22, 138, line(*SHORT_LINE), taper_thickness(0.020 * s, 0.012 * s), tip=0.3,
                   tip_round=0.8, groove=0.85, bumps=curl_bumps(138, 0.026 * s, 0.017 * s))
    return locks, cap(body, line(*STANDARD), 0.004 * s)


def _afro(body, ob, s):
    line_ = line(0.29, 0.38, 0.52, 0.72)
    return cloud_mass(body, ob, line_, 0.066 * s, 139, 0.030 * s, 0.016 * s), cap(body, line_, 0.010 * s)


def _twists(body, ob, s):
    std = line(*STANDARD)
    # Many narrow rope-like locks from the crown, deep grooves between them, hanging a little.
    roots = crown_roots(ob, 0.14 * PI, PI, 40, radius=0.030 * s, seed=113)
    flow = Flow(gravity=10.0, lift=0.005 * s, cling_below=0.1, avoid_face=std)
    locks = shell(ob, roots, 0.17 * s, flow, taper_thickness(0.020 * s, 0.016 * s), 113, tip=0.1, tip_round=0.9,
                  groove=0.08, sub_columns=4, notch=0.02 * s, length_jitter=0.2, thick_jitter=0.25)
    return locks, cap(body, std, 0.008 * s)


def _mohawk(body, ob, s):
    std = line(*STANDARD)
    # A fin of locks standing along the middle of the head, swept back.
    roots = part_roots(ob, (0.30 * PI, 0.0), (0.46 * PI, PI), 26, width=0.012 * s, seed=140, end_fan=0.10, front_fan=0.10)
    roots = [(p, normalize(add(add(normalize(sub(p, ob.c)), mul(d, 0.30)), (0.0, 0.0, -0.35))), w) for p, d, w in roots]
    flow = Flow(gravity=-3.0, lift=0.0, cling=0.0, body=False)
    locks = shell(ob, roots, lambda k: (0.075 + 0.02 * jitter(140, k, 4)) * s, flow, taper_thickness(0.016 * s, 0.008 * s),
                  140, tip=1.0, notch=0.05 * s, tip_round=0.85, groove=0.6)
    return locks, cap(body, std, 0.0025 * s)


def _bob(body, ob, s):
    locks = _parted(body, ob, s, (0.20 * PI, 0.06 * PI), (0.08 * PI, PI * 0.95), 26, 103, -0.135,
                    taper_thickness(0.016 * s, 0.011 * s), line(*LONG_LINE), gravity=14.0, tip=0.25, tip_round=0.6,
                    curl=0.012 * s, front_hem=0.03)
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _bobbangs(body, ob, s):
    locks = _parted(body, ob, s, (0.12 * PI, 0.0), (0.08 * PI, PI * 0.95), 26, 141, -0.135,
                    taper_thickness(0.016 * s, 0.011 * s), line(*LONG_LINE), gravity=14.0, tip=0.2, tip_round=0.6,
                    curl=0.012 * s, front_length=0.02, front_hem=0.03)
    bang = line(0.40, 0.42, 0.46, 0.50)
    locks.append(_fringe(body, ob, s, 0.10 * PI, 0.36 * PI, 12, bang, 142, taper_thickness(0.013 * s, 0.009 * s), tip=0.15,
                         tip_round=0.6))
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _lob(body, ob, s):
    locks = _parted(body, ob, s, (0.20 * PI, -0.10 * PI), (0.08 * PI, PI * 0.95), 26, 144, -0.25,
                    taper_thickness(0.016 * s, 0.010 * s), line(*LONG_LINE), gravity=15.0, tip=0.35, tip_round=0.65,
                    curl=0.006 * s)
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _long(body, ob, s):
    locks = _parted(body, ob, s, (0.20 * PI, 0.04 * PI), (0.08 * PI, PI * 0.95), 26, 106, -0.62,
                    taper_thickness(0.017 * s, 0.010 * s), line(*LONG_LINE), tip=0.45, tip_round=0.7, groove=0.66)
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _longbangs(body, ob, s):
    locks = _parted(body, ob, s, (0.12 * PI, 0.0), (0.08 * PI, PI * 0.95), 26, 145, -0.62,
                    taper_thickness(0.017 * s, 0.010 * s), line(*LONG_LINE), tip=0.45, tip_round=0.7, groove=0.66,
                    front_length=0.02)
    bang = line(0.40, 0.43, 0.48, 0.50)
    locks.append(_fringe(body, ob, s, 0.10 * PI, 0.36 * PI, 12, bang, 146, taper_thickness(0.013 * s, 0.009 * s), tip=0.35,
                         tip_round=0.6))
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _longwavy(body, ob, s):
    locks = _parted(body, ob, s, (0.20 * PI, 0.12 * PI), (0.08 * PI, PI * 0.95), 26, 147, -0.62,
                    taper_thickness(0.020 * s, 0.012 * s), line(*LONG_LINE), tip=0.45, tip_round=0.7, groove=0.62,
                    wave=(0.012 * s, 0.10 * s, 0.14 * s))
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _wavy(body, ob, s):
    locks = _parted(body, ob, s, (0.20 * PI, -0.12 * PI), (0.08 * PI, PI * 0.95), 26, 148, -0.27,
                    taper_thickness(0.019 * s, 0.012 * s), line(*LONG_LINE), gravity=14.0, tip=0.4, tip_round=0.7,
                    wave=(0.010 * s, 0.09 * s, 0.12 * s))
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _layered(body, ob, s):
    under = _parted(body, ob, s, (0.20 * PI, 0.0), (0.08 * PI, PI * 0.95), 24, 149, -0.58,
                    taper_thickness(0.014 * s, 0.009 * s), line(*LONG_LINE), tip=0.5, tip_round=0.7)
    top = _parted(body, ob, s, (0.20 * PI, 0.0), (0.08 * PI, PI * 0.95), 22, 150, -0.30,
                  taper_thickness(0.010 * s, 0.006 * s), line(*LONG_LINE), tip=0.6, tip_round=0.7, flick=0.008 * s)
    # The shorter layer lies over the long one.
    c = add(head.centre(body), (0.0, -0.10, 0.0))
    top.positions = [add(p, mul(normalize(sub(p, c)), 0.004 * s)) for p in top.positions]
    under.append(top)
    return under, cap(body, line(*LONG_LINE), 0.003 * s)


def _shag(body, ob, s):
    locks = _parted(body, ob, s, (0.10 * PI, 0.0), (0.08 * PI, PI * 0.95), 26, 151, -0.20,
                    taper_thickness(0.018 * s, 0.010 * s), line(*LONG_LINE), gravity=12.0, tip=0.6, tip_round=0.6,
                    flick=0.010 * s, front_length=0.02, wave=(0.006 * s, 0.08 * s, 0.08 * s))
    bang = line(0.37, 0.42, 0.48, 0.50)
    locks.append(_fringe(body, ob, s, 0.10 * PI, 0.32 * PI, 10, bang, 152, taper_thickness(0.013 * s, 0.008 * s), tip=0.6,
                         tip_round=0.6, flick=0.004 * s))
    return locks, cap(body, line(*LONG_LINE), 0.003 * s)


def _curtain(body, ob, s):
    std = line(*LONG_LINE)
    locks = _parted(body, ob, s, (0.12 * PI, 0.0), (0.08 * PI, PI * 0.95), 26, 153, -0.30,
                    taper_thickness(0.017 * s, 0.010 * s), std, tip=0.4, tip_round=0.7, curl=-0.004 * s, front_length=0.02)
    # Curtain bangs: parted at the middle, each half swept out to its side, short at the part and
    # longer toward the cheekbones.
    for side in (1.0, -1.0):
        roots = []
        for k in range(7):
            phi = side * (0.02 + 0.30 * PI * k / 6)
            p = ob.surface(surface_dir(0.11 * PI, phi), 0.0)
            ahead = normalize(sub(ob.surface(surface_dir(0.16 * PI, phi + side * 0.05), 0.0), p))
            roots.append((p, ahead, "fringe"))

        def hem(phi, side=side):
            return PI * (0.37 + 0.20 * smoothstep(0.0, 0.40 * PI, side * phi))

        def out(p, t, side=side):
            # Toward its side as it comes down the forehead.
            v = normalize(sub(p, ob.c))
            return mul((side, 0.0, -0.3), 3.5 * smoothstep(0.15, 0.6, v[2]))
        flow = Flow(gravity=9.0, lift=0.0035 * s, stop=hem, body=False, sweep=out, cling_below=-1.0)
        locks.append(shell(ob, roots, 0.30 * s, flow, taper_thickness(0.012 * s, 0.008 * s), 164 + int(side), closed=False,
                           tip=0.6, notch=0.03 * s, tip_round=0.6, groove=0.7))
    return locks, cap(body, std, 0.003 * s)


def _curly(body, ob, s):
    face = line(0.30, 0.42, 0.50, 0.74)
    roots = crown_roots(ob, 0.12 * PI, PI, 24, radius=0.03 * s, seed=107)
    hang = Flow(gravity=10.0, lift=0.004 * s, cling_below=-0.1, avoid_face=face)
    fringe = Flow(gravity=4.0, lift=0.004 * s, stop=lambda phi: face(phi) * 1.03, body=False, cling_below=-1.0)
    flow = lambda k: fringe if roots[k][1][2] > 0.3 else hang
    locks = shell(ob, roots, 0.24 * s, flow, taper_thickness(0.024 * s, 0.016 * s), 107, tip=0.2, tip_round=0.8,
                  groove=0.8, wave=(0.008 * s, 0.05 * s, 0.05 * s), bumps=curl_bumps(107, 0.026 * s, 0.013 * s))
    return locks, cap(body, line(*LONG_LINE), 0.006 * s)


def _longcurly(body, ob, s):
    std = line(*LONG_LINE)
    roots = part_roots(ob, (0.18 * PI, 0.0), (0.08 * PI, PI * 0.95), 26, seed=154, front_fan=0.08)
    level = lambda p: ob.c[1] - 0.40 * s
    main = Flow(gravity=13.0, lift=0.0015 * s, cling_below=0.0, avoid_face=std, behind=90.0, until=lambda p: p[1] < level(p))
    front = Flow(gravity=4.0, lift=0.0015 * s, stop=lambda phi: std(phi) * 1.02, body=False, avoid_face=std)
    flows = _per_root(roots, {"front": front, "*": main})
    lengths = _per_root(roots, {"front": 0.10 * s, "*": 1.2 * s})
    paths = strand_paths(ob, roots, lengths, flows, 154)
    # A soft wavy mass, and spiral ringlets hanging over it from about the ears down.
    locks = shell(ob, roots, lengths, flows, taper_thickness(0.022 * s, 0.014 * s), 154, tip=0.3, tip_round=0.8, groove=0.6,
                  wave=(0.008 * s, 0.06 * s, 0.06 * s), paths=paths, bumps=curl_bumps(154, 0.024 * s, 0.006 * s))
    locks.append(ringlets(ob, paths, 155, start=0.14 * s, radius=0.010 * s, pitch=0.036 * s, thick=0.0075 * s,
                          which=lambda k: roots[k][2] != "front", lift=0.014 * s))
    return locks, cap(body, std, 0.006 * s)


def _ponytail(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.42 * PI, PI, std, 104)
    locks.append(tail_mass(ob, tie, _away(body, tie), 0.24 * s, 0.016 * s, 104))
    return locks, cap(body, std, 0.003 * s), tie_band(body, tie, _away(body, tie), 0.017 * s)


def _lowponytail(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.56 * PI, PI, std, 155)
    locks.append(tail_mass(ob, tie, _away(body, tie, 2.5), 0.26 * s, 0.016 * s, 155))
    return locks, cap(body, std, 0.003 * s), tie_band(body, tie, _away(body, tie, 2.5), 0.017 * s)


def _highponytail(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.30 * PI, PI, std, 156)
    locks.append(tail_mass(ob, tie, _away(body, tie, 0.5), 0.28 * s, 0.017 * s, 156, gravity=11.0, spread=0.35))
    return locks, cap(body, std, 0.003 * s), tie_band(body, tie, _away(body, tie, 0.5), 0.018 * s)


def _sideponytail(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.52 * PI, 0.62 * PI, std, 157)
    locks.append(tail_mass(ob, tie, _away(body, tie, 1.6), 0.24 * s, 0.016 * s, 157))
    return locks, cap(body, std, 0.003 * s), tie_band(body, tie, _away(body, tie, 1.6), 0.017 * s)


def _bun(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.22 * PI, PI, std, 109, lift=0.030)
    locks.append(bun_mass(body, head.point(body, 0.22 * PI, PI, 0.038 * s), 0.044 * s, 1.0))
    return locks, cap(body, std, 0.003 * s)


def _topknot(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.05 * PI, PI, std, 158, lift=0.030)
    locks.append(bun_mass(body, head.point(body, 0.05 * PI, PI, 0.040 * s), 0.036 * s, 2.0, squash=0.9))
    return locks, cap(body, std, 0.003 * s)


def _braid(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, tie = _tied(body, ob, s, 0.56 * PI, PI, std, 159, lift=0.016)
    end = add(tie, (0.0, -0.30 * s, -0.035 * s))
    locks.append(braid_mass(body, tie, end, 0.030, 10, 159))
    return locks, cap(body, std, 0.003 * s), tie_band(body, end, sub(end, tie), 0.013 * s)


def _halfup(body, ob, s):
    long_ = _parted(body, ob, s, (0.20 * PI, 0.0), (0.08 * PI, PI * 0.95), 24, 160, -0.58,
                    taper_thickness(0.014 * s, 0.009 * s), line(*LONG_LINE), tip=0.45, tip_round=0.7)
    tie = head.point(body, 0.20 * PI, PI, 0.032 * s)
    roots = hairline_roots(ob, line(0.30, 0.35, 0.36, 0.25), 18, inset=0.0, seed=161)
    roots = [r for r in roots if normalize(sub(r[0], ob.c))[2] > -0.2]
    roots.sort(key=lambda r: math.atan2(sub(r[0], ob.c)[0], sub(r[0], ob.c)[2]))
    flow = Flow(gravity=0.0, attract=tie, attract_gain=30.0, lift=0.006 * s, body=False,
                until=lambda p: length(sub(p, tie)) < 0.012 * s)
    long_.append(shell(ob, roots, lambda k: length(sub(tie, roots[k][0])) * 1.8, flow, lambda t: 0.008 * s, 161, closed=False,
                       tip=0.1, tip_round=0.3, groove=0.7, length_jitter=0.02))
    long_.append(bun_mass(body, head.point(body, 0.20 * PI, PI, 0.036 * s), 0.028 * s, 3.0))
    return long_, cap(body, line(*LONG_LINE), 0.003 * s)


def _pigtails(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks, bands = Mesh(), Mesh()
    for side in (1.0, -1.0):
        half, tie = _tied(body, ob, s, 0.47 * PI, side * 0.64 * PI, std, 110 + int(side), count=34, side=side)
        locks.append(half)
        direction = normalize((side * 0.6, -1.0, -0.1))
        locks.append(tail_mass(ob, tie, direction, 0.19 * s, 0.014 * s, 111 + int(side)))
        bands.append(tie_band(body, tie, direction, 0.015 * s))
    return locks, cap(body, std, 0.003 * s), bands


def _spacebuns(body, ob, s):
    std = line(0.30, 0.39, 0.50, 0.72)
    locks = Mesh()
    for side in (1.0, -1.0):
        half, tie = _tied(body, ob, s, 0.16 * PI, side * 0.45 * PI, std, 162 + int(side), count=34, lift=0.030, side=side)
        locks.append(half)
        locks.append(bun_mass(body, head.point(body, 0.16 * PI, side * 0.45 * PI, 0.034 * s), 0.034 * s, 4.0 + side))
    return locks, cap(body, std, 0.003 * s)


def _balding(body, ob, s):
    def lo(phi):
        a = abs(((phi + PI) % (2 * PI)) - PI)
        return PI * (0.44 - 0.11 * smoothstep(0.42 * PI, 0.62 * PI, a) - 0.03 * smoothstep(0.62 * PI, PI, a))

    def hi(phi):
        a = abs(((phi + PI) % (2 * PI)) - PI)
        return PI * (0.46 + 0.06 * smoothstep(0.42 * PI, 0.62 * PI, a) + 0.22 * smoothstep(0.62 * PI, PI, a))
    # Short hair round the sides and back below a horseshoe line, combed down to the hairline.
    roots = []
    for k in range(22):
        phi = 0.40 * PI + 1.20 * PI * k / 21
        theta = lo(phi) - 0.02
        p = ob.surface(surface_dir(theta, phi), 0.0)
        down = normalize(sub(ob.surface(surface_dir(theta + 0.05, phi), 0.0), p))
        roots.append((p, down, "band"))
    flow = Flow(gravity=4.0, lift=0.0012 * s, stop=lambda phi: hi(phi), body=False, cling_below=-1.0)
    locks = shell(ob, roots, 0.30, flow, lambda t: 0.006 * s * (1.0 - 0.6 * t), 163, closed=False, tip=0.4, notch=0.015 * s,
                  groove=0.75, tip_round=0.7)
    return locks, band_cap(body, lo, hi, 0.0025 * s, 0.40 * PI, 1.60 * PI)


STYLES = {
    "hair_short": _short, "hair_spiky": _spiky, "hair_bob": _bob, "hair_ponytail": _ponytail, "hair_buzz": _buzz,
    "hair_long": _long, "hair_curly": _curly, "hair_sidepart": _sidepart, "hair_bun": _bun, "hair_pigtails": _pigtails,
    "hair_crew": _crew, "hair_fade": _fade, "hair_shaved": _shaved, "hair_balding": _balding, "hair_quiff": _quiff,
    "hair_slick": _slick, "hair_crop": _crop, "hair_curlyshort": _curlyshort, "hair_afro": _afro, "hair_twists": _twists,
    "hair_shag": _shag, "hair_wavy": _wavy, "hair_bobbangs": _bobbangs, "hair_lob": _lob, "hair_curtain": _curtain,
    "hair_mohawk": _mohawk, "hair_topknot": _topknot, "hair_halfup": _halfup, "hair_longbangs": _longbangs,
    "hair_longwavy": _longwavy, "hair_layered": _layered, "hair_longcurly": _longcurly, "hair_braid": _braid,
    "hair_lowponytail": _lowponytail, "hair_highponytail": _highponytail, "hair_sideponytail": _sideponytail,
    "hair_spacebuns": _spacebuns,
}
