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
RADII = (0.146, 0.170, 0.150)
SCALE = {"male": 1.12, "female": 1.08}
JAW = {"male": 0.25, "female": 0.31}
JAW_ANGLE = {"male": 0.07, "female": 0.03}
CHIN = {"male": 0.0045, "female": 0.0030}
NOSE_HEIGHT = {"male": 0.0215, "female": 0.0180}

# Feature anchors in head-local metres (male scale): eye centres, brows, nose, mouth, ears.
EYE = (0.054, -0.004)           # |x|, y on the face surface
BROW = (0.056, 0.056)
MOUTH_Y = -0.071
NOSE_ROOT_Y, NOSE_TIP_Y = -0.002, -0.032
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


def _blob(x, y, cx, cy, rx, ry):
    """A smooth bump of height 1 at (cx, cy) on the face, falling to 0 at the ellipse (rx, ry)."""
    q = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
    return (1.0 - q) ** 3 if q < 1.0 else 0.0


def _smax(a, b, k):
    """Smooth maximum: the larger of a and b, blended over k."""
    h = clamp(0.5 + 0.5 * (a - b) / k, 0.0, 1.0)
    return b + (a - b) * h + k * h * (1.0 - h)


def _nose(body, x, y):
    """The nose's height above the face at front coordinates (x, y), male metres: one smooth form,
    a narrow bridge rising from between the eyes into a rounded tip whose underside turns back
    into the upper lip, widening at its base into the wings."""
    tip = NOSE_TIP_Y + (0.003 if body == "female" else 0.0)
    u = (NOSE_ROOT_Y - y) / (NOSE_ROOT_Y - tip)
    if u < -0.35 or u > 1.75:
        return 0.0
    # Along the nose: low at the root, rising to the tip, rolling round under it.
    rise = 0.12 + 0.88 * smoothstep(-0.30, 1.02, u) ** 1.35
    fall = (1.0 - smoothstep(0.96, 1.55, u)) ** 0.7
    height = NOSE_HEIGHT[body] * rise * fall * smoothstep(-0.35, 0.05, u)
    # Across it: a rounded ridge, wider toward the base where the wings are.
    narrow = 0.92 if body == "female" else 1.0
    width = narrow * (0.0085 + 0.0055 * smoothstep(0.0, 0.85, u) + 0.0075 * smoothstep(0.72, 1.18, u) - 0.003 * smoothstep(1.2, 1.6, u))
    q = (x / width) ** 2
    if q >= 1.0:
        return 0.0
    roundness = 1.25 - 0.55 * smoothstep(0.55, 1.05, u)
    return height * (1.0 - q) ** roundness


# The eye opening as the expression atlas paints it (face.py: RX, RY, CY, FLOOR in the eye tile,
# whose half-size is EYE_TILE metres): the lids are sculpted round exactly that opening.
EYE_TILE = (0.038, 0.035)
APERTURE = (0.70, 0.52, 0.02, -0.44)


def _eye(x, y, side):
    """Relief of one eye at front coordinates (x, y) (male metres): the socket under the brow, the
    eyeball rounding forward in the opening, the upper lid standing over it with its crease above,
    a slimmer lower lid, and the inner corner tucked in toward the nose."""
    rx, ry, cy, floor = APERTURE
    tx = (x - side * EYE[0]) / EYE_TILE[0]
    ty = (y - EYE[1]) / EYE_TILE[1]
    o = side * tx                      # toward the outer corner
    ny = (ty - cy) / ry if ty >= cy else (ty - cy) / (cy - floor)
    e = math.hypot(tx / rx, ny)        # 1 on the painted opening's edge
    up = smoothstep(-0.45, 0.35, ny)   # the upper lid's share
    h = 0.0
    # The socket: deepest toward the nose under the brow, shallow out at the temple.
    h -= 0.0058 * _blob(o, ty, -0.12, 0.10, 1.55, 1.30)
    # The eyeball: a dome a little larger than the opening (the lids lie on it).
    if e < 1.30:
        h += 0.0048 * (1.0 - (e / 1.30) ** 2) ** 1.4
    # The upper lid: a rounded band along the opening's top edge standing over the eyeball, fading
    # into the crease; it thins toward the corners where it meets the lower lid.
    corners = 1.0 - 0.55 * smoothstep(0.55, 1.0, abs(tx) / rx)
    t = (e - 0.93) / 0.50
    lid = math.sin(math.pi * t ** 0.65) ** 0.8 if 0.0 < t < 1.0 else 0.0
    h += (0.0022 * up * corners + 0.0010 * (1.0 - up)) * lid
    # The crease over the upper lid, softer toward the corners.
    h -= 0.0004 * up * corners * math.exp(-((e - 1.46) / 0.16) ** 2)
    # The inner corner tucks in toward the nose.
    h -= 0.0008 * _blob(o, ty, -rx * 1.02, cy - 0.06, 0.28, 0.22)
    return h


