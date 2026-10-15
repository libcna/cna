# SPDX-License-Identifier: MS-PL
"""The sculpted head (catalog v3): a star-shaped surface around the head centre, made of a smooth
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

# The male head in head-local metres; the female head is the same shape a little smaller with a
# narrower jaw. Revision 2: an adult face -- a rounded cranium over cheekbones, a jaw that tapers
# to a defined chin.
RADII = (0.148, 0.172, 0.150)
SCALE = {"male": 1.12, "female": 1.08}
JAW = {"male": 0.27, "female": 0.32}

# Feature anchors in head-local metres (male scale): eye centres, brows, nose, mouth, ears.
EYE = (0.054, -0.004)           # |x|, y on the face surface
BROW = (0.056, 0.056)
MOUTH_Y = -0.076
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
    front = max(d[2], 0.0)
    below = smoothstep(-0.10, -0.95, d[1])
    # The jaw tapers toward a defined chin; at the jaw's angle, under the ears, it stays wider.
    x *= 1.0 - JAW[body] * below * (0.45 + 0.55 * smoothstep(-0.2, 0.8, d[2]))
    # The lower face is shallower at its sides than the cranium: the jaw recedes from the chin.
    z *= 1.0 - 0.14 * below * smoothstep(0.15, 0.75, abs(d[0]))
    # The underside of the jaw rises behind the chin, so the jaw line reads and the neck has room.
    y *= 1.0 - 0.10 * smoothstep(-0.55, -1.0, d[1]) * smoothstep(0.55, -0.2, d[2])
    # The face comes forward of the cranium between the brow and the chin; the sides of the
    # cranium flatten a little above the ears.
    face = smoothstep(0.25, 0.9, d[2]) * (1.0 - smoothstep(0.55, 0.95, d[1]))
    z *= 1.0 + 0.04 * face
    x *= 1.0 - 0.035 * smoothstep(0.55, 0.95, abs(d[0])) * smoothstep(-0.2, 0.4, d[1])
    # Seen from above the head is an egg, narrower at the face: the nose leads the cheeks.
    x *= 1.0 - 0.10 * smoothstep(0.15, 1.0, d[2]) * (1.0 - smoothstep(0.55, 1.0, d[1]))
    # Cheekbones, brow, chin and soft eye sockets, pushed along the direction.
    push = 0.0
    for side in (1.0, -1.0):
        push += 0.011 * _bump(d, (side * 0.62, -0.06, 0.78), 5.0, 30.0)
        push -= 0.0045 * _bump(d, (side * EYE[0] / 0.13, EYE[1] / 0.13, 1.0), 3.0, 16.0)
        push += 0.0035 * _bump(d, (side * 0.36, 0.34, 0.86), 4.0, 22.0)
    push += 0.012 * _bump(d, (0.0, -0.78, 0.63), 4.0, 24.0)
    push += 0.004 * _bump(d, (0.0, 0.30, 0.95), 6.0, 30.0)
    push -= 0.006 * _bump(d, (0.0, -0.10, -1.0), 10.0, 70.0)
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
    """A rounded nose: a soft bridge from between the eyes into a ball tip that leads the face, with
    small wings either side."""
    s = SCALE[body]
    root, _, _ = front_point(body, 0.0, NOSE_ROOT_Y * s, -0.004 * s)
    tip_base, _, _ = front_point(body, 0.0, NOSE_TIP_Y * s)
    tip = add(tip_base, (0.0, 0.002 * s, 0.012 * s))
    m = Mesh()
    path = [lerp(root, tip, k / 5.0) for k in range(6)]
    radii = [0.0068 * s, 0.0074 * s, 0.0082 * s, 0.0096 * s, 0.0114 * s, 0.0128 * s]
    m.append(tube(path, radii, 14, HEAD, round_end=True))
    m.append(_blob(add(tip, (0.0, -0.0015 * s, -0.004 * s)), (0.0142 * s, 0.0120 * s, 0.0120 * s), 14, 10))
    for side in (1.0, -1.0):
        wing = add(tip_base, (side * 0.0128 * s, -0.0004 * s, 0.002 * s))
        m.append(_blob(wing, (0.0082 * s, 0.0074 * s, 0.0086 * s)))
    return m


def _blob(c, r, segments=12, rings=8):
    from .mesh import ellipsoid
    return ellipsoid(c, ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)), r, segments, rings, HEAD)


# Ear shapes: (height, width, depth of the lobe shell, rim height and width, rim thickness, how far
# the ear stands out from the head (0 flat .. 1), tilt back in degrees, lobe (extra drop and size of
# a separate lobe; 0 = attached)).
EAR_SHAPES = {
    "round": dict(h=0.031, w=0.019, d=0.0085, rim=(0.028, 0.016), rim_r=0.0046, out=0.30, tilt=14.0, lobe=0.0),
    "small": dict(h=0.025, w=0.016, d=0.0075, rim=(0.022, 0.0135), rim_r=0.0040, out=0.22, tilt=12.0, lobe=0.0),
    "large": dict(h=0.036, w=0.022, d=0.0095, rim=(0.033, 0.019), rim_r=0.0052, out=0.34, tilt=15.0, lobe=0.0),
    "longLobe": dict(h=0.033, w=0.018, d=0.0085, rim=(0.028, 0.016), rim_r=0.0046, out=0.28, tilt=12.0, lobe=0.010),
    "attached": dict(h=0.029, w=0.019, d=0.0080, rim=(0.027, 0.016), rim_r=0.0044, out=0.20, tilt=16.0, lobe=-0.004),
    "protruding": dict(h=0.032, w=0.020, d=0.0085, rim=(0.029, 0.017), rim_r=0.0048, out=0.62, tilt=10.0, lobe=0.0),
}
EAR_ORDER = ("round", "small", "large", "longLobe", "attached", "protruding")


def ear_meshes(body, shape="round"):
    """Ears: a flattened shell tilted back on each side of the head, a rolled rim and, for some
    shapes, a hanging lobe. Each ear is skinned to the Head joint."""
    from .mesh import ellipsoid
    e = EAR_SHAPES[shape]
    s = SCALE[body]
    m = Mesh()
    for side in (1.0, -1.0):
        surface = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
        tilt = math.radians(e["tilt"])
        up = (0.0, math.cos(tilt), -math.sin(tilt))
        back = (0.0, math.sin(tilt), math.cos(tilt))
        out = normalize((side, 0.0, -0.30 - 0.5 * (e["out"] - 0.30)))
        centre_pt = add(surface, mul(out, (0.004 + 0.004 * (e["out"] - 0.30)) * s))
        # The shell, its rolled rim (helix) along the back and top, and a softer inner ridge
        # (antihelix): a filled ear, not a hoop.
        m.append(ellipsoid(centre_pt, (out, up, back), (e["d"] * 1.25 * s, e["h"] * s, e["w"] * s), 14, 12, HEAD))
        rim, ridge = [], []
        rh, rw = e["rim"]
        for k in range(18):
            a = math.pi * (0.08 + 1.72 * k / 17)
            h = rh * s * math.cos(a) + 0.002 * s
            w = rw * s * math.sin(a)
            rim.append(add(add(add(centre_pt, mul(up, h)), mul(back, -w)), mul(out, 0.0050 * s)))
        for k in range(10):
            a = math.pi * (0.30 + 1.05 * k / 9)
            ridge.append(add(add(add(centre_pt, mul(up, rh * 0.55 * s * math.cos(a) + 0.003 * s)),
                                 mul(back, -rw * 0.55 * s * math.sin(a))), mul(out, 0.0058 * s)))
        m.append(tube(rim, [e["rim_r"] * s] * len(rim), 8, HEAD, round_end=True))
        m.append(tube(ridge, [e["rim_r"] * 0.62 * s] * len(ridge), 6, HEAD, round_end=True))
        if e["lobe"] > 0.0:
            lobe = add(add(centre_pt, mul(up, -(e["h"] * 0.82 + e["lobe"] * 0.5) * s)), mul(back, 0.002 * s))
            m.append(ellipsoid(lobe, (out, up, back), (e["d"] * 0.8 * s, (0.006 + e["lobe"] * 0.6) * s, 0.0085 * s), 12, 8, HEAD))
    return m


def ear_lobe(body, side, shape="round"):
    """Where an earring hangs from: the bottom of the ear's lobe on one side (1 left, -1 right)."""
    e = EAR_SHAPES[shape]
    s = SCALE[body]
    surface = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
    tilt = math.radians(e["tilt"])
    up = (0.0, math.cos(tilt), -math.sin(tilt))
    out = normalize((side, 0.0, -0.30 - 0.5 * (e["out"] - 0.30)))
    centre_pt = add(surface, mul(out, (0.004 + 0.004 * (e["out"] - 0.30)) * s))
    return add(add(centre_pt, mul(up, -(e["h"] * 0.80 + max(e["lobe"], 0.0)) * s)), mul(out, 0.004 * s)), out, up


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
