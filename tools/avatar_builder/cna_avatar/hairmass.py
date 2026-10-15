# SPDX-License-Identifier: MS-PL
"""Catalog v3 revision 3 hair: sculpted masses rather than strips on a helmet.

A hairstyle is one or more shells over a scalp cap. A shell is built from strand paths. Each path
starts at a root on the scalp -- round a crown, along a part, along the hairline -- and is marched
over the head under the style's flow: away from its root, bent by gravity, drawn toward a tie
point, lifted for volume; it collides with the head, the neck and the torso, so hair lies on the
head and falls from it where the head curves away, and long hair rests on the back and shoulders.

Neighbouring paths bound a clump. The shell is a closed slab over the clumps: its inner face just
above the scalp (or the path, where hair hangs free), its outer face raised by the hair's
thickness and rounded across each clump, so clumps read as locks with grooves between them; each
clump ends in a pointed, rounded tip. Clumps vary in width, length and thickness by a fixed hash,
so a style is not a repeated pattern. u runs across a clump (0.5 its crest), v from root to tip,
matching textures.hair_clump.
"""
import math

from . import rig, head, body as body_module
from .mathutil import add, sub, mul, dot, cross, normalize, length, lerp, smoothstep, clamp
from .mesh import Mesh, pack_weights
from .hair import jitter, weights_for

PI = math.pi


# ----- the head and body as obstacles -------------------------------------------------------------

class Obstacles:
    """The head (its surface radius in every direction), the neck and the torso of one body, as
    things hair must stay outside of."""

    def __init__(self, body, clearance=0.020):
        self.body = body
        self.s = head.SCALE[body]
        self.c = head.centre(body)
        p = rig.PROPORTIONS[body]
        pos = rig.joint_positions(body)
        self.neck_base = add(pos[rig.INDEX["Neck"]], (0.0, -0.05, -0.004))
        self.neck_top = add(pos[rig.INDEX["Head"]], (0.0, 0.035, 0.010))
        self.neck_r = body_module.NECK[body] * 1.05 + 0.006
        # Torso sections (with clothing room), bottom to top, for long hair to rest on.
        self.torso = [(c, rx + clearance, rz + clearance, n) for c, rx, rz, n in body_module.torso_sections(body)]
        self.torso_top = self.torso[-2][0][1]
        self.collar_y = p["collar"][1]

    def radius(self, d):
        """The head surface's distance from the head centre along unit direction d."""
        theta = math.acos(clamp(d[1], -1.0, 1.0))
        phi = math.atan2(d[0], d[2])
        return length(head.local_point(self.body, theta, phi))

    def surface(self, d, lift=0.0):
        return add(self.c, mul(d, self.radius(d) + lift))

    def push_head(self, p, lift):
        v = sub(p, self.c)
        r = length(v)
        if r < 1e-9:
            return p, False
        d = mul(v, 1.0 / r)
        limit = self.radius(d) + lift
        if r < limit:
            return add(self.c, mul(d, limit)), True
        return p, False

    def push_neck(self, p):
        a, b = self.neck_base, self.neck_top
        if not (a[1] - 0.02 <= p[1] <= b[1]):
            return p, False
        t = (p[1] - a[1]) / (b[1] - a[1])
        axis = lerp(a, b, clamp(t, 0.0, 1.0))
        dx, dz = p[0] - axis[0], p[2] - axis[2]
        r = math.hypot(dx, dz)
        if r < self.neck_r and r > 1e-9:
            k = self.neck_r / r
            return (axis[0] + dx * k, p[1], axis[2] + dz * k), True
        return p, False

    def push_torso(self, p):
        rows = self.torso
        if p[1] < rows[0][0][1] or p[1] > rows[-2][0][1]:
            return p, False
        for lo, hi in zip(rows, rows[1:]):
            if lo[0][1] <= p[1] <= hi[0][1]:
                t = (p[1] - lo[0][1]) / max(hi[0][1] - lo[0][1], 1e-9)
                c = lerp(lo[0], hi[0], t)
                rx, rz = lo[1] + (hi[1] - lo[1]) * t, lo[2] + (hi[2] - lo[2]) * t
                n = lo[3] + (hi[3] - lo[3]) * t
                dx, dz = p[0] - c[0], p[2] - c[2]
                q = (abs(dx) / rx) ** n + (abs(dz) / rz) ** n
                if q < 1.0 and (abs(dx) + abs(dz)) > 1e-9:
                    k = q ** (-1.0 / n)
                    return (c[0] + dx * k, p[1], c[2] + dz * k), True
                return p, False
        return p, False

    def push(self, p, lift):
        p, hit = self.push_head(p, lift)
        p, neck = self.push_neck(p)
        p, torso = self.push_torso(p)
        return p, hit or neck or torso

    def head_angles(self, p):
        v = normalize(sub(p, self.c))
        return math.acos(clamp(v[1], -1.0, 1.0)), math.atan2(v[0], v[2])


