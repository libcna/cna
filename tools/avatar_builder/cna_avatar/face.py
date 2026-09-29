# SPDX-License-Identifier: MS-PL
"""Face features: decal patches on the head and an original atlas of every XNA expression state.

Tiles are drawn as a viewer facing the avatar sees them, so the avatar's left eye is the one on
the image's right; `inner` is the side toward the nose. Shapes are signed distance functions in
tile units ([-1, 1], y up) composited front to back with analytic anti-aliasing.

Layers and tints (catalog materials): the eye white layer is untinted; the iris layer is tinted
with the eye colour and its highlights are holes that show the white layer beneath; eyebrows
are drawn light and tinted with the hair colour; the mouth is untinted, and its lips are
translucent so the skin colour beneath shows through on every skin tone.
"""
import math

from . import png
from .mathutil import clamp, smoothstep

EYES = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "Yawning", "Sleeping", "LookUp",
        "LookDown", "LookLeft", "LookRight", "Blink"]
EYEBROWS = ["Neutral", "Sad", "Angry", "Confused", "Raised"]
MOUTHS = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "PhoneticO", "PhoneticAi",
          "PhoneticEe", "PhoneticFv", "PhoneticW", "PhoneticL", "PhoneticDth"]
TILE = 128
COLUMNS = 16
PIX = 2.0 / TILE

LINE = (40, 26, 24)
PUPIL = (14, 10, 12)
INSIDE = (70, 20, 30)
TEETH = (248, 247, 240)
TONGUE = (214, 102, 112)
LIP = (150, 58, 62)


# ----- signed distance shapes ---------------------------------------------------------------------

class Sdf:
    def __init__(self, fn, box):
        self.fn, self.box = fn, box

    def __call__(self, x, y):
        return self.fn(x, y)


FULL = (-2.0, -2.0, 2.0, 2.0)


def ellipse(cx, cy, rx, ry):
    def fn(x, y):
        dx, dy = x - cx, y - cy
        f = (dx / rx) ** 2 + (dy / ry) ** 2 - 1.0
        g = 2.0 * math.hypot(dx / (rx * rx), dy / (ry * ry))
        return f / g if g > 1e-9 else -min(rx, ry)
    return Sdf(fn, (cx - rx, cy - ry, cx + rx, cy + ry))


def circle(cx, cy, r):
    return Sdf(lambda x, y: math.hypot(x - cx, y - cy) - r, (cx - r, cy - r, cx + r, cy + r))


