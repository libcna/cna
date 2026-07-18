#!/usr/bin/env python3
"""audit_net.md remediation (2026-07-18, third round): reproducible visual regression check for
demo_avatar, run against the REAL rendering pipeline (real content, real AvatarRenderer, real
GPU-skinned draw) - not a synthetic fixture. The independent audit that requested this explicitly
rejected "average brightness improved" alone as proof the avatar looks correct; this script does
NOT claim that either. It exists purely as a regression floor: this session's fixes (ambient-light
reorder in AvatarRenderer::DrawRealEXT, AvatarAppearanceEXT's ShoesColor default, the
LowerLeg/Foot bend-joint weight blend, ambient raised 0.35->0.5) measurably improved brightness and
reduced the near-black-pixel fraction versus the original, still-broken baseline - this script
catches any future change that silently regresses BELOW that already-measured, still-imperfect
level. It does not, and cannot, prove the avatar "looks right" - only a human (or a much more
sophisticated perceptual/structural check) can judge that. See NEXTnet.md/plan_net.md's own
avatar sections for the current, honestly-documented list of what still looks wrong (Wave-pose
torso blotches, a groin-area geometric normal singularity, jagged Shoes/Foot boundary artifacts).

Usage:
    python3 scripts/avatar_visual_regression_check.py [--build-dir cmake-build-debug]

Requires: an already-built cna_demo_avatar in --build-dir, Pillow (PIL), and a working
xvfb-run + SDL_AUDIODRIVER=dummy headless environment (matches every other screenshot-based
verification already used throughout this project's own avatar remediation work).

Exit code 0: every shot met its floor. Exit code 1: at least one shot regressed, or a demo run
itself failed - printed detail identifies exactly which shot and which metric.
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

# (label, cli_args, screenshot_filename)
SHOTS = [
    ("male T-pose", ["--gender", "male", "--yaw", "0"], "regression-male.png"),
    ("female T-pose", ["--gender", "female", "--yaw", "0"], "regression-female.png"),
    ("male Wave", ["--gender", "male", "--clip", "Wave", "--yaw", "25"], "regression-wave.png"),
]

# Floors set with real margin below this session's own measured values (male T avg=337.4/
# verydark=4.9%, female T avg=332.0/verydark=6.0%, male Wave avg=337.5/verydark=4.1% - see this
# script's own module docstring and NEXTnet.md's avatar section for the exact commit/measurement
# these came from). Not "looks good" thresholds - regression floors only.
MIN_AVG_BRIGHTNESS = 300.0  # sum of R+G+B (0-765) over non-background pixels
MAX_VERY_DARK_FRACTION_PCT = 8.0  # % of non-background pixels with R+G+B < 30

BACKGROUND_RGB = (100, 149, 237)
BACKGROUND_TOLERANCE = 20


def is_background(pixel):
    return sum(abs(pixel[i] - BACKGROUND_RGB[i]) for i in range(3)) < BACKGROUND_TOLERANCE


def compute_metrics(png_path):
    img = Image.open(png_path).convert("RGB")
    w, h = img.size
    total_brightness = 0
    very_dark_count = 0
    counted = 0
    for x in range(0, w, 2):
        for y in range(0, h, 2):
            pixel = img.getpixel((x, y))
            if is_background(pixel):
                continue
            counted += 1
            brightness = sum(pixel)
            total_brightness += brightness
            if brightness < 30:
                very_dark_count += 1
    if counted == 0:
        raise RuntimeError(f"{png_path}: no non-background pixels found - avatar not rendered?")
    return total_brightness / counted, 100.0 * very_dark_count / counted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="cmake-build-debug")
    parser.add_argument("--scratch-dir", default=None, help="Where to write screenshots (default: a temp dir)")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    demo_binary = build_dir / "cna_demo_avatar"
    if not demo_binary.is_file():
        sys.exit(f"cna_demo_avatar not found at {demo_binary} - build it first "
                  f"(cmake --build {args.build_dir} --target cna_demo_avatar)")

    scratch_dir = Path(args.scratch_dir).resolve() if args.scratch_dir else build_dir / "avatar_regression_scratch"
    scratch_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["SDL_AUDIODRIVER"] = "dummy"

    failures = []
    for label, cli_args, filename in SHOTS:
        screenshot_path = scratch_dir / filename
        cmd = ["xvfb-run", "-a", str(demo_binary), *cli_args, "--smoke", "30", "--screenshot", str(screenshot_path)]
        result = subprocess.run(cmd, cwd=build_dir, env=env, capture_output=True, text=True, timeout=60)
        if result.returncode != 0 or not screenshot_path.is_file():
            failures.append(f"{label}: demo run failed (exit {result.returncode}, screenshot "
                             f"{'missing' if not screenshot_path.is_file() else 'present'})\n{result.stderr}")
            continue

        avg_brightness, very_dark_pct = compute_metrics(screenshot_path)
        ok = True
        detail = []
        if avg_brightness < MIN_AVG_BRIGHTNESS:
            ok = False
            detail.append(f"avg brightness {avg_brightness:.1f} < floor {MIN_AVG_BRIGHTNESS}")
        if very_dark_pct > MAX_VERY_DARK_FRACTION_PCT:
            ok = False
            detail.append(f"very-dark fraction {very_dark_pct:.1f}% > ceiling {MAX_VERY_DARK_FRACTION_PCT}%")

        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {label}: avg_brightness={avg_brightness:.1f} very_dark_pct={very_dark_pct:.1f}%")
        if not ok:
            failures.append(f"{label}: {'; '.join(detail)}")

    if failures:
        print("\nREGRESSION DETECTED:")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("\nAll shots at or above their regression floor. This does NOT mean the avatar looks "
          "fully correct - see NEXTnet.md's avatar section for known, still-open visual issues.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
