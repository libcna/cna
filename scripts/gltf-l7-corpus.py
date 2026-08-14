#!/usr/bin/env python3
"""Run the deterministic OPENGLES3 L7 oracle over every canonical glTF fixture.

The harness launches the production cna-gltf-viewer in two independent processes per asset.  A
renderable asset must produce byte-identical PNG files in both processes and pixels matching its
committed EasyGL golden.  A deliberately rejected asset must fail in both processes with the
policy's stable diagnostic fragment.  This gives all 145 corpus assets an explicit disposition;
an exception can never appear merely because the harness skipped a file it could not draw.

Run this below an X server (normally ``xvfb-run -a``).  ``--update-goldens`` is intentionally
explicit and also requires ``--report-out`` so a reviewed policy/report change accompanies every
golden refresh.
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
from typing import Any

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
            ["git", "-C", str(path), "rev-parse", "HEAD"], text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def terminal_error(output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in reversed(lines):
        if line.startswith("cna-gltf-viewer:"):
            return line
    return lines[-1] if lines else ""


def count_non_clear(image: Image.Image) -> int:
    # The clear sentinel is the complete transparent RGBA value.  A black opaque surface is real
    # foreground and must not be mistaken for an empty capture.
    return sum(pixel != CLEAR for pixel in image.getdata())


def compare_to_golden(actual_path: Path, golden_path: Path,
                      rgb_tolerance: int, alpha_tolerance: int) -> dict[str, int]:
    actual = Image.open(actual_path).convert("RGBA")
    golden = Image.open(golden_path).convert("RGBA")
    if actual.size != RESOLUTION or golden.size != RESOLUTION:
        raise RuntimeError(
            f"image size mismatch: actual={actual.size}, golden={golden.size}, "
            f"required={RESOLUTION}"
        )

    difference = ImageChops.difference(actual, golden)
    extrema = difference.getextrema()
    max_rgb = max(channel[1] for channel in extrema[:3])
    max_alpha = extrema[3][1]
    if max_rgb > rgb_tolerance or max_alpha > alpha_tolerance:
        raise RuntimeError(
            f"golden mismatch: max RGB delta {max_rgb} (allowed {rgb_tolerance}), "
            f"max alpha delta {max_alpha} (allowed {alpha_tolerance})"
        )
    return {"maximumRgbDelta": max_rgb, "maximumAlphaDelta": max_alpha}


def run_viewer(viewer: Path, asset: Path, png: Path,
               extra_arguments: list[str], timeout: int) -> dict[str, Any]:
    environment = dict(os.environ)
    environment.update({
        "SDL_VIDEODRIVER": "x11",
        "SDL_AUDIODRIVER": "dummy",
        # The committed EasyGL oracle is intentionally one reproducible renderer/driver route.
        # Hardware GPUs remain useful development comparisons, but cannot share a byte oracle.
        "LIBGL_ALWAYS_SOFTWARE": "1",
    })
    command = [
        str(viewer), str(asset), "--direct", "--capture", str(png),
        "--reference-capture", *extra_arguments,
    ]
    try:
        completed = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            timeout=timeout, env=environment,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        return {
            "returnCode": None,
            "timedOut": True,
            "output": output,
            "terminalError": terminal_error(output),
            "png": png if png.is_file() else None,
        }
    return {
        "returnCode": completed.returncode,
        "timedOut": False,
        "output": completed.stdout,
        "terminalError": terminal_error(completed.stdout),
        "png": png if png.is_file() else None,
    }


def validate_policy(policy: dict[str, Any], asset_ids: list[str]) -> None:
    if policy.get("schemaVersion") != 1 or policy.get("renderer") != "OPENGLES3/EasyGL":
        raise RuntimeError("unsupported L7 policy schema or renderer")
    overrides = policy.get("overrides", {})
    unknown = sorted(set(overrides) - set(asset_ids))
    if unknown:
        raise RuntimeError(f"policy names assets outside the corpus: {', '.join(unknown)}")
    for asset_id, override in overrides.items():
        disposition = override.get("disposition", "capture")
        if disposition not in ("capture", "reject"):
            raise RuntimeError(f"{asset_id}: unsupported disposition {disposition!r}")
        if disposition == "reject" and not override.get("errorContains"):
            raise RuntimeError(f"{asset_id}: rejected disposition needs errorContains")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, required=True,
                        help="OPENGLES3 cna_gltf_viewer executable")
    parser.add_argument("--viewer-source", type=Path,
                        help="optional viewer checkout for provenance")
    parser.add_argument("--policy", type=Path,
                        help="override the committed EasyGL L7 policy")
    parser.add_argument("--output", type=Path,
                        help="empty directory in which to retain the two raw runs")
    parser.add_argument("--report-out", type=Path,
                        help="write machine-readable evidence for this run")
    parser.add_argument("--update-goldens", action="store_true",
                        help="replace committed goldens from run 1 after run 2 agrees byte-for-byte")
    parser.add_argument("--timeout", type=int, default=20,
                        help="per-viewer-process timeout in seconds (default: 20)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    corpus = repo / "tests/assets/gltf"
    corpus_manifest_path = corpus / "manifest.json"
    policy_path = (args.policy or repo / "tests/gltf-l7/easygl-policy.json").resolve()
    golden_root = repo / "tests/gltf-l7/easygl"
    viewer = args.viewer.resolve()
    if not viewer.is_file() or not os.access(viewer, os.X_OK):
        raise RuntimeError(f"viewer is not an executable file: {viewer}")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.update_goldens and args.report_out is None:
        parser.error("--update-goldens requires --report-out")
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("DISPLAY is unset; run this harness below xvfb-run -a")

    corpus_manifest = json.loads(corpus_manifest_path.read_text(encoding="utf-8"))
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    assets = corpus_manifest["assets"]
    asset_ids = [asset["id"] for asset in assets]
    if len(asset_ids) != 145 or len(asset_ids) != len(set(asset_ids)):
        raise RuntimeError("corpus manifest must contain exactly 145 unique assets")
    validate_policy(policy, asset_ids)

    expectations = {
        asset_id: json.loads(
            (corpus / f"{asset_id}.expected.json").read_text(encoding="utf-8")
        )
        for asset_id in asset_ids
    }
    expected_rejections = {
        asset_id for asset_id in asset_ids
        if expectations[asset_id].get("rejection") is not None
    }
    policy_rejections = {
        asset_id for asset_id, override in policy.get("overrides", {}).items()
        if override.get("disposition") == "reject"
    }
    if expected_rejections != policy_rejections:
        missing = sorted(expected_rejections - policy_rejections)
        extra = sorted(policy_rejections - expected_rejections)
        raise RuntimeError(
            f"rejection policy differs from L1-L6 manifests: missing={missing}, extra={extra}"
        )

    comparison = policy["goldenComparison"]
    rgb_tolerance = int(comparison["rgbTolerance"])
    alpha_tolerance = int(comparison["alphaTolerance"])
    if not (0 <= rgb_tolerance <= 255 and 0 <= alpha_tolerance <= 255):
        raise RuntimeError("golden tolerances must be in [0,255]")

    temporary: tempfile.TemporaryDirectory[str] | None = None
    if args.output is None:
        temporary = tempfile.TemporaryDirectory(prefix="cna-gltf-l7-")
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

    results: list[dict[str, Any]] = []
    renderers: set[str] = set()
    try:
        for index, asset in enumerate(assets, 1):
            asset_id = asset["id"]
            source_name = f"{asset_id}.gltf"
            source = corpus / source_name
            override = policy.get("overrides", {}).get(asset_id, {})
            disposition = override.get("disposition", "capture")
            extra_arguments = list(override.get("viewerArguments", []))
            run_1 = run_viewer(
                viewer, source, first_root / f"{asset_id}.png", extra_arguments, args.timeout
            )
            run_2 = run_viewer(
                viewer, source, second_root / f"{asset_id}.png", extra_arguments, args.timeout
            )
            for output_text in (run_1["output"], run_2["output"]):
                for line in output_text.splitlines():
                    if line.startswith("EasyGLRenderer initialized with "):
                        renderers.add(line.removeprefix("EasyGLRenderer initialized with "))

            base_result: dict[str, Any] = {
                "id": asset_id,
                "source": source_name,
                "sourceSha256": sha256(source),
                "disposition": disposition,
                "viewerArguments": extra_arguments,
            }
            if disposition == "reject":
                expected_fragment = override["errorContains"]
                for run_number, run in enumerate((run_1, run_2), 1):
                    if run["timedOut"] or run["returnCode"] == 0 or run["png"] is not None:
                        raise RuntimeError(
                            f"{asset_id} run {run_number}: expected a prompt rejection, "
                            f"got returnCode={run['returnCode']} png={run['png']}"
                        )
                    if expected_fragment not in run["terminalError"]:
                        raise RuntimeError(
                            f"{asset_id} run {run_number}: diagnostic lacks "
                            f"{expected_fragment!r}: {run['terminalError']}"
                        )
                if run_1["terminalError"] != run_2["terminalError"]:
                    raise RuntimeError(f"{asset_id}: rejection diagnostic is not deterministic")
                base_result.update({
                    "returnCode": run_1["returnCode"],
                    "owningTask": expectations[asset_id]["rejection"]["task"],
                    "errorContains": expected_fragment,
                    "terminalError": run_1["terminalError"],
                    "twoProcessDispositionIdentical": True,
                })
                results.append(base_result)
                print(f"[{index:03d}/145] {asset_id}: deterministic rejection", flush=True)
                continue

            for run_number, run in enumerate((run_1, run_2), 1):
                if run["timedOut"] or run["returnCode"] != 0 or run["png"] is None:
                    raise RuntimeError(
                        f"{asset_id} run {run_number}: capture failed: {run['terminalError']}"
                    )
            first_png = run_1["png"]
            second_png = run_2["png"]
            first_hash = sha256(first_png)
            second_hash = sha256(second_png)
            if first_hash != second_hash or first_png.read_bytes() != second_png.read_bytes():
                raise RuntimeError(f"{asset_id}: two independent PNG files are not byte-identical")

            image = Image.open(first_png).convert("RGBA")
            if image.size != RESOLUTION:
                raise RuntimeError(f"{asset_id}: captured {image.size}, expected {RESOLUTION}")
            non_clear = count_non_clear(image)
            allow_empty = bool(override.get("allowEmptyForeground", False))
            if non_clear == 0 and not allow_empty:
                raise RuntimeError(
                    f"{asset_id}: capture is entirely clear without an explicit policy reason"
                )
            golden = golden_root / f"{asset_id}.png"
            if args.update_goldens:
                shutil.copy2(first_png, golden)
            if not golden.is_file():
                raise RuntimeError(f"{asset_id}: committed golden is missing: {golden}")
            metrics = compare_to_golden(
                first_png, golden, rgb_tolerance=rgb_tolerance,
                alpha_tolerance=alpha_tolerance,
            )
            bbox = image.getbbox()
            base_result.update({
                "golden": str(golden.relative_to(repo)),
                "goldenPngSha256": sha256(golden),
                "twoProcessPngSha256": first_hash,
                "twoProcessPngByteIdentical": True,
                "resolution": list(RESOLUTION),
                "nonClearPixelCount": non_clear,
                "nonClearBoundingBox": list(bbox) if bbox is not None else None,
                "emptyForegroundAllowed": allow_empty,
                "comparison": metrics,
            })
            results.append(base_result)
            print(
                f"[{index:03d}/145] {asset_id}: deterministic capture "
                f"({non_clear} non-clear pixels)", flush=True,
            )

        golden_names = {path.stem for path in golden_root.glob("*.png")}
        captured_ids = {result["id"] for result in results if result["disposition"] == "capture"}
        if golden_names != captured_ids:
            raise RuntimeError(
                "golden set differs from captured disposition set: "
                f"missing={sorted(captured_ids-golden_names)}, stale={sorted(golden_names-captured_ids)}"
            )

        viewer_source = args.viewer_source.resolve() if args.viewer_source else None
        report = {
            "schemaVersion": 1,
            "tasks": ["GLTF-009", "GLTF-390", "GLTF-391"],
            "capturedOn": datetime.date.today().isoformat(),
            "corpusManifest": "tests/assets/gltf/manifest.json",
            "corpusManifestSha256": sha256(corpus_manifest_path),
            "distinctAssetCount": len(results),
            "capturedAssetCount": sum(r["disposition"] == "capture" for r in results),
            "rejectedAssetCount": sum(r["disposition"] == "reject" for r in results),
            "renderer": policy["renderer"],
            "rendererEnvironment": sorted(renderers),
            "cnaCommit": git_revision(repo),
            "viewerCommit": git_revision(viewer_source) if viewer_source else None,
            "viewerExecutableSha256": sha256(viewer),
            "capture": {
                "processesPerAsset": 2,
                "resolution": list(RESOLUTION),
                "clearPixel": list(CLEAR),
                "baseViewerArguments": ["--direct", "--capture", "--reference-capture"],
                "environment": {
                    "SDL_VIDEODRIVER": "x11",
                    "SDL_AUDIODRIVER": "dummy",
                    "LIBGL_ALWAYS_SOFTWARE": "1",
                },
            },
            "goldenComparison": comparison,
            "classification": {
                "scope": (
                    "Differences between this run and the committed same-renderer goldens. "
                    "Reported/deferred glTF features remain owned by their L1-L6 diagnostics; "
                    "the independent Khronos subset is docs/gltf-reference-comparison.json."
                ),
                "activeGoldenDivergences": [],
                "resolvedDivergences": policy.get("resolvedDivergences", []),
                "presentationRigExceptions": [
                    {
                        "asset": asset_id,
                        "ownerTask": "GLTF-009",
                        "reason": override["reason"],
                    }
                    for asset_id, override in sorted(policy.get("overrides", {}).items())
                    if override.get("viewerArguments")
                ],
                "intentionalClearCaptures": [
                    {
                        "asset": asset_id,
                        "ownerTask": "GLTF-009",
                        "reason": override["reason"],
                    }
                    for asset_id, override in sorted(policy.get("overrides", {}).items())
                    if override.get("allowEmptyForeground")
                ],
                "expectedSafeRejections": [
                    {
                        "asset": asset_id,
                        "ownerTask": expectations[asset_id]["rejection"]["task"],
                    }
                    for asset_id in sorted(policy_rejections)
                ],
            },
            "assets": results,
        }
        if args.report_out is not None:
            report_path = args.report_out.resolve()
            report_path.parent.mkdir(parents=True, exist_ok=True)
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(
            f"PASS: {len(results)} explicit dispositions; "
            f"{report['capturedAssetCount']} deterministic PNGs, "
            f"{report['rejectedAssetCount']} deterministic safe rejections",
            flush=True,
        )
        return 0
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
