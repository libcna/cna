# SPDX-License-Identifier: MS-PL
"""A minimal PNG encoder, and the corpus's reference texture (plans/plan_gltf.md ``GLTF-190``).

The corpus had no textured fixture at all, because emitting one needs an image and the generator
may not take a third-party dependency (see the README's rules). ``zlib`` and ``struct`` are the
standard library, and a truecolour-with-alpha PNG is a short enough format to write out exactly, so
that constraint costs about forty lines rather than a dependency.

**Why the reference texture looks the way it does.** A UV bug is a *rearrangement*: the V axis
flipped, the two UV sets swapped, a rotation applied that should not have been. Against a
symmetrical image — a plain checkerboard, a gradient, a solid colour — every one of those produces
a perfectly plausible picture, which is exactly why they keep getting shipped. So this texture is
asymmetric under every one of them at once:

* each quadrant is a **different, saturated colour**, so a swap of any two is a colour change;
* each quadrant carries a different **numeral**, so a 180° rotation (which preserves the colour
  layout of an opposite pair) is still visible;
* the numerals are not symmetric top-to-bottom, so a V flip is visible *within* a quadrant even if
  a reader only looks at one.

The numerals are 3×5 pixel glyphs drawn here rather than rendered, because a font would be a
dependency and a font would also make the bytes depend on a version of it.
"""

from __future__ import annotations

import struct
import zlib
from typing import Sequence

#: Quadrant colours, in reading order: top-left, top-right, bottom-left, bottom-right. Chosen so no
#: two are within a channel of each other and none is a shade of any other -- a texture sampled from
#: the wrong quadrant is then wrong by a whole hue, not by a brightness anyone might blame on
#: lighting.
QUADRANT_COLORS: tuple[tuple[int, int, int], ...] = (
    (220, 30, 30),    # 1 -- red
    (30, 160, 60),    # 2 -- green
    (40, 70, 220),    # 3 -- blue
    (230, 190, 20),   # 4 -- yellow
)

#: 3x5 glyphs for the numerals 1..4, one string per row, '#' meaning "ink".
_GLYPHS: dict[int, tuple[str, ...]] = {
    1: (".#.", "##.", ".#.", ".#.", "###"),
    2: ("###", "..#", "###", "#..", "###"),
    3: ("###", "..#", "###", "..#", "###"),
    4: ("#.#", "#.#", "###", "..#", "..#"),
}


def encode_png(width: int, height: int, rows: Sequence[Sequence[tuple[int, int, int, int]]]) -> bytes:
    """Encodes RGBA pixels as an 8-bit truecolour-with-alpha PNG.

    Every scanline uses filter type 0 (None). A filtered encoding would be smaller and would make
    the committed bytes depend on the filter heuristic, which is exactly the kind of thing that
    changes between Python versions and turns a golden into a moving target.

    :param width: image width in pixels.
    :param height: image height in pixels.
    :param rows: ``height`` rows of ``width`` ``(r, g, b, a)`` tuples, each component 0-255.
    :return: the complete PNG file.
    """
    if len(rows) != height:
        raise ValueError(f"expected {height} rows, got {len(rows)}")

    raw = bytearray()
    for y, row in enumerate(rows):
        if len(row) != width:
            raise ValueError(f"row {y}: expected {width} pixels, got {len(row)}")
        raw.append(0)  # filter type: None
        for pixel in row:
            if len(pixel) != 4 or any(not 0 <= c <= 255 for c in pixel):
                raise ValueError(f"row {y}: {pixel!r} is not an 8-bit RGBA tuple")
            raw.extend(pixel)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    # Bit depth 8, colour type 6 (RGBA), deflate, adaptive filtering, no interlace.
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    # Fixed compression level so the bytes are reproducible rather than merely valid.
    compressed = zlib.compress(bytes(raw), 9)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", compressed) + chunk(b"IEND", b""))


def reference_texture(size: int = 16) -> bytes:
    """The corpus's asymmetric reference texture, as PNG bytes.

    :param size: the image's width and height in pixels; must be even and at least 14 so a 3x5
        glyph fits inside a quadrant with a margin.
    :return: the encoded PNG.
    """
    if size % 2 != 0 or size < 14:
        raise ValueError("the reference texture must be even-sized and at least 14 pixels")

    half = size // 2
    ink = (255, 255, 255, 255)
    rows: list[list[tuple[int, int, int, int]]] = []
    for y in range(size):
        row: list[tuple[int, int, int, int]] = []
        for x in range(size):
            quadrant = (0 if y < half else 2) + (0 if x < half else 1)
            r, g, b = QUADRANT_COLORS[quadrant]
            row.append((r, g, b, 255))
        rows.append(row)

    # One numeral per quadrant, at a fixed inset from the quadrant's own top-left corner. The inset
    # is deliberately NOT centred: a centred glyph is symmetric under a 180-degree rotation of the
    # quadrant, which is one of the rearrangements this texture exists to catch.
    inset_x, inset_y = 2, 2
    for quadrant in range(4):
        glyph = _GLYPHS[quadrant + 1]
        origin_x = (quadrant % 2) * half + inset_x
        origin_y = (quadrant // 2) * half + inset_y
        for gy, line in enumerate(glyph):
            for gx, cell in enumerate(line):
                if cell != "#":
                    continue
                rows[origin_y + gy][origin_x + gx] = ink

    return encode_png(size, size, rows)