def _features(body, x, y):
    """Relief of the face at front coordinates (x, y) (male metres), outward: broad, soft forms."""
    ex, ey = EYE
    h = 0.0
    for side in (1.0, -1.0):
        if side * x > -0.012:
            h += _eye(x, y, side)
        # A brow over each eye, and the cheekbone below and outside it.
        h += 0.0030 * _blob(x, y, side * (ex + 0.002), ey + 0.046, 0.054, 0.021)
        h += 0.0035 * _blob(x, y, side * 0.086, ey - 0.056, 0.050, 0.034)
    # The mouth region rounds forward as one form; the lips sit on it.
    h += 0.0042 * _blob(x, y, 0.0, MOUTH_Y + 0.004, 0.052, 0.040)
    h += _mouth(x, y)
    h += CHIN[body] * _blob(x, y, 0.0, MOUTH_Y - 0.050, 0.034, 0.028)
    return h + _nose(body, x, y)


# The mouth's parting line as the expression atlas paints the neutral mouth (face.py, the mouth
# tile at FEATURES["mouth"]): its height at the middle, how far it rises to the corners and where
# the corners are (male metres).
MOUTH_LINE = (-0.0742, 0.0044, 0.034)


def _mouth(x, y):
    """The lips at front coordinates (x, y) (male metres): an upper lip rising from the parting line
    to its border under the philtrum, a fuller lower lip, both thinning into the corners, which tuck
    in; the parting line itself a soft groove, and a groove under the lower lip."""
    y0, rise, half = MOUTH_LINE
    u = x / half
    line = y0 + rise * min(u * u, 1.6)
    d = y - line
    across = max(0.0, 1.0 - (x / 0.031) ** 2)
    h = 0.0
    if across > 0.0:
        if d >= 0.0:
            top = 0.0015 + 0.0040 * across ** 0.7
            t = min(d / top, 1.0)
            h += 0.0024 * across ** 0.5 * math.sin(math.pi * t) ** 0.6
        else:
            bottom = 0.0020 + 0.0072 * across ** 0.7
            t = min(-d / bottom, 1.0)
            h += 0.0029 * across ** 0.5 * math.sin(math.pi * t ** 0.8) ** 0.6
    # The lips part along the line, the parting deepening into the corners.
    h -= 0.0009 * math.exp(-(d / 0.0010) ** 2) * smoothstep(1.25, 0.85, abs(u))
    for side in (1.0, -1.0):
        h -= 0.0013 * _blob(x, y, side * 0.0355, y0 + rise * 1.09, 0.0085, 0.0065)
    # The philtrum: a shallow channel between two soft ridges from under the nose to the lip.
    nose_bottom = NOSE_TIP_Y - 0.011
    span = smoothstep(line + 0.0040, line + 0.0075, y) * (1.0 - smoothstep(nose_bottom - 0.007, nose_bottom - 0.001, y))
    if span > 0.0:
        h += span * (0.00022 * math.exp(-((abs(x) - 0.0055) / 0.0022) ** 2) - 0.00030 * math.exp(-(x / 0.0034) ** 2))
    # Under the lower lip a soft groove, then the chin.
    h -= 0.0012 * _blob(x, y, 0.0, y0 - 0.0135, 0.026, 0.0070)
    return h


