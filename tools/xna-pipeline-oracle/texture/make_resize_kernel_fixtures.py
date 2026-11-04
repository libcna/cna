#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""TQ-038: the sources that show XNA's resize kernel, its step order and its mip filter on odd and
one-sided dimensions, as the genuine BuildContent builds them.

Every file is an 8-bit RGBA PNG (colour type 6) with no gAMA, sRGB or iCCP chunk, so GDI+ and
CNA's decoder read the same samples, and every row is written unfiltered. The noisy ones are drawn
from fixed seeds: a smooth image averages to values whose fraction the dither cannot show.

  order_3x1.png          an opaque texel beside two transparent white ones (TQ-034): resized to
                         4x1, premultiplying first or last answers visibly different second texels.
  probe_resize_5x4.png   5x4 -> 8x4 with partial alpha and one magenta texel: the height is already
                         a power of two and XNA still filters it.
  probe_resize_6x3.png   6x3 -> 8x4, opaque: the kernel and the dither alone.
  probe_mip_8x2.png      mips 8x2 -> 4x1 -> 2x1 -> 1x1: an even axis halved beside a unit one.
  probe_mip_14x5.png     mips 14x5 -> 7x2 -> 3x1 -> 1x1: an even axis halved beside an odd one,
                         then 7 -> 3, which no area average reproduces.
  probe_mip_9x9.png      mips 9x9 -> 4x4 -> 2x2 -> 1x1 with partial alpha: 9 -> 4 after the
                         default premultiply.

    python3 tools/xna-pipeline-oracle/texture/make_resize_kernel_fixtures.py [output directory]
"""
from __future__ import annotations

import os
import random
import struct
import sys
import zlib


def chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def png(path: str, width: int, height: int, pixels: list) -> None:
    rows = [bytes(sum((pixels[y * width + x] for x in range(width)), [])) for y in range(height)]
    raw = b"".join(b"\x00" + row for row in rows)
    data = (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(data)


def noisy(width: int, height: int, seed: int, alpha: str) -> list:
    """RGBA texels; alpha 'opaque', or 'mixed' (0, 255 and partial values)."""
    generator = random.Random(seed)
    pixels = []
    for _ in range(width * height):
        r, g, b = generator.randrange(256), generator.randrange(256), generator.randrange(256)
        if alpha == "opaque":
            a = 255
        else:
            a = generator.choice([0, 255, generator.randrange(1, 255), generator.randrange(1, 255)])
        if (r, g, b, a) == (255, 0, 255, 255):
            r = 254  # only the texel placed on purpose is the default colour key
        pixels.append([r, g, b, a])
    return pixels


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    here = os.path.dirname(os.path.abspath(__file__))
    into = argv[0] if argv else os.path.join(os.path.abspath(os.path.join(here, "..", "..", "..")),
                                             "tests", "assets", "xna40", "texture")
    keyed = noisy(5, 4, 380051, "mixed")
    keyed[1 * 5 + 2] = [255, 0, 255, 255]
    fixtures = {
        "order_3x1.png": (3, 1, [[200, 100, 50, 255], [255, 255, 255, 0], [255, 255, 255, 0]]),
        "probe_resize_5x4.png": (5, 4, keyed),
        "probe_resize_6x3.png": (6, 3, noisy(6, 3, 380052, "opaque")),
        "probe_mip_8x2.png": (8, 2, noisy(8, 2, 380053, "opaque")),
        "probe_mip_14x5.png": (14, 5, noisy(14, 5, 380054, "opaque")),
        "probe_mip_9x9.png": (9, 9, noisy(9, 9, 380055, "mixed")),
    }
    for name, (width, height, pixels) in fixtures.items():
        path = os.path.join(into, name)
        png(path, width, height, pixels)
        print("%s: %dx%d RGBA8, %d bytes" % (path, width, height, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
