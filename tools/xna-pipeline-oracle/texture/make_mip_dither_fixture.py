#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-223: the source that shows XNA's mip filter taking
the same dither shift the block decoder takes.

Twelve by twelve, uncompressed, so nothing but the filter is in the answer: the first generated
level is six wide, which is two modulo four, and the shift moves every odd row of it. The pixels
are drawn from a fixed seed and are deliberately noisy -- a smooth image averages to values whose
fraction the dither cannot show.

    python3 tools/xna-pipeline-oracle/texture/make_mip_dither_fixture.py
"""
from __future__ import annotations

import os
import random
import struct
import sys

WIDTH = HEIGHT = 12
SEED = 2262230910


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    into = os.path.join(os.path.abspath(os.path.join(here, "..", "..", "..")),
                        "tests", "assets", "xna40", "texture", "probe_mip_dither.dds")
    generator = random.Random(SEED)
    pixels = bytearray()
    for _ in range(WIDTH * HEIGHT):
        pixels += bytes([generator.randrange(256), generator.randrange(256),
                         generator.randrange(256), 255])          # B, G, R, A
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<I", header, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8)   # + DDSD_PITCH
    struct.pack_into("<I", header, 12, HEIGHT)
    struct.pack_into("<I", header, 16, WIDTH)
    struct.pack_into("<I", header, 20, WIDTH * 4)
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, 0x41)                            # RGB | ALPHAPIXELS
    struct.pack_into("<I", header, 88, 32)
    struct.pack_into("<I", header, 92, 0x00FF0000)
    struct.pack_into("<I", header, 96, 0x0000FF00)
    struct.pack_into("<I", header, 100, 0x000000FF)
    struct.pack_into("<I", header, 104, 0xFF000000)
    struct.pack_into("<I", header, 108, 0x1000)
    with open(into, "wb") as handle:
        handle.write(bytes(header) + bytes(pixels))
    print("%s: %dx%d uncompressed, %d bytes" % (into, WIDTH, HEIGHT, 128 + len(pixels)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
