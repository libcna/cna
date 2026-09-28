# SPDX-License-Identifier: MS-PL
"""Skinned triangle meshes built from parametric grids (ellipsoids, tapered capsules, lofts)."""
import math

from .mathutil import add, sub, mul, cross, dot, normalize, length, lerp, frame_from_axis, smoothstep


def pack_weights(weights):
    """dict slot -> weight  ->  (4 joints, 4 bytes summing to 255)."""
    items = sorted(((w, s) for s, w in weights.items() if w > 1e-4), key=lambda e: (-e[0], e[1]))[:4]
    total = sum(w for w, _ in items)
    joints = [s for _, s in items] + [0] * (4 - len(items))
    raw = [w / total * 255.0 for w, _ in items] + [0.0] * (4 - len(items))
    quantized = [int(math.floor(r)) for r in raw]
    # Hand the rounding remainder to the largest fractions so every vertex sums to exactly 255.
    remainder = 255 - sum(quantized)
    for index in sorted(range(len(items)), key=lambda i: (-(raw[i] - quantized[i]), i))[:remainder]:
        quantized[index] += 1
    return tuple(joints), tuple(quantized)


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.skin, self.indices = [], [], [], [], []
        self.patch_origin = (0.0, 0.0, 0.0)

    def validate(self):
        n = len(self.positions)
        assert n and len(self.normals) == n and len(self.uvs) == n and len(self.skin) == n
        assert len(self.indices) % 3 == 0 and all(0 <= i < n for i in self.indices)
        for joints, weights in self.skin:
            assert sum(weights) == 255 and all(0 <= j < 71 for j in joints)

    def append(self, other):
        base = len(self.positions)
        self.positions += other.positions
        self.normals += other.normals
        self.uvs += other.uvs
        self.skin += other.skin
        self.indices += [base + i for i in other.indices]
        return self

    def grid(self, rows, weight_fn, uv_seam=False, flip=False, wrap=True):
        """rows: list of rings, each a list of points (same count), wrapped around.

        Normals come from area-weighted face normals, so any parametric shape shades smoothly;
        a ring whose points coincide (a pole) gets the average of its neighbours.
        """
        cols = len(rows[0])
        count = len(rows) * cols
        points = [p for ring in rows for p in ring]
        acc = [(0.0, 0.0, 0.0)] * count
        tris = []
        for r in range(len(rows) - 1):
            for c in range(cols if wrap else cols - 1):
                a, b = r * cols + c, r * cols + (c + 1) % cols
                d, e = (r + 1) * cols + c, (r + 1) * cols + (c + 1) % cols
                for tri in ((a, d, e), (a, e, b)) if not flip else ((a, e, d), (a, b, e)):
                    n = cross(sub(points[tri[1]], points[tri[0]]), sub(points[tri[2]], points[tri[0]]))
                    if length(n) < 1e-12:
                        continue
                    tris.append(tri)
                    for v in tri:
                        acc[v] = add(acc[v], n)
        # Whatever the parameterization direction, faces point away from the shape's centre (an
        # open patch instead faces away from `outward`, supplied by the caller).
        centre = mul(points[0], 0.0)
        for p in points:
            centre = add(centre, p)
        centre = mul(centre, 1.0 / count)
        volume = sum(dot(sub(points[t[0]], centre), cross(sub(points[t[1]], centre), sub(points[t[2]], centre)))
                     for t in tris)
        if not wrap:
            volume = sum(dot(cross(sub(points[t[1]], points[t[0]]), sub(points[t[2]], points[t[0]])),
                             sub(points[t[0]], self.patch_origin)) for t in tris)
        if volume < 0:
            tris = [(t[0], t[2], t[1]) for t in tris]
            acc = [mul(n, -1.0) for n in acc]
        for r, ring in enumerate(rows):
            if max(length(sub(p, ring[0])) for p in ring) < 1e-9:
                total = (0.0, 0.0, 0.0)
                for c in range(cols):
                    total = add(total, acc[r * cols + c])
                for c in range(cols):
                    acc[r * cols + c] = total
        normals = [normalize(n) for n in acc]
        base = len(self.positions)
        emitted = cols + 1 if uv_seam else cols
        for r, ring in enumerate(rows):
            v = r / (len(rows) - 1)
            for c in range(emitted):
                source = r * cols + c % cols
                self.positions.append(points[source])
                self.normals.append(normals[source])
                self.uvs.append((c / cols if wrap else c / (cols - 1), v))
                self.skin.append(pack_weights(weight_fn(points[source], v, c / cols)))
        for tri in tris:
            mapped = []
            for index in tri:
                r, c = divmod(index, cols)
                if uv_seam and c == 0 and any((t % cols) == cols - 1 for t in tri):
                    c = cols
                mapped.append(base + r * emitted + c)
            self.indices += mapped
        return self


