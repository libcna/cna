#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-224: block-compressed `.dds` files whose dimensions
are not a whole number of blocks.

A DXT level of w by h pixels holds `ceil(w/4) * ceil(h/4)` blocks, which is what an encoder writes
and what `dwPitchOrLinearSize` describes -- so a 5x5 DXT1 surface carries 2x2 blocks, four of them,
and the pixels of the fourth row and column of each edge block are padding the file still stores.
What the genuine `TextureImporter` answers for that is the question these fixtures ask.

Each block's bytes are distinguishable, so the *placement* of the data in the answered bitmap is
readable as well as its size.

    python3 tools/xna-pipeline-oracle/texture/make_dds_block_fixtures.py [outdir]
"""
from __future__ import annotations

import os
import struct
import sys

CASES = (
    ("dds_blocks_dxt1_5x5.dds",   "DXT1",  5,  5, 1),
    ("dds_blocks_dxt1_9x9.dds",   "DXT1",  9,  9, 1),
    ("dds_blocks_dxt1_17x3.dds",  "DXT1", 17,  3, 1),
    ("dds_blocks_dxt3_5x5.dds",   "DXT3",  5,  5, 1),
    ("dds_blocks_dxt3_9x9.dds",   "DXT3",  9,  9, 1),
    ("dds_blocks_dxt5_5x5.dds",   "DXT5",  5,  5, 1),
    ("dds_blocks_dxt5_17x3.dds",  "DXT5", 17,  3, 1),
    ("dds_blocks_dxt1_1x1.dds",   "DXT1",  1,  1, 1),
    ("dds_blocks_dxt1_22x22.dds", "DXT1", 22, 22, 5),
    ("dds_blocks_dxt1_6x10.dds",  "DXT1",  6, 10, 4),
    ("dds_blocks_dxt1_9x9_mips.dds", "DXT1", 9, 9, 4),
)


def blocks(pixels):
    return (pixels + 3) // 4


def write(path, four_cc, width, height, levels):
    block_bytes = 8 if four_cc == "DXT1" else 16
    out = bytearray(b"DDS ")
    def word(value):
        out.extend(struct.pack("<I", value & 0xFFFFFFFF))
    word(124)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000
    if levels > 1:
        flags |= 0x20000
    word(flags)
    word(height)
    word(width)
    word(blocks(width) * blocks(height) * block_bytes)
    word(0)
    word(levels)
    for _ in range(11):
        word(0)
    word(32)
    word(0x4)
    out.extend(four_cc.encode("ascii"))
    for _ in range(5):
        word(0)
    word(0x1000 | ((0x400000 | 0x8) if levels > 1 else 0))
    for _ in range(4):
        word(0)
    for level in range(levels):
        level_width = max(1, width >> level)
        level_height = max(1, height >> level)
        for block in range(blocks(level_width) * blocks(level_height)):
            for i in range(block_bytes):
                out.append((block * 31 + i * 7 + level * 3) & 0xFF)
    with open(path, "wb") as handle:
        handle.write(bytes(out))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")),
        "tests/assets/xna40/texture")
    os.makedirs(out, exist_ok=True)
    for name, four_cc, width, height, levels in CASES:
        write(os.path.join(out, name), four_cc, width, height, levels)
    print("wrote %d files to %s" % (len(CASES), out))


if __name__ == "__main__":
    main()
