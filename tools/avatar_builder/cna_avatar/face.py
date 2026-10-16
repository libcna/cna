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
# Lips: a deeper rose than any skin, translucent over it, so they darken and warm every skin tone
# rather than lighten a deep one toward pink.
LIP = (118, 44, 50)


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


def grow(a, d):
    """Shape a, grown outward by d (shrunk for negative d)."""
    pad = max(d, 0.0)
    return Sdf(lambda x, y: a(x, y) - d, (a.box[0] - pad, a.box[1] - pad, a.box[2] + pad, a.box[3] + pad))


def band(a, width):
    """The outline of shape a, `width` wide, centred on its edge."""
    return Sdf(lambda x, y: abs(a(x, y)) - 0.5 * width, (a.box[0] - width, a.box[1] - width, a.box[2] + width, a.box[3] + width))


def render(layers, size=TILE):
    """Straight-alpha RGBA tile from (shape, colour, opacity) layers, back to front. A colour or an
    opacity may be a function of (x, y)."""
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
                cover = clamp(0.5 - shape(x, y) / PIX, 0.0, 1.0)
                if cover > 0.0:
                    cover *= opacity(x, y) if callable(opacity) else opacity
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

# Catalog v3 revision 3: an almond opening whose upper lid rests on the top of the iris, a slim
# tapered lid line, a warm sclera in the lid's shade, and a small highlight.
RX, RY, CY, FLOOR = 0.70, 0.52, 0.02, -0.44
LID = (48, 32, 28)
SCLERA = (232, 228, 221)


def _opening(rx=RX, ry=RY):
    return both(ellipse(0.0, CY, rx, ry), above(lambda x: FLOOR - 0.06 * (x / rx) ** 2))


