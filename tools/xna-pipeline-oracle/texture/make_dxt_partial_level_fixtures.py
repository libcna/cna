#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-222: the .dds fixtures that pin what the genuine
pipeline does to a DXT level that is not a whole number of blocks.

Three things happen at such a level and nowhere else, and each fixture is built so that getting it
wrong changes bytes rather than merely rounding differently:

* the ordered dither's column is shifted on an odd row by `(-width) mod 4`, so a 2x2 and a 3x3
  level do not read the matrix the way a 4x4 one does;
* the level is re-encoded, so a block of a single colour comes back as a 565 round trip of it;
* a block whose `c0` is not above its `c1` takes the three-colour rule in every DXT kind, not only
  in DXT1.

    python3 tools/xna-pipeline-oracle/texture/make_dxt_partial_level_fixtures.py
"""
from __future__ import annotations

import os
import struct
import sys

# Chosen by sweeping the endpoint space for pairs where the rule is *visible*: the first makes
# three of a 2x2 level's twelve channels disagree with the unshifted matrix, and the second makes
# the 565 round trip move a 1x1 level by four units across its channels.
SHIFT_PAIR = (0x13B0, 0x1224)
ROUND_TRIP_PAIR = (0x23D0, 0x0005)
THREE_COLOUR_PAIR = (0x001F, 0xFFE0)      # c0 below c1, so the three-colour rule applies
WHOLE_PAIR = (0xFFE0, 0x001F)

MAP = 0xE4E4E4E4        # indices 0, 1, 2, 3 repeating: both endpoints and both blends
ALL_TWO = 0xAAAAAAAA    # every texel index 2
ENDPOINTS_ONLY = 0x44444444  # indices 0 and 1 only, so the re-encode is the identity


def block(c0: int, c1: int, lookup: int, a0: int = 210, a1: int = 40,
          alpha_bits: int = 0) -> bytes:
    """One DXT5 block: an alpha half, then the colour half.

    A level that is not a whole number of blocks has its *alpha* re-encoded as well as its colour,
    and only the degenerate corner of that is closed, so every such level here carries one alpha
    value: what the fixture is asking about is the colour rule, and a varying alpha would ask a
    second question it cannot answer.
    """
    return (bytes([a0, a1]) + alpha_bits.to_bytes(6, "little") +
            struct.pack("<HHI", c0, c1, lookup))


def colour_only(c0: int, c1: int, lookup: int) -> bytes:
    """One DXT1 block."""
    return struct.pack("<HHI", c0, c1, lookup)


def dds(path: str, width: int, height: int, fourcc: bytes, levels: list[bytes]) -> None:
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000
    if len(levels) > 1:
        flags |= 0x20000
    struct.pack_into("<I", header, 8, flags)
    struct.pack_into("<I", header, 12, height)
    struct.pack_into("<I", header, 16, width)
    struct.pack_into("<I", header, 20, len(levels[0]))
    struct.pack_into("<I", header, 28, len(levels))
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, 0x4)
    header[84:88] = fourcc
    struct.pack_into("<I", header, 108, 0x401008 if len(levels) > 1 else 0x1000)
    with open(path, "wb") as handle:
        handle.write(bytes(header) + b"".join(levels))
    print("%s: %dx%d %s, %d level(s), %d bytes"
          % (path, width, height, fourcc.decode(), len(levels), 128 + sum(len(l) for l in levels)))


def blocks_for(width: int, height: int, one: bytes) -> bytes:
    return one * (max(1, (width + 3) // 4) * max(1, (height + 3) // 4))


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    into = os.path.join(os.path.abspath(os.path.join(here, "..", "..", "..")),
                        "tests", "assets", "xna40", "texture")
    os.makedirs(into, exist_ok=True)

    # 4x4 down to 1x1: the smallest chain that carries a whole level, a 2x2 and a 1x1.
    dds(os.path.join(into, "probe_dxt5_partial_pot.dds"), 4, 4, b"DXT5", [
        block(*WHOLE_PAIR, MAP),                                   # 4x4, whole: the plain decode
        block(*SHIFT_PAIR, ENDPOINTS_ONLY, a0=210, a1=210),        # 2x2: the shifted dither
        block(*ROUND_TRIP_PAIR, ALL_TWO, a0=210, a1=210),          # 1x1: the 565 round trip
    ])

    # 24x24 down to 1x1, so the shift is asked for a width of 6 (two modulo four) and of 3 (three
    # modulo four) as well, which are different shifts.
    dds(os.path.join(into, "probe_dxt5_partial_npot.dds"), 24, 24, b"DXT5", [
        blocks_for(24, 24, block(*WHOLE_PAIR, MAP)),
        blocks_for(12, 12, block(*WHOLE_PAIR, ENDPOINTS_ONLY)),
        blocks_for(6, 6, block(*SHIFT_PAIR, ENDPOINTS_ONLY, a0=210, a1=210)),
        blocks_for(3, 3, block(*SHIFT_PAIR, ENDPOINTS_ONLY, a0=210, a1=210)),
        blocks_for(1, 1, block(*ROUND_TRIP_PAIR, ALL_TWO, a0=210, a1=210)),
    ])

    # The three-colour rule, on a whole level of a kind that is not DXT1 -- and its DXT1 twin, where
    # the same index is also fully transparent.
    dds(os.path.join(into, "probe_dxt5_three_colour.dds"), 8, 8, b"DXT5",
        [blocks_for(8, 8, block(*THREE_COLOUR_PAIR, MAP))])
    dds(os.path.join(into, "probe_dxt1_three_colour.dds"), 8, 8, b"DXT1",
        [blocks_for(8, 8, colour_only(*THREE_COLOUR_PAIR, MAP))])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
