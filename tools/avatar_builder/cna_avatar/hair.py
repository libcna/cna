# SPDX-License-Identifier: MS-PL
"""Catalog v3 hair: a scalp cap inside the style's hairline, covered by sculpted locks -- few,
broad clumps so a style reads as one shaped mass.

A lock is a flattened, tapering tube along a path that follows the head (waypoints in the head's
(theta, phi) angles with a lift above the surface) and may continue through head-local space
(long hair, tails). u runs around a lock with 0 at its outer crest, v from root to tip, matching
textures.hair_clump. Randomness comes from a fixed integer hash, so output is reproducible.
"""
import math

from . import rig, head
from .mathutil import add, sub, mul, normalize, cross, dot, length, lerp, smoothstep, clamp
from .mesh import Mesh, ellipsoid, tube

I = rig.INDEX
PI = math.pi


def jitter(*keys):
    """Deterministic value in [-1, 1] for a tuple of small integers."""
    h = 2166136261
    for k in keys:
        h = ((h ^ (int(k) & 0xffffffff)) * 16777619) & 0xffffffff
    h ^= h >> 15
    h = (h * 2246822519) & 0xffffffff
    h ^= h >> 13
    return (h & 0xffff) / 32767.5 - 1.0


def hairline(front, temple, side, back, wrap=(0.56, 0.70)):
    """theta_max(phi): the lowest angle of the cap at the face (phi 0), temples, sides and nape.
    Behind the ear a hairline wraps down to the nape at once (`wrap`: from where to where, in
    turns of pi), as a real one does, so no bare scalp shows between the ear and the back of the
    head; a hat's edge passes round more gradually."""
    def fn(phi):
        a = abs(((phi + PI) % (2 * PI)) - PI)
        if a < 0.30 * PI:
            return lerp((front, 0, 0), (temple, 0, 0), smoothstep(0.0, 0.30 * PI, a))[0]
        if a < 0.55 * PI:
            return lerp((temple, 0, 0), (side, 0, 0), smoothstep(0.30 * PI, 0.55 * PI, a))[0]
        return lerp((side, 0, 0), (back, 0, 0), smoothstep(wrap[0] * PI, wrap[1] * PI, a))[0]
    return fn


def weights_for(body):
    """Head for everything on the head; hanging hair below the jaw follows the neck and back."""
    p = rig.PROPORTIONS[body]
    head_y, neck_y, upper_y = p["head_joint"], p["neck"], p["back_upper"]

    def fn(point):
        y = point[1]
        if y >= head_y - 0.02:
            return {I["Head"]: 1.0}
        k = smoothstep(head_y - 0.02, neck_y - 0.04, y)
        w = {I["Head"]: 1.0 - k, I["Neck"]: k}
        if y < neck_y - 0.04:
            b = smoothstep(neck_y - 0.04, upper_y + 0.10, y)
            w = {j: v * (1.0 - b) for j, v in w.items()}
            w[I["BackUpper"]] = w.get(I["BackUpper"], 0.0) + b
        return w
    return fn


def cap(body, theta_max, lift, bulge=lambda theta, phi: 0.0, segments=36, rings=10):
    """The scalp shell down to theta_max(phi), its rim tucked in so the edge never shows."""
    rows = []
    for r in range(rings + 2):
        ring = []
        for k in range(segments):
            phi = PI + 2.0 * PI * k / segments
            if r <= rings:
                theta = theta_max(phi) * (0.02 + 0.98 * r / rings)
                extra = lift + bulge(theta, phi)
            else:
                theta, extra = theta_max(phi), lift * 0.15
            ring.append(head.point(body, theta, phi, extra))
        rows.append(ring)
    rows.insert(0, [head.point(body, 0.0, 0.0, lift + bulge(0.0, 0.0))] * segments)
    w = weights_for(body)
    return Mesh().grid(rows, lambda p, v, u: w(p), uv_seam=True)


def _catmull(points, steps):
    out = []
    pts = [points[0]] + list(points) + [points[-1]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for s in range(steps):
            t = s / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                                    (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(len(p1))))
    out.append(tuple(points[-1]))
    return out


def surface_path(body, waypoints, steps=4):
    """Waypoints are ('s', theta, phi, lift) on the head or ('w', x, y, z) head-local points."""
    c = head.centre(body)
    pts = []
    for w in waypoints:
        if w[0] == "s":
            pts.append(sub(head.point(body, w[1], w[2], w[3]), c))
        else:
            pts.append(mul(w[1:], head.SCALE[body]))
    return [add(c, p) for p in _catmull(pts, steps)]


def lock(body, path, width, thickness, segments=6, twist=0.0):
    """A flattened tapering tube along `path`; width/thickness are functions of t in [0, 1]."""
    c = head.centre(body)
    n = len(path)
    rows = []
    for i, p in enumerate(path):
        t = i / (n - 1)
        tangent = normalize(sub(path[min(i + 1, n - 1)], path[max(i - 1, 0)]))
        radial = normalize(sub(p, add(c, (0.0, -0.05, 0.0))))
        normal = normalize(sub(radial, mul(tangent, dot(radial, tangent))))
        binormal = cross(tangent, normal)
        if twist:
            a = twist * t
            normal, binormal = (add(mul(normal, math.cos(a)), mul(binormal, math.sin(a))),
                                add(mul(binormal, math.cos(a)), mul(normal, -math.sin(a))))
        w, th = width(t), thickness(t)
        rows.append([add(p, add(mul(normal, th * math.cos(2 * PI * k / segments)), mul(binormal, w * math.sin(2 * PI * k / segments))))
                     for k in range(segments)])
    tip = add(path[-1], mul(normalize(sub(path[-1], path[-2])), thickness(1.0)))
    rows = [[path[0]] * segments] + rows + [[tip] * segments]
    wf = weights_for(body)
    return Mesh().grid(rows, lambda q, v, u: wf(q), uv_seam=True)


