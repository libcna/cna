#!/usr/bin/env python3
"""plan_dx9.md Phase D9-A (D9-A4): diff a real-XNA-4.0 PNG against a CNA PNG.

Usage:
    scripts/xna-diff.py <xna.png> <cna.png> [--diff-out diff.png] [--tolerance N]

Reports the count of differing pixels and the max per-channel delta, and (with --diff-out)
writes a visual diff image: unchanged pixels stay black, any differing pixel is drawn full-red
regardless of magnitude, so even a single off-by-one shows up clearly.

Threshold discipline (plan_dx9.md's own "the whole ballgame" warning): --tolerance defaults to
0 (exact match required). Every scene in tools/xna-oracle/scenes/ that involves no floating-point-
sensitive lighting/blending math is expected to pass at tolerance 0 -- CNA/D3D9 and the real XNA
4.0 runtime both execute through the SAME DXVK D3D9 implementation (see this file's own
prerequisite: DXVK must be installed into the XNA oracle's Wine prefix, not just CNA's, or this
script would silently measure a driver difference and blame CNA for it). Do NOT raise
--tolerance to turn a red comparison green without a documented, per-scene reason -- that is
exactly how an authenticity project quietly becomes a parity project.

Depends on Pillow (PIL) for PNG decoding -- not previously a dependency of this project's other
scripts/*.py tools, but the standard library has no PNG decoder and Pillow is the ubiquitous,
already-installed choice on this machine.
"""
import argparse
import sys

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("xna_png", help="reference PNG produced by tools/xna-oracle/Oracle.cs")
    parser.add_argument("cna_png", help="PNG produced by tools/xna-oracle/CnaOracleRender.cpp")
    parser.add_argument("--diff-out", help="optional path to write a visual diff PNG")
    parser.add_argument("--tolerance", type=int, default=0,
                         help="max allowed per-channel delta before a pixel counts as differing (default: 0, exact match)")
    args = parser.parse_args()

    xna = Image.open(args.xna_png).convert("RGBA")
    cna = Image.open(args.cna_png).convert("RGBA")

    if xna.size != cna.size:
        print(f"FAIL: size mismatch -- xna={xna.size} cna={cna.size}")
        return 1

    width, height = xna.size
    xna_px = xna.load()
    cna_px = cna.load()

    diff_img = Image.new("RGB", (width, height), (0, 0, 0)) if args.diff_out else None
    diff_px = diff_img.load() if diff_img else None

    differing = 0
    max_delta = 0
    for y in range(height):
        for x in range(width):
            a = xna_px[x, y]
            b = cna_px[x, y]
            delta = max(abs(a[i] - b[i]) for i in range(4))
            if delta > max_delta:
                max_delta = delta
            if delta > args.tolerance:
                differing += 1
                if diff_px:
                    diff_px[x, y] = (255, 0, 0)

    if diff_img:
        diff_img.save(args.diff_out)

    total = width * height
    passed = differing == 0
    status = "PASS" if passed else "FAIL"
    print(f"{status}: {differing}/{total} pixels differ beyond tolerance={args.tolerance}, "
          f"max per-channel delta={max_delta}")
    if args.diff_out:
        print(f"diff image written to {args.diff_out}")

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
