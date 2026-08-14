#!/usr/bin/env python3
"""Execute plan_gltf.md Gate C's exact 14-row viewer retake matrix.

The script is deliberately opt-in: it requires an already-built production OPENGLES3 viewer, the
pinned Khronos reference renderer, and a sparse checkout containing Fox, DamagedHelmet and Sponza
from CNA's pinned glTF-Sample-Assets revision.  It fetches nothing and commits no third-party byte.

Run below an X server (normally ``xvfb-run -a``). Every CNA case is captured in two independent
processes, compared byte-for-byte, then rendered by the independent Khronos implementation with
the exact camera reported by the CNA viewer. Row 12 intentionally has two cases because the spec
has separate sparse attribute and sparse index accessors; the result is still fourteen gate rows.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime
import functools
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading
import time
from typing import Any
from urllib.parse import unquote

from PIL import Image


PINNED_RENDERER_COMMIT = "863b981fb755359063e370ff7b6e956bda0716e2"
PINNED_SAMPLE_ASSETS_COMMIT = "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf"
RESOLUTION = (512, 512)
CLEAR = (0, 0, 0, 0)
MINIMUM_MASK_IOU = 0.99
MINIMUM_COVERAGE_RATIO = 0.99
MAXIMUM_COVERAGE_RATIO = 1.01
MAXIMUM_RGB_MAE = 100.0


@dataclasses.dataclass(frozen=True)
class RetakeCase:
    row: int
    case_id: str
    row_name: str
    proof: str
    source_kind: str
    relative_path: str
    viewer_arguments: tuple[str, ...] = ()
    animation_name: str | None = None
    animation_time: float | None = None
    contrast_arguments: tuple[str, ...] | None = None
    golden_id: str | None = None


CASES = (
    RetakeCase(1, "static-untextured", "Static untextured mesh", "Phases 2-5",
               "corpus", "xf-identity.gltf", golden_id="xf-identity"),
    RetakeCase(2, "textured", "Textured mesh", "Phases 9-10",
               "corpus", "tex-reference-checkerboard.gltf",
               golden_id="tex-reference-checkerboard"),
    RetakeCase(3, "full-pbr", "Full PBR model (all maps + factors)", "Phase 11",
               "derived", "DamagedHelmet/DamagedHelmetFactors.gltf"),
    RetakeCase(4, "hierarchical", "Hierarchical multi-part model",
               "Phase 5 center-collapse retake", "corpus", "xf-parent-child.gltf",
               golden_id="xf-parent-child"),
    RetakeCase(5, "skinned-bind", "Skinned model in bind pose", "Phase 12",
               "corpus", "skin-armature-ancestor.gltf",
               golden_id="skin-armature-ancestor"),
    RetakeCase(6, "skinned-animated", "Animated skinned model", "Phases 12 + 14",
               "sample", "Fox/glTF/Fox.gltf",
               viewer_arguments=("--clip", "Run", "--animation-time", "0.5"),
               animation_name="Run", animation_time=0.5, contrast_arguments=()),
    RetakeCase(7, "rigid-animated", "Rigid node animation", "GLTF-293",
               "corpus", "anim-rigid-node.gltf",
               viewer_arguments=("--clip", "Spin", "--animation-time", "0.5"),
               animation_name="Spin", animation_time=0.5, contrast_arguments=()),
    RetakeCase(8, "morph-target", "Morph-target model", "Phase 13",
               "corpus", "morph-position-normal-tangent.gltf",
               golden_id="morph-position-normal-tangent"),
    RetakeCase(9, "glb", ".glb container", "Phase 1", "corpus", "glb-basic.glb",
               golden_id="glb-basic"),
    RetakeCase(10, "external-resources",
               ".gltf with external .bin and external images", "Phase 1",
               "sample", "DamagedHelmet/glTF/DamagedHelmet.gltf"),
    RetakeCase(11, "interleaved", "Interleaved-buffer model", "Phase 2",
               "corpus", "interleaved-position-normal.gltf",
               golden_id="interleaved-position-normal"),
    RetakeCase(12, "sparse-attribute", "Sparse accessor: attribute",
               "Phase 2 + GLTF-063", "corpus", "sparse-position.gltf",
               golden_id="sparse-position"),
    RetakeCase(12, "sparse-index", "Sparse accessor: index",
               "Phase 2 + GLTF-063", "corpus", "sparse-indices.gltf",
               golden_id="sparse-indices"),
    RetakeCase(13, "draco", "Draco-compressed model", "Phase 17",
               "corpus", "draco-triangle.gltf", golden_id="draco-triangle"),
    RetakeCase(14, "large-real-world", "Large real-world model (>= 50 MiB)", "Phase 22",
               "sample", "Sponza/glTF/Sponza.gltf"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def require_revision(path: Path, expected: str, description: str) -> None:
    actual = git_revision(path)
    if actual != expected:
        raise RuntimeError(f"{description} commit is {actual}, expected {expected}")


def non_clear_pixels(path: Path) -> int:
    image = Image.open(path).convert("RGBA")
    if image.size != RESOLUTION:
        raise RuntimeError(f"{path.name}: capture is {image.size}, expected {RESOLUTION}")
    return sum(pixel != CLEAR for pixel in image.getdata())


def mask_metrics(reference_path: Path, cna_path: Path) -> dict[str, float | int]:
    reference = Image.open(reference_path).convert("RGBA")
    cna = Image.open(cna_path).convert("RGBA")
    if reference.size != RESOLUTION or cna.size != RESOLUTION:
        raise RuntimeError(
            f"capture sizes differ: reference={reference.size}, CNA={cna.size}"
        )
    reference_pixels = list(reference.getdata())
    cna_pixels = list(cna.getdata())
    reference_mask = {i for i, pixel in enumerate(reference_pixels) if pixel != CLEAR}
    cna_mask = {i for i, pixel in enumerate(cna_pixels) if pixel != CLEAR}
    union = reference_mask | cna_mask
    intersection = reference_mask & cna_mask
    if not reference_mask or not cna_mask or not union or not intersection:
        raise RuntimeError("reference or CNA capture has no comparable foreground")
    rgb_errors = [
        abs(reference_pixels[index][channel] - cna_pixels[index][channel])
        for index in intersection for channel in range(3)
    ]
    return {
        "referenceForegroundPixels": len(reference_mask),
        "cnaForegroundPixels": len(cna_mask),
        "foregroundCoverageRatio": len(cna_mask) / len(reference_mask),
        "nonClearMaskIntersectionOverUnion": len(intersection) / len(union),
        "nonClearMaskDifferentPixels": len(reference_mask ^ cna_mask),
        "intersectionRgbMeanAbsoluteError": sum(rgb_errors) / len(rgb_errors),
        "intersectionRgbMaxAbsoluteError": max(rgb_errors),
    }


def terminal_error(output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in reversed(lines):
        if line.startswith("cna-gltf-viewer:"):
            return line
    return lines[-1] if lines else ""


def run_viewer(viewer: Path, asset: Path, png: Path,
               arguments: tuple[str, ...], timeout: int) -> dict[str, Any]:
    environment = dict(os.environ)
    environment.update({
        "SDL_VIDEODRIVER": "x11",
        "SDL_AUDIODRIVER": "dummy",
        "LIBGL_ALWAYS_SOFTWARE": "1",
    })
    command = [
        "/usr/bin/time", "-f", "CNA_TIME elapsed=%e maxRssKiB=%M",
        str(viewer), str(asset), "--direct", "--capture", str(png),
        "--reference-capture", *arguments,
    ]
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            timeout=timeout, env=environment,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise RuntimeError(f"{asset.name}: viewer timed out: {terminal_error(output)}") from error
    elapsed = time.monotonic() - started
    if completed.returncode != 0 or not png.is_file():
        raise RuntimeError(
            f"{asset.name}: viewer capture failed with {completed.returncode}: "
            f"{terminal_error(completed.stdout)}"
        )
    camera_lines = [
        line.removeprefix("CNA_REFERENCE_CAMERA=")
        for line in completed.stdout.splitlines()
        if line.startswith("CNA_REFERENCE_CAMERA=")
    ]
    if len(camera_lines) != 1:
        raise RuntimeError(f"{asset.name}: expected exactly one CNA_REFERENCE_CAMERA line")
    timing = re.search(r"^CNA_TIME elapsed=([0-9.]+) maxRssKiB=([0-9]+)$",
                       completed.stdout, re.MULTILINE)
    renderers = [
        line.removeprefix("EasyGLRenderer initialized with ")
        for line in completed.stdout.splitlines()
        if line.startswith("EasyGLRenderer initialized with ")
    ]
    return {
        "camera": json.loads(camera_lines[0]),
        "wallSeconds": elapsed,
        "gnuTimeWallSeconds": float(timing.group(1)) if timing else None,
        "maximumRssKiB": int(timing.group(2)) if timing else None,
        "graphicsRenderer": renderers[-1] if renderers else None,
        "pngSha256": sha256(png),
        "nonClearPixelCount": non_clear_pixels(png),
    }


def resource_footprint(asset: Path) -> dict[str, Any]:
    if asset.suffix == ".glb":
        return {"bytes": asset.stat().st_size, "files": [asset.name]}
    document = json.loads(asset.read_text(encoding="utf-8"))
    relative_uris: list[str] = []
    for collection in ("buffers", "images"):
        for entry in document.get(collection, []):
            uri = entry.get("uri")
            if uri and not uri.startswith("data:"):
                relative_uris.append(unquote(uri))
    files = [asset]
    root = asset.parent.resolve()
    for uri in relative_uris:
        path = (asset.parent / uri).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise RuntimeError(f"{asset.name}: unsafe or missing external resource {uri!r}")
        files.append(path)
    return {
        "bytes": sum(path.stat().st_size for path in files),
        "files": [str(path.relative_to(asset.parent)) for path in files],
    }


def triangle_count(asset: Path) -> int | None:
    if asset.suffix != ".gltf":
        return None
    document = json.loads(asset.read_text(encoding="utf-8"))
    total = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                continue
            accessor_index = primitive.get("indices")
            if accessor_index is None:
                position = primitive.get("attributes", {}).get("POSITION")
                if position is None:
                    continue
                count = document["accessors"][position]["count"]
            else:
                count = document["accessors"][accessor_index]["count"]
            total += count // 3
    return total


def create_factor_authored_helmet(samples: Path, destination: Path) -> Path:
    source_root = samples / "Models/DamagedHelmet/glTF"
    source = source_root / "DamagedHelmet.gltf"
    document = json.loads(source.read_text(encoding="utf-8"))
    if len(document.get("materials", [])) != 1:
        raise RuntimeError("pinned DamagedHelmet no longer has exactly one material")
    material = document["materials"][0]
    pbr = material["pbrMetallicRoughness"]
    required = (
        pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture"),
        material.get("normalTexture"), material.get("occlusionTexture"),
        material.get("emissiveTexture"),
    )
    if any(value is None for value in required):
        raise RuntimeError("pinned DamagedHelmet no longer carries all five core texture roles")
    pbr.update({
        "baseColorFactor": [0.8, 0.9, 1.0, 1.0],
        "metallicFactor": 0.85,
        "roughnessFactor": 0.4,
    })
    material["normalTexture"]["scale"] = 0.65
    material["occlusionTexture"]["strength"] = 0.75
    material["emissiveFactor"] = [0.2, 0.3, 0.4]

    destination.mkdir(parents=True)
    output = destination / "DamagedHelmetFactors.gltf"
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    for entry in (*document.get("buffers", []), *document.get("images", [])):
        uri = entry.get("uri")
        if uri and not uri.startswith("data:"):
            name = unquote(uri)
            shutil.copy2(source_root / name, destination / name)
    return output


def validate_case_semantics(case: RetakeCase, asset: Path,
                            footprint: dict[str, Any]) -> dict[str, Any]:
    if asset.suffix != ".gltf":
        return {}
    document = json.loads(asset.read_text(encoding="utf-8"))
    evidence: dict[str, Any] = {}
    if case.row == 3:
        material = document["materials"][0]
        pbr = material["pbrMetallicRoughness"]
        evidence["authoredMapRoles"] = [
            "baseColor", "metallicRoughness", "normal", "occlusion", "emissive"
        ]
        evidence["authoredFactors"] = {
            "baseColor": pbr["baseColorFactor"],
            "metallic": pbr["metallicFactor"],
            "roughness": pbr["roughnessFactor"],
            "normalScale": material["normalTexture"]["scale"],
            "occlusionStrength": material["occlusionTexture"]["strength"],
            "emissive": material["emissiveFactor"],
        }
    elif case.row == 8:
        weights = [weight for mesh in document["meshes"] for weight in mesh.get("weights", [])]
        if not weights or not any(weight != 0 for weight in weights):
            raise RuntimeError("morph retake asset has no non-zero authored morph weight")
        evidence["authoredMorphWeights"] = weights
    elif case.row == 10:
        buffers = [entry["uri"] for entry in document.get("buffers", [])
                   if entry.get("uri") and not entry["uri"].startswith("data:")]
        images = [entry["uri"] for entry in document.get("images", [])
                  if entry.get("uri") and not entry["uri"].startswith("data:")]
        if not buffers or not images:
            raise RuntimeError("external-resource retake needs both external buffers and images")
        evidence.update({"externalBuffers": buffers, "externalImages": images})
    elif case.row == 11:
        if not any(view.get("byteStride", 0) > 0 for view in document.get("bufferViews", [])):
            raise RuntimeError("interleaved retake asset has no bufferView.byteStride")
    elif case.row == 12:
        primitive = document["meshes"][0]["primitives"][0]
        if case.case_id == "sparse-attribute":
            indices = primitive["attributes"].values()
        else:
            indices = (primitive["indices"],)
        if not any("sparse" in document["accessors"][index] for index in indices):
            raise RuntimeError(f"{case.case_id}: selected accessor is not sparse")
    elif case.row == 13:
        extensions = document.get("extensionsRequired", [])
        if "KHR_draco_mesh_compression" not in extensions:
            raise RuntimeError("Draco retake asset does not require KHR_draco_mesh_compression")
    elif case.row == 14:
        if footprint["bytes"] < 50 * 1024 * 1024:
            raise RuntimeError(
                f"large retake footprint is {footprint['bytes']} bytes, below 50 MiB"
            )
        evidence["triangleCount"] = triangle_count(asset)
    return evidence


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True,
                        help="built detached glTF-Sample-Renderer checkout")
    parser.add_argument("--sample-assets", type=Path, required=True,
                        help="pinned sparse glTF-Sample-Assets checkout")
    parser.add_argument("--viewer", type=Path, required=True,
                        help="built OPENGLES3 cna_gltf_viewer executable")
    parser.add_argument("--viewer-source", type=Path, required=True,
                        help="cna-gltf-viewer source checkout used for --viewer")
    parser.add_argument("--output", type=Path, required=True,
                        help="empty directory for disposable PNGs and sidecars")
    parser.add_argument("--report-out", type=Path,
                        help="optional aggregate JSON report path")
    parser.add_argument("--timeout", type=int, default=300,
                        help="per viewer/reference process timeout (default: 300 seconds)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    corpus = repo / "tests/assets/gltf"
    renderer = args.renderer.resolve()
    samples = args.sample_assets.resolve()
    viewer = args.viewer.resolve()
    viewer_source = args.viewer_source.resolve()
    output = args.output.resolve()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not os.environ.get("DISPLAY"):
        raise RuntimeError("DISPLAY is unset; run this harness below xvfb-run -a")
    if not viewer.is_file() or not os.access(viewer, os.X_OK):
        raise RuntimeError(f"viewer is not executable: {viewer}")
    if not Path("/usr/bin/time").is_file():
        raise RuntimeError("/usr/bin/time is required for retake timing evidence")
    require_revision(renderer, PINNED_RENDERER_COMMIT, "reference renderer")
    require_revision(samples, PINNED_SAMPLE_ASSETS_COMMIT, "sample assets")
    if not (renderer / "dist/gltf-viewer.module.js").is_file():
        raise RuntimeError("reference renderer is not built")
    if not (renderer / "node_modules/playwright/index.mjs").is_file():
        raise RuntimeError("reference renderer dependencies are missing; run npm ci")
    for model in ("Fox", "DamagedHelmet", "Sponza"):
        model_root = samples / "Models" / model
        if not model_root.is_dir() or not (model_root / "README.md").is_file() \
                or not (model_root / "LICENSE.md").is_file():
            raise RuntimeError(f"sample checkout lacks model or licence metadata: {model}")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise RuntimeError(f"output directory must be empty: {output}")

    with tempfile.TemporaryDirectory(prefix="cna-gltf-retake-derived-") as derived_temp, \
         tempfile.TemporaryDirectory(prefix="cna-gltf-retake-server-") as server_temp:
        derived = Path(derived_temp)
        create_factor_authored_helmet(samples, derived / "DamagedHelmet")
        server_root = Path(server_temp)
        for name in ("gltf-viewer.module.js", "gltf-viewer.module.js.map"):
            os.symlink(renderer / "dist" / name, server_root / name)
        os.symlink(renderer / "dist/libs", server_root / "libs")
        os.symlink(repo / "third_party/draco/javascript", server_root / "draco")
        shutil.copy2(repo / "tools/gltf_reference/reference_harness.html",
                     server_root / "reference_harness.html")
        web_assets = server_root / "corpus"
        web_assets.mkdir()
        os.symlink(corpus, web_assets / "generated")
        os.symlink(samples / "Models", web_assets / "samples")
        os.symlink(derived, web_assets / "derived")

        handler = functools.partial(QuietHandler, directory=server_root)
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base_url = f"http://127.0.0.1:{server.server_port}"
        results: list[dict[str, Any]] = []
        try:
            for index, case in enumerate(CASES, 1):
                if case.source_kind == "corpus":
                    asset = corpus / case.relative_path
                    web_asset = f"generated/{case.relative_path}"
                elif case.source_kind == "sample":
                    asset = samples / "Models" / case.relative_path
                    web_asset = f"samples/{case.relative_path}"
                else:
                    asset = derived / case.relative_path
                    web_asset = f"derived/{case.relative_path}"
                if not asset.is_file():
                    raise RuntimeError(f"retake asset is missing: {asset}")

                first_png = output / f"{case.case_id}.cna-1.png"
                second_png = output / f"{case.case_id}.cna-2.png"
                reference_png = output / f"{case.case_id}.reference.png"
                raw_metadata = output / f"{case.case_id}.reference.raw.json"
                first = run_viewer(
                    viewer, asset, first_png, case.viewer_arguments, args.timeout
                )
                second = run_viewer(
                    viewer, asset, second_png, case.viewer_arguments, args.timeout
                )
                if first_png.read_bytes() != second_png.read_bytes():
                    raise RuntimeError(
                        f"row {case.row} {case.case_id}: CNA captures are not byte-identical"
                    )
                if first["camera"] != second["camera"]:
                    raise RuntimeError(
                        f"row {case.row} {case.case_id}: CNA cameras are not identical"
                    )

                golden: dict[str, Any] | None = None
                if case.golden_id is not None:
                    golden_path = repo / "tests/gltf-l7/easygl" / f"{case.golden_id}.png"
                    if not golden_path.is_file() or first_png.read_bytes() != golden_path.read_bytes():
                        raise RuntimeError(
                            f"row {case.row} {case.case_id}: CNA capture differs from L7 golden"
                        )
                    golden = {
                        "path": str(golden_path.relative_to(repo)),
                        "sha256": sha256(golden_path),
                        "byteIdentical": True,
                    }

                contrast: dict[str, Any] | None = None
                if case.contrast_arguments is not None:
                    contrast_png = output / f"{case.case_id}.contrast.png"
                    contrast_run = run_viewer(
                        viewer, asset, contrast_png, case.contrast_arguments, args.timeout
                    )
                    if first_png.read_bytes() == contrast_png.read_bytes():
                        raise RuntimeError(
                            f"row {case.row} {case.case_id}: fixed animation equals bind/time-zero"
                        )
                    contrast = {
                        "viewerArguments": list(case.contrast_arguments),
                        "pngSha256": contrast_run["pngSha256"],
                        "differentFromRetake": True,
                    }

                camera = dict(first["camera"])
                if case.animation_name is not None:
                    camera["animationName"] = case.animation_name
                    camera["animationTime"] = case.animation_time
                subprocess.run([
                    "node", str(repo / "tools/gltf_reference/capture.mjs"), str(renderer),
                    base_url, web_asset, json.dumps(camera, separators=(",", ":")),
                    str(reference_png), str(raw_metadata),
                ], check=True, timeout=args.timeout)
                reference_metadata = json.loads(raw_metadata.read_text(encoding="utf-8"))
                raw_metadata.unlink()
                metrics = mask_metrics(reference_png, first_png)
                if metrics["nonClearMaskIntersectionOverUnion"] < MINIMUM_MASK_IOU:
                    raise RuntimeError(
                        f"row {case.row} {case.case_id}: mask IoU "
                        f"{metrics['nonClearMaskIntersectionOverUnion']:.6f} < "
                        f"{MINIMUM_MASK_IOU}"
                    )
                coverage = metrics["foregroundCoverageRatio"]
                if not MINIMUM_COVERAGE_RATIO <= coverage <= MAXIMUM_COVERAGE_RATIO:
                    raise RuntimeError(
                        f"row {case.row} {case.case_id}: coverage {coverage:.6f} outside "
                        f"[{MINIMUM_COVERAGE_RATIO}, {MAXIMUM_COVERAGE_RATIO}]"
                    )
                if metrics["intersectionRgbMeanAbsoluteError"] > MAXIMUM_RGB_MAE:
                    raise RuntimeError(
                        f"row {case.row} {case.case_id}: RGB MAE "
                        f"{metrics['intersectionRgbMeanAbsoluteError']:.2f} > {MAXIMUM_RGB_MAE}"
                    )

                footprint = resource_footprint(asset)
                semantic = validate_case_semantics(case, asset, footprint)
                result = {
                    "row": case.row,
                    "case": case.case_id,
                    "rowName": case.row_name,
                    "proof": case.proof,
                    "sourceKind": case.source_kind,
                    "source": case.relative_path,
                    "sourceSha256": sha256(asset),
                    "resourceFootprint": footprint,
                    "semanticEvidence": semantic,
                    "viewerArguments": ["--direct", "--reference-capture",
                                        *case.viewer_arguments],
                    "camera": first["camera"],
                    "twoProcessPngByteIdentical": True,
                    "cnaPngSha256": first["pngSha256"],
                    "cnaNonClearPixelCount": first["nonClearPixelCount"],
                    "cnaRuns": [first, second],
                    "committedGolden": golden,
                    "animationContrast": contrast,
                    "referencePngSha256": sha256(reference_png),
                    "metrics": metrics,
                    "referenceEnvironment": {
                        key: reference_metadata[key]
                        for key in ("browser", "webglVendor", "webglRenderer",
                                    "unmaskedVendor", "unmaskedRenderer", "state")
                    },
                }
                results.append(result)
                print(
                    f"[{index:02d}/{len(CASES)}] row {case.row:02d} {case.case_id}: "
                    f"IoU={metrics['nonClearMaskIntersectionOverUnion']:.6f}, "
                    f"coverage={coverage:.6f}, "
                    f"RGB-MAE={metrics['intersectionRgbMeanAbsoluteError']:.2f}",
                    flush=True,
                )
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    rows = sorted({result["row"] for result in results})
    if rows != list(range(1, 15)) or len(results) != 15:
        raise RuntimeError(f"retake result does not cover exactly rows 1-14: {rows}")
    report = {
        "schemaVersion": 1,
        "task": "GLTF-429",
        "capturedOn": datetime.date.today().isoformat(),
        "result": "pass",
        "gateRows": 14,
        "captureCases": len(results),
        "pins": {
            "referenceRenderer": {
                "repository": "KhronosGroup/glTF-Sample-Renderer",
                "commit": PINNED_RENDERER_COMMIT,
            },
            "sampleAssets": {
                "repository": "KhronosGroup/glTF-Sample-Assets",
                "commit": PINNED_SAMPLE_ASSETS_COMMIT,
                "models": ["Fox", "DamagedHelmet", "Sponza"],
                "redistributed": False,
            },
        },
        "cna": {
            "commit": git_revision(repo),
            "viewerCommit": git_revision(viewer_source),
            "viewerExecutableSha256": sha256(viewer),
        },
        "protocol": {
            "resolution": list(RESOLUTION),
            "clearPixel": list(CLEAR),
            "twoIndependentCnaProcesses": True,
            "requireByteIdenticalCnaPng": True,
            "minimumNonClearMaskIntersectionOverUnion": MINIMUM_MASK_IOU,
            "foregroundCoverageRatioRange": [
                MINIMUM_COVERAGE_RATIO, MAXIMUM_COVERAGE_RATIO
            ],
            "maximumIntersectionRgbMeanAbsoluteError": MAXIMUM_RGB_MAE,
            "fixedLightingDifference": (
                "CNA and Khronos use independent directional-light rigs; the silhouette/coverage "
                "gate is primary and RGB MAE rejects gross material/shader failures."
            ),
            "sparseRowCases": ["sparse-attribute", "sparse-index"],
        },
        "cases": results,
    }
    report_path = (args.report_out.resolve() if args.report_out
                   else output / "retake-report.json")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: all 14 Gate C rows ({len(results)} cases); report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