# Catalog v3 clumps: a fifth fewer locks, each a third broader and a little thicker.
CLUMP_COUNT, CLUMP_WIDTH, CLUMP_THICKNESS = 0.8, 1.35, 1.15


def taper(w0, tip=0.12, power=1.6, swell=0.25):
    """Lock profile: full near the root, a little fuller at a third, narrowing to `tip`."""
    return lambda t: w0 * (1.0 + swell * math.sin(PI * min(t * 1.5, 1.0))) * (tip + (1.0 - tip) * (1.0 - t ** power))


def flow_locks(body, count, rows, theta_end, lift, width, thickness, seed, forward=0.0, curl=0.0, spread=0.9,
               start=(0.04, 0.22), phi_center=0.0, phi_range=PI):
    """Locks whose roots circle the crown and which flow outward and down to theta_end(phi)."""
    out = Mesh()
    count = max(4, int(round(count * CLUMP_COUNT)))
    width, thickness = width * CLUMP_WIDTH, thickness * CLUMP_THICKNESS
    for r in range(rows):
        for k in range(count):
            key = (seed, r, k)
            phi = phi_center + phi_range * ((k + 0.5 * (r % 2)) / count * 2.0 - 1.0) + 0.06 * jitter(*key, 1)
            theta0 = start[0] + (start[1] - start[0]) * r / max(rows - 1, 1) + 0.02 * jitter(*key, 2)
            end = theta_end(phi) * (1.0 + 0.04 * jitter(*key, 3))
            drift = forward * math.cos(phi) + 0.10 * jitter(*key, 4)
            mid_lift = lift * (1.25 + 0.2 * jitter(*key, 5))
            way = [("s", theta0, phi, lift * 0.4),
                   ("s", lerp((theta0, 0, 0), (end, 0, 0), 0.45)[0], phi + drift * 0.4, mid_lift),
                   ("s", end, phi + drift + curl, lift * 0.85)]
            path = surface_path(body, way, steps=3)
            w = width * (1.0 + 0.15 * jitter(*key, 6)) * spread
            out.append(lock(body, path, taper(w), taper(thickness, 0.25)))
    return out


def spikes(body, count, theta_range, lift, length_, width, seed):
    out = Mesh()
    c = head.centre(body)
    for k in range(count):
        key = (seed, k)
        golden = k * 2.399963
        theta = theta_range[0] + (theta_range[1] - theta_range[0]) * math.sqrt((k + 0.5) / count)
        phi = golden + 0.1 * jitter(*key, 1)
        root = head.point(body, theta, phi, lift * 0.5)
        radial = normalize(sub(root, add(c, (0.0, -0.08, 0.03))))
        direction = normalize(add(radial, (0.0, 0.35, -0.25)))
        l = length_ * (0.8 + 0.3 * jitter(*key, 2))
        mid = add(root, mul(direction, l * 0.5))
        tip = add(root, add(mul(direction, l), (0.0, -0.01, -0.01)))
        path = _catmull([root, mid, tip], 4)
        out.append(lock(body, path, taper(width, 0.05, 1.2, 0.1), taper(width * 0.6, 0.05, 1.2, 0.1), segments=7))
    return out


def ringlets(body, count, theta_max, lift, size, seed):
    """Short coiled curls spread evenly over the scalp, each hanging downhill from its root."""
    out = Mesh()
    w = weights_for(body)
    c = head.centre(body)
    for k in range(count):
        key = (seed, k)
        phi = k * 2.399963
        f = (k + 0.5) / count
        theta = math.acos(1.0 - f * (1.0 - math.cos(theta_max(phi)))) * 0.98
        r = size * (0.85 + 0.25 * jitter(*key, 1))
        base = head.point(body, theta, phi, lift + 0.004 * jitter(*key, 2))
        normal = normalize(sub(base, c))
        # Downhill along the scalp; at the crown, any direction.
        downhill = sub(head.point(body, min(theta + 0.08, PI), phi + 0.3 * jitter(*key, 3), lift), base)
        down = normalize(sub(downhill, mul(normal, dot(downhill, normal))))
        side = cross(normal, down)
        turns = 1.5 + 0.35 * jitter(*key, 4)
        phase = 2.0 * PI * (0.5 + 0.5 * jitter(*key, 5))
        coil, span, steps = r * 0.42, r * (1.3 + 0.3 * jitter(*key, 6)), 7
        path, radii = [], []
        for i in range(steps + 1):
            t = i / steps
            a = phase + 2.0 * PI * turns * t
            path.append(add(add(base, mul(down, span * (t - 0.25))),
                            add(mul(normal, coil * (1.0 + 0.9 * math.cos(a))), mul(side, coil * math.sin(a)))))
            radii.append(r * 0.5 * (1.0 - 0.3 * t))
        out.append(tube(path, radii, 5, lambda p: w(p), round_end=True))
    return out