def eye_layers(state, side):
    """(white layer, iris layer) shape lists for 'Left'/'Right'."""
    inner = -1.0 if side == "Left" else 1.0
    iris_c, iris_r = (0.0, 0.03), 0.44
    rx, ry = RX, RY
    lid = None         # upper lid height at the inner and outer corners (a straight cut)
    raise_low = None   # lower lid pushed up (a smile squint)
    closed = None
    if state == "Sad":
        lid, iris_c = (0.46, 0.10), (0.0, -0.12)
    elif state == "Angry":
        lid, iris_c = (0.02, 0.48), (0.0, -0.06)
    elif state == "Confused":
        if side == "Left":
            lid, iris_c = (0.30, 0.22), (inner * 0.10, -0.04)
        else:
            rx, ry, iris_c = 0.73, 0.64, (inner * 0.10, 0.06)
    elif state == "Shocked":
        rx, ry, iris_c, iris_r = 0.76, 0.72, (0.0, 0.06), 0.31
    elif state == "Happy":
        raise_low, iris_c = -0.30, (0.0, 0.04)
    elif state == "LookUp":
        iris_c = (0.0, 0.22)
    elif state == "LookDown":
        lid, iris_c = (0.26, 0.26), (0.0, -0.22)
    elif state == "LookLeft":
        iris_c = (0.28, 0.02)   # the avatar's left is the viewer's right
    elif state == "LookRight":
        iris_c = (-0.28, 0.02)
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
    top_of = lambda x: CY + ry * math.sqrt(max(0.0, 1.0 - (x / rx) ** 2))
    if lid:
        # A straight upper lid from the inner corner (x = inner * rx) to the outer one.
        y_in, y_out = lid
        cut = lambda x: y_in + (y_out - y_in) * (inner * rx - x) / (2.0 * inner * rx)
        visible = both(visible, below(cut))
        top_of = lambda x, cut=cut, t=top_of: min(t(x), cut(x))
    if raise_low is not None:
        # Cheeks push the lower lid up into an arch.
        visible = both(visible, above(lambda x: raise_low + 0.30 * (1.0 - (x / rx) ** 2)))

    def sclera(x, y):
        # The upper lid's shade falls across the top of the eye; the corners are in shadow too.
        under_lid = smoothstep(0.30, 0.0, top_of(x) - y)
        # The eyeball rounds away toward the corners and under the lids.
        shade = 0.44 * under_lid + 0.30 * smoothstep(0.22, 0.70, abs(x)) ** 1.3 + 0.12 * smoothstep(-0.05, -0.42, y)
        c = mix(SCLERA, (172, 160, 160), shade)
        # The pink inner corner.
        corner = smoothstep(0.20, 0.0, math.hypot(x - inner * (rx - 0.08), y - (CY - 0.06)))
        return mix(c, (214, 152, 150), 0.40 * corner)

    # The lid line: slim at the inner corner, fuller toward the outer one, ending in a short flick.
    def outer_of(x):
        return clamp((-inner * x / rx + 1.0) * 0.5, 0.0, 1.0)

    def lid_width(x):
        o = outer_of(x)
        return 0.034 + 0.072 * smoothstep(0.0, 0.72, o) - 0.030 * smoothstep(0.80, 1.0, o)
    # The line follows the lid down round the outer corner a little, into the flick.
    upper = both(Sdf(lambda x, y: abs(visible(x, y)) - 0.5 * lid_width(x), FULL),
                 Sdf(lambda x, y: (CY - 0.06 - 0.08 * smoothstep(0.6, 1.0, outer_of(x))) - y, FULL))
    lower = both(band(visible, 0.035), Sdf(lambda x, y: y - (CY - 0.18), FULL))
    wing = stroke([(-inner * rx * 0.94, CY + 0.02), (-inner * (rx + 0.09), CY + 0.12)], [0.06, 0.018])
    crease = curve(lambda x: CY + ry + 0.12 - 0.22 * (x / rx) ** 2, -rx * 0.75, rx * 0.8, 0.035)
    lashes = []
    for a, length_ in ((0.80, 0.12), (0.98, 0.13)):
        # Two short flicks off the outer end of the upper lid.
        sx = -inner * rx * math.sin(a) * 0.98
        sy = CY + ry * 0.92 * math.cos(a) * (0.35 if lid else 1.0) + (0.0 if not lid else 0.15)
        ex, ey = sx - inner * length_ * 0.8, sy + length_ * 0.6
        lashes.append(stroke([(sx, sy), (ex, ey)], [0.05, 0.015]))
    white = [(visible, sclera, 1.0), (lower, (96, 62, 56), 0.35), (upper, LID, 0.96), (wing, LID, 0.92)]
    white += [(lash, LID, 0.85) for lash in lashes]

    ix, iy = iris_c

    def iris_colour(x, y):
        r = math.hypot(x - ix, y - iy) / iris_r
        a = math.atan2(y - iy, x - ix)
        streak = 0.06 * math.cos(11.0 * a) + 0.04 * math.cos(23.0 * a + 1.0)
        v = 0.48 + 0.40 * smoothstep(1.0, 0.62, r) + 0.14 * smoothstep(0.62, 0.45, r) + streak * smoothstep(1.0, 0.5, r)
        # Lighter toward the bottom, where light passes through; the lid's shadow darkens the top.
        v *= 1.0 + 0.10 * smoothstep(0.0, -0.8, (y - iy) / iris_r)
        v *= 1.0 - 0.34 * smoothstep(top_of(x) - 0.30, top_of(x), y)
        v = clamp(v, 0.0, 1.0)
        return (255 * v, 255 * v, 255 * v)
    iris = both(circle(ix, iy, iris_r), visible)
    pupil = both(circle(ix, iy - 0.01, iris_r * 0.42), visible)
    highlight = either(circle(ix - 0.34 * iris_r, iy + 0.36 * iris_r, iris_r * 0.17),
                       circle(ix + 0.32 * iris_r, iy - 0.30 * iris_r, iris_r * 0.07))
    ring = both(band(circle(ix, iy, iris_r - 0.02), 0.06), visible)
    iris_layers = [(minus(iris, highlight), iris_colour, 1.0), (minus(ring, highlight), (52, 52, 52), 0.6),
                   (minus(pupil, highlight), PUPIL, 1.0)]
    return white, iris_layers