# ----- strands ------------------------------------------------------------------------------------

class Flow:
    """How a style's strands travel: step length, gravity (how fast a strand turns downward, per
    metre), an attractor (a point strands turn toward, and how fast), how far above the scalp a
    strand lies, and an optional stop at a hairline (theta as a function of phi)."""

    def __init__(self, gravity=12.0, attract=None, attract_gain=0.0, lift=0.0015, stop=None, step=0.004, rise=0.0,
                 body=True, sweep=None, until=None, cling=1.0, cling_below=0.05, avoid_face=None, behind=0.0):
        self.gravity, self.attract, self.attract_gain = gravity, attract, attract_gain
        self.lift, self.stop, self.step, self.rise, self.body = lift, stop, step, rise, body
        self.sweep, self.until = sweep, until
        # Hair lies on the head where the head is under it (its direction from the head centre
        # above cling_below); below that it hangs free.
        self.cling, self.cling_below = cling, cling_below
        # A front hairline (theta of phi): strands that would fall over the face turn aside round it.
        self.avoid_face = avoid_face
        # How firmly hair reaching the shoulders is turned to fall behind them (long hair rests on
        # the back, not on top of the shoulders or over the arms).
        self.behind = behind

    def settle(self, ob, p, lift):
        """p kept outside the head, body and neck, and drawn down onto the scalp where hair lies on it."""
        if self.cling > 0.0:
            v = sub(p, ob.c)
            r = length(v)
            if r > 1e-9:
                d = mul(v, 1.0 / r)
                hold = self.cling * smoothstep(self.cling_below - 0.15, self.cling_below + 0.05, d[1])
                if hold > 0.0:
                    limit = ob.radius(d) + lift
                    if r > limit:
                        p = add(ob.c, mul(d, r - (r - limit) * hold))
        return ob.push(p, lift) if self.body else ob.push_head(p, lift)


def march(ob, root, direction, length_, flow):
    """A strand from `root` along `direction` for `length_` metres (or until it reaches flow.stop):
    the list of its points."""
    p, _ = flow.settle(ob, root, flow.lift)
    d = normalize(direction)
    points = [p]
    travelled = 0.0
    while travelled < length_ - 1e-9:
        step = min(flow.step, length_ - travelled)
        t = travelled / max(length_, 1e-9)
        turn = (0.0, -flow.gravity * step, 0.0)
        if flow.attract is not None:
            to = normalize(sub(flow.attract, p))
            turn = add(turn, mul(sub(to, d), min(1.0, flow.attract_gain * step)))
        if flow.sweep is not None:
            turn = add(turn, mul(flow.sweep(p, t), step))
        if flow.avoid_face is not None:
            theta, phi = ob.head_angles(p)
            v = normalize(sub(p, ob.c))
            near = smoothstep(0.15, 0.45, v[2]) * smoothstep(flow.avoid_face(phi) - 0.06, flow.avoid_face(phi) + 0.02, theta)
            if near > 0.0:
                aside = normalize((1.0 if v[0] >= 0.0 else -1.0, 0.0, -0.35))
                turn = add(turn, mul(sub(aside, d), min(1.0, 60.0 * near * step)))
        if flow.behind and p[1] < ob.collar_y + 0.09 and abs(p[0]) > ob.neck_r * 0.8:
            reach = smoothstep(ob.collar_y + 0.09, ob.collar_y - 0.02, p[1]) * smoothstep(ob.neck_r * 0.8, ob.neck_r * 2.0, abs(p[0]))
            back = normalize((-0.35 * (1.0 if p[0] > 0.0 else -1.0), -0.4, -1.0))
            turn = add(turn, mul(sub(back, d), min(1.0, flow.behind * reach * step)))
        d = normalize(add(d, turn))
        q = add(p, mul(d, step))
        lift = flow.lift + flow.rise * t
        q, hit = flow.settle(ob, q, lift)
        if hit:
            d = normalize(sub(q, p)) if length(sub(q, p)) > 1e-9 else d
        travelled += length(sub(q, p))
        p = q
        points.append(p)
        if flow.stop is not None:
            theta, phi = ob.head_angles(p)
            if theta >= flow.stop(phi):
                break
        if flow.until is not None and flow.until(p):
            break
        if len(points) > 4000:
            break
    return points