def _clamped_spline(knots):
    """A C2 cubic spline through (t, value) knots with zero slope at both ends (so the head is
    smooth at its poles); returns the function of t."""
    ts = [k[0] for k in knots]
    vs = [k[1] for k in knots]
    n = len(ts)
    h = [ts[i + 1] - ts[i] for i in range(n - 1)]
    # Tridiagonal system for the second derivatives with clamped (zero) end slopes.
    a, b, c, r = [0.0] * n, [0.0] * n, [0.0] * n, [0.0] * n
    b[0], c[0], r[0] = 2.0 * h[0], h[0], 6.0 * ((vs[1] - vs[0]) / h[0])
    for i in range(1, n - 1):
        a[i], b[i], c[i] = h[i - 1], 2.0 * (h[i - 1] + h[i]), h[i]
        r[i] = 6.0 * ((vs[i + 1] - vs[i]) / h[i] - (vs[i] - vs[i - 1]) / h[i - 1])
    a[n - 1], b[n - 1], r[n - 1] = h[n - 2], 2.0 * h[n - 2], -6.0 * ((vs[n - 1] - vs[n - 2]) / h[n - 2])
    for i in range(1, n):
        w = a[i] / b[i - 1]
        b[i] -= w * c[i - 1]
        r[i] -= w * r[i - 1]
    m = [0.0] * n
    m[n - 1] = r[n - 1] / b[n - 1]
    for i in range(n - 2, -1, -1):
        m[i] = (r[i] - c[i] * m[i + 1]) / b[i]

    def fn(t):
        t = min(max(t, ts[0]), ts[-1])
        i = 0
        while i < n - 2 and t > ts[i + 1]:
            i += 1
        u = t - ts[i]
        hi = h[i]
        slope = (vs[i + 1] - vs[i]) / hi - hi * (2.0 * m[i] + m[i + 1]) / 6.0
        return vs[i] + slope * u + m[i] * u * u / 2.0 + (m[i + 1] - m[i]) * u ** 3 / (6.0 * hi)
    return fn


def _polar(curve):
    """(height, outward) silhouette points -> radius as a function of theta from the crown."""
    return _clamped_spline([(math.atan2(b, a), math.hypot(a, b)) for a, b in curve])


# The head's three silhouettes in head-local metres (male, before SCALE), crown to the bottom of
# the head where the neck enters: the mid-line profile of the face (height, forward) without its
# features, the back of the head (height, backward) and the outline seen from the front (height,
# sideways) -- a rounded cranium, a flat-fronted face that tapers to a defined chin, a jaw whose
# angle sits under the ear.
PROFILE = {
    "front": [(0.170, 0.0), (0.155, 0.068), (0.118, 0.116), (0.070, 0.142), (0.032, 0.150), (0.000, 0.149),
              (-0.040, 0.150), (-0.076, 0.150), (-0.104, 0.147), (-0.128, 0.140), (-0.147, 0.122), (-0.155, 0.092),
              (-0.152, 0.058), (-0.145, 0.025), (-0.142, 0.0)],
    "back": [(0.170, 0.0), (0.148, 0.082), (0.098, 0.132), (0.030, 0.149), (-0.030, 0.142), (-0.072, 0.118),
             (-0.104, 0.084), (-0.128, 0.042), (-0.142, 0.0)],
    "side": [(0.170, 0.0), (0.150, 0.082), (0.108, 0.128), (0.058, 0.145), (0.000, 0.146), (-0.040, 0.140),
             (-0.078, 0.124), (-0.106, 0.102), (-0.128, 0.068), (-0.142, 0.0)],
}
# The female head: a narrower jaw, a smaller chin and a softer brow.
FEMALE_CHANGES = {"front": {-0.104: -0.003, -0.128: -0.005, -0.147: -0.006, -0.155: -0.004, 0.032: -0.002},
                  "side": {-0.078: -0.006, -0.106: -0.012, -0.128: -0.010}}
# How square the head's horizontal sections are in front (2 round; more, broad planes that turn at
# the cheeks; less, pointed like the chin) by theta.
SQUARENESS = [(0.0, 2.0), (0.30 * math.pi, 2.1), (0.44 * math.pi, 2.40), (0.53 * math.pi, 2.30), (0.62 * math.pi, 2.05),
              (0.72 * math.pi, 1.95), (0.84 * math.pi, 1.95), (math.pi, 2.0)]


def _silhouettes(body):
    out = {}
    for name, curve in PROFILE.items():
        changes = FEMALE_CHANGES.get(name, {}) if body == "female" else {}
        out[name] = _polar([(a, b + changes.get(a, 0.0)) for a, b in curve])
    out["squareness"] = _clamped_spline(SQUARENESS)
    return out


_SILHOUETTES = {}