def hanging(body, root_angles, drop_to, lift, width, thickness, seed, count, spread_phi, sway=0.0, inward=0.3):
    """Longer locks: over the head to the jaw, then hanging to head-local height drop_to."""
    out = Mesh()
    s = head.SCALE[body]
    count = max(4, int(round(count * CLUMP_COUNT)))
    width, thickness = width * CLUMP_WIDTH, thickness * CLUMP_THICKNESS
    for k in range(count):
        key = (seed, k)
        phi = root_angles[1] + spread_phi * ((k + 0.5) / count * 2.0 - 1.0) + 0.04 * jitter(*key, 1)
        side = head.point(body, 0.66 * PI, phi, lift * 1.3)
        c = head.centre(body)
        local = sub(side, c)
        out_dir = normalize((local[0], 0.0, local[2]))
        bottom = add(mul(out_dir, (0.13 + 0.02 * jitter(*key, 2)) * s * (1.0 - inward * 0.3)),
                     (0.0, drop_to * s * (1.0 + 0.05 * jitter(*key, 3)), sway * s))
        way = [("s", root_angles[0] + 0.03 * jitter(*key, 4), phi * 0.7, lift * 0.5),
               ("s", 0.40 * PI, phi, lift * 1.4),
               ("s", 0.62 * PI, phi, lift * 1.5),
               ("w",) + tuple(mul(add(mul(local, 1.0 / s), (0.0, -0.06, 0.0)), 1.0)),
               ("w",) + tuple(mul(bottom, 1.0 / s))]
        path = surface_path(body, way, steps=4)
        out.append(lock(body, path, taper(width * (1.0 + 0.1 * jitter(*key, 5)), 0.2, 1.4, 0.15),
                        taper(thickness, 0.3)))
    return out


def band_cap(body, theta_min, theta_max, lift, phi_from, phi_to, segments=36, rings=8):
    """A strip of scalp between two hairlines over phi_from..phi_to (through the back): a balding
    pattern's horseshoe."""
    rows = []
    for r in range(rings + 1):
        ring = []
        for k in range(segments + 1):
            phi = phi_from + (phi_to - phi_from) * k / segments
            lo, hi = theta_min(phi), theta_max(phi)
            theta = lo + (hi - lo) * r / rings
            edge = min(r, rings - r) / (rings * 0.5)
            ends = min(k, segments - k) / 3.0
            ring.append(head.point(body, theta, phi, lift * (0.25 + 0.75 * min(1.0, edge * 2.0, ends))))
        rows.append(ring)
    w = weights_for(body)
    m = Mesh()
    m.patch_origin = head.centre(body)
    return m.grid(rows, lambda p, v, u: w(p), wrap=False)


def waved(path, amplitude, waves, phase, body):
    """A lock path with a lateral wave growing from root to tip (wavy hair)."""
    c = head.centre(body)
    out = []
    n = len(path)
    for i, p in enumerate(path):
        t = i / (n - 1)
        tangent = normalize(sub(path[min(i + 1, n - 1)], path[max(i - 1, 0)]))
        radial = normalize(sub(p, c))
        side = normalize(cross(tangent, radial))
        out.append(add(p, mul(side, amplitude * smoothstep(0.1, 0.5, t) * math.sin(phase + 2.0 * PI * waves * t))))
    return out


def flowing(body, count, rows, theta_end, lift, width, thickness, seed, forward=0.0, curl=0.0, start=(0.04, 0.22),
            phi_center=0.0, phi_range=PI, wave=0.0, waves=1.5, tip=0.12):
    """flow_locks with optional waves; locks circle the crown and flow out and down to theta_end(phi)."""
    out = Mesh()
    count = max(4, int(round(count * CLUMP_COUNT)))
    width, thickness = width * CLUMP_WIDTH, thickness * CLUMP_THICKNESS
    for r in range(rows):
        for k in range(count):
            key = (seed, r, k)
            phi = phi_center + phi_range * ((k + 0.5 * (r % 2)) / count * 2.0 - 1.0) + 0.06 * jitter(*key, 1)
            theta0 = start[0] + (start[1] - start[0]) * r / max(rows - 1, 1) + 0.02 * jitter(*key, 2)
            end = theta_end(phi) * (1.0 + 0.04 * jitter(*key, 3))
            drift = forward * math.cos(phi) + 0.10 * jitter(*key, 4)
            mid_lift = lift * (1.25 + 0.2 * jitter(*key, 5))
            way = [("s", theta0, phi, lift * 0.4),
                   ("s", lerp((theta0, 0, 0), (end, 0, 0), 0.45)[0], phi + drift * 0.4, mid_lift),
                   ("s", end, phi + drift + curl, lift * 0.85)]
            path = surface_path(body, way, steps=4 if wave else 3)
            if wave:
                path = waved(path, wave * head.SCALE[body], waves, 2.0 * PI * (0.5 + 0.5 * jitter(*key, 7)), body)
            w = width * (1.0 + 0.15 * jitter(*key, 6))
            out.append(lock(body, path, taper(w, tip), taper(thickness, 0.25)))
    return out


def falling(body, count, phi_center, phi_spread, drop_to, lift, width, thickness, seed, wave=0.0, waves=2.0, out_k=1.0,
            root_theta=0.10 * PI, front=False, tip=0.6):
    """Long locks from the crown over the side or back of the head, hanging to head-local height
    drop_to (male-scale metres below the head centre), optionally waved."""
    out = Mesh()
    s = head.SCALE[body]
    c = head.centre(body)
    count = max(3, int(round(count * CLUMP_COUNT)))
    # Broad, gently tapering locks that overlap into one sheet of hair.
    width, thickness = width * CLUMP_WIDTH * 1.45 * s, thickness * CLUMP_THICKNESS * s
    for k in range(count):
        key = (seed, k)
        phi = phi_center + phi_spread * ((k + 0.5) / count * 2.0 - 1.0) + 0.04 * jitter(*key, 1)
        side = head.point(body, 0.64 * PI, phi, lift * 1.3)
        local = sub(side, c)
        out_dir = normalize((local[0], 0.0, local[2]))
        bottom = add(mul(out_dir, (0.125 + 0.015 * jitter(*key, 2)) * s * out_k),
                     (0.0, drop_to * s * (1.0 + 0.04 * jitter(*key, 3)), 0.0))
        way = [("s", root_theta + 0.03 * jitter(*key, 4), phi * (0.5 if front else 0.75), lift * 0.5),
               ("s", 0.38 * PI, phi, lift * 1.4),
               ("s", 0.60 * PI, phi, lift * 1.5),
               ("w",) + tuple(mul(add(local, (0.0, -0.05 * s, 0.0)), 1.0 / s)),
               ("w",) + tuple(mul(bottom, 1.0 / s))]
        path = surface_path(body, way, steps=4 if wave else 3)
        if wave:
            path = waved(path, wave * s, waves, 2.0 * PI * (0.5 + 0.5 * jitter(*key, 6)), body)
        out.append(lock(body, path, taper(width * (1.0 + 0.1 * jitter(*key, 5)), tip, 1.6, 0.10), taper(thickness, 0.45)))
    return out


