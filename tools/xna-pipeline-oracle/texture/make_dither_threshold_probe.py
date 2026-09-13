#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-222: read D3DX's ordered-dither threshold straight
off the genuine pipeline, at every position of every level size.

A *flat* block -- both endpoints the same 565 value -- has one exact rational per channel and no
interpolation at all, so the byte the pipeline writes says exactly which threshold that destination
position used. Sweeping the endpoint over all thirty-two five-bit values walks the fraction across
the whole threshold range, and one file carries every level of a chain at once.

    python3 tools/xna-pipeline-oracle/texture/make_dither_threshold_probe.py <outdir>
    CNA_DIFFERENTIAL_SOURCES=<outdir>/src \
        tools/xna-pipeline-oracle/differential/run-differential-oracle.sh \
        dither_threshold <outdir>/out

The manifest it writes is a differential-oracle corpus, so the same runner that builds the
committed corpus builds this one; nothing here is committed but the generator.
"""
from __future__ import annotations

import json
import os
import struct
import sys

# Level-0 shapes whose chains between them cover every width from 1 to 88 that is 0, 1, 2 or 3
# modulo four, and heights that differ from their widths.
SHAPES = {"p8": (8, 8), "n24": (24, 24), "m24x16": (24, 16), "w44": (44, 44), "w36": (36, 36),
          "w28": (28, 28), "w60": (60, 60), "r88x20": (88, 20)}


def flat_block(colour: int, alpha: int = 200) -> bytes:
    """A DXT5 block with one colour and one alpha, so only the dither can move its output."""
    return (bytes([alpha, alpha]) + (0).to_bytes(6, "little") +
            struct.pack("<HHI", colour, colour, 0))


def chain(width: int, height: int, colour: int) -> list[bytes]:
    levels, w, h = [], width, height
    while True:
        levels.append(flat_block(colour) *
                      (max(1, (w + 3) // 4) * max(1, (h + 3) // 4)))
        if w == 1 and h == 1:
            break
        w = max(1, w // 2)
        h = max(1, h // 2)
    return levels


def dds(path: str, width: int, height: int, levels: list[bytes]) -> None:
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<I", header, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 |
                     (0x20000 if len(levels) > 1 else 0))
    struct.pack_into("<I", header, 12, height)
    struct.pack_into("<I", header, 16, width)
    struct.pack_into("<I", header, 20, len(levels[0]))
    struct.pack_into("<I", header, 28, len(levels))
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, 0x4)
    header[84:88] = b"DXT5"
    struct.pack_into("<I", header, 108, 0x401008 if len(levels) > 1 else 0x1000)
    with open(path, "wb") as handle:
        handle.write(bytes(header) + b"".join(levels))


def main(argv) -> int:
    out = argv[0] if argv else "build/xna-sample-sweep/probe-dither-threshold"
    sources = os.path.join(out, "src", "texture")
    os.makedirs(sources, exist_ok=True)
    cases = []
    for endpoint in range(32):
        # Three channels sweeping independently, so one file asks three fractions rather than one.
        colour = (endpoint << 11) | (((2 * endpoint) % 64) << 5) | (31 - endpoint)
        for tag, (width, height) in SHAPES.items():
            name = "%s_q%02d" % (tag, endpoint)
            dds(os.path.join(sources, name + ".dds"), width, height, chain(width, height, colour))
            cases.append({"case": "dither/" + name, "source": "texture/%s.dds" % name,
                          "importer": "TextureImporter", "processor": "TextureProcessor",
                          "platform": "Windows", "profile": "HiDef",
                          "parameters": {"ColorKeyEnabled": "False", "PremultiplyAlpha": "False"}})
    manifest = os.path.join(out, "dither_threshold.json")
    with open(manifest, "w") as handle:
        json.dump({"format": "CNA.XnaDifferential.Corpus", "version": 1,
                   "comment": "plans/plan_xna_sample_xnb_sweep.md XNASWEEP-222 dither threshold",
                   "cases": cases}, handle, indent=1)
    print("%d cases, %d sources, manifest %s" % (len(cases), len(os.listdir(sources)), manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
