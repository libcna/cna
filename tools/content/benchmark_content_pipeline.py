#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Benchmark serial and parallel CNA Content Pipeline graph execution.

The harness uses repository fixtures, times only cna-content subprocesses, alternates worker order
between samples, and verifies that every worker count produces the same complete output tree for a
scenario. It is a developer benchmark, not a CI performance threshold.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Callable


SCHEMA = "CNA.ContentPipeline.Benchmark"
SCHEMA_VERSION = 1


def parse_arguments() -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--content-executable",
        type=Path,
        default=repository / "cmake-build-debug" / "cna-content",
        help="stock cna-content executable",
    )
    parser.add_argument(
        "--custom-content-executable",
        type=Path,
        default=repository / "cmake-build-debug" / "cna_custom_content_compiler_example",
        help="custom compiler executable used for the shared-dependency graph",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=min(4, max(2, os.cpu_count() or 1)),
        help="parallel worker count to compare with workers=1 (2..64)",
    )
    parser.add_argument("--iterations", type=int, default=7, help="samples per scenario/count")
    parser.add_argument(
        "--assets-per-kind",
        type=int,
        default=32,
        help="number of PNG, WAV, glTF, and CNJ assets in the mixed fixture",
    )
    parser.add_argument(
        "--shared-dependents",
        type=int,
        default=96,
        help="number of custom assets depending on one shared build node",
    )
    parser.add_argument("--json-output", type=Path, help="optional JSON result path")
    parser.add_argument(
        "--keep-workspace",
        action="store_true",
        help="retain the generated temporary fixture for inspection",
    )
    arguments = parser.parse_args()
    if not 2 <= arguments.workers <= 64:
        parser.error("--workers must be between 2 and 64")
    if arguments.iterations < 2:
        parser.error("--iterations must be at least 2")
    if arguments.assets_per_kind < 1 or arguments.shared_dependents < 1:
        parser.error("asset counts must be positive")
    return arguments


def require_executable(path: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise RuntimeError(f"content compiler is not executable: {resolved}")
    return resolved


def run_compiler(executable: Path, source: Path, output: Path, workers: int) -> float:
    command = [
        str(executable),
        "build",
        str(source),
        "-o",
        str(output),
        "--workers",
        str(workers),
        "--quiet",
    ]
    started = time.perf_counter()
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        raise RuntimeError(
            f"content compiler exited with {result.returncode}: {' '.join(command)}\n"
            f"{result.stdout}"
        )
    return elapsed


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(8, "little"))
        digest.update(relative)
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
    return digest.hexdigest()


def copy_numbered(source: Path, destination: Path, count: int, suffix: str) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for index in range(count):
        shutil.copyfile(source, destination / f"asset-{index:04d}{suffix}")


def prepare_mixed_fixture(repository: Path, root: Path, count: int) -> tuple[Path, Path, bytes, bytes]:
    source = root / "mixed-source"
    image = repository / "tests/assets/media/thumbnails/large_400x300.png"
    changed_image = repository / "tests/assets/media/pictures/Vacation/Day 2/sunset.png"
    wave = repository / "tests/assets/media/music/Artist Two/Album Gamma/01 - Nocturne.wav"
    model = repository / "tests/assets/gltf/skin-four-weighted.glb"
    cnj = repository / "tests/assets/content_pipeline_cmake/Nested/curve.cnj"
    for fixture in (image, changed_image, wave, model, cnj):
        if not fixture.is_file():
            raise RuntimeError(f"required benchmark fixture is missing: {fixture}")

    copy_numbered(image, source / "Images", count, ".png")
    copy_numbered(wave, source / "Sounds", count, ".wav")
    copy_numbered(model, source / "Models", count, ".glb")
    copy_numbered(cnj, source / "Documents", count, ".cnj")
    change_target = source / "Images" / "asset-0000.png"
    return source, change_target, image.read_bytes(), changed_image.read_bytes()


def prepare_shared_fixture(root: Path, dependents: int) -> tuple[Path, Path, bytes, bytes]:
    source = root / "shared-source"
    shared = source / "Shared" / "root.greeting"
    shared.parent.mkdir(parents=True, exist_ok=True)
    baseline = b"shared baseline\n"
    changed = b"shared changed\n"
    shared.write_bytes(baseline)

    assets: dict[str, object] = {}
    for index in range(dependents):
        relative = f"Parents/parent-{index:04d}.greeting"
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"parent {index}\n", encoding="utf-8")
        assets[relative] = {
            "parameters": {"dependsOn": {"type": "string", "value": "Shared/root"}}
        }
    configuration = {
        "format": "CNA.ContentPipeline.Config",
        "version": 1,
        "assets": assets,
    }
    (source / ".cna-content.json").write_text(
        json.dumps(configuration, separators=(",", ":")), encoding="utf-8"
    )
    return source, shared, baseline, changed


Sample = Callable[[int], tuple[float, str]]


def collect_samples(iterations: int, parallel_workers: int, sample: Sample) -> dict[int, list[float]]:
    samples = {1: [], parallel_workers: []}
    digests = {1: set(), parallel_workers: set()}
    for iteration in range(iterations):
        order = (1, parallel_workers) if iteration % 2 == 0 else (parallel_workers, 1)
        for workers in order:
            elapsed, digest = sample(workers)
            samples[workers].append(elapsed)
            digests[workers].add(digest)
    combined = digests[1] | digests[parallel_workers]
    if len(combined) != 1:
        raise RuntimeError(
            "serial and parallel output trees were not deterministic: "
            f"workers=1 {sorted(digests[1])}, workers={parallel_workers} "
            f"{sorted(digests[parallel_workers])}"
        )
    return samples