def fringe(body, count, width_phi, end_theta, lift, width, thickness, seed, side_sweep=0.0, centre_part=False):
    """Bangs: locks from the front of the crown falling forward over the forehead to end_theta."""
    out = Mesh()
    s = head.SCALE[body]
    for k in range(count):
        key = (seed, k)
        u = (k + 0.5) / count * 2.0 - 1.0
        phi = width_phi * u
        sweep = side_sweep + (0.30 * (1.0 if u > 0 else -1.0) * (0.4 + 0.6 * abs(u)) if centre_part else 0.0)
        way = [("s", 0.09 * PI + 0.02 * jitter(*key, 1), phi * 0.6, lift * 0.5),
               ("s", 0.24 * PI, phi + sweep * 0.4, lift * 1.5),
               ("s", end_theta * (1.0 + 0.03 * jitter(*key, 2)) - (0.05 * PI * abs(u) if centre_part else 0.0),
                phi + sweep, lift * 1.15)]
        path = surface_path(body, way, steps=4)
        out.append(lock(body, path, taper(width * s * (1.0 + 0.12 * jitter(*key, 3)), 0.30, 1.6, 0.2), taper(thickness * s, 0.4)))
    return out


def tail(body, tie, direction, length, width, count, seed, wave=0.0, spread=1.0):
    """A gathered tail from a tie point (ponytail): locks fanning a little and tapering."""
    out = Mesh()
    s = head.SCALE[body]
    w = weights_for(body)
    out.append(ellipsoid(tie, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.020 * s, 0.020 * s, 0.018 * s), 10, 8, lambda p, v, u: w(p)))
    d = normalize(direction)
    side = normalize(cross(d, (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)))
    up = cross(side, d)
    for k in range(count):
        a = 2.0 * PI * k / count
        off = add(mul(side, 0.012 * s * math.cos(a)), mul(up, 0.012 * s * math.sin(a)))
        pts = [add(tie, off)]
        for t in (0.25, 0.6, 1.0):
            sag = (0.0, -0.06 * s * t * t, 0.0)
            pts.append(add(add(add(tie, mul(d, length * s * t)), mul(off, 1.0 + 0.8 * spread * t)), sag))
        path = _catmull(pts, 4)
        if wave:
            path = waved(path, wave * s, 1.5, a, body)
        out.append(lock(body, path, taper(width * s, 0.15, 1.3, 0.4), taper(width * s * 0.7, 0.2)))
    return out


def bun(body, centre, size, seed):
    """A wound bun: a ball with a few wrapped strands."""
    s = head.SCALE[body]
    w = weights_for(body)
    m = ellipsoid(centre, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (size * s, size * 0.88 * s, size * 0.92 * s), 16, 12, lambda p, v, u: w(p))
    for k in range(5):
        a = 2.0 * PI * k / 5 + 0.3 * jitter(seed, k)
        ring = [add(centre, (size * 1.04 * s * math.cos(a + b * 0.5), size * 0.88 * s * math.sin(b * 0.9) * 0.6,
                             size * 0.96 * s * math.sin(a + b * 0.5))) for b in range(7)]
        m.append(tube(ring, [size * 0.24 * s] * len(ring), 7, lambda p: w(p), round_end=True))
    return m


def braid(body, start, end, width, links, seed):
    """A plaited braid: alternating lobes along a path, ending in a small tuft."""
    s = head.SCALE[body]
    w = weights_for(body)
    m = Mesh()
    d = normalize(sub(end, start))
    side = normalize(cross(d, (0.0, 0.0, 1.0)))
    for k in range(links):
        t = (k + 0.5) / links
        centre = add(lerp(start, end, t), mul(side, 0.30 * width * s * (1.0 if k % 2 else -1.0)))
        r = width * s * (1.0 - 0.35 * t)
        axes = (side, d, normalize(cross(side, d)))
        m.append(ellipsoid(centre, axes, (r * 0.9, r * 1.25, r * 0.75), 10, 8, lambda p, v, u: w(p)))
    tip = add(end, mul(d, 0.025 * s))
    m.append(lock(body, [end, lerp(end, tip, 0.5), tip], taper(width * 0.7 * s, 0.2), taper(width * 0.5 * s, 0.3)))
    return m


def cloud(body, theta_max, lift, bumps, seed, segments=40, rings=18):
    """Voluminous textured hair (an afro): a rounded mass around the head, gently lumpy."""
    c = head.centre(body)
    rows = []
    for r in range(rings + 1):
        ring = []
        for k in range(segments):
            phi = PI + 2.0 * PI * k / segments
            theta = theta_max(phi) * r / rings
            base = head.point(body, theta, phi, 0.0)
            d = normalize(sub(base, c))
            # Fullest over the crown and sides, tucked in at the hairline.
            fall = 1.0 - 0.85 * smoothstep(0.80, 1.0, r / rings)
            lump = 1.0 + bumps * (0.4 * math.sin(9.0 * phi + 4.0 * theta + seed) + 0.35 * math.sin(13.0 * theta - 7.0 * phi) +
                                  0.25 * math.sin(17.0 * phi - 11.0 * theta + 2.0 * seed))
            ring.append(add(base, mul(d, lift * (0.15 + 0.85 * fall) * lump)))
        rows.append(ring)
    rows.insert(0, [head.point(body, 0.0, 0.0, lift * (1.0 + bumps))] * segments)
    w = weights_for(body)
    return Mesh().grid(rows, lambda p, v, u: w(p), uv_seam=True)


