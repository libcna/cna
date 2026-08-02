#!/usr/bin/env python3
"""Keep the accepted Skia raster release scope synchronized with its plan and capability code."""

from __future__ import annotations

import pathlib
import re
import sys
from collections import Counter


BASELINE_LAST_TASK = 114


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
    diagnostic_path = root / "include/CNA/Internal/Backends/Skia/SkiaStartupDiagnostic.hpp"
    parity_path = root / "docs/skia-easygl-parity-ledger.md"
    feature_matrix_path = root / "docs/graphics-backend-feature-matrix.md"
    generated_blender_path = root / "docs/skia-generated-blender.md"

    try:
        plan = plan_path.read_text()
        release = release_path.read_text()
        surface_adr = surface_adr_path.read_text()
        capability_header = capability_path.read_text()
        backend = backend_path.read_text()
        tests = tests_path.read_text()
        diagnostic = diagnostic_path.read_text()
        parity = parity_path.read_text()
        feature_matrix = feature_matrix_path.read_text()
        generated_blender = generated_blender_path.read_text()
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
    highest_task = max(max(task_counts, default=0), BASELINE_LAST_TASK)
    expected_tasks = set(range(1, highest_task + 1))
    actual_tasks = set(task_counts)
    for task in sorted(expected_tasks - actual_tasks):
        errors.append(f"{plan_path}: missing SKIA-{task} row")
    for task in sorted(actual_tasks - expected_tasks):
        errors.append(f"{plan_path}: unexpected SKIA-{task} row")
    for task, count in sorted(task_counts.items()):
        if count != 1:
            errors.append(f"{plan_path}: SKIA-{task} appears {count} times")
    allowed_statuses = {"✅", "⬜", "🟨"}
    successor_statuses: list[str] = []
    successor_status_by_task: dict[int, str] = {}
    for task_text, status_text in task_rows:
        task = int(task_text)
        status = status_text.strip()
        if status not in allowed_statuses:
            errors.append(f"{plan_path}: SKIA-{task} has unknown status {status!r}")
        if task <= BASELINE_LAST_TASK and status != "✅":
            errors.append(f"{plan_path}: baseline SKIA-{task} is not complete ({status!r})")
        if task > BASELINE_LAST_TASK:
            successor_statuses.append(status)
            successor_status_by_task[task] = status
    if "Status: COMPLETE — verified CPU-raster 2D backend." not in plan:
        errors.append(f"{plan_path}: baseline COMPLETE status banner is missing")

    if successor_statuses:
        expansion_match = re.search(
            r"Expansion status: (ACTIVE|COMPLETE) — SKIA-115 through SKIA-(\d+)", plan
        )
        if not expansion_match:
            errors.append(f"{plan_path}: successor expansion status banner is missing")
        else:
            expansion_status, expansion_last_text = expansion_match.groups()
            expansion_last = int(expansion_last_text)
            if expansion_last != highest_task:
                errors.append(
                    f"{plan_path}: expansion banner ends at SKIA-{expansion_last}, "
                    f"but the highest row is SKIA-{highest_task}"
                )
            if expansion_status == "COMPLETE" and any(
                status != "✅" for status in successor_statuses
            ):
                errors.append(
                    f"{plan_path}: expansion banner says COMPLETE while successor rows remain open"
                )

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

    if successor_status_by_task.get(124) == "✅":
        promoted_blend_markers = (
            (release_path, release, "SKIA-124 arbitrary-raster-blend promotion: PASS"),
            (diagnostic_path, diagnostic, "blend=all-valid-13-factor/5-function-tuples"),
            (parity_path, parity, "All 714,025 valid factor/function tuples"),
            (feature_matrix_path, feature_matrix, "All 714,025 valid selector tuples"),
            (generated_blender_path, generated_blender, "Status: promoted arbitrary raster blend surface"),
        )
        for path, contents, marker in promoted_blend_markers:
            if marker not in contents:
                errors.append(
                    f"{path}: completed SKIA-124 requires promotion marker {marker!r}"
                )

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
        f"Skia release gate passed: {BASELINE_LAST_TASK}/{BASELINE_LAST_TASK} baseline tasks, "
        f"{sum(status == '✅' for status in successor_statuses)}/{len(successor_statuses)} "
        f"successor tasks, {len(release_rows)}/{len(capability_names)} capabilities, "
        f"raster baseline"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