def resample(points, count):
    """`count` points evenly spaced by arc length along a polyline."""
    if len(points) < 2:
        return [points[0]] * count
    acc = [0.0]
    for a, b in zip(points, points[1:]):
        acc.append(acc[-1] + length(sub(b, a)))
    total = acc[-1]
    out = []
    j = 0
    for i in range(count):
        target = total * i / (count - 1)
        while j < len(points) - 2 and acc[j + 1] < target:
            j += 1
        seg = acc[j + 1] - acc[j]
        f = 0.0 if seg < 1e-12 else (target - acc[j]) / seg
        out.append(lerp(points[j], points[j + 1], clamp(f, 0.0, 1.0)))
    return out


def path_length(points):
    return sum(length(sub(b, a)) for a, b in zip(points, points[1:]))


# ----- roots --------------------------------------------------------------------------------------

def surface_dir(theta, phi):
    return (math.sin(theta) * math.sin(phi), math.cos(theta), math.sin(theta) * math.cos(phi))


def crown_roots(ob, theta, phi, count, radius=0.035, seed=0):
    """Roots on a small loop round a crown at head angles (theta, phi), each heading away from it
    over the scalp; the loop runs round the crown in order (a closed shell)."""
    centre_dir = surface_dir(theta, phi)
    u = normalize(cross((0.0, 1.0, 0.0) if abs(centre_dir[1]) < 0.95 else (1.0, 0.0, 0.0), centre_dir))
    v = cross(centre_dir, u)
    roots = []
    for k in range(count):
        a = 2.0 * PI * (k + 0.25 * jitter(seed, k, 1)) / count
        offset = add(mul(u, math.cos(a)), mul(v, math.sin(a)))
        d = normalize(add(centre_dir, mul(offset, radius / ob.radius(centre_dir))))
        p = ob.surface(d, 0.0)
        roots.append((p, offset, "crown"))
    return roots