def ellipsoid(center, axes, radii, segments, rings, weight_fn, uv_seam=False, v_range=(0.0, 1.0), phase=0.0,
              u_range=(0.0, 1.0)):
    """axes: (x, up, z) unit vectors; v runs from the +up pole (0) to the -up pole (1); u runs
    around from +z (phase 0). A partial u_range makes an open patch instead of a closed ring."""
    rows = []
    patch = u_range != (0.0, 1.0)
    columns = segments + 1 if patch else segments
    for r in range(rings + 1):
        v = v_range[0] + (v_range[1] - v_range[0]) * r / rings
        theta = math.pi * v
        ring = []
        for c in range(columns):
            phi = phase + 2.0 * math.pi * (u_range[0] + (u_range[1] - u_range[0]) * c / segments)
            local = (radii[0] * math.sin(theta) * math.sin(phi), radii[1] * math.cos(theta),
                     radii[2] * math.sin(theta) * math.cos(phi))
            ring.append(add(center, add(add(mul(axes[0], local[0]), mul(axes[1], local[1])), mul(axes[2], local[2]))))
        rows.append(ring)
    m = Mesh()
    m.patch_origin = center
    return m.grid(rows, weight_fn, uv_seam=uv_seam, wrap=not patch)


def capsule(p0, p1, r0, r1, segments, weight_fn, body_rings=6, cap_rings=4, cap_start=True, cap_end=True,
            aspect=1.0, hint=(0.0, 0.0, 1.0), start_t=0.0, end_t=1.0, flare=0.0):
    """Tapered capsule from p0 to p1; cross-section radii (r, r*aspect). start_t/end_t trim an
    open shell (sleeves, trouser legs); flare widens an open end."""
    axis = sub(p1, p0)
    u, v, w = frame_from_axis(axis, hint)
    total = length(axis)
    sections = []
    if cap_start:
        for i in range(cap_rings, 0, -1):
            a = math.pi * 0.5 * i / cap_rings
            sections.append((-r0 * math.sin(a), r0 * math.cos(a)))
    for i in range(body_rings + 1):
        t = start_t + (end_t - start_t) * i / body_rings
        radius = r0 + (r1 - r0) * t
        if flare and not cap_end:
            radius += flare * smoothstep(0.6, 1.0, i / body_rings)
        sections.append((t * total, radius))
    if cap_end:
        for i in range(1, cap_rings + 1):
            a = math.pi * 0.5 * i / cap_rings
            sections.append((total + r1 * math.sin(a), r1 * math.cos(a)))
    rows = []
    for along, radius in sections:
        c = add(p0, mul(w, along))
        rows.append([add(c, add(mul(u, radius * math.cos(2 * math.pi * k / segments)),
                                mul(v, radius * aspect * math.sin(2 * math.pi * k / segments))))
                     for k in range(segments)])
    return Mesh().grid(rows, lambda p, vv, uu: weight_fn(p, dot(sub(p, p0), w) / total))


def loft(sections, segments, weight_fn, cap_bottom=None, cap_top=None):
    """sections: (center, rx, rz) ellipses in horizontal planes, bottom to top. A cap is the
    height of a dome closing that end (0 for flat), or None to leave it open."""
    rows = []
    if cap_bottom is not None:
        c, rx, rz = sections[0]
        for i in range(4, 0, -1):
            a = math.pi * 0.5 * i / 4
            rows.append(_ring(add(c, (0.0, -cap_bottom * math.sin(a), 0.0)), rx * math.cos(a), rz * math.cos(a), segments))
    for c, rx, rz in sections:
        rows.append(_ring(c, rx, rz, segments))
    if cap_top is not None:
        c, rx, rz = sections[-1]
        for i in range(1, 5):
            a = math.pi * 0.5 * i / 4
            rows.append(_ring(add(c, (0.0, cap_top * math.sin(a), 0.0)), rx * math.cos(a), rz * math.cos(a), segments))
    return Mesh().grid(rows, lambda p, v, u: weight_fn(p))


