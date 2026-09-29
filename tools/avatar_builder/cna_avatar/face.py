# SPDX-License-Identifier: MS-PL
"""Face features: decal patches on the head and an original atlas of every XNA expression state.

Tiles are drawn as a viewer facing the avatar sees them, so the avatar's left eye is the one on
the image's right; `inner` is the side toward the nose. The iris layer is tinted with the eye
color; its highlight is a hole that lets the white eye layer beneath show through.
"""
import math

from . import rig, png
from .mathutil import add, clamp
from .mesh import Mesh, ellipsoid

EYES = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "Yawning", "Sleeping", "LookUp",
        "LookDown", "LookLeft", "LookRight", "Blink"]
EYEBROWS = ["Neutral", "Sad", "Angry", "Confused", "Raised"]
MOUTHS = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "PhoneticO", "PhoneticAi",
          "PhoneticEe", "PhoneticFv", "PhoneticW", "PhoneticL", "PhoneticDth"]
TILE = 64
COLUMNS = 16

OUTLINE = (46, 32, 30)
WHITE = (250, 250, 248)
PUPIL = (18, 14, 16)
LIP = (150, 66, 66)
INSIDE = (74, 22, 34)
TEETH = (250, 250, 244)
TONGUE = (222, 112, 122)


# ----- tiny shape language: each shape is (inside(x, y) -> bool, rgb) -------------------------

def ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0


def ring(cx, cy, rx, ry, width):
    inner = ellipse(cx, cy, max(rx - width, 1e-3), max(ry - width, 1e-3))
    outer = ellipse(cx, cy, rx, ry)
    return lambda x, y: outer(x, y) and not inner(x, y)


def arc(cx, cy, r, a0, a1, width):
    def fn(x, y):
        d = math.hypot(x - cx, y - cy)
        if abs(d - r) > width / 2:
            # Rounded ends.
            for a in (a0, a1):
                ex, ey = cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))
                if math.hypot(x - ex, y - ey) <= width / 2:
                    return True
            return False
        a = math.degrees(math.atan2(y - cy, x - cx)) % 360.0
        return a0 <= a <= a1
    return fn


def stroke(points, width):
    """Polyline with round caps."""
    def fn(x, y):
        for (x0, y0), (x1, y1) in zip(points, points[1:]):
            dx, dy = x1 - x0, y1 - y0
            t = clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy), 0.0, 1.0)
            if math.hypot(x - (x0 + t * dx), y - (y0 + t * dy)) <= width / 2:
                return True
        return False
    return fn


def halfplane(x0, y0, x1, y1):
    """Points below the line from (x0, y0) to (x1, y1) (y smaller)."""
    return lambda x, y: y <= y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def both(a, b): return lambda x, y: a(x, y) and b(x, y)
def minus(a, b): return lambda x, y: a(x, y) and not b(x, y)


def render(shapes, width=TILE, height=TILE, samples=3):
    """Supersampled straight-alpha RGBA tile; later shapes cover earlier ones."""
    pixels = bytearray()
    for py in range(height):
        for px in range(width):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(samples):
                for sx in range(samples):
                    x = ((px + (sx + 0.5) / samples) / width) * 2.0 - 1.0
                    y = 1.0 - ((py + (sy + 0.5) / samples) / height) * 2.0
                    color = None
                    for inside, rgb in shapes:
                        if inside(x, y):
                            color = rgb
                    if color:
                        acc[0] += color[0]; acc[1] += color[1]; acc[2] += color[2]; acc[3] += 1.0
            n = samples * samples
            if acc[3]:
                pixels += bytes((int(round(acc[0] / acc[3])), int(round(acc[1] / acc[3])), int(round(acc[2] / acc[3])),
                                 int(round(255 * acc[3] / n))))
            else:
                pixels += b"\0\0\0\0"
    return pixels


# ----- eyes -------------------------------------------------------------------------------------

