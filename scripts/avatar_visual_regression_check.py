#!/usr/bin/env python3
"""Reproducible visual regression check for demo_avatar, run against the REAL rendering pipeline
(real content, real AvatarRenderer, real GPU-skinned draw) - not a synthetic fixture.

audit_net.md remediation, fourth round (2026-07-18): the third-round version of this script
measured only *global* average brightness and a global very-dark-pixel fraction. The audit
correctly rejected that as insufficient - a global average can pass while a localized defect
(a jagged shoe/foot boundary, a black groin wedge) is still plainly visible. This version adds
per-REGION checks and, more importantly, a STRUCTURAL metric that targets the actual defect
shape rather than overall darkness:

  "speckle" = the number of dark pixels that are directly adjacent to a bright pixel.

That is what an interpenetration seam between two low-poly meshes looks like: the renderer
alternates between the two surfaces along a jagged intersection curve, producing many
dark/bright transitions packed into a small area. A smooth, correctly-shaded region - even a
legitimately dark one, like a shoe - has few such transitions, because its darkness is
contiguous. So speckle distinguishes "this area is dark" (fine) from "this area is shattered
into interleaved dark and light fragments" (the real defect).

Regions are fixed pixel boxes. That is reproducible because the demo's camera is fully
determined by --gender/--yaw/--clip, which this script pins.

Usage:
    python3 scripts/avatar_visual_regression_check.py [--build-dir cmake-build-debug]
    python3 scripts/avatar_visual_regression_check.py --report   # print metrics, never fail

Requires: a built cna_demo_avatar in --build-dir, Pillow, and headless xvfb-run.
Exit 0 = all checks at or better than their recorded floor. Exit 1 = a regression.

IMPORTANT: passing does NOT mean the avatar looks correct. It means it has not regressed below
the state recorded when these thresholds were last updated. See NEXTnet.md's avatar section for
the list of defects still known to be open.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("This script requires Pillow: pip install Pillow")

BACKGROUND_RGB = (100, 149, 237)
BACKGROUND_TOLERANCE = 20

# Dark/bright thresholds for the speckle metric, on the 0..765 R+G+B scale. A pixel is "dark"
# below DARK_SUM and "bright" above BRIGHT_SUM; the gap between them stops ordinary smooth
# shading gradients from registering as transitions.
DARK_SUM = 150
BRIGHT_SUM = 420

# label -> (cli args, screenshot filename, {region_name: (x0, y0, x1, y1)})
SHOTS = [
    ("male T-pose", ["--gender", "male", "--yaw", "0"], "regression-male.png", {
        "groin": (370, 218, 430, 258),
        "feet": (352, 378, 448, 458),
    }),
    ("female T-pose", ["--gender", "female", "--yaw", "0"], "regression-female.png", {
        "groin": (370, 218, 430, 258),
        "feet": (352, 378, 448, 458),
    }),
    ("male Wave", ["--gender", "male", "--clip", "Wave", "--yaw", "25"], "regression-wave.png", {
        "torso_shoulder": (350, 100, 450, 260),
    }),
]

# Recorded ceilings. Global: min average brightness / max very-dark fraction. Per region:
# max speckle count and max very-dark fraction. Regenerate with --report after an intentional,
# reviewed improvement, and record WHY in the commit message.
# Recorded 2026-07-18 (fourth round), measured values in parentheses. very-dark is 0.0% in every
# region since the EasyGL emissive double-multiply fix, so its ceilings are deliberately tight -
# any reappearance of near-black pixels is a real regression, not noise.
GLOBAL_MIN_AVG_BRIGHTNESS = 330.0  # measured 351-359
GLOBAL_MAX_VERY_DARK_PCT = 0.5     # measured 0.0
REGION_LIMITS = {
    # (max_speckle, max_very_dark_pct)
    ("male T-pose", "groin"): (45, 1.0),            # measured speckle 25
    ("male T-pose", "feet"): (160, 1.0),            # measured speckle 119
    ("female T-pose", "groin"): (45, 1.0),          # measured speckle 5
    ("female T-pose", "feet"): (160, 1.0),          # measured speckle 119
    ("male Wave", "torso_shoulder"): (170, 1.0),    # measured speckle 125
}


def is_background(p):
    return sum(abs(p[i] - BACKGROUND_RGB[i]) for i in range(3)) < BACKGROUND_TOLERANCE


def global_metrics(img):
    w, h = img.size
    px = img.load()
    total = 0
    n = 0
    very_dark = 0
    for x in range(0, w, 2):
        for y in range(0, h, 2):
            p = px[x, y]
            if is_background(p):
                continue
            n += 1
            s = sum(p)
            total += s
            if s < 30:
                very_dark += 1
    if n == 0:
        raise RuntimeError("no non-background pixels - avatar did not render")
    return total / n, 100.0 * very_dark / n


def region_metrics(img, box):
    """Returns (speckle, very_dark_pct, n). speckle counts dark pixels having a bright
    4-neighbour: the signature of an interleaved intersection seam."""
    x0, y0, x1, y1 = box
    px = img.load()
    n = 0
    very_dark = 0
    speckle = 0
    for x in range(x0, x1):
        for y in range(y0, y1):
            p = px[x, y]
            if is_background(p):
                continue
            n += 1
            s = sum(p)
            if s < 30:
                very_dark += 1
            if s < DARK_SUM:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    q = px[x + dx, y + dy]
                    if not is_background(q) and sum(q) > BRIGHT_SUM:
                        speckle += 1
                        break
    if n == 0:
        return 0, 0.0, 0
    return speckle, 100.0 * very_dark / n, n


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", default="cmake-build-debug")
    ap.add_argument("--scratch-dir", default=None)
    ap.add_argument("--report", action="store_true",
                    help="print measured values and always exit 0 (for re-recording thresholds)")
    args = ap.parse_args()

    build_dir = Path(args.build_dir).resolve()
    demo = build_dir / "cna_demo_avatar"
    if not demo.is_file():
        sys.exit(f"cna_demo_avatar not found at {demo} - build it first")

    scratch = Path(args.scratch_dir).resolve() if args.scratch_dir else build_dir / "avatar_regression_scratch"
    scratch.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["SDL_AUDIODRIVER"] = "dummy"

    failures = []
    for label, cli, filename, regions in SHOTS:
        shot = scratch / filename
        cmd = ["xvfb-run", "-a", str(demo), *cli, "--smoke", "30", "--screenshot", str(shot)]
        r = subprocess.run(cmd, cwd=build_dir, env=env, capture_output=True, text=True, timeout=120)
        if r.returncode != 0 or not shot.is_file():
            failures.append(f"{label}: demo run failed (exit {r.returncode})\n{r.stderr}")
            continue

        img = Image.open(shot).convert("RGB")
        avg, vdark = global_metrics(img)
        ok = True
        detail = []
        if avg < GLOBAL_MIN_AVG_BRIGHTNESS:
            ok, _ = False, detail.append(f"global avg {avg:.1f} < {GLOBAL_MIN_AVG_BRIGHTNESS}")
        if vdark > GLOBAL_MAX_VERY_DARK_PCT:
            ok, _ = False, detail.append(f"global very-dark {vdark:.1f}% > {GLOBAL_MAX_VERY_DARK_PCT}%")
        print(f"[{label}] global: avg={avg:.1f} very_dark={vdark:.1f}%")

        for rname, box in sorted(regions.items()):
            speckle, rvdark, n = region_metrics(img, box)
            lim = REGION_LIMITS.get((label, rname))
            mark = ""
            if lim and not args.report:
                max_speckle, max_vd = lim
                if speckle > max_speckle:
                    ok = False
                    mark = " <<< SPECKLE REGRESSION"
                    detail.append(f"{rname} speckle {speckle} > {max_speckle}")
                if rvdark > max_vd:
                    ok = False
                    mark += " <<< DARKNESS REGRESSION"
                    detail.append(f"{rname} very-dark {rvdark:.1f}% > {max_vd}%")
            print(f"    {rname:16s} speckle={speckle:5d}  very_dark={rvdark:5.1f}%  (px={n}){mark}")

        if not ok:
            failures.append(f"{label}: " + "; ".join(detail))

    if args.report:
        print("\n--report: thresholds not enforced.")
        return 0
    if failures:
        print("\nREGRESSION DETECTED:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nAll global and per-region checks at or better than their recorded floor. This does "
          "NOT mean the avatar looks fully correct - see NEXTnet.md's avatar section for the "
          "defects still known to be open.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
