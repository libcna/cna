#!/usr/bin/env python3
"""Validate the SKIA-2 EasyGL test/golden/oracle classification matrix."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import Counter


ALLOWED_KINDS = {"ctest", "tool", "golden", "oracle"}
ALLOWED_CATEGORIES = {"2d-direct", "2d-emulation", "3d", "device-dependent"}


def expected_entries(root: pathlib.Path) -> set[str]:
    cmake = (root / "cmake/Tests/EasyGLTests.cmake").read_text()
    cmake = re.sub(r"#[^\n]*", "", cmake)
    tests = re.findall(
        r"\bcna_register_backend_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)", cmake
    )
    tools = re.findall(
        r"\bcna_easygl_test\s*\(\s*(cna_diag_easygl|cna_oracle_render_easygl)\b", cmake
    )
    expected = {f"ctest:{name}" for name in tests}
    expected.update(f"tool:{name}" for name in tools)
    golden_root = root / "examples/golden"
    expected.update(f"golden:{path.name}" for path in golden_root.glob("*.png"))
    oracle_root = root / "tools/xna-oracle/scenes"
    expected.update(f"oracle:{path.name}" for path in oracle_root.glob("*.scene"))
    if len(tests) != len(set(tests)):
        duplicates = sorted(name for name, count in Counter(tests).items() if count > 1)
        raise ValueError("duplicate EasyGL CTest registrations: " + ", ".join(duplicates))
    return expected


def matrix_entries(matrix: pathlib.Path) -> tuple[list[str], list[str]]:
    entries: list[str] = []
    errors: list[str] = []
    for line_number, line in enumerate(matrix.read_text().splitlines(), start=1):
        if not line.startswith("| `"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4:
            errors.append(f"{matrix}:{line_number}: expected four table cells")
            continue
        entry_match = re.fullmatch(r"`([^`]+)`", cells[0])
        kind_match = re.fullmatch(r"`([^`]+)`", cells[1])
        category_match = re.fullmatch(r"`([^`]+)`", cells[2])
        if not entry_match or not kind_match or not category_match:
            errors.append(f"{matrix}:{line_number}: first three cells must use code spans")
            continue
        entry = entry_match.group(1)
        kind = kind_match.group(1)
        category = category_match.group(1)
        if kind not in ALLOWED_KINDS:
            errors.append(f"{matrix}:{line_number}: invalid kind {kind!r}")
        if category not in ALLOWED_CATEGORIES:
            errors.append(f"{matrix}:{line_number}: invalid category {category!r}")
        if not entry.startswith(kind + ":"):
            errors.append(f"{matrix}:{line_number}: entry prefix does not match kind {kind!r}")
        if not cells[3]:
            errors.append(f"{matrix}:{line_number}: Skia route/evidence is required")
        entries.append(entry)
    return entries, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--dump", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        expected = expected_entries(root)
    except (OSError, ValueError) as error:
        print(f"Skia test-matrix extraction failed: {error}", file=sys.stderr)
        return 2
    if args.dump:
        print("\n".join(sorted(expected)))
        return 0
    matrix = root / "docs/skia-easygl-test-matrix.md"
    try:
        actual, errors = matrix_entries(matrix)
    except OSError as error:
        print(f"Skia test-matrix read failed: {error}", file=sys.stderr)
        return 2
    duplicates = sorted(entry for entry, count in Counter(actual).items() if count > 1)
    missing = sorted(expected - set(actual))
    extra = sorted(set(actual) - expected)
    for error in errors:
        print(error, file=sys.stderr)
    if duplicates:
        print("duplicate matrix entries:\n  " + "\n  ".join(duplicates), file=sys.stderr)
    if missing:
        print("missing matrix entries:\n  " + "\n  ".join(missing), file=sys.stderr)
    if extra:
        print("stale/unknown matrix entries:\n  " + "\n  ".join(extra), file=sys.stderr)
    if errors or duplicates or missing or extra:
        return 1
    category_counts: Counter[str] = Counter()
    for line in matrix.read_text().splitlines():
        if line.startswith("| `"):
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) == 4:
                match = re.fullmatch(r"`([^`]+)`", cells[2])
                if match:
                    category_counts[match.group(1)] += 1
    summary = ", ".join(f"{name}={category_counts[name]}" for name in sorted(ALLOWED_CATEGORIES))
    print(f"Skia test matrix covers {len(expected)} entries ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
