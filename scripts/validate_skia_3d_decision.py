#!/usr/bin/env python3
"""Require the SKIA-101 ADR to decide every live Skia 3D requirement exactly once."""

from __future__ import annotations

import pathlib
import re
import sys
from collections import Counter


ALLOWED_DISPOSITIONS = {
    "prototype-only", "2d-only", "transfer-only", "bounded-2d-sampling", "reject",
}


def feature_ids(path: pathlib.Path) -> list[str]:
    ids: list[str] = []
    for line in path.read_text().splitlines():
        match = re.match(r"\| `(3D-[A-Z0-9-]+)` \|", line)
        if match:
            ids.append(match.group(1))
    return ids


def decision_rows(path: pathlib.Path) -> tuple[list[tuple[str, str]], list[str]]:
    rows: list[tuple[str, str]] = []
    errors: list[str] = []
    text = path.read_text()
    if "Status: Accepted" not in text:
        errors.append(f"{path}: ADR must have accepted status")
    if "Decision: 2D-only" not in text:
        errors.append(f"{path}: ADR must state the 2D-only decision explicitly")

    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.startswith("| `3D-"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4:
            errors.append(f"{path}:{line_number}: expected four decision-table cells")
            continue
        feature_match = re.fullmatch(r"`(3D-[A-Z0-9-]+)`", cells[0])
        disposition_match = re.fullmatch(r"`([a-z0-9-]+)`", cells[1])
        if not feature_match or not disposition_match:
            errors.append(
                f"{path}:{line_number}: feature and disposition must be code spans"
            )
            continue
        feature = feature_match.group(1)
        disposition = disposition_match.group(1)
        if disposition not in ALLOWED_DISPOSITIONS:
            errors.append(
                f"{path}:{line_number}: invalid disposition {disposition!r}"
            )
        if not cells[2] or not cells[3]:
            errors.append(
                f"{path}:{line_number}: evidence and SKIA-102 consequence are required"
            )
        rows.append((feature, disposition))
    return rows, errors


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_skia_3d_decision.py <repository-root>", file=sys.stderr)
        return 2
    root = pathlib.Path(sys.argv[1]).resolve()
    matrix = root / "docs/skia-3d-call-effect-matrix.md"
    decision = root / "docs/skia-3d-emulation-adr.md"
    try:
        matrix_ids = feature_ids(matrix)
        rows, errors = decision_rows(decision)
    except OSError as error:
        print(f"Skia 3D decision audit failed to read input: {error}", file=sys.stderr)
        return 1

    matrix_counts = Counter(matrix_ids)
    row_counts = Counter(feature for feature, _ in rows)
    errors.extend(
        f"{matrix}: duplicate requirement ID {feature}"
        for feature, count in sorted(matrix_counts.items()) if count != 1
    )
    errors.extend(
        f"{decision}: duplicate decision row {feature}"
        for feature, count in sorted(row_counts.items()) if count != 1
    )
    matrix_set = set(matrix_ids)
    decision_set = set(row_counts)
    errors.extend(
        f"{decision}: missing decision for {feature}"
        for feature in sorted(matrix_set - decision_set)
    )
    errors.extend(
        f"{decision}: stale/unknown decision for {feature}"
        for feature in sorted(decision_set - matrix_set)
    )
    if len(matrix_set) != 37:
        errors.append(f"{matrix}: expected 37 live requirement IDs, found {len(matrix_set)}")

    if errors:
        print("Skia 3D decision audit failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    distribution = Counter(disposition for _, disposition in rows)
    summary = ", ".join(
        f"{name}={distribution[name]}" for name in sorted(ALLOWED_DISPOSITIONS)
    )
    print(f"Skia 3D decision audit passed: {len(rows)}/37 requirements ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