def _base_point(body, theta, phi):
    s = SCALE[body]
    if body not in _SILHOUETTES:
        _SILHOUETTES[body] = _silhouettes(body)
    curves = _SILHOUETTES[body]
    sp, cp = math.sin(phi), math.cos(phi)
    side = curves["side"](theta)
    if cp >= 0.0:
        depth, n = curves["front"](theta), curves["squareness"](theta)
    else:
        depth, n = curves["back"](theta), 2.0
    # The horizontal section through this direction: a superellipse through the three silhouettes.
    ax, az = abs(sp), abs(cp)
    if ax < 1e-12:
        r = depth
    elif az < 1e-12:
        r = side
    else:
        r = ((ax / side) ** n + (az / depth) ** n) ** (-1.0 / n)
    d = (math.sin(theta) * sp, math.cos(theta), math.sin(theta) * cp)
    x, y, z = d[0] * r, d[1] * r, d[2] * r
    # Features: relief over the front of the face, pushed outward along the direction.
    gate = smoothstep(0.30, 0.62, d[2])
    if gate > 0.0:
        # The relief is sculpted in front coordinates: find the push whose pushed point lies at the
        # coordinates it was measured at (the decals are placed by front coordinates too).
        push = _features(body, x, y) * gate
        for _ in range(2):
            push = _features(body, x + d[0] * push, y + d[1] * push) * gate
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


def _crowded(count, crowds, period):
    """count + 1 parameters over [0, period] (wrapping when period is 2 pi), crowded where the face's
    features are: the density is 1 + the sum of gain * exp(-((a - centre) / width)^2) over the
    (centre, width, gain) crowds, so features get fine facets and the rest of the head stays light."""
    samples = 4096
    mass, total = [0.0], 0.0
    for k in range(samples):
        a = period * (k + 0.5) / samples
        density = 1.0
        for centre, width, gain in crowds:
            delta = a - centre
            if period > 4.0:
                delta = (delta + math.pi) % (2.0 * math.pi) - math.pi
            density += gain * math.exp(-(delta / width) ** 2)
        total += density * period / samples
        mass.append(total)
    out, j = [], 0
    for i in range(count + 1):
        target = total * i / count
        while j < samples and mass[j + 1] < target:
            j += 1
        f = (target - mass[j]) / max(mass[j + 1] - mass[j], 1e-12) if j < samples else 0.0
        out.append(period * (j + f) / samples)
    return out


# Where the head's rings and columns crowd: (centre, width, gain) -- the face, the eyes and brows,
# the mouth; the face's columns, the eyes'.
RING_CROWDS = ((0.60 * math.pi, 0.36, 4.0), (0.475 * math.pi, 0.20, 6.0), (0.645 * math.pi, 0.10, 4.0))
COLUMN_CROWDS = ((math.pi, 0.55, 5.0), (math.pi + 0.36, 0.20, 3.0), (math.pi - 0.36, 0.20, 3.0))


_HEAD_MESHES = {}


def head_mesh(body, segments=132, rings=116):
    """The head surface, UV-mapped around (u, 0.5 at the face) and top to bottom (v = theta / pi),
    seam at the back; facets crowd onto the face, finest where the lids and lips are."""
    key = (body, segments, rings)
    if key in _HEAD_MESHES:
        return _HEAD_MESHES[key].copy()
    thetas = _crowded(rings, RING_CROWDS, math.pi)
    phis = _crowded(segments, COLUMN_CROWDS, 2.0 * math.pi)[:-1]
    rows = [[point(body, theta, math.pi + phi) for phi in phis] for theta in thetas]
    m = Mesh().grid(rows, HEAD, uv_seam=True)
    emitted = segments + 1
    for r, theta in enumerate(thetas):
        for c in range(emitted):
            m.uvs[r * emitted + c] = (phis[c] / (2.0 * math.pi) if c < segments else 1.0, theta / math.pi)
    _HEAD_MESHES[key] = m
    return m.copy()


def nose_mesh(body):
    """The nose is part of the head surface (_nose); nothing separate is drawn."""
    return Mesh()


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


def _ear_frame(body, side, e):
    """(centre, up, back, out) of one ear: tilted back, standing out from the head by its shape."""
    s = SCALE[body]
    surface = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
    tilt = math.radians(e["tilt"])
    up = (0.0, math.cos(tilt), -math.sin(tilt))
    back = (0.0, math.sin(tilt), math.cos(tilt))
    out = normalize((side, 0.0, -0.30 - 0.5 * (e["out"] - 0.30)))
    return add(surface, mul(out, (0.002 + 0.004 * (e["out"] - 0.30)) * s)), up, back, out


