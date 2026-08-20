#!/usr/bin/env python3
"""plans/plan_dx9.md Phase D9-A (D9-A4): diff a real-XNA-4.0 PNG against a CNA PNG.

Usage:
    scripts/xna-diff.py <xna.png> <cna.png> [--diff-out diff.png] [--tolerance N]
                        [--rgb-tolerance N] [--alpha-tolerance N]
                        [--max-raw-differing-pixels N]
                        [--allowed-raw-diff-rect X,Y,W,H]

Reports the count of differing pixels and the max per-channel delta, and (with --diff-out)
writes a visual diff image. The legacy mode marks pixels beyond --tolerance; extended-policy mode
marks every raw difference so tolerated variance remains visible. The optional policy arguments
support a narrowly bounded renderer variance: separate RGB/alpha thresholds, a raw-difference
pixel budget, and a rectangle outside which even an otherwise tolerated raw difference is
forbidden.

Threshold discipline (plans/plan_dx9.md's own "the whole ballgame" warning): --tolerance defaults to
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


def parse_rect(value: str) -> tuple[int, int, int, int]:
    try:
        values = tuple(int(part) for part in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("rectangle must be X,Y,W,H") from error
    if len(values) != 4 or values[2] <= 0 or values[3] <= 0:
        raise argparse.ArgumentTypeError("rectangle must be X,Y,W,H with positive W and H")
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("xna_png", help="reference PNG produced by tools/xna-oracle/Oracle.cs")
    parser.add_argument("cna_png", help="PNG produced by tools/xna-oracle/CnaOracleRender.cpp")
    parser.add_argument("--diff-out", help="optional path to write a visual diff PNG")
    parser.add_argument("--tolerance", type=int, default=0,
                         help="max allowed per-channel delta before a pixel counts as differing (default: 0, exact match)")
    parser.add_argument("--rgb-tolerance", type=int,
                        help="override --tolerance for the RGB channels")
    parser.add_argument("--alpha-tolerance", type=int,
                        help="override --tolerance for the alpha channel")
    parser.add_argument("--max-raw-differing-pixels", type=int,
                        help="maximum pixels allowed to have any non-zero channel delta")
    parser.add_argument("--allowed-raw-diff-rect", type=parse_rect, metavar="X,Y,W,H",
                        help="forbid raw differences outside this rectangle")
    args = parser.parse_args()

    rgb_tolerance = args.tolerance if args.rgb_tolerance is None else args.rgb_tolerance
    alpha_tolerance = args.tolerance if args.alpha_tolerance is None else args.alpha_tolerance
    for name, value in (("tolerance", args.tolerance), ("RGB tolerance", rgb_tolerance),
                        ("alpha tolerance", alpha_tolerance)):
        if value < 0 or value > 255:
            parser.error(f"{name} must be between 0 and 255")
    if args.max_raw_differing_pixels is not None and args.max_raw_differing_pixels < 0:
        parser.error("--max-raw-differing-pixels must be non-negative")

    xna = Image.open(args.xna_png).convert("RGBA")
    cna = Image.open(args.cna_png).convert("RGBA")

    if xna.size != cna.size:
        print(f"FAIL: size mismatch -- xna={xna.size} cna={cna.size}")
        return 1

    width, height = xna.size
    if args.allowed_raw_diff_rect is not None:
        rect_x, rect_y, rect_width, rect_height = args.allowed_raw_diff_rect
        if rect_x < 0 or rect_y < 0 or rect_x + rect_width > width or rect_y + rect_height > height:
            parser.error("--allowed-raw-diff-rect must be contained within both images")

    xna_px = xna.load()
    cna_px = cna.load()

    diff_img = Image.new("RGB", (width, height), (0, 0, 0)) if args.diff_out else None
    diff_px = diff_img.load() if diff_img else None
    extended_policy = (args.rgb_tolerance is not None or args.alpha_tolerance is not None
                       or args.max_raw_differing_pixels is not None
                       or args.allowed_raw_diff_rect is not None)

    differing = 0
    raw_differing = 0
    raw_differing_outside_rect = 0
    max_delta = 0
    max_rgb_delta = 0
    max_alpha_delta = 0
    for y in range(height):
        for x in range(width):
            a = xna_px[x, y]
            b = cna_px[x, y]
            deltas = tuple(abs(a[i] - b[i]) for i in range(4))
            rgb_delta = max(deltas[:3])
            alpha_delta = deltas[3]
            delta = max(rgb_delta, alpha_delta)
            if delta > max_delta:
                max_delta = delta
            if rgb_delta > max_rgb_delta:
                max_rgb_delta = rgb_delta
            if alpha_delta > max_alpha_delta:
                max_alpha_delta = alpha_delta

            raw_difference = delta > 0
            if raw_difference:
                raw_differing += 1
                if args.allowed_raw_diff_rect is not None:
                    rect_x, rect_y, rect_width, rect_height = args.allowed_raw_diff_rect
                    if not (rect_x <= x < rect_x + rect_width and rect_y <= y < rect_y + rect_height):
                        raw_differing_outside_rect += 1

            beyond_tolerance = rgb_delta > rgb_tolerance or alpha_delta > alpha_tolerance
            if beyond_tolerance:
                differing += 1
            if diff_px and (raw_difference if extended_policy else beyond_tolerance):
                diff_px[x, y] = (255, 0, 0)

    if diff_img:
        diff_img.save(args.diff_out)

    total = width * height
    raw_budget_passed = (args.max_raw_differing_pixels is None
                         or raw_differing <= args.max_raw_differing_pixels)
    rect_passed = args.allowed_raw_diff_rect is None or raw_differing_outside_rect == 0
    passed = differing == 0 and raw_budget_passed and rect_passed
    status = "PASS" if passed else "FAIL"
    if not extended_policy:
        print(f"{status}: {differing}/{total} pixels differ beyond tolerance={args.tolerance}, "
              f"max per-channel delta={max_delta}")
    else:
        raw_limit = ("unbounded" if args.max_raw_differing_pixels is None
                     else str(args.max_raw_differing_pixels))
        print(f"{status}: {differing}/{total} pixels differ beyond RGB tolerance={rgb_tolerance} "
              f"and alpha tolerance={alpha_tolerance}; raw differences={raw_differing}/{total} "
              f"(limit={raw_limit}); outside allowed rectangle={raw_differing_outside_rect}; "
              f"max RGB delta={max_rgb_delta}, max alpha delta={max_alpha_delta}")
    if args.diff_out:
        print(f"diff image written to {args.diff_out}")

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