def _closed_eye(kind, inner):
    if kind == "laugh":
        # Tight upward arcs.
        line = curve(lambda x: -0.10 + 0.32 * (1.0 - (x / 0.64) ** 2), -0.64, 0.64, lambda t: 0.10 + 0.05 * math.sin(math.pi * t))
        return [(line, LINE, 1.0)]
    if kind == "squeeze":
        line = curve(lambda x: 0.02 - 0.22 * (1.0 - (x / 0.63) ** 2), -0.63, 0.63, lambda t: 0.09 + 0.04 * math.sin(math.pi * t))
        tension = curve(lambda x: 0.30 + 0.06 * x * -inner, -0.30, 0.30, 0.035)
        return [(line, LINE, 1.0), (tension, LINE, 0.3)]
    if kind == "sleep":
        line = curve(lambda x: -0.06 - 0.20 * (1.0 - (x / 0.64) ** 2), -0.64, 0.64, lambda t: 0.10)
    else:  # blink: the lids meet just below the middle
        line = curve(lambda x: -0.10 - 0.08 * (1.0 - (x / 0.66) ** 2), -0.66, 0.66, lambda t: 0.09 + 0.03 * math.sin(math.pi * t))
    lashes = []
    for k in range(3):
        sx = -inner * (0.26 + 0.14 * k)
        base_y = (-0.06 - 0.20 * (1.0 - (sx / 0.64) ** 2)) if kind == "sleep" else (-0.10 - 0.08 * (1.0 - (sx / 0.66) ** 2))
        lashes.append(stroke([(sx, base_y), (sx - inner * 0.09, base_y - 0.14)], [0.05, 0.015]))
    crease = curve(lambda x: 0.30 - 0.12 * (x / 0.5) ** 2, -0.44, 0.44, 0.04)
    return [(line, LINE, 1.0), (crease, LINE, 0.16)] + [(lash, LINE, 0.9) for lash in lashes]


# ----- eyebrows (drawn light: tinted by the hair colour) ------------------------------------------

# Eyebrow styles: (inner x, outer x, peak position 0..1 from inner to outer, arch height, outer
# drop, base height, width at inner/peak/outer, feathered edge). x is in tile units as the viewer
# sees the avatar's right brow (inner end toward the nose at +x for the left brow... mirrored by side).
BROW_STYLES = {
    "classic": dict(inner=0.70, outer=-0.78, peak=0.45, arch=0.12, drop=0.16, base=0.10, widths=(0.36, 0.30, 0.10), feather=0.0),
    "natural": dict(inner=0.72, outer=-0.80, peak=0.50, arch=0.14, drop=0.14, base=0.08, widths=(0.40, 0.32, 0.12), feather=0.35),
    "straight": dict(inner=0.72, outer=-0.80, peak=0.60, arch=0.02, drop=0.04, base=0.10, widths=(0.36, 0.32, 0.18), feather=0.0),
    "softArch": dict(inner=0.70, outer=-0.82, peak=0.55, arch=0.22, drop=0.20, base=0.06, widths=(0.32, 0.28, 0.10), feather=0.0),
    "highArch": dict(inner=0.68, outer=-0.84, peak=0.62, arch=0.38, drop=0.34, base=0.02, widths=(0.30, 0.26, 0.08), feather=0.0),
    "thin": dict(inner=0.66, outer=-0.80, peak=0.52, arch=0.16, drop=0.16, base=0.12, widths=(0.18, 0.16, 0.06), feather=0.0),
    "thick": dict(inner=0.74, outer=-0.80, peak=0.48, arch=0.10, drop=0.12, base=0.06, widths=(0.56, 0.48, 0.20), feather=0.45),
    "short": dict(inner=0.62, outer=-0.46, peak=0.55, arch=0.10, drop=0.08, base=0.10, widths=(0.36, 0.32, 0.16), feather=0.0),
    "long": dict(inner=0.76, outer=-0.96, peak=0.42, arch=0.14, drop=0.30, base=0.08, widths=(0.34, 0.30, 0.08), feather=0.0),
    "angled": dict(inner=0.70, outer=-0.82, peak=0.66, arch=0.30, drop=0.40, base=-0.02, widths=(0.34, 0.30, 0.08), feather=0.0,
                   sharp=True),
    "rounded": dict(inner=0.66, outer=-0.78, peak=0.50, arch=0.26, drop=0.26, base=0.04, widths=(0.32, 0.32, 0.22), feather=0.0),
    "bushy": dict(inner=0.76, outer=-0.84, peak=0.45, arch=0.08, drop=0.10, base=0.04, widths=(0.64, 0.54, 0.26), feather=0.7),
    "lowFlat": dict(inner=0.72, outer=-0.82, peak=0.58, arch=0.04, drop=0.10, base=-0.06, widths=(0.40, 0.34, 0.14), feather=0.2),
}