def _ear_outline(e, a):
    """The ear's outline at angle a (0 top, pi/2 toward the back of the head, pi the lobe, 3/2 pi
    the face side) as (up, back) in metres before SCALE: broad and round above, narrowing into the
    lobe, its front edge straighter where it joins the head."""
    h, w = e["h"], e["w"] * 1.22
    up = h * math.cos(a)
    back = w * math.sin(a)
    lower = smoothstep(0.0, -1.0, math.cos(a))
    back *= 1.0 - 0.30 * lower
    if math.sin(a) < 0.0:
        back *= 0.70
    up -= max(e["lobe"], 0.0) * lower ** 2
    return up, back + 0.12 * w * lower


def _ear_relief(e, r, a):
    """How far the front of the ear stands out of its plane (0..1 of its depth) at (r, a): a rolled
    helix rim open toward the face, a shallow bowl, the antihelix ridge inside the rim, the tragus
    over the opening and a full lobe."""
    top_to_back = smoothstep(-0.25, 0.10, math.cos(a - 0.35 * math.pi))      # helix: over the top and down the back
    opening = smoothstep(0.92 * math.pi, 1.10 * math.pi, a) * (1.0 - smoothstep(1.62 * math.pi, 1.82 * math.pi, a))
    rim = smoothstep(0.60, 0.84, r) * max(top_to_back, 1.0 - opening) * (1.0 - 0.55 * opening)
    f = 0.22 + 0.10 * smoothstep(0.0, 0.5, r)
    f = _smax(f, rim, 0.12)
    # The antihelix: a softer ridge inside the rim from the top round the back, forking upward.
    ridge_band = math.exp(-((r - 0.56) / 0.09) ** 2)
    ridge = 0.72 * ridge_band * smoothstep(0.05 * math.pi, 0.25 * math.pi, a) * (1.0 - smoothstep(0.80 * math.pi, 0.95 * math.pi, a))
    f = _smax(f, ridge, 0.10)
    # The tragus, a small flap in front of the opening.
    da = (a - 1.42 * math.pi + math.pi) % (2.0 * math.pi) - math.pi
    f = _smax(f, 0.80 * math.exp(-(da / 0.18) ** 2 - ((r - 0.62) / 0.14) ** 2), 0.08)
    # The lobe is full, not hollow.
    lobe = smoothstep(0.78 * math.pi, 0.95 * math.pi, a) * (1.0 - smoothstep(1.10 * math.pi, 1.26 * math.pi, a))
    f = _smax(f, 0.72 * lobe * smoothstep(0.30, 0.65, r), 0.10)
    return f


def ear_meshes(body, shape="round"):
    """Ears: each one closed form tilted back on the side of the head -- the rolled helix rim, the
    bowl it frames, the antihelix ridge, the tragus and the lobe sculpted into its front, a smooth
    back meeting the head. Skinned to the Head joint. Returns (shell, folds): the bowl, in the
    rim's shade, and the rest of the ear."""
    e = EAR_SHAPES[shape]
    s = SCALE[body]
    shell, folds = Mesh(), Mesh()
    columns = 28
    front_r = [0.0, 0.12, 0.24, 0.36, 0.46, 0.54, 0.60, 0.66, 0.72, 0.78, 0.84, 0.89, 0.93, 0.96, 0.985, 1.0]
    back_r = [0.97, 0.88, 0.70, 0.45, 0.0]
    depth = e["d"] * 1.25 * s
    for side in (1.0, -1.0):
        centre_pt, up, back, out = _ear_frame(body, side, e)

        def at(r, a, front):
            u, w = _ear_outline(e, a)
            # The edge rolls over: the height falls to nothing as r reaches the outline.
            roll = math.sqrt(max(0.0, 1.0 - ((r - 0.80) / 0.20) ** 2)) if r > 0.80 else 1.0
            if front:
                height = depth * _ear_relief(e, r, a) * roll
            else:
                height = -depth * 0.55 * roll * (0.5 + 0.5 * (1.0 - r))
            # Toward the face the ear lies against the head; its back edge stands out.
            lean = 0.5 + 0.5 * math.sin(a)
            height += depth * 0.35 * (lean - 0.5) * r
            p = add(centre_pt, add(mul(up, u * s * r), mul(back, -w * s * r)))
            return add(p, mul(out, height))
        rows = [[at(r, 2.0 * math.pi * c / columns, True) for c in range(columns)] for r in front_r]
        rows += [[at(r, 2.0 * math.pi * c / columns, False) for c in range(columns)] for r in back_r]
        whole = Mesh()
        whole.patch_origin = centre_pt
        whole.grid(rows, HEAD)
        # The bowl (front rows inside the antihelix) is drawn in its shade; the rows overlap by one
        # ring so the two parts close.
        split = front_r.index(0.54)
        cols = columns
        for target, r0, r1 in ((shell, 0, split), (folds, split, len(rows) - 1)):
            part = Mesh()
            index = {}
            for r in range(r0, r1 + 1):
                for c in range(cols):
                    index[r * cols + c] = len(part.positions)
                    part.positions.append(whole.positions[r * cols + c])
                    part.normals.append(whole.normals[r * cols + c])
                    part.uvs.append(whole.uvs[r * cols + c])
                    part.skin.append(whole.skin[r * cols + c])
            for k in range(0, len(whole.indices), 3):
                tri = whole.indices[k:k + 3]
                if all(t in index for t in tri):
                    part.indices += [index[t] for t in tri]
            target.append(part)
    return shell, folds