def stroke(points, widths):
    """Polyline with round caps; width per point (or one width), interpolated along segments."""
    if not isinstance(widths, (list, tuple)):
        widths = [widths] * len(points)

    def fn(x, y):
        best = 1e9
        for (x0, y0), (x1, y1), w0, w1 in zip(points, points[1:], widths, widths[1:]):
            dx, dy = x1 - x0, y1 - y0
            t = clamp(((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy + 1e-12), 0.0, 1.0)
            d = math.hypot(x - (x0 + t * dx), y - (y0 + t * dy)) - 0.5 * (w0 + (w1 - w0) * t)
            best = min(best, d)
        return best
    pad = max(widths)
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    return Sdf(fn, (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad))


def curve(fn_y, x0, x1, widths, steps=24):
    """Stroke along y = fn_y(x) from x0 to x1; widths is a function of t in [0, 1] or a number."""
    pts = [(x0 + (x1 - x0) * k / steps, fn_y(x0 + (x1 - x0) * k / steps)) for k in range(steps + 1)]
    ws = [widths(k / steps) if callable(widths) else widths for k in range(steps + 1)]
    return stroke(pts, ws)


def below(fn_y):
    """Inside where y < fn_y(x) (distance measured vertically; good enough for gentle curves)."""
    return Sdf(lambda x, y: y - fn_y(x), FULL)


def above(fn_y):
    return Sdf(lambda x, y: fn_y(x) - y, FULL)


def both(a, b):
    return Sdf(lambda x, y: max(a(x, y), b(x, y)), (max(a.box[0], b.box[0]), max(a.box[1], b.box[1]),
                                                     min(a.box[2], b.box[2]), min(a.box[3], b.box[3])))


def either(a, b):
    return Sdf(lambda x, y: min(a(x, y), b(x, y)), (min(a.box[0], b.box[0]), min(a.box[1], b.box[1]),
                                                    max(a.box[2], b.box[2]), max(a.box[3], b.box[3])))


def minus(a, b):
    return Sdf(lambda x, y: max(a(x, y), -b(x, y)), a.box)


def band(a, width):
    """The outline of shape a, `width` wide, centred on its edge."""
    return Sdf(lambda x, y: abs(a(x, y)) - 0.5 * width, (a.box[0] - width, a.box[1] - width, a.box[2] + width, a.box[3] + width))


def render(layers, size=TILE):
    """Straight-alpha RGBA tile from (shape, colour, opacity) layers, back to front. A colour may
    be a function of (x, y)."""
    pixels = bytearray()
    for py in range(size):
        y = 1.0 - (py + 0.5) / size * 2.0
        for px in range(size):
            x = (px + 0.5) / size * 2.0 - 1.0
            r = g = b = a = 0.0
            for shape, colour, opacity in layers:
                box = shape.box
                if x < box[0] - PIX or x > box[2] + PIX or y < box[1] - PIX or y > box[3] + PIX:
                    continue
                cover = clamp(0.5 - shape(x, y) / PIX, 0.0, 1.0) * opacity
                if cover <= 0.0:
                    continue
                c = colour(x, y) if callable(colour) else colour
                r = c[0] * cover + r * (1.0 - cover)
                g = c[1] * cover + g * (1.0 - cover)
                b = c[2] * cover + b * (1.0 - cover)
                a = cover + a * (1.0 - cover)
            if a > 0.0:
                pixels += bytes((int(round(r / a)), int(round(g / a)), int(round(b / a)), int(round(255.0 * a))))
            else:
                pixels += b"\0\0\0\0"
    return pixels


def mix(c0, c1, t):
    t = clamp(t, 0.0, 1.0)
    return tuple(c0[i] + (c1[i] - c0[i]) * t for i in range(3))


# ----- eyes -------------------------------------------------------------------------------------

RX, RY, CY, FLOOR = 0.62, 0.72, 0.04, -0.56


def _opening(rx=RX, ry=RY):
    return both(ellipse(0.0, CY, rx, ry), above(lambda x: FLOOR - 0.08 * (x / rx) ** 2))


def eye_layers(state, side):
    """(white layer, iris layer) shape lists for 'Left'/'Right'."""
    inner = -1.0 if side == "Left" else 1.0
    iris_c, iris_r = (0.0, -0.06), 0.40
    rx, ry = RX, RY
    lid = None         # upper lid height at the inner and outer corners (a straight cut)
    raise_low = None   # lower lid pushed up (a smile squint)
    closed = None
    if state == "Sad":
        lid, iris_c = (0.52, 0.12), (0.0, -0.16)
    elif state == "Angry":
        lid, iris_c = (0.05, 0.55), (0.0, -0.10)
    elif state == "Confused":
        if side == "Left":
            lid, iris_c = (0.34, 0.26), (inner * 0.10, -0.08)
        else:
            rx, ry, iris_c = 0.66, 0.80, (inner * 0.10, 0.0)
    elif state == "Shocked":
        rx, ry, iris_c, iris_r = 0.70, 0.84, (0.0, 0.02), 0.30
    elif state == "Happy":
        raise_low, iris_c = -0.30, (0.0, 0.02)
    elif state == "LookUp":
        iris_c = (0.0, 0.26)
    elif state == "LookDown":
        lid, iris_c = (0.36, 0.36), (0.0, -0.30)
    elif state == "LookLeft":
        iris_c = (0.24, -0.06)   # the avatar's left is the viewer's right
    elif state == "LookRight":
        iris_c = (-0.24, -0.06)
    elif state == "Laughing":
        closed = "laugh"
    elif state == "Yawning":
        closed = "squeeze"
    elif state == "Sleeping":
        closed = "sleep"
    elif state == "Blink":
        closed = "blink"
    if closed:
        return _closed_eye(closed, inner), []
    opening = _opening(rx, ry)
    visible = opening
    if lid:
        # A straight upper lid from the inner corner (x = inner * rx) to the outer one.
        y_in, y_out = lid
        visible = both(visible, below(lambda x: y_in + (y_out - y_in) * (inner * rx - x) / (2.0 * inner * rx)))
    if raise_low is not None:
        # Cheeks push the lower lid up into an arch.
        visible = both(visible, above(lambda x: raise_low + 0.30 * (1.0 - (x / rx) ** 2)))

    def sclera(x, y):
        # Soft shadow under the upper lid and toward the corners.
        shade = 0.20 * smoothstep(0.05, ry * 0.95, y - CY) + 0.10 * smoothstep(0.35, 0.62, abs(x))
        return mix((252, 251, 248), (196, 194, 200), shade)

    upper = both(band(visible, 0.13), Sdf(lambda x, y: (CY - 0.18) - y, FULL))
    lower = both(band(visible, 0.05), Sdf(lambda x, y: y - (CY - 0.18), FULL))
    crease = curve(lambda x: CY + ry + 0.09 - 0.20 * (x / rx) ** 2, -rx * 0.8, rx * 0.8, 0.045)
    lashes = []
    for k, (a, length_) in enumerate(((0.55, 0.16), (0.78, 0.20), (0.98, 0.18))):
        # Three short flicks off the outer end of the upper lid.
        sx = -inner * rx * math.sin(a) * 0.98
        sy = CY + ry * 0.92 * math.cos(a) * (0.35 if lid else 1.0) + (0.0 if not lid else 0.15)
        ex, ey = sx - inner * length_ * 0.8, sy + length_ * 0.7
        lashes.append(stroke([(sx, sy), (ex, ey)], [0.07, 0.02]))
    white = [(visible, sclera, 1.0), (lower, LINE, 0.55), (upper, LINE, 1.0), (crease, LINE, 0.18)]
    white += [(lash, LINE, 1.0) for lash in lashes]

    ix, iy = iris_c

    def iris_colour(x, y):
        r = math.hypot(x - ix, y - iy) / iris_r
        a = math.atan2(y - iy, x - ix)
        streak = 0.06 * math.cos(11.0 * a) + 0.04 * math.cos(23.0 * a + 1.0)
        v = 0.50 + 0.42 * smoothstep(1.0, 0.55, r) + 0.12 * smoothstep(0.62, 0.45, r) + streak * smoothstep(1.0, 0.5, r)
        # The lid's shadow darkens the top of the iris.
        v *= 1.0 - 0.28 * smoothstep(iy + 0.05, iy + iris_r, y)
        v = clamp(v, 0.0, 1.0)
        return (255 * v, 255 * v, 255 * v)
    iris = both(circle(ix, iy, iris_r), visible)
    pupil = both(circle(ix, iy - 0.01, iris_r * 0.44), visible)
    highlight = either(circle(ix - 0.36 * iris_r, iy + 0.38 * iris_r, iris_r * 0.24),
                       circle(ix + 0.34 * iris_r, iy - 0.30 * iris_r, iris_r * 0.10))
    ring = both(band(circle(ix, iy, iris_r - 0.02), 0.05), visible)
    iris_layers = [(minus(iris, highlight), iris_colour, 1.0), (minus(ring, highlight), (70, 70, 70), 0.5),
                   (minus(pupil, highlight), PUPIL, 1.0)]
    return white, iris_layers


def _closed_eye(kind, inner):
    if kind == "laugh":
        # Tight upward arcs.
        line = curve(lambda x: -0.10 + 0.34 * (1.0 - (x / 0.56) ** 2), -0.56, 0.56, lambda t: 0.10 + 0.05 * math.sin(math.pi * t))
        return [(line, LINE, 1.0)]
    if kind == "squeeze":
        line = curve(lambda x: 0.02 - 0.22 * (1.0 - (x / 0.55) ** 2), -0.55, 0.55, lambda t: 0.09 + 0.04 * math.sin(math.pi * t))
        tension = curve(lambda x: 0.30 + 0.06 * x * -inner, -0.30, 0.30, 0.035)
        return [(line, LINE, 1.0), (tension, LINE, 0.3)]
    if kind == "sleep":
        line = curve(lambda x: -0.06 - 0.20 * (1.0 - (x / 0.56) ** 2), -0.56, 0.56, lambda t: 0.10)
    else:  # blink: the lids meet just below the middle
        line = curve(lambda x: -0.10 - 0.08 * (1.0 - (x / 0.58) ** 2), -0.58, 0.58, lambda t: 0.09 + 0.03 * math.sin(math.pi * t))
    lashes = []
    for k in range(3):
        sx = -inner * (0.26 + 0.14 * k)
        base_y = (-0.06 - 0.20 * (1.0 - (sx / 0.56) ** 2)) if kind == "sleep" else (-0.10 - 0.08 * (1.0 - (sx / 0.58) ** 2))
        lashes.append(stroke([(sx, base_y), (sx - inner * 0.09, base_y - 0.14)], [0.05, 0.015]))
    crease = curve(lambda x: 0.30 - 0.12 * (x / 0.5) ** 2, -0.44, 0.44, 0.04)
    return [(line, LINE, 1.0), (crease, LINE, 0.16)] + [(lash, LINE, 0.9) for lash in lashes]


# ----- eyebrows (drawn light: tinted by the hair colour) ------------------------------------------

def eyebrow(state, side):
    inner = -1.0 if side == "Left" else 1.0
    if state == "Neutral":
        ys = (0.02, 0.22, 0.24, 0.06)
    elif state == "Sad":
        ys = (0.34, 0.20, 0.02, -0.18)
    elif state == "Angry":
        ys = (-0.34, -0.06, 0.14, 0.18)
    elif state == "Raised":
        ys = (0.26, 0.52, 0.54, 0.30)
    else:  # Confused: the left brow lifts, the right one knits down
        ys = (0.20, 0.48, 0.50, 0.26) if side == "Left" else (-0.18, -0.02, 0.04, -0.10)
    xs = (0.70, 0.22, -0.30, -0.78)
    pts = [(inner * x, y) for x, y in zip(xs, ys)]
    shape = stroke(pts, [0.36, 0.34, 0.25, 0.10])

    def colour(x, y):
        # A darker core with lighter, hair-like edges.
        return mix((255, 255, 255), (214, 214, 214), smoothstep(-0.02, 0.10, shape(x, y) + 0.12))
    return [(shape, colour, 1.0)]


# ----- mouths -----------------------------------------------------------------------------------

def mouth(state):
    s = []
    lips = lambda shape, o=0.30: (shape, LIP, o)
    if state in ("Neutral", "Happy", "Sad", "Confused"):
        if state == "Neutral":
            fn = lambda x: -0.02 + 0.06 * (x / 0.5) ** 2
            x0, x1 = -0.46, 0.46
        elif state == "Happy":
            fn = lambda x: 0.04 - 0.30 * (1.0 - (x / 0.62) ** 2)
            x0, x1 = -0.62, 0.62
        elif state == "Sad":
            fn = lambda x: -0.26 + 0.28 * (1.0 - (x / 0.52) ** 2)
            x0, x1 = -0.52, 0.52
        else:
            fn = lambda x: 0.02 + 0.10 * math.sin((x + 0.5) * 2.4 * math.pi / 1.0) * 0.5 + 0.10 * x
            x0, x1 = -0.5, 0.5
        lower_lip = curve(lambda x: fn(x) - 0.11, x0 * 0.75, x1 * 0.75, lambda t: 0.10 * math.sin(math.pi * t) + 0.02)
        line = curve(fn, x0, x1, lambda t: 0.055 + 0.03 * math.sin(math.pi * t))
        s += [lips(lower_lip, 0.22), (line, LINE, 0.9)]
        if state == "Happy":
            for side in (-1.0, 1.0):
                s.append((curve(lambda x, side=side: fn(side * 0.62) + 0.02 + 0.5 * (x - side * 0.62) * side,
                                side * 0.60 - 0.06 * side, side * 0.60 + 0.02 * side, 0.04), LINE, 0.6))
        return s
    if state == "Laughing":
        inside = both(ellipse(0.0, 0.10, 0.66, 0.68), below(lambda x: 0.14 - 0.04 * (x / 0.6) ** 2))
        teeth = both(inside, above(lambda x: 0.14 - 0.04 * (x / 0.6) ** 2 - 0.17))
        tongue = both(inside, ellipse(0.0, -0.46, 0.36, 0.22))
        s += [lips(band(inside, 0.16), 0.35), (inside, INSIDE, 1.0), (teeth, TEETH, 1.0), (tongue, TONGUE, 1.0),
              (band(inside, 0.05), LINE, 0.85)]
        return s
    if state == "Angry":
        outer = ellipse(0.0, -0.04, 0.60, 0.30)
        teeth = ellipse(0.0, -0.04, 0.52, 0.22)
        gap = both(teeth, Sdf(lambda x, y: abs(y + 0.05) - 0.018, FULL))
        s += [lips(outer, 0.45), (outer, INSIDE, 0.9), (teeth, TEETH, 1.0), (gap, (120, 110, 110), 0.9),
              (band(outer, 0.05), LINE, 0.9)]
        return s
    sizes = {"Shocked": (0.30, 0.48), "PhoneticO": (0.24, 0.30), "PhoneticAi": (0.46, 0.38),
             "PhoneticEe": (0.56, 0.18), "PhoneticFv": (0.40, 0.15), "PhoneticW": (0.15, 0.17), "PhoneticL": (0.42, 0.34),
             "PhoneticDth": (0.40, 0.22)}
    rx, ry = sizes[state]
    inside = ellipse(0.0, -0.02, rx, ry)
    s += [lips(ellipse(0.0, -0.02, rx + 0.10, ry + 0.10), 0.28), (inside, INSIDE, 1.0)]
    if state in ("PhoneticAi", "PhoneticL"):
        s.append((both(inside, above(lambda x: -0.02 + ry * 0.55)), TEETH, 1.0))
    if state == "PhoneticEe":
        s += [(both(inside, above(lambda x: -0.02)), TEETH, 1.0), (both(inside, below(lambda x: -0.035)), TEETH, 0.9)]
    if state == "PhoneticFv":
        s.append((both(inside, above(lambda x: -0.06)), TEETH, 1.0))
    if state == "PhoneticL":
        s.append((both(inside, ellipse(0.0, 0.06, 0.24, 0.18)), TONGUE, 1.0))
    if state == "PhoneticDth":
        s += [(both(inside, above(lambda x: 0.02)), TEETH, 1.0), (ellipse(0.0, -0.04, 0.22, 0.12), TONGUE, 1.0)]
    if state in ("Shocked", "PhoneticAi"):
        s.append((both(inside, ellipse(0.0, -0.02 - ry * 0.72, rx * 0.62, ry * 0.36)), TONGUE, 1.0))
    s.append((band(inside, 0.05), LINE, 0.85))
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
    # name: (x centre, y centre, width, height) in head-local metres on the face (male scale)
    "eyeLeft": (0.054, -0.004, 0.076, 0.084), "eyeRight": (-0.054, -0.004, 0.076, 0.084),
    "eyebrowLeft": (0.056, 0.056, 0.080, 0.044), "eyebrowRight": (-0.056, 0.056, 0.080, 0.044),
    "mouth": (0.0, -0.082, 0.118, 0.072),
}
LIFT = 0.0012


def build_decals(body_type):
    from . import head
    s = head.SCALE[body_type]
    return {name: head.decal_patch(body_type, x * s, y * s, w * s, h * s, LIFT * s)
            for name, (x, y, w, h) in FEATURES.items()}
