# SPDX-License-Identifier: MS-PL
"""Hair helpers shared by the hairstyles (hairstyles.py, built as masses by hairmass.py) and by
facial hair: the scalp cap inside a style's hairline, hairlines, skin weights for hair, and a
tapering lock along a path. Randomness comes from a fixed integer hash, so output is reproducible.
"""
import math

from . import rig, head
from .mathutil import add, sub, mul, normalize, cross, dot, length, lerp, smoothstep, clamp
from .mesh import Mesh

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
    """The scalp shell down to theta_max(phi), its rim tucked in so the edge never shows. A thin
    shell gets finer facets, or the head shows between them."""
    if lift < 0.0095:
        segments, rings = max(segments, 56), max(rings, 16)
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


# The texture each style's locks are drawn with (hair_clump otherwise).
TEXTURE = {"hair_afro": "hair_coil", "hair_twists": "hair_twist"}


def build_parts(body, style):
    """(locks Mesh, cap Mesh, ties Mesh) for a style."""
    from . import hairstyles
    return hairstyles.build(body, style)


def build(body, style):
    """(locks Mesh, cap Mesh) for a style."""
    return build_parts(body, style)[:2]
