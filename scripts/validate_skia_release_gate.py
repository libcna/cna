#!/usr/bin/env python3
"""Keep the accepted Skia raster release scope synchronized with its plan and capability code."""

from __future__ import annotations

import pathlib
import re
import sys
from collections import Counter


LAST_TASK = 114


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_skia_release_gate.py <repository-root>", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1]).resolve()
    plan_path = root / "plan_skia.md"
    release_path = root / "docs/skia-release-gate.md"
    surface_adr_path = root / "docs/skia-surface-mode-adr.md"
    capability_path = root / "include/CNA/GraphicsCapability.hpp"
    backend_path = root / "src/CNA/Internal/Backends/Skia/SkiaGraphicsBackend.cpp"
    tests_path = root / "cmake/Tests/SkiaTests.cmake"

    try:
        plan = plan_path.read_text()
        release = release_path.read_text()
        surface_adr = surface_adr_path.read_text()
        capability_header = capability_path.read_text()
        backend = backend_path.read_text()
        tests = tests_path.read_text()
    except OSError as error:
        print(f"Skia release gate failed to read input: {error}", file=sys.stderr)
        return 1

    errors: list[str] = []

    task_rows = re.findall(
        r"^\| SKIA-(\d+) \|.*?\|\s*(✅|⬜|🟨|[^|]+?)\s*\|[^|]*\|$",
        plan,
        flags=re.MULTILINE,
    )
    task_counts = Counter(int(task) for task, _ in task_rows)
    expected_tasks = set(range(1, LAST_TASK + 1))
    actual_tasks = set(task_counts)
    for task in sorted(expected_tasks - actual_tasks):
        errors.append(f"{plan_path}: missing SKIA-{task} row")
    for task in sorted(actual_tasks - expected_tasks):
        errors.append(f"{plan_path}: unexpected SKIA-{task} row")
    for task, count in sorted(task_counts.items()):
        if count != 1:
            errors.append(f"{plan_path}: SKIA-{task} appears {count} times")
    for task, status in task_rows:
        if status.strip() != "✅":
            errors.append(f"{plan_path}: SKIA-{task} is not complete ({status.strip()!r})")
    if "Status: COMPLETE — verified CPU-raster 2D backend." not in plan:
        errors.append(f"{plan_path}: final COMPLETE status banner is missing")

    capability_match = re.search(
        r"enum class GraphicsCapability\s*\{(?P<body>.*?)\n\s*\};",
        capability_header,
        flags=re.DOTALL,
    )
    if not capability_match:
        errors.append(f"{capability_path}: GraphicsCapability enum was not found")
        capability_names: set[str] = set()
    else:
        capability_names = set(
            re.findall(r"^\s{8}([A-Z][A-Za-z0-9]+),?\s*$", capability_match.group("body"), re.MULTILINE)
        )

    release_rows = re.findall(
        r"^\| `GraphicsCapability::([A-Za-z0-9]+)` \| `(true|false)` \| (.+) \|$",
        release,
        flags=re.MULTILINE,
    )
    release_counts = Counter(name for name, _, _ in release_rows)
    release_names = set(release_counts)
    for name in sorted(capability_names - release_names):
        errors.append(f"{release_path}: missing capability row {name}")
    for name in sorted(release_names - capability_names):
        errors.append(f"{release_path}: stale capability row {name}")
    for name, count in sorted(release_counts.items()):
        if count != 1:
            errors.append(f"{release_path}: capability {name} appears {count} times")
    true_capabilities = {name for name, value, _ in release_rows if value == "true"}
    if true_capabilities != {"Texture3D"}:
        errors.append(
            f"{release_path}: expected only Texture3D=true, found {sorted(true_capabilities)}"
        )

    implementation = re.search(
        r"bool SkiaGraphicsBackend::SupportsCapability\([^)]*\) const\s*\{(?P<body>.*?)\n\s*\}",
        backend,
        flags=re.DOTALL,
    )
    if not implementation:
        errors.append(f"{backend_path}: SupportsCapability implementation was not found")
    else:
        returned = set(
            re.findall(r"capability\s*==\s*CNA::GraphicsCapability::([A-Za-z0-9]+)",
                       implementation.group("body"))
        )
        if returned != true_capabilities:
            errors.append(
                f"{backend_path}: true capabilities {sorted(returned)} do not match release table "
                f"{sorted(true_capabilities)}"
            )

    if "Status: passed" not in release:
        errors.append(f"{release_path}: passed status is missing")
    for marker in (
        "Engineering sign-off: PASS",
        "Direct / bounded emulation / refusal coverage",
        "Known release boundaries",
        "Validation performed",
    ):
        if marker not in release:
            errors.append(f"{release_path}: required marker {marker!r} is missing")
    if "Status: accepted for the CPU-raster release" not in surface_adr:
        errors.append(f"{surface_adr_path}: accepted raster decision is missing")

    accelerated_registrations = len(
        re.findall(r"^\s*cna_register_skia_accelerated_test\s*\(", tests, flags=re.MULTILINE)
    )
    if accelerated_registrations != 0:
        errors.append(
            f"{tests_path}: expected no registered accelerated test, found "
            f"{accelerated_registrations}"
        )

    if errors:
        print("Skia release gate failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"Skia release gate passed: {len(task_rows)}/{LAST_TASK} tasks, "
        f"{len(release_rows)}/{len(capability_names)} capabilities, raster-only mode"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
