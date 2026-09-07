#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-139: the font sheets `FontTextureProcessor` reads.

XNA's `FontTextureProcessor` takes a texture whose glyphs are separated by magenta and turns it
into a `SpriteFont`. These four sheets are what that behaviour was measured from, and each answers
one question the others cannot:

* `font_sheet.png` -- three equal 5x7 cells: the glyph rectangles, the characters, the line
  spacing, the kerning and the cropping, and the atlas the packer produces from them.
* `font_sheet_uneven.png` -- three cells of different widths: whether the glyphs come out in
  source order (they do) and in what order they are packed (widest first).
* `font_sheet_many.png` -- ten equal cells: where a row wraps and what the padding is.
* `font_sheet_alpha_border.png` -- the same sheet bordered in transparent black: whether the
  separator colour is read from the image (it is not; the build is refused).
* `font_sheet_edge_touch.png` -- a sheet whose last glyph runs to the right edge with no separator
  column after it: whether a glyph has to be enclosed (it does not).
* `font_sheet_white_corner.png` -- a sheet whose corner texel is white while the rest of its border
  is magenta: whether the scan starts at the image's origin or at the first magenta texel. It is
  the first magenta texel, which is the one rule that explains all six of these sheets at once.

Nothing is downloaded and nothing is third-party: every byte is written here.
"""
from __future__ import annotations

import os
import struct
import sys
import zlib


def write_png(path, pixels):
    height = len(pixels)
    width = len(pixels[0])
    raw = b""
    for row in pixels:
        raw += b"\x00" + b"".join(bytes(texel) for texel in row)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    image = b"\x89PNG\r\n\x1a\n"
    image += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    image += chunk(b"IDAT", zlib.compress(raw, 9))
    image += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(image)
    print("make_font_sheet_fixtures: wrote %s (%dx%d, %d bytes)"
          % (os.path.basename(path), width, height, len(image)))


def sheet(path, cells, border, ink=(255, 255, 255, 255)):
    """One row of glyph cells, each with ink on its left column and top row."""
    width = 1 + sum(cell[0] + 1 for cell in cells)
    height = 1 + max(cell[1] for cell in cells) + 1
    pixels = [[border] * width for _ in range(height)]
    x = 1
    for cellWidth, cellHeight in cells:
        for y in range(1, 1 + cellHeight):
            for column in range(x, x + cellWidth):
                pixels[y][column] = ink if (column == x or y == 1) else (0, 0, 0, 0)
        x += cellWidth + 1
    write_png(path, pixels)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.abspath(os.path.join(here, "..", "..", "..", "tests/assets/xna40/texture"))
    magenta = (255, 0, 255, 255)
    sheet(os.path.join(out, "font_sheet.png"), [(5, 7)] * 3, magenta)
    sheet(os.path.join(out, "font_sheet_uneven.png"), [(4, 6), (7, 6), (3, 6)], magenta)
    sheet(os.path.join(out, "font_sheet_many.png"), [(6, 8)] * 10, magenta)
    sheet(os.path.join(out, "font_sheet_alpha_border.png"), [(5, 7)] * 2, (0, 0, 0, 0))

    # Two cells in a sheet one texel too narrow to hold the separator column after the second, so
    # the second glyph runs to the edge.
    pixels = [[magenta] * 12 for _ in range(9)]
    for x0 in (1, 7):
        for y in range(1, 8):
            for x in range(x0, min(x0 + 5, 12)):
                pixels[y][x] = (255, 255, 255, 255) if (x == x0 or y == 1) else (0, 0, 0, 0)
    write_png(os.path.join(out, "font_sheet_edge_touch.png"), pixels)

    # Three ordinary cells, with the corner texel white rather than magenta.
    pixels = [[magenta] * 19 for _ in range(9)]
    for cell in range(3):
        x0 = 1 + cell * 6
        for y in range(1, 8):
            for x in range(x0, x0 + 5):
                pixels[y][x] = (255, 255, 255, 255) if (x == x0 or y == 1) else (0, 0, 0, 0)
    pixels[0][0] = (255, 255, 255, 255)
    write_png(os.path.join(out, "font_sheet_white_corner.png"), pixels)
    return 0


if __name__ == "__main__":
    sys.exit(main())
