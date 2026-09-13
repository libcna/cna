#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-218: two PNGs that differ only in their alpha channel.

Both declare the same non-standard `gAMA` of 0.45. GDI+ -- which is what XNA's `TextureImporter`
loads an image through -- applies a file gamma to the one with an alpha channel and not to the one
without, because the two take different pixel formats on the way in. The corpus splits on it
exactly and these two say it on their own, in eight by eight pixels with no other variable.

    python3 tools/xna-pipeline-oracle/texture/make_png_gamma_fixtures.py
"""
from __future__ import annotations

import os
import struct
import sys
import zlib

WIDTH = HEIGHT = 8
FILE_GAMMA = 45000


def chunk(kind: bytes, payload: bytes) -> bytes:
    """One PNG chunk: length, type, payload and the CRC over the last two."""
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write(colour_type: int, path: str) -> None:
    """A gradient in `colour_type` (2 truecolour, 6 truecolour with alpha) carrying `gAMA`.

    The values step by 32, 32 and 16 so that a gamma correction moves most of them and the two
    ends stay put, which is what makes the difference readable rather than a digest.
    """
    raw = b""
    for y in range(HEIGHT):
        raw += b"\x00"
        for x in range(WIDTH):
            pixel = bytes([(x * 32 + 8) & 0xFF, (y * 32 + 16) & 0xFF, ((x + y) * 16 + 24) & 0xFF])
            raw += pixel + (b"\xff" if colour_type == 6 else b"")
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, colour_type, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", header)
                     + chunk(b"gAMA", struct.pack(">I", FILE_GAMMA))
                     + chunk(b"IDAT", zlib.compress(raw, 9))
                     + chunk(b"IEND", b""))
    print("%s (colour type %d)" % (path, colour_type))


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    into = os.path.join(repo, "tests", "assets", "xna40", "texture")
    write(2, os.path.join(into, "png_gamma_no_alpha.png"))
    write(6, os.path.join(into, "png_gamma_with_alpha.png"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
