# SPDX-License-Identifier: MS-PL
"""Procedural textures for catalog v2: near-white multipliers the runtime tints with a
description colour (skin, hair, clothing), carrying soft shading and detail.

Deterministic integer-hash value noise only; every texture is small and low-frequency enough to
read without mipmaps (the renderer samples LinearClamp).
"""
import math

from . import png
from .mathutil import clamp, smoothstep


def _hash(x, y, seed):
    h = (x * 374761393 + y * 668265263 + seed * 144269504) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0


def noise(x, y, seed=0, wrap_x=None):
    """Smooth value noise at (x, y) in cells; wrap_x makes it tile horizontally."""
    x0, y0 = math.floor(x), math.floor(y)
    fx, fy = x - x0, y - y0
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def h(i, j):
        if wrap_x:
            i %= wrap_x
        return _hash(i, j, seed)
    a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * sx
    b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * sx
    return a + (b - a) * sy


def fbm(x, y, seed=0, octaves=3, wrap_x=None):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * noise(x * (2 ** o), y * (2 ** o), seed + o * 17, wrap_x * (2 ** o) if wrap_x else None)
        norm += amp
        amp *= 0.5
    return total / norm


def image(width, height, fn):
    """PNG from fn(u, v) -> (r, g, b) or grey in 0..1 at pixel centres (u right, v down)."""
    pixels = bytearray()
    for y in range(height):
        v = (y + 0.5) / height
        for x in range(width):
            u = (x + 0.5) / width
            c = fn(u, v)
            if not isinstance(c, tuple):
                c = (c, c, c)
            pixels += bytes(int(round(255 * clamp(k, 0.0, 1.0))) for k in c) + b"\xff"
    return png.encode_rgba(width, height, pixels)


def hair_clump():
    """u around a lock (0 = its outer crest), v from root (0) to tip (1): strands, darker roots
    and crevices where locks meet."""
    def fn(u, v):
        crest = 0.5 + 0.5 * math.cos(2.0 * math.pi * u)
        strands = fbm(u * 24.0, v * 3.0, 11, 2, wrap_x=24)
        shade = 0.64 + 0.30 * crest + 0.14 * (strands - 0.5)
        shade *= 0.86 + 0.14 * smoothstep(0.0, 0.25, v)
        shine = 0.10 * crest * smoothstep(0.15, 0.35, v) * (1.0 - smoothstep(0.45, 0.7, v))
        return min(1.0, shade + shine)
    return image(128, 64, fn)


def hair_cap():
    """u around the head, v from crown to hairline: fine strands running down."""
    def fn(u, v):
        strands = fbm(u * 64.0, v * 4.0, 23, 2, wrap_x=64)
        return 0.74 + 0.20 * (strands - 0.5) - 0.06 * smoothstep(0.7, 1.0, v)
    return image(256, 64, fn)


def skin_head():
    """Head multiplier: soft cheek warmth, a little lip-area and ear warmth (u = 0.5 is the face)."""
    def fn(u, v):
        warm = 0.0
        for cu in (0.5 - 0.092, 0.5 + 0.092):
            d = ((u - cu) / 0.045) ** 2 + ((v - 0.60) / 0.06) ** 2
            warm = max(warm, max(0.0, 1.0 - d) ** 2)
        chin = max(0.0, 1.0 - ((u - 0.5) / 0.05) ** 2 - ((v - 0.78) / 0.05) ** 2) ** 2
        g = 1.0 - 0.13 * warm - 0.03 * chin
        b = 1.0 - 0.15 * warm - 0.04 * chin
        return (1.0, g, b)
    return image(128, 64, fn)


def cloth(kind, width=128, height=128, seed=0):
    """Fabric multipliers, u around a garment (side seams at 0.25 and 0.75), v along it:
    'knit' (jersey), 'rib', 'cable' (chunky knit), 'fleece', 'denim', 'canvas', 'pleat',
    'leather', 'plain'."""
    def fabric(u, v):
        if kind == "denim":
            twill = 0.5 + 0.5 * math.sin((u * 96.0 + v * 48.0) * 2.0 * math.pi)
            n = fbm(u * 12.0, v * 12.0, seed + 3, 3, wrap_x=12)
            return 0.80 + 0.07 * twill + 0.16 * (n - 0.5)
        if kind == "fleece":
            return 0.87 + 0.12 * (fbm(u * 20.0, v * 20.0, seed + 5, 3, wrap_x=20) - 0.5)
        if kind == "leather":
            return 0.84 + 0.18 * (fbm(u * 10.0, v * 10.0, seed + 7, 3, wrap_x=10) - 0.5)
        if kind == "canvas":
            weave = 0.5 + 0.25 * math.sin(u * 128.0 * 2.0 * math.pi) + 0.25 * math.sin(v * 128.0 * 2.0 * math.pi)
            return 0.84 + 0.05 * weave + 0.10 * (fbm(u * 8.0, v * 8.0, seed + 9, 2, wrap_x=8) - 0.5)
        if kind == "rib":
            return 0.80 + 0.14 * (0.5 + 0.5 * math.cos(u * 48.0 * 2.0 * math.pi))
        if kind == "cable":
            column = (u * 12.0) % 1.0
            braid = 0.5 + 0.5 * math.cos((v * 10.0 + (0.25 if column > 0.5 else -0.25) * math.sin(column * math.pi)) * 2.0 * math.pi)
            ridge = math.sin(column * math.pi)
            return 0.70 + 0.18 * ridge * (0.6 + 0.4 * braid) + 0.06 * (fbm(u * 24.0, v * 24.0, seed + 13, 2, wrap_x=24) - 0.5)
        if kind == "pleat":
            folds = 0.5 + 0.5 * math.cos(u * 16.0 * 2.0 * math.pi)
            return 0.78 + 0.18 * folds * smoothstep(0.1, 0.9, v)
        if kind == "plain":
            return 0.95
        return 0.88 + 0.10 * (fbm(u * 16.0, v * 16.0, seed + 1, 3, wrap_x=16) - 0.5)

    def fn(u, v):
        f = fabric(u, v)
        if kind not in ("plain", "leather", "rib"):
            # Side seams with a line of stitches beside them.
            for seam in (0.25, 0.75):
                d = abs(u - seam) * width
                f *= 1.0 - 0.22 * max(0.0, 1.0 - d / 1.2)
                if 2.0 < d < 3.2 and (v * height) % 6.0 < 3.0:
                    f *= 0.9
        return f
    return image(width, height, fn)