def percentile95(values: list[float]) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(0.95 * len(ordered))))
    return ordered[index]


def summarize(name: str, samples: dict[int, list[float]], parallel_workers: int) -> dict[str, object]:
    serial = samples[1]
    parallel = samples[parallel_workers]
    serial_median = statistics.median(serial)
    parallel_median = statistics.median(parallel)
    return {
        "scenario": name,
        "workers": {
            "1": {
                "seconds": serial,
                "medianSeconds": serial_median,
                "p95Seconds": percentile95(serial),
            },
            str(parallel_workers): {
                "seconds": parallel,
                "medianSeconds": parallel_median,
                "p95Seconds": percentile95(parallel),
            },
        },
        "medianSpeedup": serial_median / parallel_median,
    }


def revision(repository: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repository, text=True, capture_output=True, check=True
    )
    return result.stdout.strip()


def main() -> int:
    arguments = parse_arguments()
    repository = Path(__file__).resolve().parents[2]
    content = require_executable(arguments.content_executable)
    custom = require_executable(arguments.custom_content_executable)
    workspace = Path(tempfile.mkdtemp(prefix="cna-content-benchmark-"))
    try:
        mixed, changed_image, image_baseline, image_alternate = prepare_mixed_fixture(
            repository, workspace, arguments.assets_per_kind
        )
        shared, changed_shared, shared_baseline, shared_alternate = prepare_shared_fixture(
            workspace, arguments.shared_dependents
        )
        mixed_output = workspace / "mixed-output"
        shared_output = workspace / "shared-output"

        def cold(workers: int) -> tuple[float, str]:
            shutil.rmtree(mixed_output, ignore_errors=True)
            elapsed = run_compiler(content, mixed, mixed_output, workers)
            return elapsed, tree_digest(mixed_output)

        def no_op(workers: int) -> tuple[float, str]:
            shutil.rmtree(mixed_output, ignore_errors=True)
            run_compiler(content, mixed, mixed_output, workers)
            elapsed = run_compiler(content, mixed, mixed_output, workers)
            return elapsed, tree_digest(mixed_output)

        def one_change(workers: int) -> tuple[float, str]:
            changed_image.write_bytes(image_baseline)
            shutil.rmtree(mixed_output, ignore_errors=True)
            run_compiler(content, mixed, mixed_output, workers)
            changed_image.write_bytes(image_alternate)
            elapsed = run_compiler(content, mixed, mixed_output, workers)
            changed_image.write_bytes(image_baseline)
            return elapsed, tree_digest(mixed_output)

        def shared_change(workers: int) -> tuple[float, str]:
            changed_shared.write_bytes(shared_baseline)
            shutil.rmtree(shared_output, ignore_errors=True)
            run_compiler(custom, shared, shared_output, workers)
            changed_shared.write_bytes(shared_alternate)
            elapsed = run_compiler(custom, shared, shared_output, workers)
            changed_shared.write_bytes(shared_baseline)
            return elapsed, tree_digest(shared_output)

        scenarios = []
        for name, sample in (
            ("mixed-cold", cold),
            ("mixed-no-op", no_op),
            ("mixed-one-image-change", one_change),
            ("shared-dependency-change", shared_change),
        ):
            scenarios.append(
                summarize(
                    name,
                    collect_samples(arguments.iterations, arguments.workers, sample),
                    arguments.workers,
                )
            )

        result = {
            "format": SCHEMA,
            "version": SCHEMA_VERSION,
            "revision": revision(repository),
            "platform": platform.platform(),
            "processor": platform.processor(),
            "logicalCpuCount": os.cpu_count(),
            "parallelWorkers": arguments.workers,
            "iterations": arguments.iterations,
            "mixedAssetCounts": {
                "Texture2D": arguments.assets_per_kind,
                "SoundEffect": arguments.assets_per_kind,
                "Model": arguments.assets_per_kind,
                "CurveCnj": arguments.assets_per_kind,
            },
            "sharedDependencyParents": arguments.shared_dependents,
            "scenarios": scenarios,
        }
        encoded = json.dumps(result, indent=2) + "\n"
        if arguments.json_output:
            arguments.json_output.parent.mkdir(parents=True, exist_ok=True)
            arguments.json_output.write_text(encoded, encoding="utf-8")

        print(f"CNA Content Pipeline benchmark at {result['revision']}")
        print(f"workspace: {workspace}")
        print(f"workers: 1 vs {arguments.workers}; iterations: {arguments.iterations}")
        print("scenario                         serial median   parallel median   speedup")
        for scenario in scenarios:
            workers = scenario["workers"]
            print(
                f"{scenario['scenario']:<32}"
                f"{workers['1']['medianSeconds']:>10.6f} s"
                f"{workers[str(arguments.workers)]['medianSeconds']:>13.6f} s"
                f"{scenario['medianSpeedup']:>10.3f}x"
            )
        return 0
    finally:
        if arguments.keep_workspace:
            print(f"retained benchmark workspace: {workspace}")
        else:
            shutil.rmtree(workspace, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