def eye_layers(state, side):
    """(white layer shapes, iris layer shapes) for 'Left'/'Right'."""
    inner = -1.0 if side == "Left" else 1.0
    rx, ry = 0.58, 0.76
    iris = (0.0, -0.05)
    iris_r, pupil_r = 0.44, 0.2
    opening = ellipse(0.0, 0.0, rx, ry)
    lid = None      # (y at the inner corner, y at the outer corner) of an upper lid line
    closed = None
    if state == "Sad":
        lid = (0.62, 0.1)
        iris = (0.0, -0.22)
    elif state == "Angry":
        lid = (0.05, 0.6)
        iris = (0.0, -0.12)
    elif state == "Confused":
        ry = 0.5 if side == "Left" else 0.66
        opening = ellipse(0.0, 0.0, rx, ry)
        iris = (0.18, 0.08)
    elif state == "Shocked":
        rx, ry = 0.68, 0.86
        opening = ellipse(0.0, 0.0, rx, ry)
        iris, iris_r, pupil_r = (0.0, 0.0), 0.34, 0.12
    elif state == "LookUp":
        iris = (0.0, 0.3)
    elif state == "LookDown":
        iris = (0.0, -0.32)
    elif state == "LookLeft":
        iris = (0.26, -0.04)   # the avatar's left is the viewer's right
    elif state == "LookRight":
        iris = (-0.26, -0.04)
    elif state == "Happy":
        closed = arc(0.0, -0.55, 0.62, 35, 145, 0.17)
    elif state == "Laughing":
        closed = arc(0.0, -0.72, 0.7, 42, 138, 0.22)
    elif state == "Yawning":
        closed = arc(0.0, 0.62, 0.66, 222, 318, 0.16)
    elif state == "Sleeping":
        closed = arc(0.0, 0.5, 0.6, 215, 325, 0.13)
    elif state == "Blink":
        closed = stroke([(-0.55, -0.08), (0.0, -0.14), (0.55, -0.08)], 0.12)
    if closed:
        return [(closed, OUTLINE)], []
    below = lambda offset: (lambda x, y: True) if lid is None else \
        halfplane(inner * rx, lid[0] + offset, -inner * rx, lid[1] + offset)
    outline = both(ellipse(0.0, 0.0, rx + 0.07, ry + 0.07), below(0.07))
    visible = both(opening, below(0.0))
    white = [(outline, OUTLINE), (visible, WHITE)]
    iris_shape = both(visible, ellipse(iris[0], iris[1], iris_r, iris_r))
    pupil = both(visible, ellipse(iris[0], iris[1] - 0.02, pupil_r, pupil_r * 1.1))
    highlight = ellipse(iris[0] - 0.15, iris[1] + 0.18, 0.11, 0.11)
    return white, [(minus(iris_shape, highlight), (255, 255, 255)), (minus(pupil, highlight), PUPIL)]


# ----- eyebrows (drawn white: tinted by the hair color) -----------------------------------------

def eyebrow(state, side):
    inner = -1.0 if side == "Left" else 1.0
    w = 0.34
    if state == "Neutral":
        pts = [(inner * 0.72, -0.25), (inner * 0.2, 0.2), (-inner * 0.35, 0.22), (-inner * 0.75, -0.1)]
    elif state == "Sad":
        pts = [(inner * 0.72, 0.35), (inner * 0.1, 0.1), (-inner * 0.75, -0.35)]
    elif state == "Angry":
        pts = [(inner * 0.72, -0.45), (inner * 0.1, -0.05), (-inner * 0.75, 0.35)]
    elif state == "Raised":
        pts = [(inner * 0.72, 0.1), (inner * 0.2, 0.62), (-inner * 0.35, 0.64), (-inner * 0.75, 0.25)]
    else:  # Confused: the left brow lifts, the right one knits
        if side == "Left":
            pts = [(inner * 0.72, 0.05), (inner * 0.15, 0.5), (-inner * 0.4, 0.55), (-inner * 0.75, 0.2)]
        else:
            pts = [(inner * 0.72, -0.35), (inner * 0.1, -0.15), (-inner * 0.75, -0.2)]
    return [(stroke(pts, w), (255, 255, 255))]


# ----- mouths -----------------------------------------------------------------------------------