def twists(body, count, theta_max, lift, length, width, seed):
    """Short twisted locks standing out from the scalp."""
    out = Mesh()
    c = head.centre(body)
    for k in range(count):
        key = (seed, k)
        phi = k * 2.399963
        f = (k + 0.5) / count
        theta = math.acos(1.0 - f * (1.0 - math.cos(theta_max(phi)))) * 0.97
        root = head.point(body, theta, phi, lift * 0.4)
        d = normalize(add(normalize(sub(root, c)), (0.0, -0.9 * theta / PI - 0.15, 0.0)))
        l = length * (0.8 + 0.25 * jitter(*key, 1))
        path = [root, add(root, mul(d, l * 0.5)), add(add(root, mul(d, l)), (0.0, -0.004, 0.0))]
        out.append(lock(body, _catmull(path, 3), taper(width, 0.5, 1.0, 0.1), taper(width * 0.8, 0.5, 1.0, 0.1),
                        segments=5, twist=4.0))
    return out


def swept_back(body, line, theta, phi, count=16):
    """Hair drawn smoothly from the hairline all round to a tie at (theta, phi)."""
    s = head.SCALE[body]
    locks = Mesh()
    for k in range(count):
        ph = 2.0 * PI * k / count
        way = [("s", 0.29 * PI if abs(((ph + PI) % (2 * PI)) - PI) < 0.35 * PI else line(ph) * 0.95, ph, 0.006 * s),
               ("s", 0.5 * (theta + 0.15 * PI), ph + (phi - ph) * 0.4, 0.011 * s), ("s", theta, phi, 0.015 * s)]
        locks.append(lock(body, surface_path(body, way, 5), taper(0.030 * s, 0.4), taper(0.008 * s, 0.5)))
    return locks


def _side_line(front, temple, side, back):
    return hairline(front * PI, temple * PI, side * PI, back * PI)


