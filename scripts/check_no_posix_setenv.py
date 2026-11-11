#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Keep POSIX setenv/unsetenv out of CNA sources so Windows (MinGW-w64) cross-builds keep compiling.

`setenv`/`unsetenv` are POSIX. MinGW-w64 does not have them, so a single call site anywhere in a
translation unit that reaches a Windows cross-build stops `CnaTests.exe` compiling -- which is how
the Direct3D renderers once went years without a test binary. Every call site uses
`System::Environment::SetEnvironmentVariable(name, value)` instead (`std::nullopt` removes the
variable), and new POSIX calls kept reappearing until this check existed. Test-support code that
links nothing uses `CNA::TestSupport::SetProcessEnvironmentVariable`, the one allowed header.

It runs as the `NoPosixSetenv` ctest (cmake/Tests/ModuleProbes.cmake) and as a
general-tests-ci step.

A rule nothing enforces is a rule that decays, so this enforces it. It is a text check, deliberately:
the point is to fail on a Linux build long before anyone tries a Windows one.

Usage: check_no_posix_setenv.py [repo-root]
"""

import re
import sys
from pathlib import Path

CALL = re.compile(r"(?<![_A-Za-z:])(?:::)?(?:un)?setenv\s*\(")

# Third-party trees CNA does not own, and build trees.
SKIP_PARTS = ("third_party", "vendor", "_deps")

# Test-support targets link nothing, sharp-runtime included, so they cannot use
# System::Environment. This header is their one portable setter, with its own _WIN32 branch.
ALLOWED = {"tests/support/CNA/TestSupport/TestScratchEnvironment.hpp"}

SUFFIXES = (".cpp", ".hpp", ".c", ".h")


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    problems = []
    scanned = 0
    for directory in ("modules", "tools", "tests"):
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(p for p in base.rglob("*") if p.suffix in SUFFIXES and p.is_file()):
            relative = path.relative_to(root)
            if relative.as_posix() in ALLOWED:
                continue
            if any(part in SKIP_PARTS or part.startswith("cmake-build") for part in relative.parts):
                continue
            scanned += 1
            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), start=1):
                stripped = line.strip()
                if stripped.startswith(("//", "*", "/*")):
                    continue
                if CALL.search(line):
                    problems.append(f"{relative}:{number}: {stripped}")

    if problems:
        print("POSIX setenv/unsetenv found -- MinGW-w64 has neither, so this breaks every Windows "
              "cross-build's CnaTests.exe:")
        for problem in problems:
            print(f"  {problem}")
        print("\nUse System::Environment::SetEnvironmentVariable(name, value) instead; "
              "std::nullopt removes the variable. Test-support code that links nothing uses "
              "CNA::TestSupport::SetProcessEnvironmentVariable.")
        return 1

    print(f"{scanned} sources, no POSIX setenv/unsetenv outside third-party trees.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
