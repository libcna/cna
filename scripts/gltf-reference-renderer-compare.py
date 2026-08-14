#!/usr/bin/env python3
"""Capture and compare CNA against the pinned Khronos glTF Sample Renderer.

The script expects both disposable renderer dependencies to be built already. It does not fetch
anything and writes all PNGs below --output. The aggregate JSON is compact enough to retain as
release evidence while the reproducible PNGs remain disposable.
"""

from __future__ import annotations

import argparse
import datetime
import functools
import hashlib
import http.server
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from typing import Iterable

from PIL import Image


PINNED_RENDERER_COMMIT = "863b981fb755359063e370ff7b6e956bda0716e2"
ASSETS = (
    "xf-identity.gltf",
    "interleaved-position-normal.gltf",
    "sparse-position.gltf",
    "sparse-indices.gltf",
    "mode-triangle-strip.gltf",
    "mat-unlit.gltf",
    "mat-basecolor-factor-times-texture.gltf",
    "tex-reference-checkerboard.gltf",
    "draco-triangle.gltf",
    "non-indexed-triangles.gltf",
    "normalized-u8-color.gltf",
    "glb-basic.glb",
)
IDENTITY = (1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def distance_squared(a: Iterable[float], b: Iterable[float]) -> float:
    return sum((x - y) ** 2 for x, y in zip(a, b))


def sphere_from_points(points: list[list[float]]) -> tuple[list[float], float]:
    """The same Ritter-style point-order-sensitive algorithm as BoundingSphere::CreateFromPoints."""
    min_x = max_x = min_y = max_y = min_z = max_z = points[0]
    for point in points:
        if point[0] < min_x[0]: min_x = point
        if point[0] > max_x[0]: max_x = point
        if point[1] < min_y[1]: min_y = point
        if point[1] > max_y[1]: max_y = point
        if point[2] < min_z[2]: min_z = point
        if point[2] > max_z[2]: max_z = point

    sq_x = distance_squared(max_x, min_x)
    sq_y = distance_squared(max_y, min_y)
    sq_z = distance_squared(max_z, min_z)
    low, high = min_x, max_x
    if sq_y > sq_x and sq_y > sq_z:
        low, high = min_y, max_y
    if sq_z > sq_x and sq_z > sq_y:
        low, high = min_z, max_z

    center = [(a + b) * 0.5 for a, b in zip(low, high)]
    radius = math.sqrt(distance_squared(high, center))
    squared_radius = radius * radius
    for point in points:
        squared_distance = distance_squared(point, center)
        if squared_distance > squared_radius:
            point_distance = math.sqrt(squared_distance)
            direction = [(p - c) / point_distance for p, c in zip(point, center)]
            opposite = [c - radius * d for c, d in zip(center, direction)]
            center = [(g + p) * 0.5 for g, p in zip(opposite, point)]
            radius = math.sqrt(distance_squared(point, center))
            squared_radius = radius * radius
    return center, radius


def camera_for_asset(expected_path: Path) -> dict[str, object]:
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    instances = expected.get("l4", {}).get("instances", [])
    if len(instances) != 1:
        raise RuntimeError(f"{expected_path.name}: reference subset requires exactly one instance")
    matrix = instances[0]["worldMatrixColumnMajor"]
    if any(abs(actual - wanted) > 1e-6 for actual, wanted in zip(matrix, IDENTITY)):
        raise RuntimeError(f"{expected_path.name}: reference subset requires an identity placement")
    center, radius = sphere_from_points(instances[0]["worldPositions"])
    scene_radius = max(radius, 0.1)
    distance = max(scene_radius * 3.0, 1.5)
    return {
        "target": center,
        "sceneRadius": scene_radius,
        "distance": distance,
        "near": max(scene_radius * 0.001, 0.001),
        "far": max(distance + scene_radius * 4.0, 100.0),
    }


def mask_metrics(reference_path: Path, cna_path: Path) -> dict[str, object]:
    reference = Image.open(reference_path).convert("RGBA")
    cna = Image.open(cna_path).convert("RGBA")
    if reference.size != (512, 512) or cna.size != reference.size:
        raise RuntimeError(f"capture sizes differ: reference={reference.size}, CNA={cna.size}")
    reference_pixels = list(reference.getdata())
    cna_pixels = list(cna.getdata())
    # The transparent clear value is the coverage sentinel. glTF OPAQUE explicitly ignores a
    # material/vertex alpha value for compositing, so treating framebuffer alpha as geometry would
    # turn normalized COLOR_0 alpha into a false silhouette mismatch even though every visible RGB
    # sample is present. A non-clear RGBA mask still catches missing/extra fragments, including a
    # correctly-rendered opaque black surface whose RGB happens to equal the clear RGB.
    clear = (0, 0, 0, 0)
    ref_mask = {index for index, pixel in enumerate(reference_pixels) if pixel != clear}
    cna_mask = {index for index, pixel in enumerate(cna_pixels) if pixel != clear}
    union = ref_mask | cna_mask
    intersection = ref_mask & cna_mask
    if not union:
        raise RuntimeError("both captures have an empty foreground mask")
    rgb_error = [
        abs(reference_pixels[index][channel] - cna_pixels[index][channel])
        for index in intersection for channel in range(3)
    ]
    alpha_error = [
        abs(reference_pixels[index][3] - cna_pixels[index][3])
        for index in intersection
    ]
    return {
        "referenceForegroundPixels": len(ref_mask),
        "cnaForegroundPixels": len(cna_mask),
        "foregroundCoverageRatio": len(cna_mask) / len(ref_mask),
        "nonClearMaskIntersectionOverUnion": len(intersection) / len(union),
        "nonClearMaskDifferentPixels": len(ref_mask ^ cna_mask),
        "intersectionRgbMeanAbsoluteError": sum(rgb_error) / len(rgb_error),
        "intersectionRgbMaxAbsoluteError": max(rgb_error),
        "intersectionAlphaMeanAbsoluteError": sum(alpha_error) / len(alpha_error),
        "intersectionAlphaMaxAbsoluteError": max(alpha_error),
    }


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", type=Path, required=True,
                        help="built detached glTF-Sample-Renderer checkout")
    parser.add_argument("--viewer", type=Path, required=True,
                        help="built cna_gltf_viewer executable")
    parser.add_argument("--viewer-source", type=Path, required=True,
                        help="cna-gltf-viewer source checkout used to build --viewer")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    corpus = repo / "tests/assets/gltf"
    renderer = args.renderer.resolve()
    viewer = args.viewer.resolve()
    viewer_source = args.viewer_source.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise RuntimeError(f"output directory must be empty: {output}")

    commit = subprocess.check_output(
        ["git", "-C", str(renderer), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != PINNED_RENDERER_COMMIT:
        raise RuntimeError(f"renderer commit is {commit}, expected {PINNED_RENDERER_COMMIT}")
    if not (renderer / "dist/gltf-viewer.module.js").is_file():
        raise RuntimeError("renderer is not built (dist/gltf-viewer.module.js is missing)")
    if not (renderer / "node_modules/playwright/index.mjs").is_file():
        raise RuntimeError("renderer dependencies are missing; run npm ci")
    if not viewer.is_file():
        raise RuntimeError(f"viewer executable is missing: {viewer}")
    if not (viewer_source / ".git").exists():
        raise RuntimeError(f"viewer source is not a git checkout: {viewer_source}")

    cna_commit = subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
    ).strip()
    viewer_commit = subprocess.check_output(
        ["git", "-C", str(viewer_source), "rev-parse", "HEAD"], text=True
    ).strip()

    with tempfile.TemporaryDirectory(prefix="cna-gltf-reference-server-") as server_temp:
        server_root = Path(server_temp)
        for name in ("gltf-viewer.module.js", "gltf-viewer.module.js.map"):
            os.symlink(renderer / "dist" / name, server_root / name)
        os.symlink(renderer / "dist/libs", server_root / "libs")
        os.symlink(corpus, server_root / "corpus")
        os.symlink(repo / "third_party/draco/javascript", server_root / "draco")
        shutil.copy2(repo / "tools/gltf_reference/reference_harness.html",
                     server_root / "reference_harness.html")

        handler = functools.partial(QuietHandler, directory=server_root)
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base_url = f"http://127.0.0.1:{server.server_port}"
        results = []
        try:
            for asset_file in ASSETS:
                asset_id = Path(asset_file).stem
                expected_path = corpus / f"{asset_id}.expected.json"
                asset_path = corpus / asset_file
                camera = camera_for_asset(expected_path)
                camera_json = json.dumps(camera, separators=(",", ":"))
                cna_png = output / f"{asset_id}.cna.png"
                reference_png = output / f"{asset_id}.reference.png"
                raw_metadata = output / f"{asset_id}.reference.raw.json"

                subprocess.run([
                    "xvfb-run", "-a", str(viewer), str(asset_path), "--direct",
                    "--capture", str(cna_png), "--reference-capture"
                ], check=True, stdout=subprocess.DEVNULL)
                subprocess.run([
                    "node", str(repo / "tools/gltf_reference/capture.mjs"), str(renderer),
                    base_url, asset_file, camera_json, str(reference_png), str(raw_metadata)
                ], check=True)

                metadata = json.loads(raw_metadata.read_text(encoding="utf-8"))
                raw_metadata.unlink()
                metrics = mask_metrics(reference_png, cna_png)
                if metrics["nonClearMaskIntersectionOverUnion"] < 0.99:
                    raise RuntimeError(
                        f"{asset_id}: non-clear-mask IoU "
                        f"{metrics['nonClearMaskIntersectionOverUnion']:.6f} < 0.99"
                    )
                if not 0.99 <= metrics["foregroundCoverageRatio"] <= 1.01:
                    raise RuntimeError(
                        f"{asset_id}: foreground coverage ratio "
                        f"{metrics['foregroundCoverageRatio']:.6f} outside [0.99, 1.01]"
                    )
                # The two renderers intentionally use different fixed light rigs and only the
                # Khronos side tone-maps, so byte equality would be a false requirement. Still
                # reject a gross colour failure: the real EasyGL unlit-NaN bug measured 189.33
                # here, while the largest healthy result in this matrix is 67.60.
                if metrics["intersectionRgbMeanAbsoluteError"] > 80.0:
                    raise RuntimeError(
                        f"{asset_id}: foreground RGB MAE "
                        f"{metrics['intersectionRgbMeanAbsoluteError']:.2f} > 80.00"
                    )

                result = {
                    "asset": asset_file,
                    "assetSha256": sha256(asset_path),
                    "camera": metadata.pop("camera"),
                    "referencePngSha256": sha256(reference_png),
                    "cnaPngSha256": sha256(cna_png),
                    "metrics": metrics,
                    "environment": metadata,
                }
                (output / f"{asset_id}.json").write_text(
                    json.dumps(result, indent=2) + "\n", encoding="utf-8"
                )
                results.append(result)
                print(
                    f"{asset_id}: IoU={metrics['nonClearMaskIntersectionOverUnion']:.6f}, "
                    f"coverage={metrics['foregroundCoverageRatio']:.6f}, "
                    f"RGB-MAE={metrics['intersectionRgbMeanAbsoluteError']:.2f}"
                )
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    aggregate = {
        "task": "GLTF-411",
        "capturedOn": datetime.date.today().isoformat(),
        "rendererRepository": "KhronosGroup/glTF-Sample-Renderer",
        "rendererCommit": commit,
        "cnaCapture": {
            "cnaRepository": "openeggbert/cna",
            "cnaCommit": cna_commit,
            "viewerRepository": "openeggbert/cna-gltf-viewer",
            "viewerCommit": viewer_commit,
            "viewerExecutableSha256": sha256(viewer),
            "viewerArguments": ["--direct", "--reference-capture"],
            "graphicsRenderer": "OPENGLES3 (EasyGL)",
        },
        "comparison": {
            "resolution": [512, 512],
            "clearPixel": [0, 0, 0, 0],
            "minimumNonClearMaskIntersectionOverUnion": 0.99,
            "foregroundCoverageRatioRange": [0.99, 1.01],
            "maximumIntersectionRgbMeanAbsoluteError": 80.0,
            "alphaMetricsAreDiagnosticOnly": True,
            "rgbThresholdRationale": (
                "independent fixed lighting/tone-map pipelines; 80 bounds the measured healthy "
                "maximum 67.60 and rejects the observed unlit-NaN failure at 189.33"
            ),
        },
        "assetCount": len(results),
        "assets": results,
    }
    (output / "comparison.json").write_text(
        json.dumps(aggregate, indent=2) + "\n", encoding="utf-8"
    )
    print(f"PASS: {len(results)} assets match; report: {output / 'comparison.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
