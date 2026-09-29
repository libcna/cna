# SPDX-License-Identifier: MS-PL
"""The sculpted head (catalog v2): a star-shaped surface around the head centre, made of a smooth
union of ellipsoids (cranium, face, cheeks, brow, muzzle, chin) with soft eye sockets, plus a
separate nose and ears. Everything is skinned to the Head joint.

Head-local axes: +x the avatar's left, +y up, +z the face. Angles follow mesh.ellipsoid: theta
from the crown (0) to under the jaw (pi), phi from the face (0) toward the avatar's left (pi/2).
"""
import math

from . import rig
from .mathutil import add, sub, mul, normalize, length, dot, cross, lerp, smoothstep, clamp
from .mesh import Mesh, tube

I = rig.INDEX
HEAD = lambda *args: {I["Head"]: 1.0}

# The male head in head-local metres; the female head is the same shape at 0.955 of its size
# with a slightly narrower jaw.
RADII = (0.145, 0.172, 0.150)
SCALE = {"male": 1.08, "female": 1.03}
JAW = {"male": 0.20, "female": 0.24}

# Feature anchors in head-local metres (male scale): eye centres, brows, nose, mouth, ears.
EYE = (0.054, -0.004)           # |x|, y on the face surface
BROW = (0.056, 0.056)
MOUTH_Y = -0.082
NOSE_ROOT_Y, NOSE_TIP_Y = -0.006, -0.028
EAR = (-0.012, -0.014)          # y, z of the ear centre; x is on the side surface


def centre(body):
    return (0.0, rig.PROPORTIONS[body]["head_center"], 0.0)


def _bump(d, towards, inner, outer):
    """1 when direction d points at `towards`, falling to 0 between cos(inner) and cos(outer)."""
    return smoothstep(math.cos(math.radians(outer)), math.cos(math.radians(inner)), dot(d, normalize(towards)))


def local_point(body, theta, phi, lift=0.0):
    """Surface point (head-local, the body's size) at (theta, phi), `lift` metres outward."""
    p = _base_point(body, theta, phi)
    if lift:
        p = add(p, mul(_normal(body, theta, phi), lift))
    return p


def _base_point(body, theta, phi):
    s = SCALE[body]
    d = (math.sin(theta) * math.sin(phi), math.cos(theta), math.sin(theta) * math.cos(phi))
    x, y, z = d[0] * RADII[0], d[1] * RADII[1], d[2] * RADII[2]
    # An egg: the jaw narrows below the eyes, the chin stays forward.
    below = smoothstep(0.05, -1.0, d[1])
    x *= 1.0 - JAW[body] * below
    z *= 1.0 - 0.06 * below
    # A flatter face plane between the brow and the chin.
    front = smoothstep(0.35, 0.95, d[2]) * (1.0 - smoothstep(0.45, 0.85, abs(d[1])))
    z *= 1.0 - 0.07 * front
    # Soft cheeks, brow, chin and the back of the skull, pushed along the direction.
    push = 0.0
    for side in (1.0, -1.0):
        push += 0.010 * _bump(d, (side * 0.55, -0.30, 0.78), 6.0, 34.0)
        push -= 0.0040 * _bump(d, (side * EYE[0] / 0.13, EYE[1] / 0.13, 1.0), 3.0, 17.0)
        push += 0.0030 * _bump(d, (side * 0.36, 0.34, 0.86), 4.0, 22.0)
    push += 0.012 * _bump(d, (0.0, -0.78, 0.62), 4.0, 26.0)
    push += 0.008 * _bump(d, (0.0, -0.10, -1.0), 10.0, 60.0)
    x, y, z = x + d[0] * push, y + d[1] * push, z + d[2] * push
    return (x * s, y * s, z * s)


def _normal(body, theta, phi):
    e = 1e-4
    t = min(max(theta, e), math.pi - e)
    p = _base_point(body, t, phi)
    a = sub(_base_point(body, t + e, phi), _base_point(body, t - e, phi))
    b = sub(_base_point(body, t, phi + e), _base_point(body, t, phi - e))
    n = normalize(cross(a, b))
    return n if dot(n, p) > 0 else mul(n, -1.0)


def surface_normal(body, theta, phi):
    return _normal(body, theta, phi)


def point(body, theta, phi, lift=0.0):
    return add(centre(body), local_point(body, theta, phi, lift))


def _solve(body, target, guess, axes):
    """(theta, phi) whose surface point matches `target` on the two given axes (Newton)."""
    theta, phi = guess
    for _ in range(12):
        p = _base_point(body, theta, phi)
        f = (p[axes[0]] - target[0], p[axes[1]] - target[1])
        if abs(f[0]) + abs(f[1]) < 1e-9:
            break
        e = 1e-5
        pt = _base_point(body, theta + e, phi)
        pp = _base_point(body, theta, phi + e)
        j = ((pt[axes[0]] - p[axes[0]]) / e, (pp[axes[0]] - p[axes[0]]) / e,
             (pt[axes[1]] - p[axes[1]]) / e, (pp[axes[1]] - p[axes[1]]) / e)
        det = j[0] * j[3] - j[1] * j[2]
        if abs(det) < 1e-12:
            break
        theta -= (j[3] * f[0] - j[1] * f[1]) / det
        phi -= (-j[2] * f[0] + j[0] * f[1]) / det
    return theta, phi


def front_point(body, x, y, lift=0.0):
    """The surface point seen straight from the front at head-local (x, y); returns
    (world point, theta, phi)."""
    s = SCALE[body]
    guess = (math.acos(clamp(y / (RADII[1] * s), -0.99, 0.99)), math.asin(clamp(x / (RADII[0] * s), -0.9, 0.9)))
    theta, phi = _solve(body, (x, y), guess, (0, 1))
    return add(centre(body), local_point(body, theta, phi, lift)), theta, phi