def build_revised(body, style):
    """The styles added in catalog v3 revision 2: (locks Mesh, cap Mesh)."""
    s = head.SCALE[body]
    std = _side_line(0.30, 0.39, 0.50, 0.74)
    if style == "hair_crew":
        return flowing(body, 18, 3, lambda phi: std(phi) * 0.97, 0.006 * s, 0.026 * s, 0.008 * s, 31, forward=0.06,
                       start=(0.03, 0.30), tip=0.25), cap(body, std, 0.006 * s)
    if style == "hair_fade":
        top = _side_line(0.30, 0.34, 0.30, 0.34)
        locks = flowing(body, 14, 3, lambda phi: top(phi) * 1.05, 0.010 * s, 0.030 * s, 0.010 * s, 32, forward=0.12,
                        start=(0.02, 0.18), phi_range=0.85 * PI)
        return locks, cap(body, std, 0.0028 * s)
    if style == "hair_shaved":
        # A cap this close to the head needs fine facets, or the head shows between them.
        return Mesh(), cap(body, _side_line(0.31, 0.39, 0.50, 0.72), 0.0022 * s, segments=72, rings=20)
    if style == "hair_balding":
        def lo(phi):
            a = abs(((phi + PI) % (2 * PI)) - PI)
            return PI * (0.44 - 0.11 * smoothstep(0.42 * PI, 0.62 * PI, a) - 0.03 * smoothstep(0.62 * PI, PI, a))
        def hi(phi):
            a = abs(((phi + PI) % (2 * PI)) - PI)
            return PI * (0.46 + 0.06 * smoothstep(0.42 * PI, 0.62 * PI, a) + 0.22 * smoothstep(0.62 * PI, PI, a))
        return Mesh(), band_cap(body, lo, hi, 0.004 * s, 0.40 * PI, 1.60 * PI)
    if style == "hair_quiff":
        sides = cap(body, std, 0.005 * s)
        locks = flowing(body, 12, 2, _side_line(0.22, 0.30, 0.36, 0.45), 0.024 * s, 0.034 * s, 0.012 * s, 33,
                        forward=-0.05, start=(0.20, 0.10), phi_range=0.45 * PI)
        locks.append(flowing(body, 14, 2, lambda phi: std(phi) * 0.98, 0.008 * s, 0.030 * s, 0.009 * s, 34,
                             start=(0.10, 0.30), phi_center=PI, phi_range=0.75 * PI))
        return locks, sides
    if style == "hair_slick":
        locks = Mesh()
        for k in range(16):
            phi = -0.75 * PI + 1.5 * PI * (k + 0.5) / 16
            # Roots along the front and sides, never below the top of the ear.
            way = [("s", min(std(phi) * 0.92, 0.48 * PI), phi * 0.9, 0.004 * s), ("s", 0.18 * PI, phi * 0.6, 0.012 * s),
                   ("s", 0.45 * PI, PI if phi >= 0 else -PI, 0.008 * s)]
            locks.append(lock(body, surface_path(body, way, 4), taper(0.028 * s, 0.4), taper(0.007 * s, 0.4)))
        return locks, cap(body, std, 0.007 * s)
    if style == "hair_crop":
        locks = flowing(body, 16, 3, lambda phi: std(phi), 0.010 * s, 0.028 * s, 0.010 * s, 35, forward=0.22,
                        start=(0.03, 0.24), tip=0.2)
        locks.append(fringe(body, 6, 0.24 * PI, 0.34 * PI, 0.010, 0.022, 0.008, 36))
        return locks, cap(body, std, 0.008 * s)
    if style == "hair_curlyshort":
        return ringlets(body, 70, lambda phi: std(phi) * 1.0, 0.010 * s, 0.024 * s, 37), cap(body, std, 0.010 * s)
    if style == "hair_afro":
        line = _side_line(0.29, 0.38, 0.52, 0.72)
        return cloud(body, line, 0.072 * s, 0.08, 1.3, segments=56, rings=24), cap(body, line, 0.012 * s)
    if style == "hair_twists":
        return twists(body, 70, lambda phi: std(phi), 0.010 * s, 0.040 * s, 0.017 * s, 38), cap(body, std, 0.012 * s)
    if style == "hair_shag":
        ends = _side_line(0.40, 0.58, 0.72, 0.76)
        locks = flowing(body, 18, 3, ends, 0.016 * s, 0.040 * s, 0.012 * s, 39, start=(0.03, 0.26), tip=0.18)
        locks.append(fringe(body, 6, 0.26 * PI, 0.37 * PI, 0.014, 0.030, 0.010, 40))
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_wavy":
        ends = _side_line(0.36, 0.60, 0.78, 0.80)
        locks = flowing(body, 18, 3, ends, 0.016 * s, 0.042 * s, 0.012 * s, 41, start=(0.03, 0.26), wave=0.010, waves=1.5)
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_bobbangs":
        ends = _side_line(0.38, 0.62, 0.80, 0.82)
        locks = flowing(body, 18, 3, ends, 0.016 * s, 0.046 * s, 0.013 * s, 42, start=(0.06, 0.26), phi_center=PI,
                        phi_range=0.78 * PI)
        locks.append(fringe(body, 9, 0.30 * PI, 0.37 * PI, 0.016, 0.034, 0.011, 43))
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_lob":
        locks = flowing(body, 14, 2, _side_line(0.36, 0.50, 0.58, 0.60), 0.014 * s, 0.040 * s, 0.011 * s, 44,
                        start=(0.03, 0.14))
        for side in (1.0, -1.0):
            locks.append(falling(body, 6, side * 0.66 * PI, 0.24 * PI, -0.24, 0.016 * s, 0.044, 0.013, 45 + side))
        locks.append(falling(body, 9, PI, 0.38 * PI, -0.26, 0.018 * s, 0.050, 0.014, 47))
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_curtain":
        ends = _side_line(0.36, 0.50, 0.74, 0.76)
        locks = flowing(body, 16, 2, ends, 0.016 * s, 0.042 * s, 0.012 * s, 48, start=(0.05, 0.20), phi_center=PI,
                        phi_range=0.80 * PI)
        locks.append(fringe(body, 8, 0.32 * PI, 0.42 * PI, 0.016, 0.032, 0.010, 49, centre_part=True))
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_mohawk":
        strip = _side_line(0.42, 0.20, 0.16, 0.40)
        locks = Mesh()
        c = head.centre(body)
        for k in range(9):
            theta = 0.06 * PI + 0.50 * PI * k / 8
            root = head.point(body, theta, 0.0 if theta < 0.35 * PI else PI, 0.006 * s)
            d = normalize(sub(root, add(c, (0.0, -0.06 * s, 0.0))))
            tip = add(root, mul(d, 0.075 * s * (1.0 - 0.3 * abs(k - 3) / 5)))
            locks.append(lock(body, _catmull([root, lerp(root, tip, 0.5), tip], 3), taper(0.026 * s, 0.08, 1.2, 0.1),
                              taper(0.012 * s, 0.1, 1.2, 0.1), segments=7))
        return locks, cap(body, std, 0.0025 * s)
    if style == "hair_topknot":
        knot = head.point(body, 0.05 * PI, PI, 0.045 * s)
        locks = flowing(body, 14, 1, lambda phi: 0.12 * PI, 0.010 * s, 0.030 * s, 0.008 * s, 50, start=(0.30, 0.30))
        locks.append(bun(body, knot, 0.038, 51))
        return locks, cap(body, std, 0.006 * s)
    if style == "hair_halfup":
        knot = head.point(body, 0.18 * PI, PI, 0.035 * s)
        locks = flowing(body, 14, 1, _side_line(0.38, 0.46, 0.50, 0.40), 0.012 * s, 0.036 * s, 0.010 * s, 52,
                        start=(0.04, 0.04))
        for side in (1.0, -1.0):
            locks.append(falling(body, 6, side * 0.66 * PI, 0.24 * PI, -0.40, 0.016 * s, 0.044, 0.012, 53 + side))
        locks.append(falling(body, 8, PI, 0.34 * PI, -0.44, 0.018 * s, 0.050, 0.013, 55, root_theta=0.30 * PI))
        locks.append(bun(body, knot, 0.032, 56))
        return locks, cap(body, std, 0.010 * s)
    if style in ("hair_longbangs", "hair_longwavy", "hair_layered"):
        wave = 0.012 if style == "hair_longwavy" else 0.0
        drop = {"hair_longbangs": -0.44, "hair_longwavy": -0.46, "hair_layered": -0.38}[style]
        locks = flowing(body, 14, 2, _side_line(0.36, 0.52, 0.60, 0.62), 0.014 * s, 0.040 * s, 0.011 * s, 57,
                        start=(0.03, 0.14), wave=wave * 0.6)
        for side in (1.0, -1.0):
            locks.append(falling(body, 7, side * 0.66 * PI, 0.24 * PI, drop + (0.08 if style == "hair_layered" else 0.0),
                                 0.016 * s, 0.044, 0.012, 58 + side, wave=wave))
        locks.append(falling(body, 11, PI, 0.40 * PI, drop - 0.04, 0.018 * s, 0.050, 0.013, 60, wave=wave))
        if style == "hair_longbangs":
            locks.append(fringe(body, 9, 0.30 * PI, 0.37 * PI, 0.016, 0.032, 0.010, 61))
        if style == "hair_layered":
            locks.append(fringe(body, 8, 0.32 * PI, 0.41 * PI, 0.016, 0.030, 0.010, 62, centre_part=True))
        return locks, cap(body, std, 0.012 * s)
    if style == "hair_longcurly":
        line = _side_line(0.28, 0.39, 0.51, 0.74)
        locks = ringlets(body, 60, lambda phi: line(phi) * 1.02, 0.016 * s, 0.030 * s, 63)
        for side in (1.0, -1.0):
            locks.append(falling(body, 6, side * 0.66 * PI, 0.24 * PI, -0.36, 0.020 * s, 0.050, 0.016, 64 + side, wave=0.016,
                                 waves=3.0))
        locks.append(falling(body, 9, PI, 0.40 * PI, -0.40, 0.022 * s, 0.056, 0.017, 66, wave=0.016, waves=3.0))
        return locks, cap(body, line, 0.016 * s)
    if style == "hair_braid":
        tie = head.point(body, 0.56 * PI, PI, 0.014 * s)
        locks = swept_back(body, std, 0.56 * PI, PI)
        end = add(tie, (0.0, -0.30 * s, -0.030 * s))
        locks.append(braid(body, tie, end, 0.030, 9, 68))
        return locks, cap(body, std, 0.008 * s)
    if style in ("hair_lowponytail", "hair_highponytail", "hair_sideponytail"):
        theta, phi = {"hair_lowponytail": (0.56 * PI, PI), "hair_highponytail": (0.30 * PI, PI),
                      "hair_sideponytail": (0.52 * PI, 0.62 * PI)}[style]
        tie = head.point(body, theta, phi, 0.020 * s)
        locks = swept_back(body, std, theta, phi)
        c = head.centre(body)
        out = normalize(sub(tie, c))
        direction = normalize(add(out, (0.0, -1.4 if style != "hair_highponytail" else -0.7, 0.0)))
        locks.append(tail(body, tie, direction, 0.22 if style == "hair_highponytail" else 0.20, 0.032, 9, 69))
        return locks, cap(body, std, 0.006 * s)
    if style == "hair_spacebuns":
        locks = flowing(body, 16, 1, lambda phi: std(phi) * 0.96, 0.008 * s, 0.030 * s, 0.008 * s, 70, start=(0.18, 0.18))
        for side in (1.0, -1.0):
            locks.append(bun(body, head.point(body, 0.16 * PI, side * 0.45 * PI, 0.034 * s), 0.034, 71 + side))
        return locks, cap(body, std, 0.007 * s)
    raise ValueError(style)