def _brow_state(state, side, u):
    """Vertical offset of a brow at u (0 inner .. 1 outer) for an expression state."""
    if state == "Sad":
        return 0.32 - 0.56 * u
    if state == "Angry":
        return -0.36 + 0.46 * u
    if state == "Raised":
        return 0.30 + 0.06 * math.sin(math.pi * u)
    if state == "Confused":
        return 0.28 + 0.04 * u if side == "Left" else -0.20 + 0.10 * u
    return 0.0


def eyebrow(state, side, style="classic"):
    p = BROW_STYLES[style]
    inner = -1.0 if side == "Left" else 1.0
    steps = 9
    pts, widths = [], []
    for k in range(steps + 1):
        u = k / steps
        x = p["inner"] + (p["outer"] - p["inner"]) * u
        if p.get("sharp"):
            rise = u / p["peak"] if u <= p["peak"] else 1.0 - (u - p["peak"]) / (1.0 - p["peak"]) * (1.0 + p["drop"] / max(p["arch"], 1e-3))
            y = p["base"] + p["arch"] * rise
        else:
            # A smooth arch peaking at `peak`, the tail dropping toward the outer end.
            a = math.sin(math.pi * min(u / (2.0 * p["peak"]), 0.5)) if u <= p["peak"] else \
                math.cos(0.5 * math.pi * (u - p["peak"]) / (1.0 - p["peak"]))
            y = p["base"] + p["arch"] * a - p["drop"] * smoothstep(p["peak"], 1.0, u) * (1.0 - a)
        y += _brow_state(state, side, u)
        pts.append((inner * x, y))
        w0, w1, w2 = p["widths"]
        widths.append(w0 + (w1 - w0) * (u / p["peak"]) if u <= p["peak"] else w1 + (w2 - w1) * ((u - p["peak"]) / (1.0 - p["peak"])))
    # Keep the stroke inside its tile, round caps, feathered edge and all.
    reach = max(abs(x) + w * 0.5 for (x, _), w in zip(pts, widths))
    limit = 0.94 - 0.035 - 0.025 * p["feather"]
    if reach > limit:
        k = limit / reach
        pts = [(x * k, y) for x, y in pts]
        widths = [w * k for w in widths]
    shape = stroke(pts, widths)
    feather = p["feather"]
    x_in, x_out = pts[0][0], pts[-1][0]
    toward = 1.0 if x_out < x_in else -1.0

    def along(x):
        return clamp((x - x_in) / (x_out - x_in), 0.0, 1.0)

    def hairs(x, y):
        """Single hairs: fine strokes that rise and fan at the brow's head and lie flat along its
        tail, each broken along its length, so the brow is hair rather than a painted band."""
        u = along(x)
        angle = (1.20 - 1.0 * smoothstep(0.0, 0.55, u)) * toward
        a = x * math.sin(angle) - y * math.cos(angle)
        b = x * math.cos(angle) + y * math.sin(angle)
        cell = a * 17.0
        k = math.floor(cell)
        line = smoothstep(0.42, 0.12, abs(cell - k - 0.5 - 0.25 * (_hash(k, 7) - 0.5)))
        dash = smoothstep(0.30, 0.62, 0.5 + 0.5 * math.sin(b * (15.0 + 14.0 * _hash(k, 13)) + 6.283 * _hash(k, 11)))
        return line * (0.35 + 0.65 * dash)

    def stray(x, y):
        # Only some hairs reach past the brow's edge.
        u = along(x)
        angle = (1.20 - 1.0 * smoothstep(0.0, 0.55, u)) * toward
        k = math.floor((x * math.sin(angle) - y * math.cos(angle)) * 17.0)
        return hairs(x, y) * smoothstep(0.40, 0.75, _hash(k, 17))

    def colour(x, y):
        # A denser, darker core; lighter single hairs at the edge.
        d = shape(x, y)
        core = smoothstep(0.02, -0.10, d)
        h = hairs(x, y)
        return mix((255, 255, 255), (168, 168, 168), clamp(0.50 * core + 0.30 * h * core, 0.0, 1.0))

    def opacity(x, y):
        # The head starts soft and the tail thins to a point; the edge feathers into single hairs
        # (feathered styles break it further), so the brow grows from the skin.
        u = along(x)
        d = shape(x, y)
        body = (0.50 + 0.50 * smoothstep(0.0, 0.30, u)) * (1.0 - 0.25 * smoothstep(0.80, 1.0, u))
        h = hairs(x, y)
        # The soft edge is centred on the stroke's own edge, so each style keeps its width.
        soft = 0.035 + 0.025 * feather
        inside = smoothstep(-soft, soft, -d) * (0.90 + 0.10 * h)
        fringe = stray(x, y) * smoothstep(soft + 0.045 + 0.04 * feather, soft, d) * (0.55 + 0.35 * feather)
        # Nothing reaches the tile's border (the patch's edge samples it).
        border = smoothstep(0.985, 0.94, max(abs(x), abs(y)))
        return body * max(inside, fringe) * border
    return [(grow(shape, 0.11 + 0.065 * feather), colour, opacity)]