def mouth(state):
    s = []
    if state == "Neutral":
        s.append((arc(0.0, 1.0, 1.25, 248, 292, 0.14), LIP))
    elif state == "Happy":
        s.append((arc(0.0, 0.95, 1.2, 236, 304, 0.17), LIP))
    elif state == "Sad":
        s.append((arc(0.0, -1.15, 1.2, 62, 118, 0.15), LIP))
    elif state == "Confused":
        pts = [(-0.55 + 1.1 * i / 12, 0.1 * math.sin(i / 12 * 2.5 * math.pi) + 0.1 * (i / 12 - 0.5)) for i in range(13)]
        s.append((stroke(pts, 0.13), LIP))
    else:
        if state == "Laughing":
            shape = both(ellipse(0.0, 0.25, 0.78, 0.8), lambda x, y: y <= 0.25)
            s += [(ellipse(0.0, 0.25, 0.86, 0.88) if False else both(ellipse(0.0, 0.25, 0.86, 0.88), lambda x, y: y <= 0.33), LIP),
                  (shape, INSIDE), (both(shape, lambda x, y: y >= 0.06), TEETH),
                  (both(shape, ellipse(0.0, -0.55, 0.42, 0.3)), TONGUE)]
            return s
        if state == "Angry":
            box = lambda x, y: abs(x) <= 0.58 and -0.28 <= y <= 0.2
            outer = lambda x, y: abs(x) <= 0.66 and -0.36 <= y <= 0.28
            s += [(outer, LIP), (box, TEETH), (both(box, lambda x, y: abs(y + 0.04) <= 0.035), INSIDE)]
            return s
        sizes = {"Shocked": (0.32, 0.5), "PhoneticO": (0.26, 0.34), "PhoneticAi": (0.5, 0.42),
                 "PhoneticEe": (0.62, 0.2), "PhoneticFv": (0.4, 0.16), "PhoneticW": (0.15, 0.19), "PhoneticL": (0.45, 0.38),
                 "PhoneticDth": (0.42, 0.24)}
        rx, ry = sizes[state]
        lip = 0.1 if state != "PhoneticW" else 0.14
        inside = ellipse(0.0, 0.0, rx, ry)
        s += [(ellipse(0.0, 0.0, rx + lip, ry + lip), LIP), (inside, INSIDE)]
        if state in ("PhoneticAi", "PhoneticL"):
            s.append((both(inside, lambda x, y: y >= ry * 0.55), TEETH))
        if state == "PhoneticEe":
            s += [(inside, TEETH), (both(inside, lambda x, y: abs(y) <= 0.025), INSIDE)]
        if state == "PhoneticFv":
            s += [(both(inside, lambda x, y: y >= -0.02), TEETH)]
        if state == "PhoneticL":
            s.append((both(inside, ellipse(0.0, 0.12, 0.26, 0.2)), TONGUE))
        if state == "PhoneticDth":
            s += [(both(inside, lambda x, y: y >= ry * 0.35), TEETH), (ellipse(0.0, -0.02, 0.24, 0.16), TONGUE)]
        if state in ("Shocked", "PhoneticAi"):
            s.append((both(inside, ellipse(0.0, -ry * 0.75, rx * 0.6, ry * 0.35)), TONGUE))
    return s


# ----- atlas ------------------------------------------------------------------------------------

def atlas():
    """Returns (png bytes, layout) where layout maps feature/state/side to tile indices."""
    tiles = []
    layout = {"tileSize": TILE, "columns": COLUMNS, "eyes": {}, "eyebrows": {}, "mouths": {}}
    for state in EYES:
        entry = {}
        for side in ("Left", "Right"):
            white, iris = eye_layers(state, side)
            entry[side.lower()] = [len(tiles), len(tiles) + 1]
            tiles += [render(white), render(iris)]
        layout["eyes"][state] = entry
    for state in EYEBROWS:
        entry = {}
        for side in ("Left", "Right"):
            entry[side.lower()] = len(tiles)
            tiles.append(render(eyebrow(state, side)))
        layout["eyebrows"][state] = entry
    for state in MOUTHS:
        layout["mouths"][state] = len(tiles)
        tiles.append(render(mouth(state)))
    rows = (len(tiles) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * TILE, rows * TILE
    pixels = bytearray(width * height * 4)
    for index, tile in enumerate(tiles):
        tx, ty = (index % COLUMNS) * TILE, (index // COLUMNS) * TILE
        for y in range(TILE):
            start = ((ty + y) * width + tx) * 4
            pixels[start:start + TILE * 4] = tile[y * TILE * 4:(y + 1) * TILE * 4]
    layout["width"], layout["height"] = width, height
    return png.encode_rgba(width, height, pixels), layout


# ----- decal geometry ---------------------------------------------------------------------------

FEATURES = {
    # name: (x center, y offset from head center, width, height) in meters on the face
    "eyeLeft": (0.043, 0.006, 0.05, 0.056), "eyeRight": (-0.043, 0.006, 0.05, 0.056),
    "eyebrowLeft": (0.045, 0.05, 0.058, 0.03), "eyebrowRight": (-0.045, 0.05, 0.058, 0.03),
    "mouth": (0.0, -0.076, 0.088, 0.052),
}


def decal(body_type, feature, lift=0.0025):
    from .body import head_center, UP_AXES
    c = head_center(body_type)
    x, y, w, h = FEATURES[feature]
    rx, ry, rz = rig.HEAD_RADII
    radii = (rx + lift, ry + lift, rz + lift)
    theta_c = math.acos(clamp(y / radii[1], -1.0, 1.0))
    phi_c = math.asin(clamp(x / (radii[0] * math.sin(theta_c)), -1.0, 1.0))
    dphi = w / (radii[0] * math.sin(theta_c)) / 2
    dtheta = h / radii[1] / 2
    m = ellipsoid(c, UP_AXES, radii, 6, 6, lambda p, v, u: {rig.INDEX["Head"]: 1.0},
                  v_range=((theta_c - dtheta) / math.pi, (theta_c + dtheta) / math.pi),
                  u_range=((phi_c - dphi) / (2 * math.pi), (phi_c + dphi) / (2 * math.pi)))
    return m


def build_decals(body_type):
    return {name: decal(body_type, name) for name in FEATURES}
