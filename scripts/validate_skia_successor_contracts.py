#!/usr/bin/env python3
"""Verify that every post-baseline Skia contract has one explicit successor route."""

from __future__ import annotations

import pathlib
import re
import sys
from collections import Counter


ENUM_CONTRACTS = {
    "FMT": (
        "include/Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp",
        "SurfaceFormat",
    ),
    "BLEND": (
        "include/Microsoft/Xna/Framework/Graphics/Blend.hpp",
        "Blend",
    ),
    "BLENDFUNC": (
        "include/Microsoft/Xna/Framework/Graphics/BlendFunction.hpp",
        "BlendFunction",
    ),
    "FILTER": (
        "include/Microsoft/Xna/Framework/Graphics/TextureFilter.hpp",
        "TextureFilter",
    ),
    "CAP": ("include/CNA/GraphicsCapability.hpp", "GraphicsCapability"),
}

FIXED_CONTRACTS = {
    "MIP-TEXTURE2D-CONSTRUCTION",
    "MIP-TEXTURE2D-TRANSFER",
    "MIP-TEXTURE2D-GENERATION",
    "MIP-RENDERTARGET2D",
    "MIP-SAMPLING-LOD",
    "FORMAT-RENDERTARGET",
    "FORMAT-CONTENT",
    "EFFECT-LANGUAGE",
    "EFFECT-VERTEX",
    "EFFECT-FRAGMENT",
    "EFFECT-UNIFORMS",
    "EFFECT-TEXTURES",
    "SAMPLING-CUBE",
    "SAMPLING-VOLUME",
    "MSAA-BACKBUFFER",
    "MSAA-TARGET",
    "ANISO-RASTER",
    "ANISO-GPU",
    "MRT-DISTINCT",
    "MRT-ATOMIC",
    "GPU-OWNERSHIP",
    "GPU-PARITY",
    "RESOURCE-BUDGET",
    "ORACLE-CROSSFEATURE",
}

BASELINE_VALUES = {
    "supported",
    "bounded",
    "fallback",
    "refused",
    "transfer-only",
    "not-selected",
}


def enum_members(text: str, enum_name: str) -> list[str]:
    """Return the declared members after removing comments and explicit values."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    match = re.search(
        rf"enum\s+class\s+{re.escape(enum_name)}\s*\{{(?P<body>.*?)\}}\s*;",
        text,
        flags=re.DOTALL,
    )
    if not match:
        raise ValueError(f"enum class {enum_name} was not found")

    members: list[str] = []
    for declaration in match.group("body").split(","):
        declaration = declaration.strip()
        if not declaration:
            continue
        name = declaration.split("=", maxsplit=1)[0].strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"malformed {enum_name} member {declaration!r}")
        members.append(name)
    return members


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_skia_successor_contracts.py <repository-root>", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1]).resolve()
    matrix_path = root / "docs/skia-successor-contract-matrix.md"
    errors: list[str] = []

    try:
        matrix = matrix_path.read_text()
    except OSError as error:
        print(f"Skia successor contract audit failed to read input: {error}", file=sys.stderr)
        return 1

    expected = set(FIXED_CONTRACTS)
    enum_counts: dict[str, int] = {}
    for prefix, (relative_path, enum_name) in ENUM_CONTRACTS.items():
        path = root / relative_path
        try:
            members = enum_members(path.read_text(), enum_name)
        except (OSError, ValueError) as error:
            errors.append(f"{path}: {error}")
            continue
        enum_counts[prefix] = len(members)
        expected.update(f"{prefix}-{member}" for member in members)

    rows = re.findall(
        r"^\| `([^`]+)` \| ([^|]+) \| ([^|]+) \| ([^|]+) \| ([^|]+) \|$",
        matrix,
        flags=re.MULTILINE,
    )
    row_counts = Counter(row[0] for row in rows)
    actual = set(row_counts)
    for contract_id in sorted(expected - actual):
        errors.append(f"{matrix_path}: missing contract row {contract_id}")
    for contract_id in sorted(actual - expected):
        errors.append(f"{matrix_path}: stale contract row {contract_id}")
    for contract_id, count in sorted(row_counts.items()):
        if count != 1:
            errors.append(f"{matrix_path}: contract {contract_id} appears {count} times")

    baseline_counts: Counter[str] = Counter()
    routed_tasks: set[int] = set()
    for contract_id, contract, baseline, successor, evidence in rows:
        contract = contract.strip()
        baseline = baseline.strip()
        successor = successor.strip()
        evidence = evidence.strip()
        if not contract or not evidence:
            errors.append(f"{matrix_path}: {contract_id} has an empty contract or acceptance rule")
        if baseline not in BASELINE_VALUES:
            errors.append(f"{matrix_path}: {contract_id} has unknown baseline {baseline!r}")
        else:
            baseline_counts[baseline] += 1

        task_match = re.fullmatch(r"SKIA-(\d+)(?:[–-](\d+))?", successor)
        if not task_match:
            errors.append(f"{matrix_path}: {contract_id} has malformed route {successor!r}")
            continue
        first = int(task_match.group(1))
        last = int(task_match.group(2) or first)
        if first > last or first < 118 or last > 170:
            errors.append(
                f"{matrix_path}: {contract_id} route {successor!r} is outside SKIA-118–170"
            )
            continue
        routed_tasks.update(range(first, last + 1))

    if "Status: active for SKIA-117 and the SKIA-115–170 expansion" not in matrix:
        errors.append(f"{matrix_path}: active expansion status banner is missing")
    if routed_tasks != set(range(118, 171)):
        missing = sorted(set(range(118, 171)) - routed_tasks)
        extra = sorted(routed_tasks - set(range(118, 171)))
        if missing:
            errors.append(f"{matrix_path}: successor tasks without a contract route: {missing}")
        if extra:
            errors.append(f"{matrix_path}: out-of-range successor task routes: {extra}")

    if errors:
        print("Skia successor contract audit failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    enum_summary = ", ".join(
        f"{prefix}={enum_counts[prefix]}" for prefix in ENUM_CONTRACTS
    )
    baseline_summary = ", ".join(
        f"{name}={baseline_counts[name]}" for name in sorted(BASELINE_VALUES)
    )
    print(
        f"Skia successor contract audit passed: {len(rows)}/{len(expected)} contracts "
        f"({enum_summary}; fixed={len(FIXED_CONTRACTS)}); {baseline_summary}; "
        f"tasks SKIA-{min(routed_tasks)}–{max(routed_tasks)} routed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