def _hash(k, seed):
    h = (k * 374761393 + seed * 668265263) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0


# ----- makeup (drawn white: tinted by the description's makeup colours) --------------------------

def _glow(cx, cy, rx, ry, strength, core=0.25, bias=None):
    """One layer of makeup: an ellipse whose opacity is `strength` within `core` of its radius and
    falls smoothly to nothing at its edge; bias(x, y) scales it further."""
    def opacity(x, y):
        q = math.hypot((x - cx) / rx, (y - cy) / ry)
        o = strength * (1.0 - smoothstep(core, 1.0, q))
        return o * (bias(x, y) if bias else 1.0)
    return (ellipse(cx, cy, rx, ry), (255, 255, 255), opacity)


def _soft(shape, strength, falloff, bias=None):
    """One layer of makeup: `shape` filled with an opacity that rises smoothly from its edge over
    `falloff` inward to `strength` (no rings or hard edges); bias(x, y) scales it further."""
    def opacity(x, y):
        o = strength * smoothstep(0.0, falloff, -shape(x, y))
        return o * (bias(x, y) if bias else 1.0)
    return (grow(shape, 0.0), (255, 255, 255), opacity)


def eye_shadow(style, side):
    """Lid colour between the lash line and the brow, strongest at the lashes and fading upward (the
    eye's white covers the opening while the eye is open; a closed eye shows a coloured lid). Tiles
    are on the eye shadow patch, the eye patch raised and enlarged (SHADOW_PATCH), where the eye
    opening's centre is at y = -0.26 and its upper lid at about y = 0.20."""
    inner = -1.0 if side == "Left" else 1.0
    ey = -0.26
    erx = RX * 0.87
    lash = ey + 0.20
    rising = lambda x, y: 1.0 - 0.55 * smoothstep(lash + 0.15, lash + 0.60, y)
    if style == "soft":
        return [_glow(-inner * 0.03, ey + 0.40, erx * 1.14, 0.48, 1.0, 0.45, rising)]
    if style == "smoky":
        return [_glow(-inner * 0.08, ey + 0.42, erx * 1.28, 0.56, 1.0, 0.55, rising),
                _glow(-inner * 0.04, ey - 0.30, erx * 0.95, 0.15, 0.55, 0.2)]
    if style == "winged":
        wing = stroke([(-inner * erx * 0.55, ey + 0.32), (-inner * (erx + 0.32), ey + 0.66)], [0.42, 0.05])
        return [_glow(-inner * 0.03, ey + 0.38, erx * 1.08, 0.44, 1.0, 0.45, rising), _soft(wing, 0.95, 0.10)]
    # "highlight": a light touch at the inner corner and softly under the brow's arch.
    return [_glow(inner * erx * 0.74, ey + 0.10, 0.24, 0.30, 0.75, 0.2),
            _glow(-inner * 0.10, ey + 0.86, erx * 0.85, 0.20, 0.45, 0.1)]


