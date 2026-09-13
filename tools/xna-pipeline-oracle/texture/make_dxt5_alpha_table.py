#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-220: one DXT5 image that reads a decoder's whole
alpha interpolation table.

A DXT5 block's alpha is two endpoints and a three-bit index per texel, and what the six or four
values in between are is the decoder's own arithmetic. Asking a corpus texture about it answers
only the endpoint pairs that texture happens to contain; this asks 1,024 pairs at once, each block
carrying every one of the eight indices twice, so a single build through the genuine pipeline
gives the table rather than a sample of it.

    python3 tools/xna-pipeline-oracle/texture/make_dxt5_alpha_table.py
"""
from __future__ import annotations

import os
import random
import struct
import sys

WIDTH = HEIGHT = 128
BLOCKS_ACROSS = WIDTH // 4
BLOCKS_DOWN = HEIGHT // 4
SEED = 20260910

# The pairs worth asking about on purpose: both orderings, both ends of the range, and neighbours
# one apart, where a rounding rule and a truncating one disagree most often.
INTERESTING = (0, 1, 2, 3, 63, 64, 127, 128, 129, 191, 192, 252, 253, 254, 255)


def endpoint_pairs() -> list[tuple[int, int]]:
    """Every interesting pair first, then random ones until there is one per block."""
    pairs = [(a0, a1) for a0 in INTERESTING for a1 in INTERESTING]
    generator = random.Random(SEED)
    while len(pairs) < BLOCKS_ACROSS * BLOCKS_DOWN:
        pairs.append((generator.randrange(256), generator.randrange(256)))
    return pairs[:BLOCKS_ACROSS * BLOCKS_DOWN]


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    into = os.path.join(repo, "tests", "assets", "xna40", "texture", "dxt5_alpha_table.dds")

    blocks = b""
    for a0, a1 in endpoint_pairs():
        indices = 0
        for texel in range(16):
            indices |= (texel % 8) << (3 * texel)
        # A flat colour half -- both endpoints the same -- so nothing in the colour channels
        # interpolates and the alpha is the only thing the image is asking about.
        blocks += bytes([a0, a1]) + indices.to_bytes(6, "little") \
            + struct.pack("<HHI", 0xFFFF, 0xFFFF, 0)

    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<I", header, 8, 0x1007)        # CAPS | HEIGHT | WIDTH | PIXELFORMAT
    struct.pack_into("<I", header, 12, HEIGHT)
    struct.pack_into("<I", header, 16, WIDTH)
    struct.pack_into("<I", header, 20, len(blocks))  # linear size
    struct.pack_into("<I", header, 76, 32)           # pixel format size
    struct.pack_into("<I", header, 80, 0x4)          # DDPF_FOURCC
    header[84:88] = b"DXT5"
    struct.pack_into("<I", header, 108, 0x1000)      # DDSCAPS_TEXTURE
    with open(into, "wb") as handle:
        handle.write(bytes(header) + blocks)
    print("%s: %d blocks, %d bytes" % (into, BLOCKS_ACROSS * BLOCKS_DOWN, 128 + len(blocks)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
