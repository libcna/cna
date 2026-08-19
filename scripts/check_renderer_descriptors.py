#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plan_modern.md Phase 16 -- every renderer descriptor must at least *parse*.

Three descriptors (OpenGL1, LLGL, bgfx) were left syntactically broken by one merge and nobody
noticed, because a renderer identity is only compiled when someone selects it. Each was found by
trying to build that renderer, one at a time, which is the slowest possible way to learn it.

This is the fast way: a brace-balance and structure check over every
`modules/renderers/*/src/*RendererDescriptor.cpp`, with no compiler and no renderer selected. It
cannot prove a descriptor is correct -- only a build does that -- but it catches the exact damage
that merge left, and it runs in milliseconds on all of them at once.

Usage: check_renderer_descriptors.py [repo-root]
"""

import re
import sys
from pathlib import Path


def strip_noise(text: str) -> str:
    """Remove comments and string/char literals so brace counting sees only code."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            i = text.find("\n", i)
            if i < 0:
                break
        elif two == "/*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif text[i] in "\"'":
            quote = text[i]
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    files = sorted((root / "modules/renderers").glob("*/src/*RendererDescriptor.cpp"))
    if not files:
        print(f"check_renderer_descriptors: no descriptors under {root}", file=sys.stderr)
        return 2

    problems = []
    for path in files:
        raw = path.read_text(encoding="utf-8")
        code = strip_noise(raw)

        depth = 0
        lowest = 0
        for character in code:
            if character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
                lowest = min(lowest, depth)
        where = path.relative_to(root)
        if lowest < 0:
            problems.append(f"{where}: a '}}' closes more than was opened -- stray closing brace")
        elif depth != 0:
            problems.append(f"{where}: braces end at depth {depth}, not 0")

        # The specific shape the merge left behind: a namespace or block whose body begins with a
        # statement, i.e. the function signature above it was deleted and its body was not.
        if re.search(r"namespace\s*\{\s*\n\s+(return|if|switch)\b", code):
            problems.append(f"{where}: a namespace body starts with a statement -- a function "
                            f"signature was deleted and its body left behind")

        if "GetDescriptor" not in code:
            problems.append(f"{where}: no GetDescriptor() -- every family's descriptor defines one")

    if problems:
        print("Phase 16: a renderer descriptor will not compile:")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print(f"Phase 16: {len(files)} renderer descriptors, all structurally sound.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
