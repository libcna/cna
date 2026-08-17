#!/usr/bin/env python3
"""Run the 14-fixture glTF material L7 oracle on the production Vulkan viewer.

EasyGL's complete two-process corpus oracle is the first renderer required by GLTF-244.  This
runner supplies the independent second renderer without implying that Vulkan has a complete
corpus oracle: it launches every generated ``mat-*.gltf`` fixture twice, requires byte-identical
512x512 captures, and compares them byte-for-byte with Vulkan-specific goldens.

Run below one X server, normally ``xvfb-run -a``, with a deterministic Vulkan ICD selected by the
caller.  For Mesa lavapipe on Debian that is typically::

    VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a \
      python3 scripts/gltf-l7-vulkan-materials.py --viewer /path/to/cna_gltf_viewer
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image, ImageChops


RESOLUTION = (512, 512)
CLEAR = (0, 0, 0, 0)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision(path: Path) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def run_viewer(viewer: Path, asset: Path, png: Path, timeout: int) -> str:
    environment = dict(os.environ)
    environment.update({"SDL_VIDEODRIVER": "x11", "SDL_AUDIODRIVER": "dummy"})
    command = [
        str(viewer), str(asset), "--direct", "--capture", str(png), "--reference-capture",
    ]
    try:
        completed = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            env=environment,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{asset.stem}: viewer timed out after {timeout}s") from error
    if completed.returncode != 0:
        raise RuntimeError(
            f"{asset.stem}: viewer exited {completed.returncode}\n{completed.stdout}"
        )
    if not png.is_file():
        raise RuntimeError(f"{asset.stem}: viewer succeeded without writing {png}")
    if "[Vulkan] GPU:" not in completed.stdout or "CNA: graphics renderer: VULKAN" not in completed.stdout:
        raise RuntimeError(f"{asset.stem}: executable did not identify a Vulkan renderer")
    return completed.stdout


def count_non_clear(path: Path) -> int:
    image = Image.open(path).convert("RGBA")
    if image.size != RESOLUTION:
        raise RuntimeError(f"{path.name}: capture size {image.size}, expected {RESOLUTION}")
    return sum(pixel != CLEAR for pixel in image.getdata())


def require_equal(left: Path, right: Path, label: str) -> None:
    left_image = Image.open(left).convert("RGBA")
    right_image = Image.open(right).convert("RGBA")
    if left_image.size != RESOLUTION or right_image.size != RESOLUTION:
        raise RuntimeError(f"{label}: both images must be {RESOLUTION}")
    extrema = ImageChops.difference(left_image, right_image).getextrema()
    maximum = max(channel[1] for channel in extrema)
    if maximum != 0:
        raise RuntimeError(f"{label}: maximum RGBA delta is {maximum}, required 0")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, required=True,
                        help="VULKAN cna_gltf_viewer executable")
    parser.add_argument("--viewer-source", type=Path,
                        help="optional viewer checkout for provenance")
    parser.add_argument("--output", type=Path,
                        help="empty directory in which to retain both raw runs")
    parser.add_argument("--report-out", type=Path,
                        help="write machine-readable evidence for this run")
    parser.add_argument("--update-goldens", action="store_true",
                        help="replace Vulkan material goldens after both runs agree")
    parser.add_argument("--timeout", type=int, default=30,
                        help="per-viewer-process timeout in seconds (default: 30)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    corpus = repo / "tests/assets/gltf"
    golden_root = repo / "tests/gltf-l7/vulkan-materials"
    viewer = args.viewer.resolve()
    assets = sorted(corpus.glob("mat-*.gltf"))
    if len(assets) != 14:
        raise RuntimeError(f"material L7 set must contain exactly 14 fixtures, found {len(assets)}")
    if not viewer.is_file() or not os.access(viewer, os.X_OK):
        raise RuntimeError(f"viewer is not executable: {viewer}")
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("DISPLAY is unset; run this harness below xvfb-run -a")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.update_goldens and args.report_out is None:
        parser.error("--update-goldens requires --report-out")

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if args.output is None:
        temporary = tempfile.TemporaryDirectory(prefix="cna-gltf-vulkan-materials-")
        output = Path(temporary.name)
    else:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=True)
        if any(output.iterdir()):
            raise RuntimeError(f"output directory must be empty: {output}")
    first_root = output / "run-1"
    second_root = output / "run-2"
    first_root.mkdir()
    second_root.mkdir()
    if args.update_goldens:
        golden_root.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, object]] = []
    gpu_lines: set[str] = set()
    try:
        for index, asset in enumerate(assets, 1):
            first = first_root / f"{asset.stem}.png"
            second = second_root / f"{asset.stem}.png"
            for output_text in (
                run_viewer(viewer, asset, first, args.timeout),
                run_viewer(viewer, asset, second, args.timeout),
            ):
                gpu_lines.update(
                    line for line in output_text.splitlines() if line.startswith("[Vulkan] GPU:")
                )
            require_equal(first, second, f"{asset.stem} two-process determinism")
            foreground = count_non_clear(first)
            if foreground == 0:
                raise RuntimeError(f"{asset.stem}: accepted material capture is completely clear")
            golden = golden_root / first.name
            if args.update_goldens:
                shutil.copyfile(first, golden)
            if not golden.is_file():
                raise RuntimeError(f"{asset.stem}: missing Vulkan material golden {golden}")
            require_equal(first, golden, f"{asset.stem} golden")
            digest = sha256(first)
            results.append({
                "id": asset.stem,
                "source": f"tests/assets/gltf/{asset.name}",
                "sourceSha256": sha256(asset),
                "golden": f"tests/gltf-l7/vulkan-materials/{golden.name}",
                "goldenPngSha256": digest,
                "twoProcessPngSha256": digest,
                "twoProcessPngByteIdentical": True,
                "nonClearPixelCount": foreground,
            })
            print(f"[{index:02d}/14] {asset.stem}: deterministic capture ({foreground} non-clear pixels)")

        if args.report_out is not None:
            report_path = args.report_out.resolve()
            report_path.parent.mkdir(parents=True, exist_ok=True)
            viewer_source = args.viewer_source.resolve() if args.viewer_source else None
            report = {
                "schemaVersion": 1,
                "tasks": ["GLTF-244", "GLTF-385"],
                "capturedOn": datetime.date.today().isoformat(),
                "scope": "All 14 generated mat-* fixtures; not a claim of whole-corpus Vulkan L7.",
                "renderer": "VULKAN",
                "rendererEnvironment": sorted(gpu_lines),
                "cnaCommit": git_revision(repo),
                "viewerCommit": git_revision(viewer_source) if viewer_source else None,
                "viewerExecutableSha256": sha256(viewer),
                "capture": {
                    "processesPerAsset": 2,
                    "resolution": list(RESOLUTION),
                    "clearPixel": list(CLEAR),
                    "viewerArguments": ["--direct", "--capture", "--reference-capture"],
                    "environment": {
                        "SDL_VIDEODRIVER": "x11",
                        "SDL_AUDIODRIVER": "dummy",
                        "VK_DRIVER_FILES": os.environ.get("VK_DRIVER_FILES"),
                    },
                },
                "goldenComparison": {"rgbTolerance": 0, "alphaTolerance": 0},
                "results": results,
            }
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        if temporary is not None:
            temporary.cleanup()

    print("PASS: 14/14 Vulkan material captures are deterministic and match their goldens")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
