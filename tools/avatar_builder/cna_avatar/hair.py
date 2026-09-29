# SPDX-License-Identifier: MS-PL
"""Catalog v2 hair: a scalp cap inside the style's hairline, covered by sculpted locks.

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


def hairline(front, temple, side, back):
    """theta_max(phi): the lowest angle of the cap at the face (phi 0), temples, sides and nape."""
    def fn(phi):
        a = abs(((phi + PI) % (2 * PI)) - PI)
        if a < 0.30 * PI:
            return lerp((front, 0, 0), (temple, 0, 0), smoothstep(0.0, 0.30 * PI, a))[0]
        if a < 0.55 * PI:
            return lerp((temple, 0, 0), (side, 0, 0), smoothstep(0.30 * PI, 0.55 * PI, a))[0]
        return lerp((side, 0, 0), (back, 0, 0), smoothstep(0.55 * PI, PI, a))[0]
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


def cap(body, theta_max, lift, bulge=lambda theta, phi: 0.0, segments=48, rings=14):
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


def taper(w0, tip=0.12, power=1.6, swell=0.25):
    """Lock profile: full near the root, a little fuller at a third, narrowing to `tip`."""
    return lambda t: w0 * (1.0 + swell * math.sin(PI * min(t * 1.5, 1.0))) * (tip + (1.0 - tip) * (1.0 - t ** power))


def flow_locks(body, count, rows, theta_end, lift, width, thickness, seed, forward=0.0, curl=0.0, spread=0.9,
               start=(0.04, 0.22), phi_center=0.0, phi_range=PI):
    """Locks whose roots circle the crown and which flow outward and down to theta_end(phi)."""
    out = Mesh()
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


def curls(body, count, theta_max, lift, size, seed):
    out = Mesh()
    w = weights_for(body)
    for k in range(count):
        key = (seed, k)
        golden = k * 2.399963
        f = (k + 0.5) / count
        phi = golden
        theta = math.acos(1.0 - f * (1.0 - math.cos(theta_max(phi)))) * 0.98
        r = size * (0.85 + 0.25 * jitter(*key, 1))
        centre = head.point(body, theta, phi, lift + r * 0.3 + 0.004 * jitter(*key, 2))
        out.append(ellipsoid(centre, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (r, r * 0.9, r), 8, 5,
                             lambda p, v, u: w(p)))
    return out


def hanging(body, root_angles, drop_to, lift, width, thickness, seed, count, spread_phi, sway=0.0, inward=0.3):
    """Longer locks: over the head to the jaw, then hanging to head-local height drop_to."""
    out = Mesh()
    s = head.SCALE[body]
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


STYLES = ["hair_short", "hair_spiky", "hair_bob", "hair_ponytail", "hair_buzz", "hair_long", "hair_curly",
          "hair_sidepart", "hair_bun", "hair_pigtails"]


def build(body, style):
    """(locks Mesh, cap Mesh) for a style."""
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
        locks = flow_locks(body, 20, 4, ends, 0.016 * s, 0.036 * s, 0.013 * s, 3, forward=0.0, start=(0.03, 0.26))
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
        locks = flow_locks(body, 14, 2, ends, 0.014 * s, 0.032 * s, 0.011 * s, 5, start=(0.03, 0.14))
        locks.append(hanging(body, (0.10 * PI, 0.62 * PI), -0.40, 0.016 * s, 0.034 * s, 0.013 * s, 6, 6, 0.34 * PI))
        locks.append(hanging(body, (0.10 * PI, -0.62 * PI), -0.40, 0.016 * s, 0.034 * s, 0.013 * s, 7, 6, 0.34 * PI))
        locks.append(hanging(body, (0.12 * PI, PI), -0.46, 0.018 * s, 0.040 * s, 0.014 * s, 8, 9, 0.40 * PI))
        return locks, cap(body, line, 0.012 * s)
    if style == "hair_curly":
        line = hairline(0.28 * PI, 0.39 * PI, 0.51 * PI, 0.74 * PI)
        return curls(body, 96, lambda phi: line(phi) * 1.02, 0.020 * s, 0.029 * s, 9), cap(body, line, 0.018 * s)
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


# Every covering hat's crown lies outside the head surface plus accessories.HAT_LIFT down to at
# least this line; under_hat() flattens hair inside it to just beneath that envelope.
HAT_ENVELOPE = hairline(0.40 * PI, 0.45 * PI, 0.47 * PI, 0.50 * PI)
UNDER_HAT_LIFT = 0.019


def under_hat(body, mesh):
    """The part of a hair mesh a covering hat leaves visible, flattened under the hat envelope:
    triangles wholly inside the covered region are dropped, the rest blend out below its line."""
    from .mathutil import clamp
    s = head.SCALE[body]
    c = head.centre(body)
    positions, covered = [], []
    for p in mesh.positions:
        local = sub(p, c)
        d = normalize(local)
        theta = math.acos(clamp(d[1], -1.0, 1.0))
        phi = math.atan2(d[0], d[2])
        line = HAT_ENVELOPE(phi)
        cover = 1.0 - smoothstep(line - 0.03 * PI, line + 0.02 * PI, theta)
        covered.append(theta < line - 0.05 * PI)
        if cover <= 0.0:
            positions.append(p)
            continue
        surface = length(head.local_point(body, theta, phi))
        limit = surface + UNDER_HAT_LIFT * s
        r = length(local)
        squashed = min(r, limit)
        positions.append(add(c, mul(d, r + (squashed - r) * cover)))
    out = Mesh()
    remap = {}
    for t in range(0, len(mesh.indices), 3):
        tri = mesh.indices[t:t + 3]
        if all(covered[i] for i in tri):
            continue
        for i in tri:
            if i not in remap:
                remap[i] = len(out.positions)
                out.positions.append(positions[i])
                out.normals.append(mesh.normals[i])
                out.uvs.append(mesh.uvs[i])
                out.skin.append(mesh.skin[i])
            out.indices.append(remap[i])
    return out