def eyeliner(style, side, closed):
    """A line along the upper lid ending in a wing (on the eye patch, like the eye tiles); `closed`
    follows the closed lid of a blink instead. It lies on the eye's own lid line, thickest toward
    the outer corner."""
    inner = -1.0 if side == "Left" else 1.0
    width = {"thin": 0.07, "winged": 0.10, "bold": 0.15}[style]
    wing = {"thin": 0.14, "winged": 0.40, "bold": 0.28}[style]
    if closed:
        lid = curve(lambda x: -0.10 - 0.08 * (1.0 - (x / 0.66) ** 2), -0.62, 0.62, width * 0.9)
        tail_from = (-inner * 0.62, -0.10)
    else:
        top = lambda x: CY + RY * math.sqrt(max(0.0, 1.0 - (x / RX) ** 2)) + 0.012
        lid = curve(top, -RX * 0.84, RX * 0.96, lambda t: width * (0.45 + 0.55 * t if inner < 0 else 1.0 - 0.55 * t))
        tail_from = (-inner * RX * 0.94, CY + 0.07)
    tail_to = (tail_from[0] - inner * wing, tail_from[1] + wing * 0.50)
    layers = [(lid, LINE, 1.0), (stroke([tail_from, tail_to], [width * 1.1, 0.02]), LINE, 1.0)]
    if style == "bold" and not closed:
        lower = curve(lambda x: CY - RY * 0.80 * math.sqrt(max(0.0, 1.0 - (x / RX) ** 2)) + 0.03, -RX * 0.2 * inner, RX * 0.9,
                      0.04)
        layers.append((lower, LINE, 0.6))
    return layers


def blush(style, side):
    """A soft flush on the cheek: strongest in its middle, fading to nothing at its edge."""
    inner = -1.0 if side == "Left" else 1.0
    if style == "sweep":
        shape = stroke([(inner * 0.30, -0.22), (-inner * 0.42, 0.20)], [0.80, 0.50])
        return [_soft(shape, 0.80, 0.38)]
    return [_glow(0.0, 0.0, 0.88, 0.70, 0.80, 0.0)]


def lips_mask(state):
    """Where lip colour goes for a mouth state: the lips of mouth(state), opaque inside with a soft
    edge (the style's opacity and colour come from the description)."""
    return [(shape, (255, 255, 255), _feathered(shape, 1.0, 0.03)) for shape, colour, opacity in mouth(state) if colour == LIP]


# ----- mouths -----------------------------------------------------------------------------------

def _feathered(shape, strength, falloff):
    """An opacity rising from nothing at the shape's edge to `strength` `falloff` inside it: colour
    that lies on a form (the lips are sculpted) rather than a cut-out."""
    return lambda x, y: strength * smoothstep(0.0, falloff, -shape(x, y))


def mouth(state):
    s = []
    lips = lambda shape, o=0.34: (shape, LIP, _feathered(shape, o, 0.07))
    if state in ("Neutral", "Happy", "Sad", "Confused"):
        if state == "Neutral":
            fn = lambda x: -0.03 + 0.11 * (x / 0.5) ** 2
            x0, x1 = -0.50, 0.50
        elif state == "Happy":
            fn = lambda x: 0.04 - 0.30 * (1.0 - (x / 0.62) ** 2)
            x0, x1 = -0.62, 0.62
        elif state == "Sad":
            fn = lambda x: -0.26 + 0.28 * (1.0 - (x / 0.52) ** 2)
            x0, x1 = -0.52, 0.52
        else:
            fn = lambda x: 0.02 + 0.10 * math.sin((x + 0.5) * 2.4 * math.pi / 1.0) * 0.5 + 0.10 * x
            x0, x1 = -0.5, 0.5
        lower_lip = curve(lambda x: fn(x) - 0.125, x0 * 0.82, x1 * 0.82, lambda t: 0.25 * math.sin(math.pi * t) ** 0.8 + 0.03)
        upper_lip = curve(lambda x: fn(x) + 0.075 - 0.035 * math.cos(math.pi * x / 0.42), x0 * 0.78, x1 * 0.78,
                          lambda t: 0.15 * math.sin(math.pi * t) ** 0.7 + 0.02)
        # The parting line: the lips meet in a sculpted groove, so the line is slim and fades into
        # the corners, which are sculpted too.
        line = curve(fn, x0, x1, lambda t: 0.026 + 0.032 * math.sin(math.pi * t) ** 0.8)
        reach = max(abs(x0), abs(x1))
        s += [lips(lower_lip, 0.36), lips(upper_lip, 0.30),
              (line, LINE, lambda x, y: 0.86 * (1.0 - 0.65 * smoothstep(0.55 * reach, reach, abs(x))))]
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