def part_roots(ob, start, end, count, width=0.004, seed=0, end_fan=0.40, front_fan=0.12):
    """Roots on a narrow loop round a part line from head angles `start` to `end` (theta, phi), in
    order: up one side heading away from the part, fanning round the far end (a crown), back down
    the other side and fanning round the near end, so the loop closes. Each root is (point,
    direction, where): where is "side", "crown" or "front"."""
    a = normalize(surface_dir(*start))
    b = normalize(surface_dir(*end))
    fan_count = max(3, int(round(count * end_fan)))
    front_count = max(2, int(round(count * front_fan)))
    side_count = max(2, (count - fan_count - front_count) // 2)
    axis = normalize(sub(b, a))

    def frame(t):
        d = normalize(lerp(a, b, t))
        tangent = normalize(sub(axis, mul(d, dot(axis, d))))
        across = normalize(cross(d, tangent))
        return d, tangent, across

    def at(d, out, distance):
        return ob.surface(normalize(add(d, mul(out, distance / ob.radius(d)))), 0.0)
    roots = []
    for k in range(side_count):
        t = (k + 0.5) / side_count
        d, tangent, across = frame(t)
        roots.append((at(d, across, width), across, "side"))
    d, tangent, across = frame(1.0)
    for k in range(fan_count):
        ang = PI * (k + 0.5) / fan_count
        out = add(mul(across, math.cos(ang)), mul(tangent, math.sin(ang)))
        roots.append((at(d, out, width + 0.006), out, "crown"))
    for k in range(side_count):
        t = 1.0 - (k + 0.5) / side_count
        d, tangent, across = frame(t)
        roots.append((at(d, mul(across, -1.0), width), mul(across, -1.0), "side"))
    d, tangent, across = frame(0.0)
    for k in range(front_count):
        ang = PI * (k + 0.5) / front_count
        out = add(mul(across, -math.cos(ang)), mul(tangent, -math.sin(ang)))
        roots.append((at(d, out, width + 0.004), out, "front"))
    return roots


def hairline_roots(ob, line, count, inset=0.02, seed=0):
    """Roots all round the hairline line(phi) (a little above it), in order: for hair pulled back
    or up to a tie, whose strands start at the face and converge."""
    roots = []
    for k in range(count):
        phi = 2.0 * PI * (k + 0.2 * jitter(seed, k, 3)) / count - PI
        theta = line(phi) - inset
        p = ob.surface(surface_dir(theta, phi), 0.0)
        up = sub(ob.surface(surface_dir(theta - 0.05, phi), 0.0), p)
        roots.append((p, normalize(up), "hairline"))
    return roots


# ----- shells -------------------------------------------------------------------------------------

def _rows(count):
    """Row parameters 0..1 along a strand, closer together at the root (where the hair rises off
    the scalp) and at the tip (where it rounds off)."""
    return [0.55 * (0.5 - 0.5 * math.cos(PI * i / count)) + 0.45 * i / count for i in range(count + 1)]


def curl_bumps(seed, size, strength):
    """A bumpy field for curly hair: round curl clusters about `size` across scattered through
    space (a fixed hash), as an outward offset of up to `strength` at a point."""
    def field(p):
        gx, gy, gz = (int(math.floor(c / size)) for c in p)
        best = 0.0
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    cx, cy, cz = gx + dx, gy + dy, gz + dz
                    centre = ((cx + 0.5 + 0.4 * jitter(seed, cx, cy, cz, 1)) * size,
                              (cy + 0.5 + 0.4 * jitter(seed, cx, cy, cz, 2)) * size,
                              (cz + 0.5 + 0.4 * jitter(seed, cx, cy, cz, 3)) * size)
                    r = length(sub(p, centre)) / (0.75 * size)
                    if r < 1.0:
                        best = max(best, (1.0 - r * r) ** 1.5)
        return strength * best
    return field


def strand_paths(ob, roots, lengths, flow, seed, samples=24, length_jitter=0.10):
    """The paths shell() marches for these roots (so a caller can build on them too)."""
    paths = []
    for k, root in enumerate(roots):
        fl = flow(k) if callable(flow) else flow
        l = lengths(k) if callable(lengths) else lengths
        l *= 1.0 + 0.5 * length_jitter * jitter(seed, k, 11)
        paths.append(resample(march(ob, root[0], root[1], l, fl), samples))
    return paths


def ringlets(ob, paths, seed, start=0.12, radius=0.010, pitch=0.034, thick=0.0065, turns_per_step=5, which=None,
             lift=0.008, segments=5):
    """Spiral ringlet curls hanging along strand paths: each a tube coiling round its path from
    `start` metres on, `radius` out and a turn per `pitch` metres, `lift` outside the hair, tapering
    to a rounded end. which(k) picks the paths that carry one."""
    from .mesh import tube
    w = weights_for(ob.body)
    out = Mesh()
    for k, path in enumerate(paths):
        if which is not None and not which(k):
            continue
        acc = [0.0]
        for a_, b_ in zip(path, path[1:]):
            acc.append(acc[-1] + length(sub(b_, a_)))
        total = acc[-1]
        if total < start + pitch:
            continue
        span = total - start
        steps = max(8, int(span / pitch * turns_per_step))
        phase = 2.0 * PI * (0.5 + 0.5 * jitter(seed, k, 41))
        pts, radii = [], []
        j = 0
        for i in range(steps + 1):
            x = start + span * i / steps
            while j < len(path) - 2 and acc[j + 1] < x:
                j += 1
            f = (x - acc[j]) / max(acc[j + 1] - acc[j], 1e-9)
            p = lerp(path[j], path[j + 1], clamp(f, 0.0, 1.0))
            along = normalize(sub(path[j + 1], path[j]))
            away = (p[0] - ob.c[0], 0.0, p[2] - ob.c[2]) if p[1] < ob.c[1] - 0.10 * ob.s else sub(p, ob.c)
            n = normalize(sub(away, mul(along, dot(away, along))))
            side = normalize(cross(n, along))
            a = phase + 2.0 * PI * (x - start) / pitch
            grow = smoothstep(0.0, 0.5, (x - start) / pitch)
            r = radius * grow * (1.0 - 0.25 * (x - start) / span)
            centre = add(p, mul(n, lift))
            pts.append(add(centre, add(mul(n, r * math.sin(a)), mul(side, r * math.cos(a)))))
            radii.append(thick * (0.6 + 0.4 * grow) * (1.0 - 0.35 * (x - start) / span))
        out.append(tube(pts, radii, segments, lambda q: w(q), round_end=True))
    return out


def shell(ob, roots, lengths, flow, thickness, seed, sub_columns=3, samples=24, rows=14, groove=0.62, tip=0.35,
          tip_round=0.75, wave=None, curl=0.0, flick=0.0, length_jitter=0.10, thick_jitter=0.18, closed=True,
          axis=None, bumps=None, paths=None, notch=0.06):
    """A slab of hair over clumps between consecutive roots.

    roots: (point, direction, where) in order (crown_roots, part_roots, hairline_roots, ...);
      closed: the roots form a loop (else an open strip whose ends thin to nothing);
    lengths: metres per root (a number, or a function of the root index);
    flow: a Flow, or a function of the root index giving one;
    thickness(t): the hair's thickness at t along the strand (0 root, 1 tip);
    groove: the thickness between clumps as a share of their crest;
    tip: how far a clump's edges stop short of its point, as a share of `notch` metres (0 square-cut);
    notch: the depth of the notch between two pointed clumps at tip 1 (never more than the strand);
    wave: (amplitude, wavelength, start) ripples along the strands' normals from `start` metres on;
    curl: tips turned under (positive) or flipped out (negative), metres;
    flick: tips swung sideways (alternating by clump), metres;
    axis: a function of a point giving the point the hair surrounds there (a tail's core),
      instead of the head and body;
    bumps: an outward offset field (curl_bumps) over the outer face.
    paths: the strands' paths, when the caller marched them already (strand_paths).
    Returns a Mesh."""
    count = len(roots)
    clumps = count if closed else count - 1
    flows = [flow(k) if callable(flow) else flow for k in range(count)]
    if paths is None:
        paths = []
        for k, root in enumerate(roots):
            l = lengths(k) if callable(lengths) else lengths
            l *= 1.0 + 0.5 * length_jitter * jitter(seed, k, 11)
            paths.append(resample(march(ob, root[0], root[1], l, flows[k]), samples))
    nxt = lambda k: (k + 1) % count
    lengths_m = [sum(length(sub(b_, a_)) for a_, b_ in zip(path, path[1:])) for path in paths]
    # Per-clump variation: how far it reaches, how thick it is, how deep the grooves beside it are.
    # Long strands vary by a few centimetres, not by a share of their length.
    reach = [1.0 - min(length_jitter, 0.04 / max(lengths_m[k], 1e-6)) * (0.5 + 0.5 * jitter(seed, k, 21))
             for k in range(count)]
    thick = [1.0 + thick_jitter * jitter(seed, k, 22) for k in range(count)]
    grooves = [clamp(groove + 0.10 * jitter(seed, k, 23), 0.2, 0.95) for k in range(count)]
    # Boundary k is the path of root k, between clumps k - 1 and k.
    b_reach, b_thick = [], []
    for k in range(count):
        before = k - 1 if (closed or k > 0) else None
        after = k if (closed or k < clumps) else None
        rs = [reach[i] for i in (before, after) if i is not None]
        b_reach.append(min(rs) * (1.0 - min(0.9, tip * notch / max(lengths_m[k], 1e-6))))
        g = [grooves[i] * thick[i] for i in (before, after) if i is not None]
        b_thick.append(sum(g) / len(g) if (closed or 0 < k < clumps) else 0.0)
    ts = _rows(rows)

    def at(path, t):
        x = t * (samples - 1)
        i = min(int(x), samples - 2)
        return lerp(path[i], path[i + 1], x - i)

    columns = []   # (clump, f, t_end, thickness scale)
    for k in range(clumps):
        last = (not closed) and k == clumps - 1
        for j in range(sub_columns + (1 if last else 0)):
            f = j / sub_columns
            bump = math.sin(PI * f) ** 0.8
            base_end = b_reach[k] + (b_reach[nxt(k)] - b_reach[k]) * f
            t_end = base_end + (reach[k] - base_end) * math.sin(PI * f) ** 0.6
            edge = b_thick[k] + (b_thick[nxt(k)] - b_thick[k]) * f
            columns.append((k, f, t_end, edge + (thick[k] - edge) * bump))
    ncols = len(columns)
    inner, along = [], []
    for k, f, t_end, scale in columns:
        a, b = paths[k], paths[nxt(k)]
        col = []
        for tn in ts:
            p = lerp(at(a, tn * t_end), at(b, tn * t_end), f)
            if axis is None:
                p, _ = flows[k].settle(ob, p, flows[k].lift)
            col.append(p)
        inner.append(col)
        along.append([normalize(sub(col[min(i + 1, len(ts) - 1)], col[max(i - 1, 0)])) for i in range(len(ts))])
    # Normals of the inner sheet, from neighbouring columns and rows, pointing away from what the
    # hair covers.
    outward = []
    for c in range(ncols):
        col_n = []
        for i in range(len(ts)):
            if closed:
                across = sub(inner[(c + 1) % ncols][i], inner[c - 1][i])
            else:
                across = sub(inner[min(c + 1, ncols - 1)][i], inner[max(c - 1, 0)][i])
            n = normalize(cross(along[c][i], across))
            q = inner[c][i]
            if axis is not None:
                away = sub(q, axis(q))
            elif q[1] < ob.c[1] - 0.10 * ob.s:
                # Hanging hair faces away from the body's vertical axis.
                away = (q[0], 0.0, q[2] - ob.c[2])
            else:
                away = sub(q, ob.c)
            if length(n) < 1e-9:
                n = normalize(away)
            elif dot(n, away) < 0.0:
                n = mul(n, -1.0)
            col_n.append(n)
        outward.append(col_n)
    # Waves, curl and flick bend the whole sheet together.
    # Every offset is continuous across the clumps: a wave's phase is set per strand and blended
    # across each clump, and a flick swings a clump's middle, never its edges (each edge is a strand
    # the neighbouring clump shares, so offsets that differ there would crack the hair open).
    for c, (k, f, t_end, scale) in enumerate(columns):
        phase = 0.6 * (jitter(seed, k, 31) + (jitter(seed, nxt(k), 31) - jitter(seed, k, 31)) * f)
        metres = lengths_m[k] + (lengths_m[nxt(k)] - lengths_m[k]) * f
        for i, tn in enumerate(ts):
            t = tn * t_end
            offset = (0.0, 0.0, 0.0)
            if wave:
                amplitude, wavelength, start = wave
                x = t * metres
                w = amplitude * smoothstep(start, start + 0.05, x) * math.sin(2.0 * PI * (x - start) / wavelength + phase)
                offset = add(offset, mul(outward[c][i], w))
            if curl:
                offset = add(offset, mul(outward[c][i], -curl * smoothstep(0.65, 1.0, t) ** 2))
            if flick:
                side = normalize(cross(outward[c][i], along[c][i]))
                offset = add(offset, mul(side, flick * (1.0 if k % 2 else -1.0) * math.sin(PI * f) * smoothstep(0.75, 1.0, t) ** 2))
            if offset != (0.0, 0.0, 0.0):
                inner[c][i] = add(inner[c][i], offset)
    # The slab: outer rows root to tip, a rounded tip, inner rows back to the root (every other
    # one: it faces the scalp and is seen only where hair hangs free).
    back_rows = list(range(len(ts) - 1, -1, -2))
    if back_rows[-1] != 0:
        back_rows.append(0)
    grid, vs = [], []
    for c, (k, f, t_end, scale) in enumerate(columns):
        col, col_v = [], []
        for i, tn in enumerate(ts):
            rise = smoothstep(0.0, 0.08, tn) ** 0.6
            taper = 1.0 - tip_round * smoothstep(0.55, 1.0, tn) ** 1.5
            h = thickness(tn * t_end) * scale * rise * taper
            p = add(inner[c][i], mul(outward[c][i], h))
            if bumps is not None and h > 0.0:
                p = add(p, mul(outward[c][i], bumps(p) * min(1.0, scale) * rise * taper))
            col.append(p)
            col_v.append(tn * t_end)
        h_tip = thickness(t_end) * scale * (1.0 - tip_round)
        col.append(add(inner[c][-1], add(mul(outward[c][-1], h_tip * 0.55), mul(along[c][-1], max(h_tip * 0.6, 0.0008)))))
        col_v.append(t_end)
        for i in back_rows:
            col.append(inner[c][i])
            col_v.append(ts[i] * t_end)
        grid.append(col)
        vs.append(col_v)
    nrows = len(grid[0])
    sheet = [[grid[c][r] for c in range(ncols)] for r in range(nrows)]
    weights = weights_for(ob.body)
    whole = Mesh()
    whole.patch_origin = ob.c
    whole.grid(sheet, lambda p, v, u: weights(p), wrap=closed)
    # Re-emit per clump so u runs 0..1 across each clump (the texture is clamped, not tiled).
    out = Mesh()
    for k in range(clumps):
        cols = [c for c in range(ncols) if columns[c][0] == k]
        if closed or k < clumps - 1:
            cols.append((cols[-1] + 1) % ncols)
        base = len(out.positions)
        for r in range(nrows):
            for j, c in enumerate(cols):
                index = r * ncols + c
                out.positions.append(whole.positions[index])
                out.normals.append(whole.normals[index])
                out.uvs.append((j / sub_columns, clamp(vs[c][r], 0.0, 1.0)))
                out.skin.append(whole.skin[index])
        width = len(cols)
        for r in range(nrows - 1):
            for j in range(width - 1):
                a = base + r * width + j
                b, d, e = a + 1, a + width, a + width + 1
                out.indices += [a, d, e, a, e, b]
    # Face out, as the whole sheet's normals do.
    agree = 0
    for t0 in range(0, len(out.indices), 3):
        i0, i1, i2 = out.indices[t0:t0 + 3]
        n = cross(sub(out.positions[i1], out.positions[i0]), sub(out.positions[i2], out.positions[i0]))
        agree += 1 if dot(n, add(add(out.normals[i0], out.normals[i1]), out.normals[i2])) >= 0.0 else -1
    if agree < 0:
        out.indices = [out.indices[i + o] for i in range(0, len(out.indices), 3) for o in (0, 2, 1)]
    # Drop slivers: triangles with no area (where the slab closes at a root or a tip).
    kept = []
    for t0 in range(0, len(out.indices), 3):
        i0, i1, i2 = out.indices[t0:t0 + 3]
        if length(cross(sub(out.positions[i1], out.positions[i0]), sub(out.positions[i2], out.positions[i0]))) > 1e-12:
            kept += [i0, i1, i2]
    out.indices = kept
    return out
