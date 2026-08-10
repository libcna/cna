#!/usr/bin/env python3
"""Validate that the Skia parity ledger covers the current graphics API surface.

The extractor intentionally uses only Python's standard library.  It is not a C++ parser; it
tracks comments, literals, braces, access labels, parameter nesting, and top-level declarations,
which is sufficient for the deliberately conventional declarations in the two audited headers.
Every overload is represented by class, name, arity, and a stable source-order discriminator.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import Counter


BACKEND_INTERFACES = (
    "IVertexBufferRenderer",
    "IIndexBufferRenderer",
    "IOcclusionQueryRenderer",
    "ITextureCubeRenderer",
    "ITexture3DRenderer",
    "ITextureRenderer",
    "IRenderTargetRenderer",
    "IRenderTargetCubeRenderer",
    "IEffectRenderer",
    "ISpriteBatchRenderer",
    "IGraphicsRenderer",
)

ALLOWED_STATUSES = {"implemented", "bounded", "unsupported", "internal"}


def sanitized_cpp(source: str) -> str:
    """Replace comments and literal contents with spaces while preserving structure/newlines."""
    output: list[str] = []
    state = "normal"
    index = 0
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "normal":
            if char == "/" and next_char == "/":
                output.extend("  ")
                index += 2
                state = "line-comment"
                continue
            if char == "/" and next_char == "*":
                output.extend("  ")
                index += 2
                state = "block-comment"
                continue
            if char in {'"', "'"}:
                output.append(" ")
                index += 1
                state = "string" if char == '"' else "character"
                continue
            output.append(char)
            index += 1
            continue
        if state == "line-comment":
            output.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "normal"
            continue
        if state == "block-comment":
            if char == "*" and next_char == "/":
                output.extend("  ")
                index += 2
                state = "normal"
            else:
                output.append("\n" if char == "\n" else " ")
                index += 1
            continue
        if char == "\\":
            output.append(" ")
            if next_char:
                output.append("\n" if next_char == "\n" else " ")
                index += 2
            else:
                index += 1
            continue
        closing = '"' if state == "string" else "'"
        output.append(" ")
        index += 1
        if char == closing:
            state = "normal"
    return "".join(output)


def braced_body(source: str, declaration_pattern: str) -> str:
    match = re.search(declaration_pattern, source)
    if not match:
        raise ValueError(f"declaration not found: {declaration_pattern}")
    opening = source.find("{", match.end())
    if opening < 0:
        raise ValueError(f"opening brace not found: {declaration_pattern}")
    depth = 1
    for index in range(opening + 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise ValueError(f"closing brace not found: {declaration_pattern}")


def top_level_public_declarations(class_body: str) -> list[str]:
    declarations: list[str] = []
    buffer: list[str] = []
    depth = 0
    access = "private"
    index = 0
    while index < len(class_body):
        char = class_body[index]
        if depth == 0 and char == ":":
            label = "".join(buffer).strip()
            if label in {"public", "protected", "private"}:
                access = label
                buffer.clear()
                index += 1
                continue
        if char == "{":
            if depth == 0:
                prefix = "".join(buffer).strip()
                if access == "public" and "(" in prefix:
                    declarations.append(prefix)
                buffer.clear()
            depth += 1
            index += 1
            continue
        if char == "}":
            depth -= 1
            index += 1
            continue
        if depth == 0 and char == ";":
            declaration = "".join(buffer).strip()
            if access == "public" and declaration:
                declarations.append(declaration)
            buffer.clear()
            index += 1
            continue
        if depth == 0:
            buffer.append(char)
        index += 1
    return declarations


def parameter_arity(declaration: str, opening: int) -> int:
    depth = 1
    angle_depth = 0
    commas = 0
    saw_token = False
    index = opening + 1
    while index < len(declaration):
        char = declaration[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return 0 if not saw_token else commas + 1
        elif depth == 1:
            if char == "<":
                angle_depth += 1
            elif char == ">" and angle_depth:
                angle_depth -= 1
            elif char == "," and angle_depth == 0:
                commas += 1
            elif not char.isspace():
                saw_token = True
        index += 1
    raise ValueError(f"unterminated parameter list: {declaration}")


def method_bases(source: str, class_name: str) -> list[str]:
    body = braced_body(source, rf"\bclass\s+{re.escape(class_name)}\b")
    result: list[str] = []
    for declaration in top_level_public_declarations(body):
        if "(" not in declaration or "= delete" in declaration:
            continue
        opening = declaration.find("(")
        prefix = declaration[:opening].strip()
        name_match = re.search(r"(~?[A-Za-z_]\w*|operator\s*[^\s]+)$", prefix)
        if not name_match:
            continue
        name = re.sub(r"\s+", "", name_match.group(1))
        if name.startswith("~"):
            continue
        result.append(f"{class_name}::{name}/{parameter_arity(declaration, opening)}")
    return result


def disambiguated(bases: list[str]) -> list[str]:
    totals = Counter(bases)
    seen: Counter[str] = Counter()
    result: list[str] = []
    for base in bases:
        seen[base] += 1
        result.append(f"{base}#{seen[base]}" if totals[base] > 1 else base)
    return result


def capability_entries(source: str) -> list[str]:
    body = braced_body(source, r"\benum\s+class\s+GraphicsCapability\b")
    entries: list[str] = []
    for item in body.split(","):
        match = re.match(r"\s*([A-Za-z_]\w*)", item)
        if match:
            entries.append(f"GraphicsCapability::{match.group(1)}")
    return entries


def expected_entries(root: pathlib.Path) -> list[str]:
    renderer_source = sanitized_cpp(
        (root / "modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp").read_text()
    )
    device_source = sanitized_cpp(
        (root / "modules/graphics/include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp").read_text()
    )
    capability_source = sanitized_cpp((root / "modules/graphics/include/CNA/GraphicsCapability.hpp").read_text())
    entries: list[str] = []
    for class_name in BACKEND_INTERFACES:
        entries.extend(disambiguated(method_bases(renderer_source, class_name)))
    entries.extend(capability_entries(capability_source))
    entries.extend(disambiguated(method_bases(device_source, "GraphicsDevice")))
    return entries


def ledger_entries(ledger: pathlib.Path) -> tuple[list[str], list[str]]:
    entries: list[str] = []
    errors: list[str] = []
    for line_number, line in enumerate(ledger.read_text().splitlines(), start=1):
        if not line.startswith("| `"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 5:
            errors.append(f"{ledger}:{line_number}: expected five table cells")
            continue
        entry_match = re.fullmatch(r"`([^`]+)`", cells[0])
        status_match = re.fullmatch(r"`([^`]+)`", cells[3])
        if not entry_match or not status_match:
            errors.append(f"{ledger}:{line_number}: entry and status must use code spans")
            continue
        status = status_match.group(1)
        if status not in ALLOWED_STATUSES:
            errors.append(f"{ledger}:{line_number}: invalid status {status!r}")
        if not cells[1] or not cells[2] or not cells[4]:
            errors.append(f"{ledger}:{line_number}: behavior, Skia result, and evidence are required")
        entries.append(entry_match.group(1))
    return entries, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--dump", action="store_true", help="print extracted entries")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        expected = expected_entries(root)
    except (OSError, ValueError) as error:
        print(f"Skia parity extraction failed: {error}", file=sys.stderr)
        return 2
    if args.dump:
        print("\n".join(expected))
        return 0

    ledger = root / "docs/skia-easygl-parity-ledger.md"
    try:
        actual, errors = ledger_entries(ledger)
    except OSError as error:
        print(f"Skia parity ledger read failed: {error}", file=sys.stderr)
        return 2
    duplicates = sorted(entry for entry, count in Counter(actual).items() if count > 1)
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    for error in errors:
        print(error, file=sys.stderr)
    if duplicates:
        print("duplicate ledger entries:\n  " + "\n  ".join(duplicates), file=sys.stderr)
    if missing:
        print("missing ledger entries:\n  " + "\n  ".join(missing), file=sys.stderr)
    if extra:
        print("stale/unknown ledger entries:\n  " + "\n  ".join(extra), file=sys.stderr)
    if errors or duplicates or missing or extra:
        return 1
    print(f"Skia parity ledger covers {len(expected)} API entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