# Styles drawn with the coiled-hair texture.
COILED = ("hair_afro",)
REVISED_STYLES = ["hair_crew", "hair_fade", "hair_shaved", "hair_balding", "hair_quiff", "hair_slick", "hair_crop",
                  "hair_curlyshort", "hair_afro", "hair_twists", "hair_shag", "hair_wavy", "hair_bobbangs", "hair_lob",
                  "hair_curtain", "hair_mohawk", "hair_topknot", "hair_halfup", "hair_longbangs", "hair_longwavy",
                  "hair_layered", "hair_longcurly", "hair_braid", "hair_lowponytail", "hair_highponytail",
                  "hair_sideponytail", "hair_spacebuns"]


STYLES = ["hair_short", "hair_spiky", "hair_bob", "hair_ponytail", "hair_buzz", "hair_long", "hair_curly",
          "hair_sidepart", "hair_bun", "hair_pigtails"]


def build(body, style):
    """(locks Mesh, cap Mesh) for a style."""
    if style in REVISED_STYLES:
        return build_revised(body, style)
    s = head.SCALE[body]
    if style == "hair_buzz":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.72 * PI)
        return Mesh(), cap(body, line, 0.0035 * s)
    if style == "hair_short":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.74 * PI)
        locks = flow_locks(body, 16, 4, lambda phi: line(phi) * 1.0, 0.010 * s, 0.030 * s, 0.011 * s, 1,
                           forward=0.10, start=(0.03, 0.30))
        return locks, cap(body, line, 0.008 * s)
    if style == "hair_spiky":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.72 * PI)
        return spikes(body, 34, (0.02 * PI, 0.34 * PI), 0.008 * s, 0.075 * s, 0.020 * s, 2), \
            cap(body, line, 0.009 * s)
    if style == "hair_bob":
        line = hairline(0.28 * PI, 0.39 * PI, 0.51 * PI, 0.74 * PI)
        ends = hairline(0.38 * PI, 0.60 * PI, 0.80 * PI, 0.82 * PI)
        locks = flow_locks(body, 20, 4, ends, 0.016 * s, 0.046 * s, 0.013 * s, 3, forward=0.0, start=(0.03, 0.26))
        return locks, cap(body, line, 0.012 * s)
    if style == "hair_ponytail":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.72 * PI)
        c = head.centre(body)
        tie = head.point(body, 0.42 * PI, PI, 0.022 * s)
        locks = Mesh()
        for k in range(14):
            phi = 2.0 * PI * k / 14
            way = [("s", 0.30 * PI, phi, 0.006 * s), ("s", 0.20 * PI, phi + (PI - phi) * 0.3, 0.010 * s),
                   ("s", 0.40 * PI, PI + (phi - PI) * 0.15, 0.014 * s)]
            if abs(((phi + PI) % (2 * PI)) - PI) < 0.35 * PI:
                way = [("s", 0.29 * PI, phi, 0.006 * s), ("s", 0.12 * PI, phi, 0.012 * s), ("s", 0.40 * PI, PI, 0.016 * s)]
            locks.append(lock(body, surface_path(body, way, 5), taper(0.026 * s, 0.4), taper(0.008 * s, 0.5)))
        w = weights_for(body)
        locks.append(ellipsoid(tie, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.022 * s, 0.022 * s, 0.016 * s), 12, 8,
                               lambda p, v, u: w(p)))
        local = sub(tie, c)
        for k in range(11):
            a = 2.0 * PI * k / 11
            off = (0.016 * math.cos(a), 0.014 * math.sin(a), 0.0)
            way = [("w",) + tuple(mul(add(local, off), 1.0 / s)),
                   ("w",) + tuple(mul(add(local, add(off, (0.0, -0.02, -0.035))), 1.0 / s)),
                   ("w",) + tuple(mul(add(local, add(mul(off, 1.6), (0.0, -0.10, -0.05))), 1.0 / s)),
                   ("w",) + tuple(mul(add(local, add(mul(off, 1.2), (0.0, -0.20, -0.035))), 1.0 / s))]
            locks.append(lock(body, surface_path(body, way, 4), taper(0.030 * s, 0.15, 1.3, 0.5), taper(0.020 * s, 0.2)))
        return locks, cap(body, line, 0.006 * s)
    if style == "hair_long":
        line = hairline(0.28 * PI, 0.39 * PI, 0.51 * PI, 0.74 * PI)
        ends = hairline(0.36 * PI, 0.52 * PI, 0.60 * PI, 0.62 * PI)
        locks = flow_locks(body, 14, 2, ends, 0.014 * s, 0.040 * s, 0.011 * s, 5, start=(0.03, 0.14))
        locks.append(hanging(body, (0.10 * PI, 0.62 * PI), -0.40, 0.016 * s, 0.046 * s, 0.013 * s, 6, 8, 0.34 * PI))
        locks.append(hanging(body, (0.10 * PI, -0.62 * PI), -0.40, 0.016 * s, 0.046 * s, 0.013 * s, 7, 8, 0.34 * PI))
        locks.append(hanging(body, (0.12 * PI, PI), -0.46, 0.018 * s, 0.052 * s, 0.014 * s, 8, 12, 0.40 * PI))
        return locks, cap(body, line, 0.012 * s)
    if style == "hair_curly":
        line = hairline(0.28 * PI, 0.39 * PI, 0.51 * PI, 0.74 * PI)
        return ringlets(body, 175, lambda phi: line(phi) * 1.02, 0.014 * s, 0.026 * s, 9), cap(body, line, 0.016 * s)
    if style == "hair_sidepart":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.74 * PI)
        ends = hairline(0.34 * PI, 0.42 * PI, 0.47 * PI, 0.64 * PI)
        locks = flow_locks(body, 16, 3, ends, 0.012 * s, 0.032 * s, 0.011 * s, 10, forward=0.0, curl=-0.35,
                           start=(0.06, 0.22), phi_center=0.30)
        return locks, cap(body, line, 0.010 * s)
    if style == "hair_bun":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.72 * PI)
        bun = head.point(body, 0.20 * PI, PI, 0.040 * s)
        locks = Mesh()
        for k in range(14):
            phi = 2.0 * PI * k / 14
            way = [("s", 0.30 * PI, phi, 0.006 * s), ("s", 0.16 * PI, phi + (PI - phi) * 0.2, 0.010 * s),
                   ("s", 0.20 * PI, PI, 0.016 * s)]
            locks.append(lock(body, surface_path(body, way, 5), taper(0.026 * s, 0.4), taper(0.008 * s, 0.5)))
        c = head.centre(body)
        w = weights_for(body)
        locks.append(ellipsoid(bun, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.050 * s, 0.044 * s, 0.046 * s), 16, 12,
                               lambda p, v, u: w(p)))
        for k in range(6):
            a = 2.0 * PI * k / 6
            ring = [add(bun, (0.052 * s * math.cos(a + b * 0.5), 0.044 * s * math.sin(b * 0.9) * 0.6, 0.048 * s * math.sin(a + b * 0.5)))
                    for b in range(7)]
            locks.append(tube(ring, [0.012 * s] * len(ring), 7, lambda p: w(p), round_end=True))
        return locks, cap(body, line, 0.006 * s)
    if style == "hair_pigtails":
        line = hairline(0.30 * PI, 0.39 * PI, 0.50 * PI, 0.72 * PI)
        locks = Mesh()
        c = head.centre(body)
        w = weights_for(body)
        for side in (1.0, -1.0):
            for k in range(8):
                phi = side * (0.05 + 0.9 * PI * k / 8)
                way = [("s", 0.03 * PI, side * 0.05, 0.006 * s), ("s", 0.25 * PI, phi, 0.010 * s),
                       ("s", 0.45 * PI, side * 0.62 * PI, 0.018 * s)]
                locks.append(lock(body, surface_path(body, way, 5), taper(0.030 * s, 0.4), taper(0.009 * s, 0.5)))
            tie = head.point(body, 0.45 * PI, side * 0.62 * PI, 0.024 * s)
            locks.append(ellipsoid(tie, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.018 * s, 0.020 * s, 0.018 * s), 12, 8,
                                   lambda p, v, u: w(p)))
            local = sub(tie, c)
            for k in range(5):
                a = 2.0 * PI * k / 5
                off = (0.008 * math.cos(a), 0.0, 0.008 * math.sin(a))
                way = [("w",) + tuple(mul(add(local, off), 1.0 / s)),
                       ("w",) + tuple(mul(add(local, add(mul(off, 1.5), (side * 0.03, -0.05, -0.01))), 1.0 / s)),
                       ("w",) + tuple(mul(add(local, add(mul(off, 1.3), (side * 0.035, -0.15, -0.02))), 1.0 / s))]
                locks.append(lock(body, surface_path(body, way, 4), taper(0.018 * s, 0.12, 1.3, 0.3), taper(0.014 * s, 0.2)))
        return locks, cap(body, line, 0.008 * s)
    raise ValueError(style)