SHADOW_STYLES = ("soft", "smoky", "winged", "highlight")
LINER_STYLES = ("thin", "winged", "bold")
BLUSH_STYLES = ("round", "sweep")


def _tile_layers(spec):
    kind, args = spec[0], spec[1:]
    if kind == "eye":
        state, side, layer = args
        return eye_layers(state, side)[layer]
    if kind == "eyebrow":
        return eyebrow(*args)
    if kind == "mouth":
        return mouth(*args)
    if kind == "eyeShadow":
        return eye_shadow(*args)
    if kind == "eyeliner":
        return eyeliner(*args)
    if kind == "blush":
        return blush(*args)
    return lips_mask(*args)


def _tile(spec):
    return render(_tile_layers(spec))


def atlas(pool=None):
    """Returns (png bytes, layout, style tiles): layout maps every expression state to tile indices;
    style tiles map each face style (eyebrows, makeup, lip masks) to its tiles. Tiles are rendered in
    `pool` (a multiprocessing pool) when given."""
    specs = []
    layout = {"tileSize": TILE, "columns": COLUMNS, "eyes": {}, "eyebrows": {}, "mouths": {}}
    for state in EYES:
        entry = {}
        for side in ("Left", "Right"):
            entry[side.lower()] = [len(specs), len(specs) + 1]
            specs += [("eye", state, side, 0), ("eye", state, side, 1)]
        layout["eyes"][state] = entry
    for state in EYEBROWS:
        entry = {}
        for side in ("Left", "Right"):
            entry[side.lower()] = len(specs)
            specs.append(("eyebrow", state, side))
        layout["eyebrows"][state] = entry
    for state in MOUTHS:
        layout["mouths"][state] = len(specs)
        specs.append(("mouth", state))
    styles = {}

    def add(key, spec):
        styles[key] = styles.get(key, []) + [len(specs)]
        specs.append(spec)
    for style in BROW_STYLES:
        if style == "classic":
            continue
        for state in EYEBROWS:
            for side in ("Left", "Right"):
                add(("eyebrows", style), ("eyebrow", state, side, style))
    for style in SHADOW_STYLES:
        for side in ("Left", "Right"):
            add(("eyeShadow", style), ("eyeShadow", style, side))
    for style in LINER_STYLES:
        for closed in (False, True):
            for side in ("Left", "Right"):
                add(("eyeliner", style), ("eyeliner", style, side, closed))
    for style in BLUSH_STYLES:
        for side in ("Left", "Right"):
            add(("blush", style), ("blush", style, side))
    for state in MOUTHS:
        add(("lips", "mask"), ("lips", state))
    tiles = pool.map(_tile, specs, chunksize=1) if pool else [_tile(spec) for spec in specs]
    rows = (len(tiles) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * TILE, rows * TILE
    pixels = bytearray(width * height * 4)
    for index, tile in enumerate(tiles):
        tx, ty = (index % COLUMNS) * TILE, (index // COLUMNS) * TILE
        for y in range(TILE):
            start = ((ty + y) * width + tx) * 4
            pixels[start:start + TILE * 4] = tile[y * TILE * 4:(y + 1) * TILE * 4]
    layout["width"], layout["height"] = width, height
    return png.encode_rgba(width, height, pixels), layout, styles


# ----- decal geometry ---------------------------------------------------------------------------

FEATURES = {
    # name: (x centre, y centre, width, height) in head-local metres on the face (male scale)
    "eyeLeft": (0.054, -0.004, 0.076, 0.070), "eyeRight": (-0.054, -0.004, 0.076, 0.070),
    "eyebrowLeft": (0.056, 0.056, 0.080, 0.044), "eyebrowRight": (-0.056, 0.056, 0.080, 0.044),
    "mouth": (0.0, -0.073, 0.136, 0.080),
}
# Makeup patches: eye shadow on a raised, larger copy of the eye patch (the shadow tiles place the
# eye opening at y = -0.26), eyeliner on the eye patch itself, blush on the cheeks, lips on the mouth.
SHADOW_PATCH = (0.0, 0.0105, 1.15, 1.30)   # dx, dy (metres, male scale), width and height factors
MAKEUP = {
    "shadowLeft": ("eyeLeft", "shadow"), "shadowRight": ("eyeRight", "shadow"),
    "linerLeft": ("eyeLeft", "same"), "linerRight": ("eyeRight", "same"),
    "lips": ("mouth", "same"),
    "blushLeft": ((0.074, -0.044, 0.070, 0.056), None), "blushRight": ((-0.074, -0.044, 0.070, 0.056), None),
}
# How far decals lie above the skin; they lie on the head's own facets (head.conforming_patch), so
# they follow the sculpted lids and lips exactly.
LIFT = 0.0008


def _painted_specs(name):
    """Every atlas tile drawn on a patch: a feature's states and styles, and the makeup sharing it."""
    if name in ("eyeLeft", "eyeRight"):
        side = name[3:]
        return [("eye", st, side, layer) for st in EYES for layer in (0, 1)] + \
            [("eyeliner", style, side, closed) for style in LINER_STYLES for closed in (False, True)]
    if name in ("eyebrowLeft", "eyebrowRight"):
        side = name[7:]
        return [("eyebrow", st, side) for st in EYEBROWS] + \
            [("eyebrow", st, side, style) for style in BROW_STYLES for st in EYEBROWS]
    if name == "mouth":
        return [("mouth", st) for st in MOUTHS] + [("lips", st) for st in MOUTHS]
    if name in ("shadowLeft", "shadowRight"):
        return [("eyeShadow", style, name[6:]) for style in SHADOW_STYLES]
    return None


_COVERAGE = {}
COVERAGE_SIZE = 48


def _coverage(name):
    """Where any tile drawn on patch `name` paints (a coarse grid, grown by a cell)."""
    if name not in _COVERAGE:
        n = COVERAGE_SIZE
        painted = [[False] * n for _ in range(n)]
        for spec in _painted_specs(name):
            pixels = render(_tile_layers(spec), n)
            for y in range(n):
                for x in range(n):
                    if pixels[(y * n + x) * 4 + 3] > 0:
                        painted[y][x] = True
        grown = [[any(painted[yy][xx] for yy in range(max(0, y - 1), min(n, y + 2)) for xx in range(max(0, x - 1), min(n, x + 2)))
                  for x in range(n)] for y in range(n)]
        _COVERAGE[name] = grown
    return _COVERAGE[name]


def _trimmed(mesh, name):
    """The patch without the facets no tile of it ever paints (they would only draw nothing)."""
    if _painted_specs(name) is None:
        return mesh
    cover, n = _coverage(name), COVERAGE_SIZE
    cell = lambda uv: (min(n - 1, int(uv[1] * n)), min(n - 1, int(uv[0] * n)))
    kept = []
    for t in range(0, len(mesh.indices), 3):
        tri = mesh.indices[t:t + 3]
        us = [mesh.uvs[i][0] for i in tri]
        vs = [mesh.uvs[i][1] for i in tri]
        r0, c0 = cell((min(us), min(vs)))
        r1, c1 = cell((max(us), max(vs)))
        if any(cover[r][c] for r in range(r0, r1 + 1) for c in range(c0, c1 + 1)):
            kept += tri
    mesh.indices = kept
    return mesh.compact()


def build_decals(body_type):
    from . import head
    s = head.SCALE[body_type]
    return {name: _trimmed(head.conforming_patch(body_type, x * s, y * s, w * s, h * s, LIFT * s), name)
            for name, (x, y, w, h) in FEATURES.items()}


def build_makeup(body_type, decals=None):
    """Makeup patches (drawn only when a description wears that makeup). Eyeliner and lips lie on
    exactly the patch of the feature they follow (`decals`, from build_decals), so they share it."""
    from . import head
    s = head.SCALE[body_type]
    decals = decals or build_decals(body_type)
    out = {}
    for name, (base, how) in MAKEUP.items():
        if how == "same":
            out[name] = decals[base]
            continue
        if how is None:
            # Blush lies on the smooth cheek and fades out before its edge: a light patch will do.
            x, y, w, h = base
            out[name] = head.decal_patch(body_type, x * s, y * s, w * s, h * s, LIFT * s * 1.5, 10, 8)
            continue
        x, y, w, h = FEATURES[base]
        dx, dy, fw, fh = SHADOW_PATCH
        x, y, w, h = x + dx, y + dy, w * fw, h * fh
        out[name] = _trimmed(head.conforming_patch(body_type, x * s, y * s, w * s, h * s, LIFT * s * 0.8), name)
    return out