def side_point(body, side, y, z, lift=0.0):
    """The surface point on the avatar's left (side 1) or right (-1) at head-local (y, z)."""
    theta, phi = _solve(body, (y, z), (math.pi / 2, side * math.pi / 2), (1, 2))
    return add(centre(body), local_point(body, theta, phi, lift)), theta, phi


def head_mesh(body, segments=64, rings=46):
    """The head surface, UV-mapped around (u) and top to bottom (v), seam at the back."""
    rows = []
    for r in range(rings + 1):
        # Rings crowd toward the face's latitudes, where the features are.
        t = r / rings
        theta = math.pi * (t - 0.06 * math.sin(2.0 * math.pi * t))
        rows.append([point(body, theta, math.pi + 2.0 * math.pi * c / segments) for c in range(segments)])
    return Mesh().grid(rows, HEAD, uv_seam=True)


def nose_mesh(body):
    """A small rounded button nose: a short soft bridge into a ball tip with little wings."""
    s = SCALE[body]
    root, _, _ = front_point(body, 0.0, NOSE_ROOT_Y * s, -0.006 * s)
    tip_base, _, _ = front_point(body, 0.0, NOSE_TIP_Y * s)
    tip = add(tip_base, (0.0, 0.002 * s, 0.009 * s))
    m = Mesh()
    path = [lerp(root, tip, k / 4.0) for k in range(5)]
    radii = [0.0065 * s, 0.0070 * s, 0.0082 * s, 0.0105 * s, 0.0125 * s]
    m.append(tube(path, radii, 14, HEAD, round_end=True))
    m.append(_blob(add(tip, (0.0, -0.001 * s, -0.002 * s)), (0.0135 * s, 0.0115 * s, 0.0110 * s), 14, 10))
    for side in (1.0, -1.0):
        wing = add(tip_base, (side * 0.0125 * s, 0.0005 * s, 0.001 * s))
        m.append(_blob(wing, (0.0080 * s, 0.0072 * s, 0.0080 * s)))
    return m


def _blob(c, r, segments=12, rings=8):
    from .mesh import ellipsoid
    return ellipsoid(c, ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), r, segments, rings, HEAD)


def ear_meshes(body):
    """Ears: a flattened lobe tilted back on each side of the head, with a rolled rim."""
    from .mesh import ellipsoid
    s = SCALE[body]
    m = Mesh()
    for side in (1.0, -1.0):
        surface = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
        tilt = math.radians(14.0)
        up = (0.0, math.cos(tilt), -math.sin(tilt))
        back = (0.0, math.sin(tilt), math.cos(tilt))
        out = normalize((side, 0.0, -0.30))
        centre_pt = add(surface, mul(out, 0.004 * s))
        axes = (normalize(cross(up, back)) if side > 0 else mul(normalize(cross(up, back)), -1.0), up, back)
        m.append(ellipsoid(centre_pt, (out, up, back), (0.0085 * s, 0.031 * s, 0.019 * s), 14, 12, HEAD))
        rim = []
        for k in range(18):
            a = math.pi * (0.08 + 1.72 * k / 17)
            h = 0.028 * s * math.cos(a) + 0.002 * s
            w = 0.016 * s * math.sin(a)
            rim.append(add(add(add(centre_pt, mul(up, h)), mul(back, -w)), mul(out, 0.0065 * s)))
        m.append(tube(rim, [0.0046 * s] * len(rim), 8, HEAD, round_end=True))
    return m


def anchors(body):
    """World positions of the features, for decals, deformers and accessories."""
    s = SCALE[body]
    out = {}
    for side, name in ((1.0, "Left"), (-1.0, "Right")):
        out["eye" + name] = front_point(body, side * EYE[0] * s, EYE[1] * s)[0]
        out["brow" + name] = front_point(body, side * BROW[0] * s, BROW[1] * s)[0]
        out["ear" + name] = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
    out["mouth"] = front_point(body, 0.0, MOUTH_Y * s)[0]
    out["noseTip"] = add(front_point(body, 0.0, NOSE_TIP_Y * s)[0], (0.0, 0.004 * s, 0.02 * s))
    out["noseRoot"] = front_point(body, 0.0, NOSE_ROOT_Y * s)[0]
    out["chin"] = front_point(body, 0.0, -0.13 * s)[0]
    out["crown"] = point(body, 0.0, 0.0)
    return out


def decal_patch(body, x, y, width, height, lift, columns=10, rows=10):
    """A patch following the head surface, centred at head-local front (x, y), UV (0,0) top-left
    as the viewer sees it (u grows toward the avatar's right... i.e. the viewer's right)."""
    grid, normals = [], []
    for r in range(rows + 1):
        row = []
        for c in range(columns + 1):
            u, v = c / columns, r / rows
            # The viewer's right is the avatar's left (+x).
            px = x + (u - 0.5) * width
            py = y + (0.5 - v) * height
            p, theta, phi = front_point(body, px, py, lift)
            row.append(p)
            normals.append(surface_normal(body, theta, phi))
        grid.append(row)
    m = Mesh()
    base = 0
    for r in range(rows + 1):
        for c in range(columns + 1):
            m.positions.append(grid[r][c])
            m.uvs.append((c / columns, r / rows))
            m.skin.append(((I["Head"], 0, 0, 0), (255, 0, 0, 0)))
    for r in range(rows):
        for c in range(columns):
            a = base + r * (columns + 1) + c
            b, d, e = a + 1, a + columns + 1, a + columns + 2
            # Counter-clockwise seen from the front (+z).
            m.indices += [a, d, b, b, d, e]
    m.normals = normals
    return m
