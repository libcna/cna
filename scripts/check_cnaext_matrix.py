#!/usr/bin/env python3
"""plans/plan_modern.md MOD-1698: every renderer identity must appear in the engine layer's matrix.

The matrix in ``docs/cnaext-engine-layer.md`` is only useful if it is complete: a renderer missing
from it reads as "nobody has thought about this one", which is indistinguishable from "this one is
fine". So the list of identities is derived from ``CNA::GraphicsRendererType`` itself -- never
hardcoded, because the enumeration grows -- and every one of them must have a row with an explicit
status.

Usage: check_cnaext_matrix.py [repository-root]
Exit code 0 when the matrix is complete, 1 otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ENUM = Path("modules/core/include/CNA/GraphicsRendererType.hpp")
MATRIX = Path("docs/cnaext-engine-layer.md")
SECTION = "### Every renderer identity"
STATUS_MARKS = ("✅", "🟨", "⬜", "⛔")


def identities(root: Path) -> list[str]:
    text = (root / ENUM).read_text(encoding="utf-8")
    body = text.split("enum class GraphicsRendererType", 1)[1]
    body = body.split("};", 1)[0]
    names: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(("/", "*", "{")):
            continue
        match = re.fullmatch(r"([A-Z][A-Za-z0-9]*)\s*,?", stripped)
        if match:
            names.append(match.group(1))
    return names


def matrix_rows(root: Path) -> dict[str, str]:
    text = (root / MATRIX).read_text(encoding="utf-8")
    if SECTION not in text:
        print(f"matrix error: '{SECTION}' is not in {MATRIX}")
        sys.exit(1)
    section = text.split(SECTION, 1)[1]
    # The table ends at the next heading of any level; everything after it belongs to another
    # section and would otherwise be parsed as renderer rows.
    for boundary in ("\n### ", "\n## ", "\n# "):
        section = section.split(boundary, 1)[0]
    rows: dict[str, str] = {}
    for line in section.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) < 2:
            continue
        name = cells[0].strip("`")
        # Skip the header row and the separator; a real row always names an identity in backticks.
        if not cells[0].startswith("`"):
            continue
        if name and name[0].isupper():
            rows[name] = cells[1]
    return rows


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    names = identities(root)
    if len(names) < 40:
        print(f"matrix error: only {len(names)} identities parsed from {ENUM}; the parser is wrong")
        return 1

    rows = matrix_rows(root)
    problems: list[str] = []
    for name in names:
        if name not in rows:
            problems.append(f"  {name}: no row in the matrix")
            continue
        if not any(mark in rows[name] for mark in STATUS_MARKS):
            problems.append(f"  {name}: row has no explicit status ({' '.join(STATUS_MARKS)})")
    for name in rows:
        if name not in names:
            problems.append(f"  {name}: in the matrix but not a GraphicsRendererType identity")

    if problems:
        print(f"the engine-layer matrix in {MATRIX} is incomplete:")
        print("\n".join(problems))
        return 1

    print(f"checked {len(names)} renderer identities: every one has an explicit engine-layer status")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