def ear_lobe(body, side, shape="round"):
    """Where an earring hangs from: the bottom of the ear's lobe on one side (1 left, -1 right)."""
    e = EAR_SHAPES[shape]
    s = SCALE[body]
    centre_pt, up, back, out = _ear_frame(body, side, e)
    u, w = _ear_outline(e, math.pi)
    r = 0.82
    point = add(centre_pt, add(mul(up, u * s * r), mul(back, -w * s * r)))
    return add(point, mul(out, e["d"] * 1.25 * s * 0.45)), out, up


def anchors(body):
    """World positions of the features, for decals, deformers and accessories."""
    s = SCALE[body]
    out = {}
    for side, name in ((1.0, "Left"), (-1.0, "Right")):
        out["eye" + name] = front_point(body, side * EYE[0] * s, EYE[1] * s)[0]
        out["brow" + name] = front_point(body, side * BROW[0] * s, BROW[1] * s)[0]
        out["ear" + name] = side_point(body, side, EAR[0] * s, EAR[1] * s)[0]
    out["mouth"] = front_point(body, 0.0, MOUTH_Y * s)[0]
    out["noseTip"] = front_point(body, 0.0, (NOSE_TIP_Y - 0.0035) * s)[0]
    out["noseRoot"] = front_point(body, 0.0, NOSE_ROOT_Y * s)[0]
    out["chin"] = front_point(body, 0.0, -0.13 * s)[0]
    out["crown"] = point(body, 0.0, 0.0)
    return out


def conforming_patch(body, x, y, width, height, lift):
    """A decal patch on the head mesh's own facets, centred at head-local front (x, y): the head's
    vertices there lifted `lift` along its normals, so the patch lies evenly over every lid, lip and
    fold however finely they are sculpted. UV (0,0) is the top-left as the viewer sees it (u grows
    toward the viewer's right, the avatar's left); the patch reaches a facet past the rectangle and
    the sampler holds the tile's transparent edge there."""
    m = head_mesh(body)
    c = centre(body)
    margin = 0.0035 * SCALE[body]
    x0, x1, y0, y1 = x - 0.5 * width, x + 0.5 * width, y - 0.5 * height, y + 0.5 * height
    near = set()
    for i, q in enumerate(m.positions):
        lx, ly, lz = q[0] - c[0], q[1] - c[1], q[2] - c[2]
        if lz > 0.0 and x0 - margin <= lx <= x1 + margin and y0 - margin <= ly <= y1 + margin:
            near.add(i)
    patch = Mesh()
    index = {}
    for t in range(0, len(m.indices), 3):
        tri = m.indices[t:t + 3]
        if not all(v in near for v in tri):
            continue
        for v in tri:
            if v not in index:
                index[v] = len(patch.positions)
                q, n = m.positions[v], m.normals[v]
                patch.positions.append(add(q, mul(n, lift)))
                patch.normals.append(n)
                patch.uvs.append((clamp((q[0] - c[0] - x0) / width, 0.0, 1.0), clamp((y1 - (q[1] - c[1])) / height, 0.0, 1.0)))
                patch.skin.append(((I["Head"], 0, 0, 0), (255, 0, 0, 0)))
        patch.indices += [index[v] for v in tri]
    return patch


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