def _ring(center, rx, rz, segments):
    return [add(center, (rx * math.sin(2 * math.pi * k / segments), 0.0, rz * math.cos(2 * math.pi * k / segments)))
            for k in range(segments)]


def box(center, axes, half, weight_fn, bevel=0.0):
    """Beveled box as a closed grid: rounded by an ellipsoid-to-box blend (superellipsoid)."""
    rows = []
    rings, segments = 8, 16
    exponent = 0.35 if bevel else 0.1
    for r in range(rings + 1):
        theta = math.pi * r / rings
        ring = []
        for c in range(segments):
            phi = 2.0 * math.pi * c / segments
            sp = lambda x: math.copysign(abs(x) ** exponent, x)
            local = (half[0] * sp(math.sin(theta)) * sp(math.sin(phi)), half[1] * sp(math.cos(theta)),
                     half[2] * sp(math.sin(theta)) * sp(math.cos(phi)))
            ring.append(add(center, add(add(mul(axes[0], local[0]), mul(axes[1], local[1])), mul(axes[2], local[2]))))
        rows.append(ring)
    return Mesh().grid(rows, lambda p, v, u: weight_fn(p))


def blend(a, b, t):
    """Weights moving from bone a (t=0) to bone b (t=1)."""
    t = min(max(t, 0.0), 1.0)
    if a == b or t <= 0.0:
        return {a: 1.0}
    if t >= 1.0:
        return {b: 1.0}
    return {a: 1.0 - t, b: t}


def segment_weights(parent, bone, child, start_blend=0.25, end_blend=0.25):
    """Weight function for a limb segment of `bone` along t in [0, 1]: half-and-half with the
    parent at t=0 and with the child at t=1, fading over the given fractions."""
    def fn(p, t):
        w = {bone: 1.0}
        if parent is not None and t < start_blend:
            k = 0.5 * (1.0 - smoothstep(0.0, start_blend, t))
            w = {bone: 1.0 - k, parent: k}
        if child is not None and t > 1.0 - end_blend:
            k = 0.5 * smoothstep(1.0 - end_blend, 1.0, t)
            w = {k2: v * (1.0 - k) for k2, v in w.items()}
            w[child] = w.get(child, 0.0) + k
        return w
    return fn


def tube(path, radius, segments, weight_fn, closed=False, cap=True, row_weights=None, round_end=False):
    """Tube along a polyline (parallel-transported frames); radius may vary per point. With
    row_weights (one dict per path point) skinning follows the path instead of weight_fn;
    round_end closes the last point with a hemisphere instead of a cone."""
    count = len(path)
    tangents = []
    for i in range(count):
        if closed:
            t = sub(path[(i + 1) % count], path[i - 1])
        else:
            t = sub(path[min(i + 1, count - 1)], path[max(i - 1, 0)])
        tangents.append(normalize(t))
    u, v, _ = frame_from_axis(tangents[0])
    rows = []
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * count
    for i in range(count):
        if i:
            # Transport u into the plane perpendicular to the new tangent.
            u = normalize(sub(u, mul(tangents[i], dot(u, tangents[i]))))
            v = cross(tangents[i], u)
        rows.append([add(path[i], add(mul(u, radii[i] * math.cos(2 * math.pi * k / segments)),
                                      mul(v, radii[i] * math.sin(2 * math.pi * k / segments))))
                     for k in range(segments)])
    per_row = list(row_weights) if row_weights else None
    if round_end and not closed:
        t, r_end = tangents[-1], radii[-1]
        for k in (1, 2):
            a = math.pi * 0.5 * k / 3
            centre = add(path[-1], mul(t, r_end * math.sin(a)))
            rows.append([add(centre, mul(sub(q, path[-1]), math.cos(a))) for q in rows[count - 1]])
            if per_row:
                per_row.append(per_row[-1])
        rows.append([add(path[-1], mul(t, r_end))] * segments)
        if per_row:
            per_row.append(per_row[-1])
        if cap:
            rows.insert(0, [path[0]] * segments)
            if per_row:
                per_row.insert(0, per_row[0])
    elif closed:
        rows.append(rows[0])
    elif cap:
        rows.insert(0, [path[0]] * segments)
        rows.append([path[-1]] * segments)
        if per_row:
            per_row = [per_row[0]] + per_row + [per_row[-1]]
    if per_row:
        last = len(rows) - 1
        return Mesh().grid(rows, lambda p, vv, uu: per_row[int(round(vv * last))])
    return Mesh().grid(rows, lambda p, vv, uu: weight_fn(p))
