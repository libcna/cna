#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""docs/cnatests-mingw-setenv-proposal.md, kept from regressing.

`setenv`/`unsetenv` are POSIX. MinGW-w64 does not have them, so a single call site anywhere in a
translation unit that reaches a Windows cross-build stops `CnaTests.exe` compiling -- which is how
the D3D11/D3D12/D3D9 renderers went years without a test binary. The proposal replaced all 62 call
sites in 2026-07 with `System::Environment::SetEnvironmentVariable(name, value)` (empty value means
unset), and by 2026-08 eleven new ones had appeared in six files.

A rule nothing enforces is a rule that decays, so this enforces it. It is a text check, deliberately:
the point is to fail on a Linux build long before anyone tries a Windows one.

Usage: check_no_posix_setenv.py [repo-root]
"""

import re
import sys
from pathlib import Path

CALL = re.compile(r"(?<![_A-Za-z:])(?:::)?(?:un)?setenv\s*\(")

# Third-party trees CNA does not own, and the file that documents the rule.
SKIP_PARTS = ("third_party", "vendor", "cmake-build", "_deps")


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    problems = []
    scanned = 0
    for directory in ("modules", "tools", "tests"):
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(list(base.rglob("*.cpp")) + list(base.rglob("*.hpp"))):
            if any(part in str(path) for part in SKIP_PARTS):
                continue
            scanned += 1
            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), start=1):
                stripped = line.strip()
                if stripped.startswith(("//", "*", "/*")):
                    continue
                if CALL.search(line):
                    problems.append(f"{path.relative_to(root)}:{number}: {stripped}")

    if problems:
        print("POSIX setenv/unsetenv found -- MinGW-w64 has neither, so this breaks every Windows "
              "cross-build's CnaTests.exe:")
        for problem in problems:
            print(f"  {problem}")
        print("\nUse System::Environment::SetEnvironmentVariable(name, value) instead; an empty "
              "value means unset. See docs/cnatests-mingw-setenv-proposal.md.")
        return 1

    print(f"{scanned} sources, no POSIX setenv/unsetenv outside third-party trees.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
