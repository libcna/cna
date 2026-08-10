#!/usr/bin/env python3
"""Verify the SKIA-134 SurfaceFormat matrix against CNA's live enum and size helpers."""

from __future__ import annotations

import pathlib
import re
import sys
from collections import Counter


# Canonical FNA/FNA3D payload facts and the task route selected by SKIA-134. The validator also
# derives the live size-helper values from Texture.cpp, so changing either CNA source of truth
# requires an intentional matrix update rather than silently changing the Skia contract.
EXPECTED = [
    ("Color", 0, 1, 4, "supported", 143),
    ("Bgr565", 1, 1, 2, "direct", 135),
    ("Bgra5551", 2, 1, 2, "conversion-shadow", 139),
    ("Bgra4444", 3, 1, 2, "conversion-shadow", 135),
    ("Dxt1", 4, 16, 8, "compressed-shadow", 140),
    ("Dxt3", 5, 16, 16, "compressed-shadow", 140),
    ("Dxt5", 6, 16, 16, "compressed-shadow", 140),
    ("NormalizedByte2", 7, 1, 2, "conversion-shadow", 139),
    ("NormalizedByte4", 8, 1, 4, "conversion-shadow", 139),
    ("Rgba1010102", 9, 1, 4, "direct", 135),
    ("Rg32", 10, 1, 4, "direct", 137),
    ("Rgba64", 11, 1, 8, "direct", 137),
    ("Alpha8", 12, 1, 1, "direct-texture-only", 137),
    ("Single", 13, 1, 4, "conversion-shadow", 138),
    ("Vector2", 14, 1, 8, "conversion-shadow", 138),
    ("Vector4", 15, 1, 16, "direct", 138),
    ("HalfSingle", 16, 1, 2, "direct", 138),
    ("HalfVector2", 17, 1, 4, "direct", 138),
    ("HalfVector4", 18, 1, 8, "direct", 138),
    ("HdrBlendable", 19, 1, 8, "direct", 138),
    ("ColorBgraEXT", 20, 1, 4, "direct-texture-only", 136),
    ("ColorSrgbEXT", 21, 1, 4, "colour-space", 136),
    ("Dxt5SrgbEXT", 22, 16, 16, "compressed-shadow", 140),
    ("Bc7EXT", 23, 16, 16, "decoder-required", 141),
    ("Bc7SrgbEXT", 24, 16, 16, "decoder-required", 141),
    ("ByteEXT", 25, 1, 1, "direct", 137),
    ("UShortEXT", 26, 1, 2, "direct", 137),
]


def enum_members(text: str) -> list[str]:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    match = re.search(r"enum\s+class\s+SurfaceFormat\s*\{(.*?)\}\s*;", text, re.DOTALL)
    if not match:
        raise ValueError("enum class SurfaceFormat was not found")
    return [
        declaration.split("=", maxsplit=1)[0].strip()
        for declaration in match.group(1).split(",")
        if declaration.strip()
    ]


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise ValueError(f"function {signature} was not found")
    open_brace = text.find("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    raise ValueError(f"function {signature} has no closing brace")


def case_returns(body: str) -> dict[str, int]:
    result: dict[str, int] = {}
    pending: list[str] = []
    token = re.compile(r"case\s+SurfaceFormat::(\w+)\s*:|return\s+(\d+)\s*;")
    for match in token.finditer(body):
        if match.group(1):
            pending.append(match.group(1))
        elif pending:
            value = int(match.group(2))
            for name in pending:
                result[name] = value
            pending.clear()
    return result


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate_skia_surface_formats.py <repository-root>", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1]).resolve()
    enum_path = root / "include/Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
    texture_path = root / "src/Graphics/Xna/Texture.cpp"
    matrix_path = root / "docs/skia-surface-format-matrix.md"
    errors: list[str] = []

    try:
        members = enum_members(enum_path.read_text())
        texture_source = texture_path.read_text()
        block_sizes = case_returns(
            function_body(texture_source, "Texture::GetBlockSizeSquaredEXT"))
        payload_sizes = case_returns(function_body(texture_source, "Texture::GetFormatSizeEXT"))
        matrix = matrix_path.read_text()
    except (OSError, ValueError) as error:
        print(f"Skia SurfaceFormat audit failed to read source: {error}", file=sys.stderr)
        return 1

    expected_names = [row[0] for row in EXPECTED]
    if members != expected_names:
        errors.append(
            f"{enum_path}: members/order differ from the canonical 27-value FNA contract: {members}")

    rows = re.findall(
        r"^\| (\d+) \| `(\w+)` \| (\d+) \| (\d+) \| ([^|]+) \| ([^|]+) \| "
        r"([^|]+) \| ([^|]+) \| ([^|]+) \| SKIA-(\d+) \|$",
        matrix,
        flags=re.MULTILINE,
    )
    names = [row[1] for row in rows]
    counts = Counter(names)
    if len(rows) != len(EXPECTED):
        errors.append(f"{matrix_path}: expected 27 data rows, found {len(rows)}")
    for name, count in sorted(counts.items()):
        if count != 1:
            errors.append(f"{matrix_path}: format {name} appears {count} times")
    for missing in sorted(set(expected_names) - set(names)):
        errors.append(f"{matrix_path}: missing format {missing}")
    for stale in sorted(set(names) - set(expected_names)):
        errors.append(f"{matrix_path}: stale format {stale}")

    by_name = {row[1]: row for row in rows}
    for name, ordinal, block_texels, payload_bytes, route, owner in EXPECTED:
        source_block = block_sizes.get(name)
        source_payload = payload_sizes.get(name)
        if source_block != block_texels:
            errors.append(
                f"{texture_path}: {name} block size is {source_block}, expected {block_texels}")
        if source_payload != payload_bytes:
            errors.append(
                f"{texture_path}: {name} payload size is {source_payload}, expected {payload_bytes}")
        row = by_name.get(name)
        if row is None:
            continue
        actual_ordinal = int(row[0])
        actual_block = int(row[2])
        actual_payload = int(row[3])
        actual_route = row[8].strip()
        actual_owner = int(row[9])
        if (actual_ordinal, actual_block, actual_payload, actual_route, actual_owner) != (
            ordinal, block_texels, payload_bytes, route, owner
        ):
            errors.append(
                f"{matrix_path}: {name} route tuple is "
                f"{(actual_ordinal, actual_block, actual_payload, actual_route, actual_owner)}, "
                f"expected {(ordinal, block_texels, payload_bytes, route, owner)}")
        for field_name, field in zip(
            ("layout", "sampling", "representation", "render-target decision"), row[4:8]
        ):
            if len(field.strip()) < 8:
                errors.append(f"{matrix_path}: {name} has an empty/vague {field_name} field")

    required_findings = (
        "`kARGB_4444` is not layout-compatible",
        "SKIA-141 implements Bc7EXT/Bc7SrgbEXT with a native BC7 decoder",
        "Texture sampling support never implies renderability",
    )
    for finding in required_findings:
        if finding not in matrix:
            errors.append(f"{matrix_path}: required SKIA-134 decision is missing: {finding}")

    if errors:
        print("Skia SurfaceFormat audit failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    route_counts = Counter(row[4] for row in EXPECTED)
    routes = ", ".join(f"{name}={route_counts[name]}" for name in sorted(route_counts))
    print(
        f"Skia SurfaceFormat audit passed: {len(rows)}/27 enum rows; "
        f"live block/payload helpers matched; routes {routes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
